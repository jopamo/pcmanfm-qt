/*
 * File launcher implementation for PCManFM-Qt
 * pcmanfm/launcher.cpp
 */

#include "launcher.h"

// LibFM-Qt Headers
#include <libfm-qt6/core/filepath.h>

// Qt Headers
#include <QApplication>
#include <QFileInfo>

// Local Headers
#include "application.h"
#include "mainwindow.h"

namespace PCManFM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

Launcher::Launcher(PCManFM::MainWindow* mainWindow)
    : Fm::FileLauncher(), mainWindow_(mainWindow), openInNewTab_(false), openWithDefaultFileManager_(false) {
    setQuickExec(appSettings().quickExec());
}

Launcher::~Launcher() = default;

// open a list of folders either in an existing main window, in new tabs, or with the system file manager
bool Launcher::openFolder(GAppLaunchContext* ctx, const Fm::FileInfoList& folderInfos, Fm::GErrorPtr& /*err*/) {
    if (folderInfos.empty()) {
        return false;
    }

    Settings& settings = appSettings();
    MainWindow* mainWindow = mainWindow_;

    auto it = folderInfos.cbegin();
    const auto end = folderInfos.cend();

    // Handle the first folder specifically (it might create the window)
    Fm::FilePath firstPath = (*it)->path();

    if (!mainWindow) {
        // If there is no PCManFM-Qt main window, we may delegate to the system default file manager.
        // This is done when:
        //   1) openWithDefaultFileManager_ is set (e.g. desktop launch)
        //   2) we are not explicitly opening in new tabs
        //   3) the default file manager exists and is not pcmanfm-qt itself
        if (openWithDefaultFileManager_ && !openInNewTab_) {
            auto defaultApp = Fm::GAppInfoPtr{g_app_info_get_default_for_type("inode/directory", FALSE), false};

            // Check if default app exists and is NOT this application
            if (defaultApp && std::strcmp(g_app_info_get_id(defaultApp.get()), "pcmanfm-qt.desktop") != 0) {
                for (const auto& folder : folderInfos) {
                    Fm::FileLauncher::launchWithDefaultApp(folder, ctx);
                }
                return true;
            }
        }

        // Fall back to opening a new PCManFM-Qt main window
        mainWindow = new MainWindow(std::move(firstPath));
        mainWindow->resize(settings.windowWidth(), settings.windowHeight());

        if (settings.windowMaximized()) {
            mainWindow->setWindowState(mainWindow->windowState() | Qt::WindowMaximized);
        }
    }
    else {
        // We already have a main window, either reuse the current tab or open a new one
        if (openInNewTab_) {
            mainWindow->addTab(std::move(firstPath));
        }
        else {
            mainWindow->chdir(std::move(firstPath));
        }
    }

    // Process remaining folders (always open in new tabs in the same window)
    ++it;  // Move past the first one we just handled
    for (; it != end; ++it) {
        mainWindow->addTab((*it)->path());
    }

    mainWindow->show();
    mainWindow->raise();
    mainWindow->activateWindow();

    // Reset the tab flag for subsequent launches
    openInNewTab_ = false;

    return true;
}

void Launcher::launchedFiles(const Fm::FileInfoList& files) const {
    Settings& settings = appSettings();
    if (settings.getRecentFilesNumber() <= 0) {
        return;
    }

    for (const auto& file : files) {
        // Only add native files that are NOT directories to recent history
        if (file->isNative() && !file->isDir()) {
            settings.addRecentFile(QString::fromUtf8(file->path().localPath().get()));
        }
    }
}

void Launcher::launchedPaths(const Fm::FilePathList& paths) const {
    Settings& settings = appSettings();
    if (settings.getRecentFilesNumber() <= 0) {
        return;
    }

    for (const auto& path : paths) {
        if (!path.isNative()) {
            continue;
        }

        const QString pathStr = QString::fromUtf8(path.localPath().get());

        // QFileInfo check is required here because FilePath doesn't know if it's a dir
        // without querying the filesystem
        if (!QFileInfo(pathStr).isDir()) {
            settings.addRecentFile(pathStr);
        }
    }
}

}  // namespace PCManFM
