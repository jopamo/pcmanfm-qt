/*
 * Main window path bar implementation
 * pcmanfm/mainwindow_pathbar.cpp
 */

#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"

// Qt Headers
#include <QToolBar>  // Ensure toolBar access

// LibFM-Qt Headers
#include <libfm-qt6/pathbar.h>
#include <libfm-qt6/pathedit.h>

namespace PCManFM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

void MainWindow::createPathBar(bool usePathButtons) {
    // path bars/entries may be created after tab pages so their paths/texts should be set
    if (splitView_) {
        createSplitViewPathBar(usePathButtons);
    }
    else {
        createSingleViewPathBar(usePathButtons);
    }
}

void MainWindow::createSplitViewPathBar(bool usePathButtons) {
    if (!ui.viewSplitter)
        return;

    for (int i = 0; i < ui.viewSplitter->count(); ++i) {
        if (auto* viewFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(i))) {
            viewFrame->createTopBar(usePathButtons);
            TabPage* curPage = currentPage(viewFrame);

            if (auto* pathBar = qobject_cast<Fm::PathBar*>(viewFrame->getTopBar())) {
                connect(pathBar, &Fm::PathBar::chdir, this, &MainWindow::onPathBarChdir);
                connect(pathBar, &Fm::PathBar::middleClickChdir, this, &MainWindow::onPathBarMiddleClickChdir);
                connect(pathBar, &Fm::PathBar::editingFinished, this, &MainWindow::onResetFocus);
                if (curPage) {
                    pathBar->setPath(curPage->path());
                }
            }
            else if (auto* pathEntry = qobject_cast<Fm::PathEdit*>(viewFrame->getTopBar())) {
                connect(pathEntry, &Fm::PathEdit::returnPressed, this, &MainWindow::onPathEntryReturnPressed);
                if (curPage) {
                    pathEntry->setText(curPage->pathName());
                }
            }
        }
    }
}

void MainWindow::createSingleViewPathBar(bool usePathButtons) {
    QWidget* bar = nullptr;
    TabPage* curPage = currentPage();

    if (usePathButtons) {
        if (pathEntry_) {
            delete pathEntry_;
            pathEntry_ = nullptr;
        }
        if (!pathBar_) {
            pathBar_ = new Fm::PathBar(this);
            bar = pathBar_;

            connect(pathBar_, &Fm::PathBar::chdir, this, &MainWindow::onPathBarChdir);
            connect(pathBar_, &Fm::PathBar::middleClickChdir, this, &MainWindow::onPathBarMiddleClickChdir);
            connect(pathBar_, &Fm::PathBar::editingFinished, this, &MainWindow::onResetFocus);

            if (curPage) {
                pathBar_->setPath(curPage->path());
            }
        }
    }
    else {
        if (pathBar_) {
            delete pathBar_;
            pathBar_ = nullptr;
        }
        if (!pathEntry_) {
            pathEntry_ = new Fm::PathEdit(this);
            bar = pathEntry_;

            connect(pathEntry_, &Fm::PathEdit::returnPressed, this, &MainWindow::onPathEntryReturnPressed);

            if (curPage) {
                pathEntry_->setText(curPage->pathName());
            }
        }
    }

    if (bar && ui.toolBar) {
        ui.toolBar->insertWidget(ui.actionGo, bar);
        ui.actionGo->setVisible(!usePathButtons);
    }
}

void MainWindow::onPathEntryReturnPressed() {
    Fm::PathEdit* pathEntry = pathEntry_;

    // In split view, the sender is the specific path entry
    if (!pathEntry) {
        pathEntry = qobject_cast<Fm::PathEdit*>(sender());
    }

    if (!pathEntry) {
        return;
    }

    const QString text = pathEntry->text();
    // Using fromPathStr handles local vs URI logic internally in libfm
    chdir(Fm::FilePath::fromPathStr(text.toLocal8Bit().constData()));
}

void MainWindow::onPathBarChdir(const Fm::FilePath& dirPath) {
    TabPage* page = nullptr;
    ViewFrame* viewFrame = nullptr;

    if (pathBar_) {
        // Single view mode
        page = currentPage();
        viewFrame = activeViewFrame_;
    }
    else {
        // Split view mode: find which PathBar sent the signal
        auto* pathBar = qobject_cast<Fm::PathBar*>(sender());
        if (pathBar) {
            viewFrame = qobject_cast<ViewFrame*>(pathBar->parentWidget());
            if (viewFrame) {
                page = currentPage(viewFrame);
            }
        }
    }

    if (page && viewFrame && dirPath != page->path()) {
        chdir(dirPath, viewFrame);
    }
}

void MainWindow::onPathBarMiddleClickChdir(const Fm::FilePath& dirPath) {
    ViewFrame* viewFrame = nullptr;

    if (pathBar_) {
        viewFrame = activeViewFrame_;
    }
    else {
        auto* pathBar = qobject_cast<Fm::PathBar*>(sender());
        if (pathBar) {
            viewFrame = qobject_cast<ViewFrame*>(pathBar->parentWidget());
        }
    }

    if (viewFrame) {
        addTab(dirPath, viewFrame);
    }
}

void MainWindow::on_actionLocationBar_triggered(bool checked) {
    if (!checked) {
        return;
    }

    // switch to text entry mode
    createPathBar(false);
    appSettings().setPathBarButtons(false);
}

void MainWindow::on_actionPathButtons_triggered(bool checked) {
    if (!checked) {
        return;
    }

    // switch to buttons mode
    createPathBar(true);
    appSettings().setPathBarButtons(true);
}

void MainWindow::on_actionGo_triggered() {
    onPathEntryReturnPressed();
}

void MainWindow::onResetFocus() {
    if (TabPage* page = currentPage()) {
        if (page->folderView() && page->folderView()->childView()) {
            page->folderView()->childView()->setFocus();
        }
    }
}

void MainWindow::focusPathEntry() {
    // Focus the path bar/entry.
    // In split view, focus the active frame's bar.
    if (splitView_) {
        if (!activeViewFrame_) {
            return;
        }

        if (auto* pathBar = qobject_cast<Fm::PathBar*>(activeViewFrame_->getTopBar())) {
            pathBar->openEditor();
        }
        else if (auto* pathEntry = qobject_cast<Fm::PathEdit*>(activeViewFrame_->getTopBar())) {
            pathEntry->setFocus();
            pathEntry->selectAll();
        }
    }
    else {
        // Single view logic
        if (pathEntry_) {
            pathEntry_->setFocus();
            pathEntry_->selectAll();
        }
        else if (pathBar_) {
            pathBar_->openEditor();
        }
    }
}

}  // namespace PCManFM
