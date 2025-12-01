/*
 * Main window navigation implementation
 * pcmanfm/mainwindow_navigation.cpp
 */

#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"

// Qt Headers
#include <QMessageBox>
#include <QStandardPaths>
#include <QTimer>

namespace PCManFM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

}  // namespace

void MainWindow::chdir(Fm::FilePath path, ViewFrame* viewFrame) {
    if (!viewFrame) {
        return;
    }

    // Wait until queued events are processed to prevent re-entrant issues
    // during rapid navigation or initialization.
    QTimer::singleShot(0, viewFrame, [this, path, viewFrame] {
        // Double check validity inside the slot execution
        if (TabPage* page = currentPage(viewFrame)) {
            page->chdir(path, true);
            setTabIcon(page);

            if (viewFrame == activeViewFrame_) {
                updateUIForCurrentPage();
            }
            else {
                // Update background view frames' location bar
                if (auto* pathBar = qobject_cast<Fm::PathBar*>(viewFrame->getTopBar())) {
                    pathBar->setPath(page->path());
                }
                else if (auto* pathEntry = qobject_cast<Fm::PathEdit*>(viewFrame->getTopBar())) {
                    pathEntry->setText(page->pathName());
                }
            }
        }
    });
}

void MainWindow::on_actionGoUp_triggered() {
    QTimer::singleShot(0, this, [this] {
        if (TabPage* page = currentPage()) {
            page->up();
            setTabIcon(page);
            updateUIForCurrentPage();
        }
    });
}

void MainWindow::on_actionGoBack_triggered() {
    QTimer::singleShot(0, this, [this] {
        if (TabPage* page = currentPage()) {
            page->backward();
            setTabIcon(page);
            updateUIForCurrentPage();
        }
    });
}

void MainWindow::on_actionGoForward_triggered() {
    QTimer::singleShot(0, this, [this] {
        if (TabPage* page = currentPage()) {
            page->forward();
            setTabIcon(page);
            updateUIForCurrentPage();
        }
    });
}

void MainWindow::on_actionHome_triggered() {
    chdir(Fm::FilePath::homeDir());
}

void MainWindow::on_actionReload_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    page->reload();

    // In single-view mode, update the main location bar
    if (pathEntry_) {
        pathEntry_->setText(page->pathName());
    }
}

void MainWindow::on_actionConnectToServer_triggered() {
    static_cast<Application*>(qApp)->connectToServer();
}

void MainWindow::on_actionComputer_triggered() {
    chdir(Fm::FilePath::fromUri("computer:///"));
}

void MainWindow::on_actionApplications_triggered() {
    chdir(Fm::FilePath::fromUri("menu://applications/"));
}

void MainWindow::on_actionTrash_triggered() {
    chdir(Fm::FilePath::fromUri("trash:///"));
}

void MainWindow::on_actionNetwork_triggered() {
    chdir(Fm::FilePath::fromUri("network:///"));
}

void MainWindow::on_actionDesktop_triggered() {
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.isEmpty()) {
        chdir(Fm::FilePath::fromLocalPath(desktop.toLocal8Bit().constData()));
    }
}

void MainWindow::on_actionOpenAsAdmin_triggered() {
    if (TabPage* page = currentPage()) {
        if (auto path = page->path()) {
            if (path.isNative()) {
                // Construct admin:// URI
                Fm::CStrPtr admin{g_strconcat("admin://", path.localPath().get(), nullptr)};
                chdir(Fm::FilePath::fromPathStr(admin.get()));
            }
        }
    }
}

void MainWindow::on_actionOpenAsRoot_triggered() {
    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    auto* app = static_cast<Application*>(qApp);
    Settings& settings = app->settings();

    if (!settings.suCommand().isEmpty()) {
        // Run the su command
        // FIXME: Better to detect current binary path dynamically than hardcoding or assuming logic
        QByteArray suCommand = settings.suCommand().toLocal8Bit();
        QByteArray programCommand = app->applicationFilePath().toLocal8Bit();
        programCommand += " %U";

        // if %s exists in the su command, substitute it with the program
        const int substPos = suCommand.indexOf("%s");
        if (substPos != -1) {
            suCommand.replace(substPos, 2, programCommand);
        }
        else {
            suCommand += ' ' + programCommand;
        }

        // Launch via GAppInfo
        Fm::GAppInfoPtr appInfo{
            g_app_info_create_from_commandline(suCommand.constData(), nullptr, GAppInfoCreateFlags(0), nullptr),
            false  // transfer_ownership
        };

        if (appInfo) {
            auto cwd = page->path();
            Fm::GErrorPtr err;
            auto uri = cwd.uri();
            GList* uris = g_list_prepend(nullptr, uri.get());

            if (!g_app_info_launch_uris(appInfo.get(), uris, nullptr, &err)) {
                QMessageBox::critical(this, tr("Error"), QString::fromUtf8(err->message));
            }

            g_list_free(uris);
        }
    }
    else {
        // Show error and open preferences
        QMessageBox::critical(this, tr("Error"), tr("Switch user command is not set"));
        app->preferences(QStringLiteral("advanced"));
    }
}

void MainWindow::on_actionFindFiles_triggered() {
    auto* app = static_cast<Application*>(qApp);

    TabPage* page = currentPage();
    if (!page) {
        return;
    }

    const auto files = page->selectedFiles();
    QStringList paths;

    if (!files.empty()) {
        for (const auto& file : files) {
            // Use local path if possible, fallback to display name for virtual paths
            // NOTE: This logic assumes findFiles handles display names or paths correctly
            if (file->isDir()) {
                if (file->isNative()) {
                    paths.append(QString::fromStdString(file->path().localPath().get()));
                }
                else {
                    paths.append(QString::fromStdString(file->path().toString().get()));
                }
            }
        }
    }

    if (paths.isEmpty()) {
        paths.append(page->pathName());
    }

    app->findFiles(paths);
}

void MainWindow::on_actionOpenTerminal_triggered() {
    if (TabPage* page = currentPage()) {
        static_cast<Application*>(qApp)->openFolderInTerminal(page->path());
    }
}

}  // namespace PCManFM
