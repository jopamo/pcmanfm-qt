/* pcmanfm/mainwindow_pathbar.cpp */

#include <libfm-qt6/pathbar.h>
#include <libfm-qt6/pathedit.h>

#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"

namespace PCManFM {

void MainWindow::createPathBar(bool usePathButtons) {
    // path bars/entries may be created after tab pages so their paths/texts should be set
    if (splitView_) {
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
                } else if (auto* pathEntry = qobject_cast<Fm::PathEdit*>(viewFrame->getTopBar())) {
                    connect(pathEntry, &Fm::PathEdit::returnPressed, this, &MainWindow::onPathEntryReturnPressed);
                    if (curPage) {
                        pathEntry->setText(curPage->pathName());
                    }
                }
            }
        }
    } else {
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
        } else {
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

        if (bar) {
            ui.toolBar->insertWidget(ui.actionGo, bar);
            ui.actionGo->setVisible(!usePathButtons);
        }
    }
}

void MainWindow::onPathEntryReturnPressed() {
    Fm::PathEdit* pathEntry = pathEntry_;
    if (!pathEntry) {
        pathEntry = qobject_cast<Fm::PathEdit*>(sender());
    }
    if (!pathEntry) {
        return;
    }

    const QString text = pathEntry->text();
    const QByteArray utext = text.toLocal8Bit();
    chdir(Fm::FilePath::fromPathStr(utext.constData()));
}

void MainWindow::onPathBarChdir(const Fm::FilePath& dirPath) {
    TabPage* page = nullptr;
    ViewFrame* viewFrame = nullptr;

    if (pathBar_) {
        page = currentPage();
        viewFrame = activeViewFrame_;
    } else {
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
    } else {
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

    // show current path in a location bar entry
    createPathBar(false);

    if (auto* app = qobject_cast<Application*>(qApp)) {
        app->settings().setPathBarButtons(false);
    }
}

void MainWindow::on_actionPathButtons_triggered(bool checked) {
    if (!checked) {
        return;
    }

    // show current path as buttons
    createPathBar(true);

    if (auto* app = qobject_cast<Application*>(qApp)) {
        app->settings().setPathBarButtons(true);
    }
}

void MainWindow::on_actionGo_triggered() { onPathEntryReturnPressed(); }

void MainWindow::onResetFocus() {
    if (TabPage* page = currentPage()) {
        if (page->folderView() && page->folderView()->childView()) {
            page->folderView()->childView()->setFocus();
        }
    }
}

void MainWindow::focusPathEntry() {
    // use text entry for the path bar
    if (splitView_) {
        if (!activeViewFrame_) {
            return;
        }

        if (auto* pathBar = qobject_cast<Fm::PathBar*>(activeViewFrame_->getTopBar())) {
            pathBar->openEditor();
        } else if (auto* pathEntry = qobject_cast<Fm::PathEdit*>(activeViewFrame_->getTopBar())) {
            pathEntry->setFocus();
            pathEntry->selectAll();
        }
    } else {
        if (pathEntry_) {
            pathEntry_->setFocus();
            pathEntry_->selectAll();
        } else if (pathBar_) {  // use button-style path bar
            pathBar_->openEditor();
        }
    }
}

}  // namespace PCManFM
