/*
 * ImageMagick backend utilities
 * pcmanfm/imagemagick_support.cpp
 */

#include "imagemagick_support.h"

#include <QFile>

#ifdef HAVE_MAGICKWAND
#include <MagickWand/MagickWand.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace {

#ifdef HAVE_MAGICKWAND

class MagickInitializer {
   public:
    MagickInitializer() { MagickWandGenesis(); }
    ~MagickInitializer() { MagickWandTerminus(); }
};

MagickInitializer g_magickInitializer;

QByteArray readFilePosix(const QString& path) {
    const QByteArray encoded = QFile::encodeName(path);
    int fd = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return {};
    }

    struct stat st{};
    if (::fstat(fd, &st) != 0) {
        ::close(fd);
        return {};
    }

    if (!S_ISREG(st.st_mode)) {
        ::close(fd);
        return {};
    }

    const qint64 size = st.st_size;
    if (size <= 0) {
        ::close(fd);
        return {};
    }

    if (size > std::numeric_limits<int>::max()) {
        ::close(fd);
        return {};
    }

    QByteArray buffer;
    buffer.resize(size);

    char* data = buffer.data();
    qint64 remaining = size;
    qint64 offset = 0;

    while (remaining > 0) {
        const ssize_t n = ::read(fd, data + offset, static_cast<size_t>(remaining));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            buffer.clear();
            break;
        }
        if (n == 0) {
            break;
        }

        offset += n;
        remaining -= n;
    }

    ::close(fd);

    if (buffer.isEmpty()) {
        return {};
    }

    if (offset != size) {
        buffer.truncate(offset);
    }

    return buffer;
}

bool writeFilePosix(const QString& path, const unsigned char* data, size_t size) {
    const QByteArray encoded = QFile::encodeName(path);
    int fd = ::open(encoded.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) {
        return false;
    }

    size_t written = 0;
    while (written < size) {
        const ssize_t n = ::write(fd, data + written, size - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            return false;
        }
        written += static_cast<size_t>(n);
    }

    // Best-effort durability: fsync before closing.
    ::fsync(fd);

    if (::close(fd) != 0) {
        return false;
    }

    return true;
}

bool loadWandFromFile(MagickWand* wand, const QString& path) {
    QByteArray data = readFilePosix(path);
    if (data.isEmpty()) {
        return false;
    }

    if (MagickReadImageBlob(wand, reinterpret_cast<const unsigned char*>(data.constData()),
                            static_cast<size_t>(data.size())) == MagickFalse) {
        return false;
    }

    return true;
}

bool saveWandToFile(MagickWand* wand, const QString& path) {
    size_t blobSize = 0;
    unsigned char* blob = MagickGetImageBlob(wand, &blobSize);
    if (!blob || blobSize == 0) {
        return false;
    }

    const bool ok = writeFilePosix(path, blob, blobSize);
    MagickRelinquishMemory(blob);
    return ok;
}

bool fillBufferFromWand(MagickWand* wand, ImageMagickBuffer& out) {
    const size_t w = MagickGetImageWidth(wand);
    const size_t h = MagickGetImageHeight(wand);

    if (w == 0 || h == 0) {
        return false;
    }

    MagickSetImageAlphaChannel(wand, ActivateAlphaChannel);
    MagickSetImageType(wand, TrueColorAlphaType);

    const size_t channels = 4;
    const size_t stride = w * channels;
    const size_t bufferSize = stride * h;

    ImageMagickBuffer buf;
    buf.width = static_cast<int>(w);
    buf.height = static_cast<int>(h);
    buf.pixels.resize(bufferSize);

    if (MagickExportImagePixels(wand, 0, 0, w, h, "RGBA", CharPixel, buf.pixels.data()) == MagickFalse) {
        return false;
    }

    out = std::move(buf);
    return true;
}

#endif  // HAVE_MAGICKWAND

}  // namespace

bool ImageMagickSupport::isAvailable() {
#ifdef HAVE_MAGICKWAND
    return true;
#else
    return false;
#endif
}

bool ImageMagickSupport::probe(const QString& path, ImageMagickInfo& outInfo) {
#ifdef HAVE_MAGICKWAND
    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, path)) {
        DestroyMagickWand(wand);
        return false;
    }

    const size_t w = MagickGetImageWidth(wand);
    const size_t h = MagickGetImageHeight(wand);

    char* fmt = MagickGetImageFormat(wand);
    const ColorspaceType cs = MagickGetImageColorspace(wand);

    outInfo.width = static_cast<quint64>(w);
    outInfo.height = static_cast<quint64>(h);
    if (fmt) {
        outInfo.format = QString::fromLatin1(fmt);
        MagickRelinquishMemory(fmt);
    }

    switch (cs) {
        case sRGBColorspace:
            outInfo.colorSpace = QStringLiteral("sRGB");
            break;
        case CMYKColorspace:
            outInfo.colorSpace = QStringLiteral("CMYK");
            break;
        default:
            outInfo.colorSpace = QStringLiteral("Other");
            break;
    }

    outInfo.hasAlpha = MagickGetImageAlphaChannel(wand) == MagickTrue;

    DestroyMagickWand(wand);
    return true;
#else
    Q_UNUSED(path)
    Q_UNUSED(outInfo)
    return false;
#endif
}

bool ImageMagickSupport::loadThumbnailBuffer(const QString& path, int maxWidth, int maxHeight, ImageMagickBuffer& out) {
#ifdef HAVE_MAGICKWAND
    if (maxWidth <= 0 || maxHeight <= 0) {
        return false;
    }

    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, path)) {
        DestroyMagickWand(wand);
        return false;
    }

    if (MagickThumbnailImage(wand, static_cast<size_t>(maxWidth), static_cast<size_t>(maxHeight)) == MagickFalse) {
        DestroyMagickWand(wand);
        return false;
    }

    const bool ok = fillBufferFromWand(wand, out);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(path)
    Q_UNUSED(maxWidth)
    Q_UNUSED(maxHeight)
    Q_UNUSED(out)
    return false;
#endif
}

bool ImageMagickSupport::loadPreviewBuffer(const QString& path, int maxWidth, int maxHeight, ImageMagickBuffer& out) {
#ifdef HAVE_MAGICKWAND
    if (maxWidth <= 0 || maxHeight <= 0) {
        return false;
    }

    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, path)) {
        DestroyMagickWand(wand);
        return false;
    }

    if (MagickResizeImage(wand, static_cast<size_t>(maxWidth), static_cast<size_t>(maxHeight), LanczosFilter) ==
        MagickFalse) {
        DestroyMagickWand(wand);
        return false;
    }

    const bool ok = fillBufferFromWand(wand, out);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(path)
    Q_UNUSED(maxWidth)
    Q_UNUSED(maxHeight)
    Q_UNUSED(out)
    return false;
#endif
}

bool ImageMagickSupport::loadImageBuffer(const QString& path, ImageMagickBuffer& out) {
#ifdef HAVE_MAGICKWAND
    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, path)) {
        DestroyMagickWand(wand);
        return false;
    }

    const bool ok = fillBufferFromWand(wand, out);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(path)
    Q_UNUSED(out)
    return false;
#endif
}

bool ImageMagickSupport::convertFormat(const QString& srcPath, const QString& dstPath, const QByteArray& format) {
#ifdef HAVE_MAGICKWAND
    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, srcPath)) {
        DestroyMagickWand(wand);
        return false;
    }

    if (!format.isEmpty()) {
        if (MagickSetImageFormat(wand, format.constData()) == MagickFalse) {
            DestroyMagickWand(wand);
            return false;
        }
    }

    const bool ok = saveWandToFile(wand, dstPath);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(srcPath)
    Q_UNUSED(dstPath)
    Q_UNUSED(format)
    return false;
#endif
}

bool ImageMagickSupport::resizeImage(const QString& srcPath,
                                     const QString& dstPath,
                                     int targetWidth,
                                     int targetHeight,
                                     bool keepAspect) {
#ifdef HAVE_MAGICKWAND
    if (targetWidth <= 0 || targetHeight <= 0) {
        return false;
    }

    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, srcPath)) {
        DestroyMagickWand(wand);
        return false;
    }

    const size_t w = MagickGetImageWidth(wand);
    const size_t h = MagickGetImageHeight(wand);
    if (w == 0 || h == 0) {
        DestroyMagickWand(wand);
        return false;
    }

    size_t newW = static_cast<size_t>(targetWidth);
    size_t newH = static_cast<size_t>(targetHeight);

    if (keepAspect) {
        const double aspectSrc = static_cast<double>(w) / static_cast<double>(h);
        const double aspectTarget = static_cast<double>(targetWidth) / static_cast<double>(targetHeight);

        if (aspectSrc > aspectTarget) {
            newH = static_cast<size_t>(static_cast<double>(newW) / aspectSrc);
        }
        else {
            newW = static_cast<size_t>(static_cast<double>(newH) * aspectSrc);
        }
    }

    if (MagickResizeImage(wand, newW, newH, LanczosFilter) == MagickFalse) {
        DestroyMagickWand(wand);
        return false;
    }

    const bool ok = saveWandToFile(wand, dstPath);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(srcPath)
    Q_UNUSED(dstPath)
    Q_UNUSED(targetWidth)
    Q_UNUSED(targetHeight)
    Q_UNUSED(keepAspect)
    return false;
#endif
}

bool ImageMagickSupport::rotateImage(const QString& srcPath, const QString& dstPath, double degrees) {
#ifdef HAVE_MAGICKWAND
    MagickWand* wand = NewMagickWand();
    if (!loadWandFromFile(wand, srcPath)) {
        DestroyMagickWand(wand);
        return false;
    }

    PixelWand* bg = NewPixelWand();
    PixelSetColor(bg, "none");

    if (MagickRotateImage(wand, bg, degrees) == MagickFalse) {
        DestroyPixelWand(bg);
        DestroyMagickWand(wand);
        return false;
    }

    DestroyPixelWand(bg);

    const bool ok = saveWandToFile(wand, dstPath);
    DestroyMagickWand(wand);
    return ok;
#else
    Q_UNUSED(srcPath)
    Q_UNUSED(dstPath)
    Q_UNUSED(degrees)
    return false;
#endif
}
