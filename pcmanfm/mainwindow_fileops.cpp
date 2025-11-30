/*
 * Main window file operations implementation
 * pcmanfm/mainwindow_fileops.cpp
 */

#include "application.h"
#include "bulkrename.h"
#include "mainwindow.h"
#include "tabpage.h"

// LibFM-Qt Headers
#include <libfm-qt6/fileoperation.h>
#include <libfm-qt6/filepropsdialog.h>
#include <libfm-qt6/utilities.h>

// Qt Headers
#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>

namespace PCManFM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() { return static_cast<Application*>(qApp)->settings(); }

}  // namespace

void MainWindow::on_actionFileProperties_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto files = page->selectedFiles();
    if (!files.empty()) {
        Fm::FilePropsDialog::showForFiles(files);
    }
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
    if (info) {
        Fm::FilePropsDialog::showForFile(info);
    }
}

void MainWindow::on_actionCopy_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto paths = page->selectedFilePaths();
    if (!paths.empty()) {
        copyFilesToClipboard(paths);
    }
}

void MainWindow::on_actionCut_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto paths = page->selectedFilePaths();
    if (!paths.empty()) {
        cutFilesToClipboard(paths);
    }
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
    auto paths = page->selectedFilePaths();
    if (paths.empty()) {
        return;
    }

    // Check if files are already in trash
    const bool trashed =
        std::any_of(paths.cbegin(), paths.cend(), [](const auto& path) { return path.hasUriScheme("trash"); });

    const bool shiftPressed = (qApp->keyboardModifiers() & Qt::ShiftModifier);

    if (settings.useTrash() && !shiftPressed && !trashed) {
        Fm::FileOperation::trashFiles(paths, settings.confirmTrash(), this);
    } else {
        Fm::FileOperation::deleteFiles(paths, settings.confirmDelete(), this);
    }
}

void MainWindow::on_actionRename_triggered() {
    // do inline renaming if only one item is selected
    // otherwise use the renaming dialog
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto files = page->selectedFiles();

    // Case 1: Inline rename
    if (files.size() == 1) {
        QAbstractItemView* view = page->folderView()->childView();
        if (!view || !view->selectionModel()) {
            return;
        }

        QModelIndexList selIndexes = view->selectionModel()->selectedIndexes();

        // In the detailed list mode, multiple columns might be selected for one row.
        // We ensure we edit the primary column (filename).
        if (!selIndexes.isEmpty()) {
            const QModelIndex editIndex = selIndexes.first();
            view->setCurrentIndex(editIndex);
            view->scrollTo(editIndex);
            view->edit(editIndex);
        }
        return;
    }

    // Case 2: Multi-file rename (sequential dialogs)
    // NOTE: For true bulk rename, use on_actionBulkRename_triggered
    if (!files.empty()) {
        for (auto& file : files) {
            if (!Fm::renameFile(file, this)) {
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
    if (!files.empty()) {
        BulkRenamer(files, this);
    }
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

    auto paths = page->selectedFilePaths();
    if (paths.size() == 1) {
        // Use QString fromUtf8 explicitly
        QApplication::clipboard()->setText(QString::fromUtf8(paths.front().toString().get()));
    }
}

void MainWindow::on_actionCleanPerFolderConfig_triggered() {
    const auto r = QMessageBox::question(this, tr("Cleaning Folder Settings"),
                                         tr("Do you want to remove settings of nonexistent folders?\nThey might be "
                                            "useful if those folders are created again."),
                                         QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (r == QMessageBox::Yes) {
        static_cast<Application*>(qApp)->cleanPerFolderConfig();
    }
}

void MainWindow::openFolderAndSelectFiles(const Fm::FilePathList& files, bool inNewTab) {
    if (files.empty()) {
        return;
    }

    if (auto path = files.front().parent()) {
        if (!inNewTab) {
            // Open in new window
            auto* win = new MainWindow(path);
            win->show();
            if (auto* page = win->currentPage()) {
                page->setFilesToSelect(files);
            }
        } else {
            // Open in new tab
            auto* newPage = new TabPage(this);
            addTabWithPage(newPage, activeViewFrame_, std::move(path));
            newPage->setFilesToSelect(files);
        }
    }
}

}  // namespace PCManFM
