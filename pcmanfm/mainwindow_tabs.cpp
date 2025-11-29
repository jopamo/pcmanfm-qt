/* pcmanfm/mainwindow_tabs.cpp */

#include <QTimer>

#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"

namespace PCManFM {

void MainWindow::addViewFrame(const Fm::FilePath& path) {
    ui.actionGo->setVisible(false);

    auto* app = qobject_cast<Application*>(qApp);
    if (!app) {
        return;
    }

    Settings& settings = app->settings();
    auto* viewFrame = new ViewFrame();
    auto* tabBar = viewFrame->getTabBar();

    tabBar->setDetachable(!splitView_);  // no tab DND with the split view
    tabBar->setTabsClosable(settings.showTabClose());
    tabBar->setAutoHide(!settings.alwaysShowTabs());
    ui.viewSplitter->addWidget(viewFrame);  // the splitter takes ownership of viewFrame

    if (ui.viewSplitter->count() == 1) {
        activeViewFrame_ = viewFrame;
    } else {
        QTimer::singleShot(0, this, [this] {
            const int count = ui.viewSplitter->count();
            if (count <= 0) {
                return;
            }
            const int widthPerFrame = ui.viewSplitter->width() / count;
            QList<int> sizes(count, widthPerFrame);
            ui.viewSplitter->setSizes(sizes);
        });
    }

    connect(tabBar, &QTabBar::currentChanged, this, &MainWindow::onTabBarCurrentChanged);
    connect(tabBar, &QTabBar::tabCloseRequested, this, &MainWindow::onTabBarCloseRequested);
    connect(tabBar, &QTabBar::tabMoved, this, &MainWindow::onTabBarTabMoved);
    connect(tabBar, &QTabBar::tabBarClicked, this, &MainWindow::onTabBarClicked);
    connect(tabBar, &QTabBar::customContextMenuRequested, this, &MainWindow::tabContextMenu);
    connect(tabBar, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
        if (index == -1) {
            on_actionNewTab_triggered();
        }
    });

    connect(viewFrame->getStackedWidget(), &QStackedWidget::widgetRemoved, this,
            &MainWindow::onStackedWidgetWidgetRemoved);

    // the tab will be detached only after the DND is finished
    connect(tabBar, &TabBar::tabDetached, this, &MainWindow::detachTab, Qt::QueuedConnection);

    if (path) {
        addTab(path, viewFrame);
    }
}

int MainWindow::addTabWithPage(TabPage* page, ViewFrame* viewFrame, Fm::FilePath path) {
    if (!page || !viewFrame) {
        return -1;
    }

    page->setFileLauncher(&fileLauncher_);
    auto* stackedWidget = viewFrame->getStackedWidget();
    const int index = stackedWidget->addWidget(page);

    connect(page, &TabPage::titleChanged, this, &MainWindow::onTabPageTitleChanged);
    connect(page, &TabPage::statusChanged, this, &MainWindow::onTabPageStatusChanged);
    connect(page, &TabPage::sortFilterChanged, this, &MainWindow::onTabPageSortFilterChanged);
    connect(page, &TabPage::backwardRequested, this, &MainWindow::on_actionGoBack_triggered);
    connect(page, &TabPage::forwardRequested, this, &MainWindow::on_actionGoForward_triggered);
    connect(page, &TabPage::backspacePressed, this, &MainWindow::on_actionGoUp_triggered);
    connect(page, &TabPage::folderUnmounted, this, &MainWindow::onFolderUnmounted);

    if (path) {
        page->chdir(path, true);
    }

    QString tabText = page->title();
    // remove newline (not all styles can handle it) and distinguish ampersand from mnemonic
    tabText.replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('&'), QLatin1String("&&"));

    auto* tabBar = viewFrame->getTabBar();
    tabBar->insertTab(index, tabText);

    auto* app = qobject_cast<Application*>(qApp);
    if (!app) {
        return index;
    }

    Settings& settings = app->settings();
    if (settings.switchToNewTab()) {
        tabBar->setCurrentIndex(index);  // also focuses the view
        if (isMinimized()) {
            setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
            show();
        }
    } else if (TabPage* tabPage = currentPage()) {
        tabPage->folderView()->childView()->setFocus();
    }

    // also set tab icon (if the folder is customized)
    setTabIcon(page);

    return index;
}

// add a new tab
void MainWindow::addTab(Fm::FilePath path, ViewFrame* viewFrame) {
    auto* newPage = new TabPage(this);
    addTabWithPage(newPage, viewFrame, path);
}

void MainWindow::addTab(Fm::FilePath path) {
    auto* app = qobject_cast<Application*>(qApp);
    if (!app) {
        return;
    }

    if (splitView_ && app->openingLastTabs()) {
        int N = app->settings().splitViewTabsNum();
        if (N > 0) {
            // Divide tabs between the first and second view frames appropriately
            // It is assumed that reopening of last tabs is started when the split view has
            // two frames, each with a single tab created in the ctor
            if (splitTabsNum_ == -1) {
                splitTabsNum_ = N - 1;
            }

            auto* firstFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(0));
            auto* secondFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(1));

            if (!firstFrame || !secondFrame) {  // unlikely but logical
                app->settings().setSplitViewTabsNum(0);
                splitTabsNum_ = -1;
                addTab(path, activeViewFrame_);
            } else if (splitTabsNum_ > 0) {
                --splitTabsNum_;
                addTab(path, firstFrame);
            } else {
                addTab(path, secondFrame);
                // On reaching the single tab of the second frame, remove it after adding a tab
                if (splitTabsNum_ == 0 && secondFrame->getStackedWidget()->count() == 2) {
                    closeTab(0, secondFrame);
                }
                splitTabsNum_ = -2;  // will not change again for the current window
            }
            return;
        }
    }

    // add the tab to the active view frame
    addTab(path, activeViewFrame_);
}

void MainWindow::closeTab(int index, ViewFrame* viewFrame) {
    auto* stackedWidget = viewFrame->getStackedWidget();
    QWidget* page = stackedWidget->widget(index);
    if (page) {
        stackedWidget->removeWidget(page);  // this does not delete the page widget
        delete page;
        // tab removal is handled in onStackedWidgetWidgetRemoved()
    }
}

void MainWindow::onTabBarCloseRequested(int index) {
    auto* tabBar = qobject_cast<TabBar*>(sender());
    if (!tabBar) {
        return;
    }

    auto* viewFrame = qobject_cast<ViewFrame*>(tabBar->parentWidget());
    if (viewFrame) {
        closeTab(index, viewFrame);
    }
}

void MainWindow::onTabBarCurrentChanged(int index) {
    auto* tabBar = qobject_cast<TabBar*>(sender());
    if (!tabBar) {
        return;
    }

    auto* viewFrame = qobject_cast<ViewFrame*>(tabBar->parentWidget());
    if (!viewFrame) {
        return;
    }

    auto* stackedWidget = viewFrame->getStackedWidget();
    stackedWidget->setCurrentIndex(index);

    if (viewFrame == activeViewFrame_) {
        updateUIForCurrentPage();
    } else {
        if (TabPage* page = currentPage(viewFrame)) {
            if (auto* pathBar = qobject_cast<Fm::PathBar*>(viewFrame->getTopBar())) {
                pathBar->setPath(page->path());
            } else if (auto* pathEntry = qobject_cast<Fm::PathEdit*>(viewFrame->getTopBar())) {
                pathEntry->setText(page->pathName());
            }
        }
    }
}

void MainWindow::onTabBarTabMoved(int from, int to) {
    auto* tabBar = qobject_cast<TabBar*>(sender());
    if (!tabBar) {
        return;
    }

    auto* viewFrame = qobject_cast<ViewFrame*>(tabBar->parentWidget());
    if (!viewFrame) {
        return;
    }

    auto* stackedWidget = viewFrame->getStackedWidget();
    QWidget* page = stackedWidget->widget(from);
    if (!page) {
        return;
    }

    // block signals to avoid onStackedWidgetWidgetRemoved() being called while reshuffling pages
    const bool previousBlockState = stackedWidget->blockSignals(true);
    stackedWidget->removeWidget(page);
    stackedWidget->insertWidget(to, page);  // insert the page to the new position
    stackedWidget->blockSignals(previousBlockState);
    stackedWidget->setCurrentWidget(page);
}

void MainWindow::onStackedWidgetWidgetRemoved(int index) {
    auto* sw = qobject_cast<QStackedWidget*>(sender());
    if (!sw) {
        return;
    }

    auto* viewFrame = qobject_cast<ViewFrame*>(sw->parentWidget());
    if (!viewFrame) {
        return;
    }

    auto* tabBar = viewFrame->getTabBar();
    tabBar->removeTab(index);

    if (tabBar->count() != 0) {
        return;
    }

    // this is the last tab in this view frame
    if (!splitView_) {
        deleteLater();  // destroy the whole window
    } else {
        // in split mode, if the last tab of a view frame is closed,
        // remove that view frame and go to the simple mode
        for (int i = 0; i < ui.viewSplitter->count(); ++i) {
            auto* thisViewFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(i));
            if (!thisViewFrame || thisViewFrame != viewFrame) {
                continue;
            }

            const int n = (i < ui.viewSplitter->count() - 1) ? i + 1 : 0;
            auto* nextViewFrame = qobject_cast<ViewFrame*>(ui.viewSplitter->widget(n));
            if (!nextViewFrame) {
                break;
            }

            if (activeViewFrame_ != nextViewFrame) {
                activeViewFrame_ = nextViewFrame;
                updateUIForCurrentPage();

                // if the window isn't active, eventFilter() won't be called,
                // so we should revert to the main palette here
                if (activeViewFrame_->palette().color(QPalette::Base) != qApp->palette().color(QPalette::Base)) {
                    activeViewFrame_->setPalette(qApp->palette());
                }
            }
            break;
        }

        ui.actionSplitView->setChecked(false);
        on_actionSplitView_triggered(false);
    }
}

ViewFrame* MainWindow::viewFrameForTabPage(TabPage* page) {
    if (!page) {
        return nullptr;
    }

    auto* sw = qobject_cast<QStackedWidget*>(page->parentWidget());
    if (!sw) {
        return nullptr;
    }

    auto* viewFrame = qobject_cast<ViewFrame*>(sw->parentWidget());
    return viewFrame;
}

void MainWindow::setTabIcon(TabPage* tabPage) {
    ViewFrame* viewFrame = viewFrameForTabPage(tabPage);
    if (!viewFrame) {
        return;
    }

    const bool isCustomized = tabPage->hasCustomizedView() || tabPage->hasInheritedCustomizedView();
    auto* tabBar = viewFrame->getTabBar();
    const int index = viewFrame->getStackedWidget()->indexOf(tabPage);

    if (!isCustomized) {
        if (!tabBar->tabIcon(index).isNull()) {
            tabBar->setTabIcon(index, QIcon());
        }
        return;
    }

    // set the tab icon of a customized folder to its view mode
    switch (tabPage->viewMode()) {
        case Fm::FolderView::IconMode:
            tabBar->setTabIcon(index, QIcon::fromTheme(QLatin1String("view-list-icons"),
                                                       style()->standardIcon(QStyle::SP_FileDialogContentsView)));
            break;
        case Fm::FolderView::CompactMode:
            tabBar->setTabIcon(index, QIcon::fromTheme(QLatin1String("view-list-text"),
                                                       style()->standardIcon(QStyle::SP_FileDialogListView)));
            break;
        case Fm::FolderView::DetailedListMode:
            tabBar->setTabIcon(index, QIcon::fromTheme(QLatin1String("view-list-details"),
                                                       style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
            break;
        case Fm::FolderView::ThumbnailMode:
            tabBar->setTabIcon(index, QIcon::fromTheme(QLatin1String("view-preview"),
                                                       style()->standardIcon(QStyle::SP_FileDialogInfoView)));
            break;
    }
}

}  // namespace PCManFM
