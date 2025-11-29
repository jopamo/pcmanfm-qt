/* pcmanfm/mainwindow_menus.cpp */

#include <QActionGroup>
#include <QFontMetrics>
#include <QMenu>
#include <QMessageBox>
#include <QToolButton>

#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"

namespace PCManFM {

void MainWindow::toggleMenuBar(bool /*checked*/) {
    auto* app = qobject_cast<Application*>(qApp);
    if (!app) {
        return;
    }

    Settings& settings = app->settings();
    const bool showMenuBar = !settings.showMenuBar();

    if (!showMenuBar) {
        const auto reply = QMessageBox::warning(
            this, tr("Hide menu bar"), tr("This will hide the menu bar completely, use Ctrl+M to show it again."),
            QMessageBox::Ok | QMessageBox::Cancel);

        if (reply == QMessageBox::Cancel) {
            ui.actionMenu_bar->setChecked(true);
            return;
        }
    }

    ui.menubar->setVisible(showMenuBar);
    ui.actionMenu_bar->setChecked(showMenuBar);
    menuSep_->setVisible(!showMenuBar);
    ui.actionMenu->setVisible(!showMenuBar);
    settings.setShowMenuBar(showMenuBar);
}

void MainWindow::updateRecenMenu() {
    auto* app = qobject_cast<Application*>(qApp);
    if (!app) {
        return;
    }

    Settings& settings = app->settings();
    const int recentNumber = settings.getRecentFilesNumber();
    const auto actions = ui.menuRecentFiles->actions();

    // there is a separator and a clear action
    if (actions.size() < recentNumber + 2) {
        return;
    }

    const auto recentFiles = settings.getRecentFiles();
    const int recentSize = recentFiles.size();
    QFontMetrics metrics(ui.menuRecentFiles->font());
    const int w = 150 * metrics.horizontalAdvance(QLatin1Char(' '));  // for eliding long texts

    for (int i = 0; i < recentNumber; ++i) {
        if (i < recentSize) {
            auto text = recentFiles.value(i)
                            .replace(QLatin1Char('&'), QLatin1String("&&"))
                            .replace(QLatin1Char('\t'), QLatin1Char(' '));

            actions.at(i)->setText(metrics.elidedText(text, Qt::ElideMiddle, w));

            QIcon icon;
            auto mimeType = Fm::MimeType::guessFromFileName(recentFiles.at(i).toLocal8Bit().constData());
            if (!mimeType->isUnknownType()) {
                if (auto icn = mimeType->icon()) {
                    icon = icn->qicon();
                }
            }

            actions.at(i)->setIcon(icon);
            actions.at(i)->setData(recentFiles.at(i));
            actions.at(i)->setVisible(true);
        } else {
            actions.at(i)->setText(QString());
            actions.at(i)->setIcon(QIcon());
            actions.at(i)->setData(QVariant());
            actions.at(i)->setVisible(false);
        }
    }

    ui.actionClearRecent->setEnabled(recentSize != 0);
}

void MainWindow::clearRecentMenu() {
    auto* app = qobject_cast<Application*>(qApp);
    if (!app) {
        return;
    }

    Settings& settings = app->settings();
    settings.clearRecentFiles();
    updateRecenMenu();
}

void MainWindow::lanunchRecentFile() {
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }

    auto* app = qobject_cast<Application*>(qApp);
    if (!app) {
        return;
    }

    Settings& settings = app->settings();
    const QString pathStr = action->data().toString();
    if (pathStr.isEmpty()) {
        return;
    }

    settings.addRecentFile(pathStr);

    const QByteArray pathArray = pathStr.toLocal8Bit();
    auto path = Fm::FilePath::fromLocalPath(pathArray.constData());
    Fm::FilePathList pathList;
    pathList.push_back(std::move(path));
    fileLauncher_.launchPaths(nullptr, pathList);
}

void MainWindow::updateStatusBarForCurrentPage() {
    TabPage* tabPage = currentPage();
    if (!tabPage) {
        ui.statusbar->clearMessage();
        return;
    }

    QString text = tabPage->statusText(TabPage::StatusTextSelectedFiles);
    if (text.isEmpty()) {
        text = tabPage->statusText(TabPage::StatusTextNormal);
    }
    ui.statusbar->showMessage(text);
}

void MainWindow::updateViewMenuForCurrentPage() {
    if (updatingViewMenu_) {  // prevent recursive calls
        return;
    }

    updatingViewMenu_ = true;

    TabPage* tabPage = currentPage();
    if (tabPage) {
        // update menus
        ui.actionShowHidden->setChecked(tabPage->showHidden());
        ui.actionPreserveView->setChecked(tabPage->hasCustomizedView() && !tabPage->hasRecursiveCustomizedView());
        ui.actionPreserveViewRecursive->setChecked(tabPage->hasRecursiveCustomizedView());
        ui.actionGoToCustomizedViewSource->setVisible(tabPage->hasInheritedCustomizedView());

        // view mode
        QAction* modeAction = nullptr;

        switch (tabPage->viewMode()) {
            case Fm::FolderView::IconMode:
                modeAction = ui.actionIconView;
                break;

            case Fm::FolderView::CompactMode:
                modeAction = ui.actionCompactView;
                break;

            case Fm::FolderView::DetailedListMode:
                modeAction = ui.actionDetailedList;
                break;

            case Fm::FolderView::ThumbnailMode:
                modeAction = ui.actionThumbnailView;
                break;
        }

        Q_ASSERT(modeAction != nullptr);
        modeAction->setChecked(true);

        // sort menu
        // WARNING: Since libfm-qt may have a column that is not handled here,
        // we should prevent a crash by setting all actions to null first and
        // check their action group later.
        QAction* sortActions[Fm::FolderModel::NumOfColumns];
        for (int i = 0; i < Fm::FolderModel::NumOfColumns; ++i) {
            sortActions[i] = nullptr;
        }
        sortActions[Fm::FolderModel::ColumnFileName] = ui.actionByFileName;
        sortActions[Fm::FolderModel::ColumnFileMTime] = ui.actionByMTime;
        sortActions[Fm::FolderModel::ColumnFileCrTime] = ui.actionByCrTime;
        sortActions[Fm::FolderModel::ColumnFileDTime] = ui.actionByDTime;
        sortActions[Fm::FolderModel::ColumnFileSize] = ui.actionByFileSize;
        sortActions[Fm::FolderModel::ColumnFileType] = ui.actionByFileType;
        sortActions[Fm::FolderModel::ColumnFileOwner] = ui.actionByOwner;
        sortActions[Fm::FolderModel::ColumnFileGroup] = ui.actionByGroup;

        if (auto* group = ui.actionByFileName->actionGroup()) {
            const auto groupActions = group->actions();
            auto* action = sortActions[tabPage->sortColumn()];
            if (groupActions.contains(action)) {
                action->setChecked(true);
            } else {
                for (auto* a : groupActions) {
                    a->setChecked(false);
                }
            }
        }

        if (auto path = tabPage->path()) {
            ui.actionByDTime->setVisible(strcmp(path.toString().get(), "trash:///") == 0);
        }

        if (tabPage->sortOrder() == Qt::AscendingOrder) {
            ui.actionAscending->setChecked(true);
        } else {
            ui.actionDescending->setChecked(true);
        }

        ui.actionCaseSensitive->setChecked(tabPage->sortCaseSensitive());
        ui.actionFolderFirst->setChecked(tabPage->sortFolderFirst());
        ui.actionHiddenLast->setChecked(tabPage->sortHiddenLast());
    }

    updatingViewMenu_ = false;
}

// Update the enabled state of File and Edit actions for selected files
void MainWindow::updateSelectedActions() {
    bool hasAccessible = false;
    bool hasDeletable = false;
    int renamable = 0;

    if (TabPage* page = currentPage()) {
        const auto files = page->selectedFiles();
        for (const auto& file : files) {
            if (file->isAccessible()) {
                hasAccessible = true;
            }
            if (file->isDeletable()) {
                hasDeletable = true;
            }
            if (file->canSetName()) {
                ++renamable;
            }
            if (hasAccessible && hasDeletable && renamable > 1) {
                break;
            }
        }
        ui.actionFileProperties->setEnabled(!files.empty());
        ui.actionCopyFullPath->setEnabled(files.size() == 1);
    }

    ui.actionCopy->setEnabled(hasAccessible);
    ui.actionCut->setEnabled(hasDeletable);
    ui.actionDelete->setEnabled(hasDeletable);
    ui.actionRename->setEnabled(renamable > 0);
    ui.actionBulkRename->setEnabled(renamable > 1);
}

void MainWindow::updateUIForCurrentPage(bool setFocus) {
    TabPage* tabPage = currentPage();

    if (tabPage) {
        setWindowTitle(tabPage->title());
        if (splitView_) {
            if (activeViewFrame_) {
                if (auto* pathBar = qobject_cast<Fm::PathBar*>(activeViewFrame_->getTopBar())) {
                    pathBar->setPath(tabPage->path());
                } else if (auto* pathEntry = qobject_cast<Fm::PathEdit*>(activeViewFrame_->getTopBar())) {
                    pathEntry->setText(tabPage->pathName());
                }
            }
        } else {
            if (pathEntry_) {
                pathEntry_->setText(tabPage->pathName());
            } else if (pathBar_) {
                pathBar_->setPath(tabPage->path());
            }
        }

        ui.statusbar->showMessage(tabPage->statusText());
        if (setFocus && tabPage->folderView() && tabPage->folderView()->childView()) {
            tabPage->folderView()->childView()->setFocus();
        }

        // update side pane
        ui.sidePane->setCurrentPath(tabPage->path());
        ui.sidePane->setShowHidden(tabPage->showHidden());

        // update back/forward/up toolbar buttons
        ui.actionGoUp->setEnabled(tabPage->canUp());
        ui.actionGoBack->setEnabled(tabPage->canBackward());
        ui.actionGoForward->setEnabled(tabPage->canForward());

        ui.actionOpenAsAdmin->setEnabled(tabPage->path() && tabPage->path().isNative());

        updateViewMenuForCurrentPage();
        updateStatusBarForCurrentPage();
    }

    // also update the enabled state of File and Edit actions
    updateSelectedActions();

    bool isWritable = false;
    bool isNative = false;

    if (tabPage && tabPage->folder()) {
        if (auto info = tabPage->folder()->info()) {
            isWritable = info->isWritable();
            isNative = info->isNative();
        }
    }

    ui.actionPaste->setEnabled(isWritable);
    ui.menuCreateNew->setEnabled(isWritable);
    // disable creation shortcuts too
    ui.actionNewFolder->setEnabled(isWritable);
    ui.actionNewBlankFile->setEnabled(isWritable);
    ui.actionCreateLauncher->setEnabled(isWritable && isNative);
}

}  // namespace PCManFM
