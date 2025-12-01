/*
 * Main window menu management implementation
 * pcmanfm/mainwindow_menus.cpp
 */

#include <QActionGroup>
#include <QCursor>
#include <QFontMetrics>
#include <QMenu>
#include <QMessageBox>
#include <QPoint>
#include <QToolBar>
#include <QToolButton>

#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"

// If you are migrating away from libfm, replace these with your new backend interfaces
#include <libfm-qt6/core/mimetype.h>

namespace PCManFM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

void MainWindow::toggleMenuBar(bool /*checked*/) {
    Settings& settings = appSettings();
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

    if (menuSep_) {
        menuSep_->setVisible(!showMenuBar);
    }

    ui.actionMenu->setVisible(!showMenuBar);
    settings.setShowMenuBar(showMenuBar);
}

void MainWindow::on_actionMenu_triggered() {
    if (!ui.menubar) {
        return;
    }

    QMenu popup(this);
    const auto actions = ui.menubar->actions();
    for (auto* action : actions) {
        if (!action || !action->isVisible()) {
            continue;
        }
        popup.addAction(action);
    }

    QPoint pos = QCursor::pos();
    if (ui.toolBar) {
        if (QWidget* toolButton = ui.toolBar->widgetForAction(ui.actionMenu)) {
            pos = toolButton->mapToGlobal(QPoint(0, toolButton->height()));
        }
    }

    popup.exec(pos);
}

void MainWindow::updateRecenMenu() {
    Settings& settings = appSettings();
    const int recentNumber = settings.getRecentFilesNumber();
    const auto actions = ui.menuRecentFiles->actions();

    // there is a separator and a clear action
    if (actions.size() < recentNumber + 2) {
        return;
    }

    const auto recentFiles = settings.getRecentFiles();
    const int recentSize = recentFiles.size();

    QFontMetrics metrics(ui.menuRecentFiles->font());
    // Optimization: Calculate max width once
    const int w = 150 * metrics.horizontalAdvance(QLatin1Char(' '));

    for (int i = 0; i < recentNumber; ++i) {
        if (i < recentSize) {
            auto text = recentFiles.value(i);

            // Format text for menu display (escape ampersands, convert tabs)
            text.replace(QLatin1Char('&'), QStringLiteral("&&")).replace(QLatin1Char('\t'), QLatin1Char(' '));

            actions.at(i)->setText(metrics.elidedText(text, Qt::ElideMiddle, w));

            QIcon icon;
            // LibFM Migration Note: Replace Fm::MimeType with standard QFileInfo/QMimeDatabase logic later
            auto mimeType = Fm::MimeType::guessFromFileName(recentFiles.at(i).toLocal8Bit().constData());
            if (!mimeType->isUnknownType()) {
                if (auto icn = mimeType->icon()) {
                    icon = icn->qicon();
                }
            }

            actions.at(i)->setIcon(icon);
            actions.at(i)->setData(recentFiles.at(i));
            actions.at(i)->setVisible(true);
        }
        else {
            actions.at(i)->setText(QString());
            actions.at(i)->setIcon(QIcon());
            actions.at(i)->setData(QVariant());
            actions.at(i)->setVisible(false);
        }
    }

    ui.actionClearRecent->setEnabled(recentSize != 0);
}

void MainWindow::clearRecentMenu() {
    Settings& settings = appSettings();
    settings.clearRecentFiles();
    updateRecenMenu();
}

void MainWindow::lanunchRecentFile() {
    auto* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }

    const QString pathStr = action->data().toString();
    if (pathStr.isEmpty()) {
        return;
    }

    Settings& settings = appSettings();
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

        if (modeAction) {
            modeAction->setChecked(true);
        }

        // sort menu
        // WARNING: Since libfm-qt may have a column that is not handled here,
        // we should prevent a crash by checking bounds carefully.

        QAction* sortActions[Fm::FolderModel::NumOfColumns];
        std::fill(std::begin(sortActions), std::end(sortActions), nullptr);

        sortActions[Fm::FolderModel::ColumnFileName] = ui.actionByFileName;
        sortActions[Fm::FolderModel::ColumnFileMTime] = ui.actionByMTime;
        sortActions[Fm::FolderModel::ColumnFileCrTime] = ui.actionByCrTime;
        sortActions[Fm::FolderModel::ColumnFileDTime] = ui.actionByDTime;
        sortActions[Fm::FolderModel::ColumnFileSize] = ui.actionByFileSize;
        sortActions[Fm::FolderModel::ColumnFileType] = ui.actionByFileType;
        sortActions[Fm::FolderModel::ColumnFileOwner] = ui.actionByOwner;
        sortActions[Fm::FolderModel::ColumnFileGroup] = ui.actionByGroup;

        // Ensure we handle action groups correctly
        if (auto* group = ui.actionByFileName->actionGroup()) {
            const auto groupActions = group->actions();

            // Validate index before array access
            const int sortCol = tabPage->sortColumn();
            QAction* targetAction = nullptr;

            if (sortCol >= 0 && sortCol < Fm::FolderModel::NumOfColumns) {
                targetAction = sortActions[sortCol];
            }

            if (targetAction && groupActions.contains(targetAction)) {
                targetAction->setChecked(true);
            }
            else {
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
        }
        else {
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
    size_t fileCount = 0;

    if (TabPage* page = currentPage()) {
        const auto files = page->selectedFiles();
        fileCount = files.size();

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
            // Optimization: break early if all flags are satisfied
            if (hasAccessible && hasDeletable && renamable > 1) {
                break;
            }
        }
        ui.actionFileProperties->setEnabled(!files.empty());
        ui.actionCopyFullPath->setEnabled(fileCount == 1);
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

        // Update Location Bar
        // NOTE: This logic supports both path bar and path entry modes
        Fm::PathBar* pathBar = nullptr;
        Fm::PathEdit* pathEdit = nullptr;

        if (splitView_) {
            if (activeViewFrame_) {
                pathBar = qobject_cast<Fm::PathBar*>(activeViewFrame_->getTopBar());
                if (!pathBar) {
                    pathEdit = qobject_cast<Fm::PathEdit*>(activeViewFrame_->getTopBar());
                }
            }
        }
        else {
            // In single view, use the member variables if available
            pathBar = pathBar_;
            pathEdit = pathEntry_;
        }

        if (pathBar) {
            pathBar->setPath(tabPage->path());
        }
        else if (pathEdit) {
            pathEdit->setText(tabPage->pathName());
        }

        ui.statusbar->showMessage(tabPage->statusText());

        if (setFocus && tabPage->folderView() && tabPage->folderView()->childView()) {
            tabPage->folderView()->childView()->setFocus();
        }

        // update side pane
        if (ui.sidePane) {
            ui.sidePane->setCurrentPath(tabPage->path());
            ui.sidePane->setShowHidden(tabPage->showHidden());
        }

        // update back/forward/up toolbar buttons
        ui.actionGoUp->setEnabled(tabPage->canUp());
        ui.actionGoBack->setEnabled(tabPage->canBackward());
        ui.actionGoForward->setEnabled(tabPage->canForward());

        // Safety check for path validity
        bool isNative = false;
        if (auto path = tabPage->path()) {
            isNative = path.isNative();
        }
        ui.actionOpenAsAdmin->setEnabled(isNative);

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
