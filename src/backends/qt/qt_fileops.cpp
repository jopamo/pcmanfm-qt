/*
 * Qt-based file operations backend implementation
 * src/backends/qt/qt_fileops.cpp
 */

#include "qt_fileops.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <atomic>

namespace PCManFM {

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
    void performCopy(const FileOpRequest& req) {
        FileOpProgress progressInfo;
        progressInfo.filesTotal = req.sources.size();
        progressInfo.filesDone = 0;
        progressInfo.bytesTotal = 0;
        progressInfo.bytesDone = 0;

        // Calculate total size
        for (const QString& source : req.sources) {
            QFileInfo fileInfo(source);
            if (fileInfo.isFile()) {
                progressInfo.bytesTotal += fileInfo.size();
            }
        }

        Q_EMIT progress(progressInfo);

        for (const QString& source : req.sources) {
            if (cancelled_.load()) {
                Q_EMIT finished(false, QStringLiteral("Operation cancelled"));
                return;
            }

            QFileInfo sourceInfo(source);
            QString destinationPath = req.destination + QLatin1Char('/') + sourceInfo.fileName();

            progressInfo.currentPath = source;
            Q_EMIT progress(progressInfo);

            if (sourceInfo.isDir()) {
                if (!copyDirectory(source, destinationPath, progressInfo)) {
                    Q_EMIT finished(false, QStringLiteral("Failed to copy directory: %1").arg(source));
                    return;
                }
            }
            else {
                if (!copyFile(source, destinationPath, progressInfo)) {
                    Q_EMIT finished(false, QStringLiteral("Failed to copy file: %1").arg(source));
                    return;
                }
            }

            progressInfo.filesDone++;
            Q_EMIT progress(progressInfo);
        }

        Q_EMIT finished(true, QString());
    }

    void performMove(const FileOpRequest& req) {
        // For move operations, try rename first, fall back to copy+delete
        for (const QString& source : req.sources) {
            if (cancelled_.load()) {
                Q_EMIT finished(false, QStringLiteral("Operation cancelled"));
                return;
            }

            QFileInfo sourceInfo(source);
            QString destinationPath = req.destination + QLatin1Char('/') + sourceInfo.fileName();

            // Try rename first (same filesystem)
            if (QFile::rename(source, destinationPath)) {
                continue;
            }

            // Fall back to copy + delete
            FileOpProgress progressInfo;
            progressInfo.filesTotal = 1;
            progressInfo.filesDone = 0;
            progressInfo.bytesTotal = sourceInfo.size();
            progressInfo.bytesDone = 0;
            progressInfo.currentPath = source;

            if (!copyFile(source, destinationPath, progressInfo)) {
                Q_EMIT finished(false, QStringLiteral("Failed to move file: %1").arg(source));
                return;
            }

            if (!QFile::remove(source)) {
                // Clean up copied file if delete fails
                QFile::remove(destinationPath);
                Q_EMIT finished(false, QStringLiteral("Failed to remove original file: %1").arg(source));
                return;
            }
        }

        Q_EMIT finished(true, QString());
    }

    void performDelete(const FileOpRequest& req) {
        for (const QString& source : req.sources) {
            if (cancelled_.load()) {
                Q_EMIT finished(false, QStringLiteral("Operation cancelled"));
                return;
            }

            QFileInfo fileInfo(source);

            FileOpProgress progressInfo;
            progressInfo.currentPath = source;
            Q_EMIT progress(progressInfo);

            if (fileInfo.isDir()) {
                QDir dir(source);
                if (!dir.removeRecursively()) {
                    Q_EMIT finished(false, QStringLiteral("Failed to delete directory: %1").arg(source));
                    return;
                }
            }
            else {
                if (!QFile::remove(source)) {
                    Q_EMIT finished(false, QStringLiteral("Failed to delete file: %1").arg(source));
                    return;
                }
            }
        }

        Q_EMIT finished(true, QString());
    }

    bool copyFile(const QString& source, const QString& destination, FileOpProgress& progressInfo) {
        QFile sourceFile(source);
        QSaveFile destFile(destination);

        if (!sourceFile.open(QIODevice::ReadOnly)) {
            return false;
        }

        if (!destFile.open(QIODevice::WriteOnly)) {
            return false;
        }

        const qint64 bufferSize = 64 * 1024;  // 64KB chunks
        char buffer[bufferSize];
        qint64 bytesRead;

        while ((bytesRead = sourceFile.read(buffer, bufferSize)) > 0) {
            if (cancelled_.load()) {
                return false;
            }

            if (destFile.write(buffer, bytesRead) != bytesRead) {
                return false;
            }

            progressInfo.bytesDone += bytesRead;
            Q_EMIT progress(progressInfo);
        }

        return destFile.commit();
    }

    bool copyDirectory(const QString& source, const QString& destination, FileOpProgress& progressInfo) {
        QDir sourceDir(source);
        if (!sourceDir.exists()) {
            return false;
        }

        QDir destDir;
        if (!destDir.mkpath(destination)) {
            return false;
        }

        // Copy all files and subdirectories
        const auto entries = sourceDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString& entry : entries) {
            if (cancelled_.load()) {
                return false;
            }

            QString sourcePath = source + QLatin1Char('/') + entry;
            QString destPath = destination + QLatin1Char('/') + entry;

            QFileInfo entryInfo(sourcePath);
            if (entryInfo.isDir()) {
                if (!copyDirectory(sourcePath, destPath, progressInfo)) {
                    return false;
                }
            }
            else {
                if (!copyFile(sourcePath, destPath, progressInfo)) {
                    return false;
                }
            }
        }

        return true;
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
    Q_UNUSED(success);
    Q_UNUSED(errorMessage);
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
    Q_UNUSED(req);
    Q_EMIT startRequest(req);
}

void QtFileOps::cancel() {
    Q_EMIT cancelRequest();
}

}  // namespace PCManFM

#include "qt_fileops.moc"