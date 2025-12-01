/*
 * Main window file operations implementation
 * pcmanfm/mainwindow_fileops.cpp
 */

#include "application.h"
#include "bulkrename.h"
#include "mainwindow.h"
#include "tabpage.h"

// New backend headers
#include "../src/backends/qt/qt_fileinfo.h"
#include "../src/core/backend_registry.h"
#include "../src/ui/filepropertiesdialog.h"

// LibFM-Qt headers
#include <libfm-qt6/utilities.h>

// Qt headers
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QObject>
#include <QtGlobal>
#include <memory>

namespace PCManFM {

namespace {

Settings& appSettings() {
    auto* app = qobject_cast<Application*>(qApp);
    Q_ASSERT(app);
    return app->settings();
}

QStringList filePathListToStringList(const Fm::FilePathList& paths) {
    QStringList result;
    result.reserve(static_cast<int>(paths.size()));
    for (const auto& path : paths) {
        result.append(QString::fromUtf8(path.toString().get()));
    }
    return result;
}

std::shared_ptr<IFileInfo> convertToIFileInfo(const std::shared_ptr<const Fm::FileInfo>& fmInfo) {
    if (!fmInfo) {
        return {};
    }
    return std::make_shared<QtFileInfo>(QString::fromUtf8(fmInfo->path().toString().get()));
}

bool renameFileWithBackend(const std::shared_ptr<const Fm::FileInfo>& file, QWidget* parent) {
    if (!file) {
        return false;
    }

    bool ok = false;

    const QString currentPath = QString::fromUtf8(file->path().toString().get());
    const QFileInfo fileInfo(currentPath);
    const QString currentName = fileInfo.fileName();

    const QString newName =
        QInputDialog::getText(parent, QApplication::translate("MainWindow", "Rename"),
                              QApplication::translate("MainWindow", "New name:"), QLineEdit::Normal, currentName, &ok);

    if (ok && !newName.isEmpty() && newName != currentName) {
        const QString newPath = fileInfo.absolutePath() + QLatin1Char('/') + newName;

        QFile fileObj(currentPath);
        if (fileObj.rename(newPath)) {
            return true;
        }

        QMessageBox::critical(
            parent, QApplication::translate("MainWindow", "Error"),
            QApplication::translate("MainWindow", "Failed to rename file: %1").arg(fileObj.errorString()));
        return false;
    }

    return ok;
}

}  // namespace

void MainWindow::on_actionFileProperties_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    const auto files = page->selectedFiles();
    if (files.empty()) {
        return;
    }

    QList<std::shared_ptr<IFileInfo>> fileInfos;
    fileInfos.reserve(static_cast<int>(files.size()));
    for (const auto& file : files) {
        auto info = convertToIFileInfo(file);
        if (info) {
            fileInfos.append(std::move(info));
        }
    }

    if (fileInfos.isEmpty()) {
        return;
    }

    auto* dialog = new FilePropertiesDialog(fileInfos, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::on_actionFolderProperties_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto folder = page->folder();
    if (!folder) {
        return;
    }

    auto info = folder->info();
    if (!info) {
        return;
    }

    auto fileInfo = convertToIFileInfo(info);
    if (!fileInfo) {
        return;
    }

    auto* dialog = new FilePropertiesDialog(fileInfo, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::on_actionCopy_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    const auto paths = page->selectedFilePaths();
    if (paths.empty()) {
        return;
    }

    copyFilesToClipboard(paths);
}

void MainWindow::on_actionCut_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    const auto paths = page->selectedFilePaths();
    if (paths.empty()) {
        return;
    }

    cutFilesToClipboard(paths);
}

void MainWindow::on_actionPaste_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    pasteFilesFromClipboard(page->path());
}

void MainWindow::on_actionDelete_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    Settings& settings = appSettings();
    const auto paths = page->selectedFilePaths();
    if (paths.empty()) {
        return;
    }

    const bool trashed =
        std::any_of(paths.cbegin(), paths.cend(), [](const auto& path) { return path.hasUriScheme("trash"); });

    const bool shiftPressed = (qApp->keyboardModifiers() & Qt::ShiftModifier);

    if (settings.useTrash() && !shiftPressed && !trashed) {
        auto trashBackend = BackendRegistry::trash();
        if (!trashBackend) {
            QMessageBox::warning(this, tr("Move to Trash Failed"), tr("Trash backend is not available."));
            return;
        }

        const QStringList pathStrings = filePathListToStringList(paths);
        for (const QString& path : pathStrings) {
            QString error;
            if (!trashBackend->moveToTrash(path, &error)) {
                QMessageBox::warning(this, tr("Move to Trash Failed"),
                                     tr("Failed to move '%1' to trash: %2").arg(path, error));
                return;
            }
        }
    }
    else {
        auto fileOps = BackendRegistry::createFileOps();
        if (!fileOps) {
            QMessageBox::warning(this, tr("Delete Failed"), tr("File operations backend is not available."));
            return;
        }

        // Keep the async backend alive until it finishes.
        IFileOps* fileOpsPtr = fileOps.release();
        fileOpsPtr->setParent(this);
        connect(fileOpsPtr, &IFileOps::finished, fileOpsPtr, &QObject::deleteLater);

        FileOpRequest req;
        req.type = FileOpType::Delete;
        req.sources = filePathListToStringList(paths);
        req.destination.clear();
        req.followSymlinks = false;
        req.overwriteExisting = false;

        fileOpsPtr->start(req);
    }
}

void MainWindow::on_actionRename_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto files = page->selectedFiles();

    if (files.size() == 1) {
        auto* folderView = page->folderView();
        if (!folderView) {
            return;
        }

        QAbstractItemView* view = folderView->childView();
        if (!view || !view->selectionModel()) {
            return;
        }

        const QModelIndexList selIndexes = view->selectionModel()->selectedIndexes();
        if (selIndexes.isEmpty()) {
            return;
        }

        const QModelIndex editIndex = selIndexes.first();
        view->setCurrentIndex(editIndex);
        view->scrollTo(editIndex);
        view->edit(editIndex);
        return;
    }

    if (!files.empty()) {
        for (auto& file : files) {
            if (!renameFileWithBackend(file, this)) {
                break;
            }
        }
    }
}

void MainWindow::on_actionBulkRename_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto files = page->selectedFiles();
    if (files.empty()) {
        return;
    }

    BulkRenamer(files, this);
}

void MainWindow::on_actionSelectAll_triggered() {
    if (TabPage* page = currentPage()) {
        page->selectAll();
    }
}

void MainWindow::on_actionDeselectAll_triggered() {
    if (TabPage* page = currentPage()) {
        page->deselectAll();
    }
}

void MainWindow::on_actionInvertSelection_triggered() {
    if (TabPage* page = currentPage()) {
        page->invertSelection();
    }
}

void MainWindow::on_actionCopyFullPath_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    const auto paths = page->selectedFilePaths();
    if (paths.size() != 1) {
        return;
    }

    QApplication::clipboard()->setText(QString::fromUtf8(paths.front().toString().get()));
}

void MainWindow::on_actionCleanPerFolderConfig_triggered() {
    const auto r = QMessageBox::question(
        this, tr("Cleaning Folder Settings"),
        tr("Do you want to remove settings of nonexistent folders?\nThey might be useful if those folders are "
           "created again."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (r == QMessageBox::Yes) {
        auto* app = qobject_cast<Application*>(qApp);
        if (app) {
            app->cleanPerFolderConfig();
        }
    }
}

void MainWindow::openFolderAndSelectFiles(const Fm::FilePathList& files, bool inNewTab) {
    if (files.empty()) {
        return;
    }

    auto path = files.front().parent();
    if (!path) {
        return;
    }

    if (!inNewTab) {
        auto* win = new MainWindow(path);
        win->show();
        if (auto* page = win->currentPage()) {
            page->setFilesToSelect(files);
        }
    }
    else {
        auto* newPage = new TabPage(this);
        addTabWithPage(newPage, activeViewFrame_, std::move(path));
        newPage->setFilesToSelect(files);
    }
}

}  // namespace PCManFM
