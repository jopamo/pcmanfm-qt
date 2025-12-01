/*
 * Qt-based file info implementation for PCManFM-Qt
 * pcmanfm-qt/src/backends/qt/qt_fileinfo.cpp
 */

#include "qt_fileinfo.h"

QtFileInfo::QtFileInfo(const QString& path) : m_fileInfo(path) {}

QtFileInfo::QtFileInfo(const QFileInfo& fileInfo) : m_fileInfo(fileInfo) {}

QString QtFileInfo::path() const {
    return m_fileInfo.absoluteFilePath();
}

QString QtFileInfo::name() const {
    return m_fileInfo.fileName();
}

QString QtFileInfo::displayName() const {
    return m_fileInfo.fileName();
}

bool QtFileInfo::isDir() const {
    return m_fileInfo.isDir();
}

bool QtFileInfo::isFile() const {
    return m_fileInfo.isFile();
}

bool QtFileInfo::isSymlink() const {
    return m_fileInfo.isSymLink();
}

bool QtFileInfo::isHidden() const {
    return m_fileInfo.isHidden();
}

qint64 QtFileInfo::size() const {
    return m_fileInfo.size();
}

QDateTime QtFileInfo::lastModified() const {
    return m_fileInfo.lastModified();
}

QString QtFileInfo::mimeType() const {
    if (m_fileInfo.isDir()) {
        return QStringLiteral("inode/directory");
    }

    auto mimeType = m_mimeDatabase.mimeTypeForFile(m_fileInfo);
    return mimeType.name();
}

QIcon QtFileInfo::icon() const {
    // Use Qt's theme icon system
    QString iconName;

    if (m_fileInfo.isDir()) {
        iconName = QStringLiteral("folder");
    }
    else {
        auto mimeType = m_mimeDatabase.mimeTypeForFile(m_fileInfo);
        iconName = mimeType.iconName();
        if (iconName.isEmpty()) {
            iconName = QStringLiteral("text-x-generic");
        }
    }

    return QIcon::fromTheme(iconName);
}