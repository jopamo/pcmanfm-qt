**Core idea:**

* ImageMagick (MagickWand) + POSIX I/O does **all** image work: decoding, scaling, format conversion, rotation
* Qt is just:

  * GUI (menus, dialogs, file selection)
  * A dumb canvas that draws RGBA buffers produced by ImageMagick
* No libfm/libfm-qt used in the new code

I’ll structure this as concrete steps with code you can drop in.

---

## 1. Architecture snapshot

### Backend: `ImageMagickSupport`

* Uses **POSIX I/O** (`open`, `read`, `write`, `close`, `fstat`)
* Uses **MagickWand**:

  * `MagickReadImageBlob` to decode from memory
  * `MagickResizeImage` / `MagickThumbnailImage` for scaling
  * `MagickExportImagePixels` for RGBA
  * `MagickGetImageBlob` for writing edited output
* Returns an `ImageMagickBuffer` (width, height, RGBA bytes) for thumbnails and previews
* Provides file-based operations for editing (convert, resize, rotate)

### UI: Qt side

* Adds context menu actions and hooks
* For thumbnails/previews:

  * Calls backend to get `ImageMagickBuffer`
  * Wraps it into `QImage` → `QPixmap` for drawing
  * Qt does no decoding/scaling/rotation, only memcpy into `QImage`
* For editing:

  * Collects parameters
  * Calls backend functions working on paths

---

## 2. CMake wiring in `pcmanfm/CMakeLists.txt`

All changes stay local to `pcmanfm/`.

### 2.1 Detect MagickWand via pkg-config

Near the top:

```cmake
find_package(PkgConfig QUIET)

if(PKG_CONFIG_FOUND)
    pkg_check_modules(MAGICKWAND MagickWand)
endif()
```

### 2.2 Add backend files and optional linkage

In the executable definition:

```cmake
add_executable(pcmanfm-qt
    application.cpp
    mainwindow.cpp
    mainwindow_menus.cpp
    mainwindow_view.cpp
    view.cpp
    tabpage.cpp
    statusbar.cpp
    imagemagick_support.cpp
    imagemagick_support.h
    # ... rest of your existing sources
)

target_link_libraries(pcmanfm-qt
    Qt6::Widgets
    Qt6::DBus
    # existing libs here
)

if(MAGICKWAND_FOUND)
    target_include_directories(pcmanfm-qt PRIVATE ${MAGICKWAND_INCLUDE_DIRS})
    target_link_libraries(pcmanfm-qt PRIVATE ${MAGICKWAND_LIBRARIES})
    target_compile_definitions(pcmanfm-qt PRIVATE HAVE_MAGICKWAND)
endif()
```

Now builds:

* With ImageMagick dev → backend enabled
* Without → backend compiled as no-op

---

## 3. Backend: `imagemagick_support.h`

Put this in `pcmanfm/imagemagick_support.h`:

```cpp
#pragma once

#include <QString>
#include <vector>

struct ImageMagickBuffer {
    int width = 0
    int height = 0
    std::vector<unsigned char> pixels    // RGBA, row-major, width * height * 4
}

struct ImageMagickInfo {
    quint64 width = 0
    quint64 height = 0
    QString format
    QString colorSpace
    bool hasAlpha = false
}

class ImageMagickSupport {
public:
    static bool isAvailable()

    static bool probe(const QString &path, ImageMagickInfo &outInfo)

    static bool loadThumbnailBuffer(const QString &path,
                                    int maxWidth,
                                    int maxHeight,
                                    ImageMagickBuffer &out)

    static bool loadPreviewBuffer(const QString &path,
                                  int maxWidth,
                                  int maxHeight,
                                  ImageMagickBuffer &out)

    static bool convertFormat(const QString &srcPath,
                              const QString &dstPath,
                              const QByteArray &format)

    static bool resizeImage(const QString &srcPath,
                            const QString &dstPath,
                            int targetWidth,
                            int targetHeight,
                            bool keepAspect = true)

    static bool rotateImage(const QString &srcPath,
                            const QString &dstPath,
                            double degrees)
}
```

---

## 4. Backend: `imagemagick_support.cpp` (POSIX + MagickWand)

Create `pcmanfm/imagemagick_support.cpp`:

```cpp
#include "imagemagick_support.h"

#include <QFile>

#ifdef HAVE_MAGICKWAND
#include <wand/MagickWand.h>
#include <vector>
#include <cstring>
#include <cerrno>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

#ifdef HAVE_MAGICKWAND

class MagickInitializer {
public:
    MagickInitializer() { MagickWandGenesis() }
    ~MagickInitializer() { MagickWandTerminus() }
}

MagickInitializer g_magickInitializer

QByteArray readFilePosix(const QString &path) {
    const QByteArray encoded = QFile::encodeName(path)
    int fd = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC)
    if (fd < 0)
        return {}

    struct stat st
    if (::fstat(fd, &st) != 0) {
        ::close(fd)
        return {}
    }

    if (!S_ISREG(st.st_mode)) {
        ::close(fd)
        return {}
    }

    const qint64 size = st.st_size
    if (size <= 0) {
        ::close(fd)
        return {}
    }

    QByteArray buffer
    buffer.resize(size)

    char *data = buffer.data()
    qint64 remaining = size
    qint64 offset = 0

    while (remaining > 0) {
        const ssize_t n = ::read(fd, data + offset, static_cast<size_t>(remaining))
        if (n < 0) {
            if (errno == EINTR)
                continue
            buffer.clear()
            break
        }
        if (n == 0)
            break

        offset += n
        remaining -= n
    }

    ::close(fd)

    if (buffer.isEmpty())
        return {}

    if (offset != size)
        buffer.truncate(offset)

    return buffer
}

bool writeFilePosix(const QString &path, const unsigned char *data, size_t size) {
    const QByteArray encoded = QFile::encodeName(path)
    int fd = ::open(encoded.constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666)
    if (fd < 0)
        return false

    size_t written = 0
    while (written < size) {
        const ssize_t n = ::write(fd, data + written, size - written)
        if (n < 0) {
            if (errno == EINTR)
                continue
            ::close(fd)
            return false
        }
        written += static_cast<size_t>(n)
    }

    if (::close(fd) != 0)
        return false

    return true
}

bool loadWandFromFile(MagickWand *wand, const QString &path) {
    QByteArray data = readFilePosix(path)
    if (data.isEmpty())
        return false

    if (MagickReadImageBlob(
            wand,
            reinterpret_cast<const unsigned char *>(data.constData()),
            static_cast<size_t>(data.size())
        ) == MagickFalse) {
        return false
    }

    return true
}

bool saveWandToFile(MagickWand *wand, const QString &path) {
    size_t blobSize = 0
    unsigned char *blob = MagickGetImageBlob(wand, &blobSize)
    if (!blob || blobSize == 0)
        return false

    const bool ok = writeFilePosix(path, blob, blobSize)
    MagickRelinquishMemory(blob)
    return ok
}

bool fillBufferFromWand(MagickWand *wand, ImageMagickBuffer &out) {
    size_t w = MagickGetImageWidth(wand)
    size_t h = MagickGetImageHeight(wand)

    if (w == 0 || h == 0)
        return false

    MagickSetImageAlphaChannel(wand, ActivateAlphaChannel)
    MagickSetImageType(wand, TrueColorAlphaType)

    const size_t channels = 4
    const size_t stride = w * channels
    const size_t bufferSize = stride * h

    ImageMagickBuffer buf
    buf.width = static_cast<int>(w)
    buf.height = static_cast<int>(h)
    buf.pixels.resize(bufferSize)

    if (MagickExportImagePixels(
            wand,
            0, 0,
            w, h,
            "RGBA",
            CharPixel,
            buf.pixels.data()
        ) == MagickFalse) {
        return false
    }

    out = std::move(buf)
    return true
}

#endif

} // namespace

bool ImageMagickSupport::isAvailable() {
#ifdef HAVE_MAGICKWAND
    return true
#else
    return false
#endif
}

bool ImageMagickSupport::probe(const QString &path, ImageMagickInfo &outInfo) {
#ifdef HAVE_MAGICKWAND
    MagickWand *wand = NewMagickWand()
    if (!loadWandFromFile(wand, path)) {
        DestroyMagickWand(wand)
        return false
    }

    size_t w = MagickGetImageWidth(wand)
    size_t h = MagickGetImageHeight(wand)

    char *fmt = MagickGetImageFormat(wand)
    ColorspaceType cs = MagickGetImageColorspace(wand)

    outInfo.width = static_cast<quint64>(w)
    outInfo.height = static_cast<quint64>(h)
    if (fmt) {
        outInfo.format = QString::fromLatin1(fmt)
        MagickRelinquishMemory(fmt)
    }

    switch (cs) {
    case sRGBColorspace:
        outInfo.colorSpace = QStringLiteral("sRGB")
        break
    case CMYKColorspace:
        outInfo.colorSpace = QStringLiteral("CMYK")
        break
    default:
        outInfo.colorSpace = QStringLiteral("Other")
        break
    }

    outInfo.hasAlpha = MagickGetImageAlphaChannel(wand) == MagickTrue

    DestroyMagickWand(wand)
    return true
#else
    Q_UNUSED(path)
    Q_UNUSED(outInfo)
    return false
#endif
}

bool ImageMagickSupport::loadThumbnailBuffer(const QString &path,
                                             int maxWidth,
                                             int maxHeight,
                                             ImageMagickBuffer &out) {
#ifdef HAVE_MAGICKWAND
    if (maxWidth <= 0 || maxHeight <= 0)
        return false

    MagickWand *wand = NewMagickWand()
    if (!loadWandFromFile(wand, path)) {
        DestroyMagickWand(wand)
        return false
    }

    if (MagickThumbnailImage(
            wand,
            static_cast<size_t>(maxWidth),
            static_cast<size_t>(maxHeight)
        ) == MagickFalse) {
        DestroyMagickWand(wand)
        return false
    }

    const bool ok = fillBufferFromWand(wand, out)
    DestroyMagickWand(wand)
    return ok
#else
    Q_UNUSED(path)
    Q_UNUSED(maxWidth)
    Q_UNUSED(maxHeight)
    Q_UNUSED(out)
    return false
#endif
}

bool ImageMagickSupport::loadPreviewBuffer(const QString &path,
                                           int maxWidth,
                                           int maxHeight,
                                           ImageMagickBuffer &out) {
#ifdef HAVE_MAGICKWAND
    if (maxWidth <= 0 || maxHeight <= 0)
        return false

    MagickWand *wand = NewMagickWand()
    if (!loadWandFromFile(wand, path)) {
        DestroyMagickWand(wand)
        return false
    }

    if (MagickResizeImage(
            wand,
            static_cast<size_t>(maxWidth),
            static_cast<size_t>(maxHeight),
            LanczosFilter,
            1.0
        ) == MagickFalse) {
        DestroyMagickWand(wand)
        return false
    }

    const bool ok = fillBufferFromWand(wand, out)
    DestroyMagickWand(wand)
    return ok
#else
    Q_UNUSED(path)
    Q_UNUSED(maxWidth)
    Q_UNUSED(maxHeight)
    Q_UNUSED(out)
    return false
#endif
}

bool ImageMagickSupport::convertFormat(const QString &srcPath,
                                       const QString &dstPath,
                                       const QByteArray &format) {
#ifdef HAVE_MAGICKWAND
    MagickWand *wand = NewMagickWand()
    if (!loadWandFromFile(wand, srcPath)) {
        DestroyMagickWand(wand)
        return false
    }

    if (!format.isEmpty()) {
        if (MagickSetImageFormat(wand, format.constData()) == MagickFalse) {
            DestroyMagickWand(wand)
            return false
        }
    }

    const bool ok = saveWandToFile(wand, dstPath)
    DestroyMagickWand(wand)
    return ok
#else
    Q_UNUSED(srcPath)
    Q_UNUSED(dstPath)
    Q_UNUSED(format)
    return false
#endif
}

bool ImageMagickSupport::resizeImage(const QString &srcPath,
                                     const QString &dstPath,
                                     int targetWidth,
                                     int targetHeight,
                                     bool keepAspect) {
#ifdef HAVE_MAGICKWAND
    if (targetWidth <= 0 || targetHeight <= 0)
        return false

    MagickWand *wand = NewMagickWand()
    if (!loadWandFromFile(wand, srcPath)) {
        DestroyMagickWand(wand)
        return false
    }

    size_t w = MagickGetImageWidth(wand)
    size_t h = MagickGetImageHeight(wand)
    if (w == 0 || h == 0) {
        DestroyMagickWand(wand)
        return false
    }

    size_t newW = static_cast<size_t>(targetWidth)
    size_t newH = static_cast<size_t>(targetHeight)

    if (keepAspect) {
        const double aspectSrc = static_cast<double>(w) / static_cast<double>(h)
        const double aspectTarget = static_cast<double>(targetWidth) /
                                    static_cast<double>(targetHeight)

        if (aspectSrc > aspectTarget) {
            newH = static_cast<size_t>(static_cast<double>(newW) / aspectSrc)
        } else {
            newW = static_cast<size_t>(static_cast<double>(newH) * aspectSrc)
        }
    }

    if (MagickResizeImage(
            wand,
            newW,
            newH,
            LanczosFilter,
            1.0
        ) == MagickFalse) {
        DestroyMagickWand(wand)
        return false
    }

    const bool ok = saveWandToFile(wand, dstPath)
    DestroyMagickWand(wand)
    return ok
#else
    Q_UNUSED(srcPath)
    Q_UNUSED(dstPath)
    Q_UNUSED(targetWidth)
    Q_UNUSED(targetHeight)
    Q_UNUSED(keepAspect)
    return false
#endif
}

bool ImageMagickSupport::rotateImage(const QString &srcPath,
                                     const QString &dstPath,
                                     double degrees) {
#ifdef HAVE_MAGICKWAND
    MagickWand *wand = NewMagickWand()
    if (!loadWandFromFile(wand, srcPath)) {
        DestroyMagickWand(wand)
        return false
    }

    PixelWand *bg = NewPixelWand()
    PixelSetColor(bg, "none")

    if (MagickRotateImage(wand, bg, degrees) == MagickFalse) {
        DestroyPixelWand(bg)
        DestroyMagickWand(wand)
        return false
    }

    DestroyPixelWand(bg)

    const bool ok = saveWandToFile(wand, dstPath)
    DestroyMagickWand(wand)
    return ok
#else
    Q_UNUSED(srcPath)
    Q_UNUSED(dstPath)
    Q_UNUSED(degrees)
    return false
#endif
}
```

Backend done: everything uses POSIX + MagickWand, no Qt image code.

---

## 5. Qt helper: wrap buffer into a QPixmap (for drawing only)

In `pcmanfm/view.cpp` (or a small helper file), add:

```cpp
#include "imagemagick_support.h"
#include <QImage>
#include <QPixmap>

static QPixmap pixmapFromMagickBuffer(const ImageMagickBuffer &buf) {
    if (buf.width <= 0 || buf.height <= 0)
        return QPixmap()

    QImage img(buf.width, buf.height, QImage::Format_RGBA8888)
    if (img.isNull())
        return QPixmap()

    const int stride = buf.width * 4
    for (int y = 0; y < buf.height; ++y) {
        unsigned char *dst = img.scanLine(y)
        const unsigned char *src = buf.pixels.data() + y * stride
        std::memcpy(dst, src, stride)
    }

    return QPixmap::fromImage(img)
}

static QPixmap createThumbnailPixmapMagick(const QString &path,
                                           const QSize &thumbSize) {
    if (!ImageMagickSupport::isAvailable())
        return QPixmap()

    ImageMagickBuffer buf
    if (!ImageMagickSupport::loadThumbnailBuffer(path,
                                                 thumbSize.width(),
                                                 thumbSize.height(),
                                                 buf)) {
        return QPixmap()
    }

    return pixmapFromMagickBuffer(buf)
}
```

Qt only wraps and draws the buffer, zero transforms.

---

## 6. Wiring into pcmanfm-qt thumbnails

Where pcmanfm-qt currently builds image thumbnails (in `view.cpp` or a related delegate), you probably have something akin to:

```cpp
QPixmap pix

if (isImageFile) {
    pix = QPixmap(filePath)
} else {
    pix = iconForNonImageFile
}
```

Replace the image branch with the Magick path:

```cpp
QPixmap pix

if (isImageFile && ImageMagickSupport::isAvailable()) {
    QPixmap magickPix = createThumbnailPixmapMagick(filePath, thumbnailSize)
    if (!magickPix.isNull())
        pix = magickPix
    else
        pix = iconForNonImageFile
} else {
    pix = iconForNonImageFile
}
```

No Qt decoding or scaling is happening here, only drawing.

---

## 7. Preview window fast path

If pcmanfm-qt has an internal preview widget:

```cpp
void PreviewWidget::showImageFromMagick(const QString &path, const QSize &maxSize) {
    if (!ImageMagickSupport::isAvailable()) {
        showNoPreviewMessage()
        return
    }

    ImageMagickBuffer buf
    if (!ImageMagickSupport::loadPreviewBuffer(path,
                                               maxSize.width(),
                                               maxSize.height(),
                                               buf)) {
        showNoPreviewMessage()
        return
    }

    QPixmap pix = pixmapFromMagickBuffer(buf)
    if (pix.isNull()) {
        showNoPreviewMessage()
        return
    }

    label->setPixmap(pix)
}
```

Again, Qt does only GUI and drawing.

---

## 8. Editing actions using backend (convert, resize, rotate)

In `pcmanfm/mainwindow_menus.cpp` (no libfm):

```cpp
#include "imagemagick_support.h"
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
```

Then wire context menu actions just like in earlier messages, calling:

* `ImageMagickSupport::convertFormat`
* `ImageMagickSupport::resizeImage`
* `ImageMagickSupport::rotateImage`

All those functions operate on file paths using POSIX + MagickWand; Qt provides only parameters and dialogs.
