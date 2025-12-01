/*
 * Custom folder view implementation for PCManFM-Qt
 * pcmanfm/view.cpp
 */

#include "view.h"

// Qt Headers
#include <QAction>
#include <QApplication>
#include <QIcon>
#include <QMessageBox>

// LibFM-Qt Headers
#include <libfm-qt6/filemenu.h>
#include <libfm-qt6/foldermenu.h>
#include <libfm-qt6/proxyfoldermodel.h>  // Ensure this is included for ProxyFolderModel usage

// Local Headers
#include "application.h"
#include "launcher.h"
#include "mainwindow.h"
#include "settings.h"

namespace PCManFM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

View::View(Fm::FolderView::ViewMode mode, QWidget* parent) : Fm::FolderView(mode, parent) {
    updateFromSettings(appSettings());
}

View::~View() = default;

void View::onFileClicked(int type, const std::shared_ptr<const Fm::FileInfo>& fileInfo) {
    if (type == MiddleClick) {
        if (fileInfo && fileInfo->isDir()) {
            // fileInfo->path() should not be used directly here
            // it can misbehave for locations like computer:/// or network:///
            Fm::FileInfoList files;
            files.emplace_back(fileInfo);
            launchFiles(std::move(files), true);
        }
        return;
    }

    if (type == ActivatedClick) {
        if (!fileLauncher()) {
            return;
        }

        auto files = selectedFiles();
        if (files.empty()) {
            return;
        }

        // Prompt user if trying to open too many files at once
        if (files.size() > 20) {
            auto reply = QMessageBox::question(
                window(), tr("Many files"),
                tr("Do you want to open these %1 files?", nullptr, files.size()).arg(files.size()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

            if (reply == QMessageBox::No) {
                return;
            }
        }

        launchFiles(std::move(files));
        return;
    }

    // Delegate other click types to base class
    Fm::FolderView::onFileClicked(type, fileInfo);
}

void View::onNewWindow() {
    auto* menu = qobject_cast<Fm::FileMenu*>(sender()->parent());
    if (!menu)
        return;

    auto files = menu->files();

    if (files.size() == 1 && !files.front()->isDir()) {
        openFolderAndSelectFile(files.front());
    }
    else {
        static_cast<Application*>(qApp)->openFolders(std::move(files));
    }
}

void View::onNewTab() {
    auto* menu = qobject_cast<Fm::FileMenu*>(sender()->parent());
    if (!menu)
        return;

    auto files = menu->files();

    if (files.size() == 1 && !files.front()->isDir()) {
        openFolderAndSelectFile(files.front(), true);
    }
    else {
        launchFiles(std::move(files), true);
    }
}

void View::onOpenInTerminal() {
    auto* app = static_cast<Application*>(qApp);
    auto* menu = qobject_cast<Fm::FileMenu*>(sender()->parent());
    if (!menu)
        return;

    auto files = menu->files();
    for (auto& file : files) {
        app->openFolderInTerminal(file->path());
    }
}

void View::onSearch() {
    // reserved for integrating a search action from the context menu
}

void View::prepareFileMenu(Fm::FileMenu* menu) {
    Settings& settings = appSettings();
    menu->setConfirmDelete(settings.confirmDelete());
    menu->setConfirmTrash(settings.confirmTrash());
    menu->setUseTrash(settings.useTrash());

    bool allNative = true;
    bool allDirectory = true;

    auto files = menu->files();
    for (auto& fi : files) {
        if (!fi->isDir()) {
            allDirectory = false;
        }
        else if (!fi->isNative()) {
            allNative = false;
        }
    }

    if (allDirectory) {
        auto* action = new QAction(QIcon::fromTheme(QStringLiteral("tab-new")), tr("Open in New T&ab"), menu);
        connect(action, &QAction::triggered, this, &View::onNewTab);
        menu->insertAction(menu->separator1(), action);

        action = new QAction(QIcon::fromTheme(QStringLiteral("window-new")), tr("Open in New Win&dow"), menu);
        connect(action, &QAction::triggered, this, &View::onNewWindow);
        menu->insertAction(menu->separator1(), action);

        // search actions can be added here when integrated

        if (allNative) {
            action = new QAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), tr("Open in Termina&l"), menu);
            connect(action, &QAction::triggered, this, &View::onOpenInTerminal);
            menu->insertAction(menu->separator1(), action);
        }
    }
    else {
        if (menu->pasteAction()) {
            menu->pasteAction()->setVisible(false);
        }
        if (menu->createAction()) {
            menu->createAction()->setVisible(false);
        }

        // Special handling for search results
        if (folder() && folder()->path().hasUriScheme("search") && files.size() == 1 && !files.front()->isDir()) {
            auto* action = new QAction(QIcon::fromTheme(QStringLiteral("tab-new")), tr("Show in New T&ab"), menu);
            connect(action, &QAction::triggered, this, &View::onNewTab);
            menu->insertAction(menu->separator1(), action);

            action = new QAction(QIcon::fromTheme(QStringLiteral("window-new")), tr("Show in New Win&dow"), menu);
            connect(action, &QAction::triggered, this, &View::onNewWindow);
            menu->insertAction(menu->separator1(), action);
        }
    }
}

void View::prepareFolderMenu(Fm::FolderMenu* menu) {
    auto folder = folderInfo();
    if (folder && folder->isNative()) {
        auto* action =
            new QAction(QIcon::fromTheme(QStringLiteral("utilities-terminal")), tr("Open in Termina&l"), menu);

        // Capture by value is safe here as long as MainWindow outlives the menu action logic
        connect(action, &QAction::triggered, this,
                [folder] { static_cast<Application*>(qApp)->openFolderInTerminal(folder->path()); });

        menu->insertAction(menu->createAction(), action);
        menu->insertSeparator(menu->createAction());
    }
}

void View::updateFromSettings(Settings& settings) {
    setIconSize(Fm::FolderView::IconMode, QSize(settings.bigIconSize(), settings.bigIconSize()));
    setIconSize(Fm::FolderView::CompactMode, QSize(settings.smallIconSize(), settings.smallIconSize()));
    setIconSize(Fm::FolderView::ThumbnailMode, QSize(settings.thumbnailIconSize(), settings.thumbnailIconSize()));
    setIconSize(Fm::FolderView::DetailedListMode, QSize(settings.smallIconSize(), settings.smallIconSize()));

    setMargins(settings.folderViewCellMargins());

    setAutoSelectionDelay(settings.singleClick() ? settings.autoSelectionDelay() : 0);
    setCtrlRightClick(settings.ctrlRightClick());
    setScrollPerPixel(settings.scrollPerPixel());

    // Cast the model to ProxyFolderModel to access extended settings
    // Using dynamic_cast for safety, though we expect ProxyFolderModel here based on TabPage setup
    if (auto* proxyModel = dynamic_cast<Fm::ProxyFolderModel*>(model())) {
        proxyModel->setShowThumbnails(settings.showThumbnails());
        proxyModel->setBackupAsHidden(settings.backupAsHidden());
    }
}

void View::launchFiles(Fm::FileInfoList files, bool inNewTabs) {
    if (!fileLauncher()) {
        return;
    }

    if (auto* launcher = dynamic_cast<Launcher*>(fileLauncher())) {
        // this path is used for the desktop and similar cases where there might not be a MainWindow
        if (!launcher->hasMainWindow()) {
            if (!inNewTabs && launcher->openWithDefaultFileManager()) {
                launcher->launchFiles(nullptr, std::move(files));
                return;
            }

            Settings& settings = appSettings();
            if (inNewTabs || settings.singleWindowMode()) {
                MainWindow* window = MainWindow::lastActive();

                if (!window) {
                    const QWidgetList windows = qApp->topLevelWidgets();
                    // Iterate backwards to find the most recently active main window
                    for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
                        if ((*it)->inherits("PCManFM::MainWindow")) {
                            window = static_cast<MainWindow*>(*it);
                            break;
                        }
                    }
                }

                // If a window is found, launch in new tabs there.
                // Otherwise this will effectively open a new window if Launcher handles nullptr parent gracefully.
                Launcher tempLauncher(window);
                tempLauncher.openInNewTab();
                tempLauncher.launchFiles(nullptr, std::move(files));
                return;
            }
        }

        if (inNewTabs) {
            launcher->openInNewTab();
        }
    }

    fileLauncher()->launchFiles(nullptr, std::move(files));
}

void View::openFolderAndSelectFile(const std::shared_ptr<const Fm::FileInfo>& fileInfo, bool inNewTab) {
    if (auto* win = qobject_cast<MainWindow*>(window())) {
        Fm::FilePathList paths;
        paths.emplace_back(fileInfo->path());
        win->openFolderAndSelectFiles(std::move(paths), inNewTab);
    }
}

}  // namespace PCManFM
