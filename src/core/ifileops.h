/*
 * File operations interface
 * src/core/ifileops.h
 */

#ifndef IFILEOPS_H
#define IFILEOPS_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace PCManFM {

enum class FileOpType { Copy, Move, Delete };

struct FileOpRequest {
    FileOpType type;
    QStringList sources;
    QString destination;
    bool followSymlinks;
    bool overwriteExisting;
    bool preserveOwnership = false;
};

struct FileOpProgress {
    quint64 bytesDone;
    quint64 bytesTotal;
    int filesDone;
    int filesTotal;
    QString currentPath;
};

class IFileOps : public QObject {
    Q_OBJECT

   public:
    using QObject::QObject;
    ~IFileOps() override = default;

    virtual void start(const FileOpRequest& req) = 0;
    virtual void cancel() = 0;

   Q_SIGNALS:
    void progress(const FileOpProgress& info);
    void finished(bool success, const QString& errorMessage);
};

}  // namespace PCManFM

#endif  // IFILEOPS_H
