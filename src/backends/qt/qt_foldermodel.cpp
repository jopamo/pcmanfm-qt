/*
 * Qt-based folder model implementation for PCManFM-Qt
 * pcmanfm-qt/src/backends/qt/qt_foldermodel.cpp
 */

#include "qt_foldermodel.h"

QtFolderModel::QtFolderModel(QObject* parent) : IFolderModel(parent), m_fileSystemModel(new QFileSystemModel(this)) {
    m_fileSystemModel->setFilter(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System | QDir::Hidden);
    m_fileSystemModel->setReadOnly(false);
}

QtFolderModel::~QtFolderModel() {
    // QObject parent-child relationship handles cleanup
}

void QtFolderModel::setDirectory(const QString& path) {
    m_fileSystemModel->setRootPath(path);
}

QString QtFolderModel::directory() const {
    return m_fileSystemModel->rootPath();
}

void QtFolderModel::refresh() {
    m_fileSystemModel->fetchMore(QModelIndex());
}

QModelIndex QtFolderModel::index(int row, int column, const QModelIndex& parent) const {
    return m_fileSystemModel->index(row, column, parent);
}

QModelIndex QtFolderModel::parent(const QModelIndex& child) const {
    return m_fileSystemModel->parent(child);
}

int QtFolderModel::rowCount(const QModelIndex& parent) const {
    return m_fileSystemModel->rowCount(parent);
}

int QtFolderModel::columnCount(const QModelIndex& parent) const {
    return m_fileSystemModel->columnCount(parent);
}

QVariant QtFolderModel::data(const QModelIndex& index, int role) const {
    return m_fileSystemModel->data(index, role);
}