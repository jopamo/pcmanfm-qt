/*
 * Qt-based file operations backend implementation
 * src/backends/qt/qt_fileops.cpp
 */

#include "qt_fileops.h"

#include "../../core/fs_ops.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QThread>

#include <atomic>
#include <functional>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>

namespace PCManFM {

namespace {

std::string toNativePath(const QString& path) {
    const QByteArray bytes = QFile::encodeName(path);
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

QString fromNativePath(const std::string& path) {
    return QString::fromLocal8Bit(path.c_str());
}

FileOpProgress toQtProgress(const FsOps::ProgressInfo& core) {
    FileOpProgress qt{};
    qt.bytesDone = core.bytesDone;
    qt.bytesTotal = core.bytesTotal;
    qt.filesDone = core.filesDone;
    qt.filesTotal = core.filesTotal;
    qt.currentPath = fromNativePath(core.currentPath);
    return qt;
}

}  // namespace

class QtFileOps::Worker : public QObject {
    Q_OBJECT

   public:
    explicit Worker(QObject* parent = nullptr) : QObject(parent), cancelled_(false) {}

   public Q_SLOTS:
    void processRequest(const FileOpRequest& req) {
        cancelled_.store(false);

        switch (req.type) {
            case FileOpType::Copy:
                performCopy(req);
                break;
            case FileOpType::Move:
                performMove(req);
                break;
            case FileOpType::Delete:
                performDelete(req);
                break;
        }
    }

    void cancel() { cancelled_.store(true); }

   Q_SIGNALS:
    void progress(const FileOpProgress& info);
    void finished(bool success, const QString& errorMessage);

   private:
    FsOps::ProgressCallback makeProgressCallback() {
        return [this](const FsOps::ProgressInfo& coreInfo) {
            Q_EMIT progress(toQtProgress(coreInfo));
            return !cancelled_.load();
        };
    }

    bool computeStatsForFile(const std::string& path, FsOps::ProgressInfo& progress, FsOps::Error& err) {
        struct stat st;
        if (::lstat(path.c_str(), &st) < 0) {
            err.code = errno;
            err.message = "lstat: " + std::string(std::strerror(errno));
            return false;
        }

        if (S_ISREG(st.st_mode)) {
            progress.bytesTotal += static_cast<std::uint64_t>(st.st_size);
        }
        return true;
    }

    bool performOperationList(
        const FileOpRequest& req,
        const std::function<bool(const std::string&, const std::string&, FsOps::ProgressInfo&, FsOps::Error&)>& op,
        bool needsDestination) {
        for (const QString& sourcePath : req.sources) {
            if (cancelled_.load()) {
                Q_EMIT finished(false, QStringLiteral("Operation cancelled"));
                return false;
            }

            const QString fileName = QFileInfo(sourcePath).fileName();
            const QString destinationPath =
                needsDestination ? (req.destination + QLatin1Char('/') + fileName) : QString();

            const std::string srcNative = toNativePath(sourcePath);
            const std::string dstNative = needsDestination ? toNativePath(destinationPath) : std::string();

            FsOps::ProgressInfo progress{};
            progress.filesTotal = 1;
            progress.filesDone = 0;
            progress.currentPath = srcNative;

            FsOps::Error err;
            if (!computeStatsForFile(srcNative, progress, err)) {
                Q_EMIT finished(false, QString::fromLocal8Bit(err.message.c_str()));
                return false;
            }

            auto opProgress = makeProgressCallback();
            if (!op(srcNative, dstNative, progress, err)) {
                if (cancelled_.load() || err.code == ECANCELED) {
                    Q_EMIT finished(false, QStringLiteral("Operation cancelled"));
                }
                else {
                    const QString msg =
                        err.isSet() ? QString::fromLocal8Bit(err.message.c_str()) : QStringLiteral("Operation failed");
                    Q_EMIT finished(false, msg);
                }
                return false;
            }

            progress.filesDone = 1;
            opProgress(progress);
        }

        Q_EMIT finished(true, QString());
        return true;
    }

    void performCopy(const FileOpRequest& req) {
        performOperationList(
            req,
            [this, req](const std::string& src, const std::string& dst, FsOps::ProgressInfo& progress,
                        FsOps::Error& err) {
                auto cb = makeProgressCallback();
                const bool preserve = req.preserveOwnership;
                return FsOps::copy_path(src, dst, progress, cb, err, preserve);
            },
            /*needsDestination=*/true);
    }

    void performMove(const FileOpRequest& req) {
        performOperationList(
            req,
            [this, req](const std::string& src, const std::string& dst, FsOps::ProgressInfo& progress,
                        FsOps::Error& err) {
                auto cb = makeProgressCallback();
                return FsOps::move_path(src, dst, progress, cb, err, /*forceCopyFallbackForTests=*/false,
                                        req.preserveOwnership);
            },
            /*needsDestination=*/true);
    }

    void performDelete(const FileOpRequest& req) {
        performOperationList(
            req,
            [this](const std::string& src, const std::string& /*unused*/, FsOps::ProgressInfo& progress,
                   FsOps::Error& err) {
                auto cb = makeProgressCallback();
                return FsOps::delete_path(src, progress, cb, err);
            },
            /*needsDestination=*/false);
    }

    std::atomic<bool> cancelled_;
};

QtFileOps::QtFileOps(QObject* parent) : IFileOps(parent), worker_(new Worker), workerThread_(new QThread) {
    worker_->moveToThread(workerThread_);

    connect(this, &QtFileOps::startRequest, worker_, &Worker::processRequest);
    connect(this, &QtFileOps::cancelRequest, worker_, &Worker::cancel);
    connect(worker_, &Worker::progress, this, &QtFileOps::progress);
    connect(worker_, &Worker::finished, this, &QtFileOps::onWorkerFinished);

    workerThread_->start();
}

void QtFileOps::onWorkerFinished(bool success, const QString& errorMessage) {
    Q_EMIT finished(success, errorMessage);
}

QtFileOps::~QtFileOps() {
    cancel();
    workerThread_->quit();
    workerThread_->wait();
    delete worker_;
    delete workerThread_;
}

void QtFileOps::start(const FileOpRequest& req) {
    Q_EMIT startRequest(req);
}

void QtFileOps::cancel() {
    Q_EMIT cancelRequest();
}

}  // namespace PCManFM

#include "qt_fileops.moc"
