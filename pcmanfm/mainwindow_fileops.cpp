/* pcmanfm/mainwindow_fileops.cpp */

#include <libfm-qt6/fileoperation.h>
#include <libfm-qt6/filepropsdialog.h>
#include <libfm-qt6/utilities.h>

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>

#include "application.h"
#include "bulkrename.h"
#include "mainwindow.h"

namespace PCManFM {

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
    copyFilesToClipboard(paths);
}

void MainWindow::on_actionCut_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto paths = page->selectedFilePaths();
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
    auto* app = qobject_cast<Application*>(qApp);
    if (!app) {
        return;
    }

    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    Settings& settings = app->settings();
    auto paths = page->selectedFilePaths();
    if (paths.empty()) {
        return;
    }

    const bool trashed = paths.cbegin() != paths.cend() && paths.cbegin()->hasUriScheme("trash");
    const bool shiftPressed = (qApp->keyboardModifiers() & Qt::ShiftModifier);

    if (settings.useTrash() && !shiftPressed && !trashed) {
        FileOperation::trashFiles(paths, settings.confirmTrash(), this);
    } else {
        FileOperation::deleteFiles(paths, settings.confirmDelete(), this);
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
    if (files.size() == 1) {
        QAbstractItemView* view = page->folderView()->childView();
        if (!view || !view->selectionModel()) {
            return;
        }

        QModelIndexList selIndexes = view->selectionModel()->selectedIndexes();
        if (selIndexes.size() > 1) {  // in the detailed list mode, only the first index is editable
            view->setCurrentIndex(selIndexes.at(0));
        }

        const QModelIndex cur = view->currentIndex();
        if (cur.isValid()) {
            view->scrollTo(cur);
            view->edit(cur);
            return;
        }
    }

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

    BulkRenamer(page->selectedFiles(), this);
}

void MainWindow::on_actionSelectAll_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    page->selectAll();
}

void MainWindow::on_actionDeselectAll_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    page->deselectAll();
}

void MainWindow::on_actionInvertSelection_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    page->invertSelection();
}

void MainWindow::on_actionCopyFullPath_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto paths = page->selectedFilePaths();
    if (paths.size() == 1) {
        QApplication::clipboard()->setText(QString::fromUtf8(paths.front().toString().get()), QClipboard::Clipboard);
    }
}

void MainWindow::on_actionCleanPerFolderConfig_triggered() {
    const auto r =
        QMessageBox::question(this, tr("Cleaning Folder Settings"),
                              tr("Do you want to remove settings of nonexistent folders?\nThey might be useful if "
                                 "those folders are created again."),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (r == QMessageBox::Yes) {
        auto* app = qobject_cast<Application*>(qApp);
        if (!app) {
            return;
        }
        app->cleanPerFolderConfig();
    }
}

void MainWindow::openFolderAndSelectFiles(const Fm::FilePathList& files, bool inNewTab) {
    if (files.empty()) {
        return;
    }

    if (auto path = files.front().parent()) {
        if (!inNewTab) {
            auto* win = new MainWindow(path);
            win->show();
            if (auto* page = win->currentPage()) {
                page->setFilesToSelect(files);
            }
        } else {
            auto* newPage = new TabPage(this);
            addTabWithPage(newPage, activeViewFrame_, std::move(path));
            newPage->setFilesToSelect(files);
        }
    }
}

}  // namespace PCManFM
