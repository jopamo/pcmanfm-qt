/*
 * Custom folder view implementation for PCManFM-Qt
 * pcmanfm/view.cpp
 */

#include "view.h"

// Qt Headers
#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QGroupBox>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QProgressDialog>
#include <QPlainTextEdit>
#include <QClipboard>
#include <QStringList>
#include <QVBoxLayout>

#include <string>
#include <algorithm>
#include <limits>

// LibFM-Qt Headers
#include <libfm-qt6/filemenu.h>
#include <libfm-qt6/foldermenu.h>
#include <libfm-qt6/proxyfoldermodel.h>  // Ensure this is included for ProxyFolderModel usage

// Local Headers
#include "application.h"
#include "launcher.h"
#include "mainwindow.h"
#include "settings.h"
#include "../src/core/fs_ops.h"
#include "../src/ui/archivejob.h"
#include "../src/ui/archiveextractjob.h"
#include "../src/ui/hexeditorwindow.h"
#include "../src/ui/disassemblywindow.h"

namespace PCManFM {

namespace {

// Helper to access Application settings concisely
Settings& appSettings() {
    return static_cast<Application*>(qApp)->settings();
}

struct ChecksumWindowWidgets {
    QPointer<QDialog> dialog;
    QLabel* pathLabel = nullptr;
    QPlainTextEdit* checksumEdit = nullptr;
    QGroupBox* errorBox = nullptr;
    QPlainTextEdit* errorEdit = nullptr;
};

QHash<QString, ChecksumWindowWidgets>& checksumWindows() {
    static QHash<QString, ChecksumWindowWidgets> windows;
    return windows;
}

QString stripArchiveExtension(const QString& fileName) {
    const QString lower = fileName.toLower();
    static const QStringList suffixes = {
        QStringLiteral(".tar.zst"), QStringLiteral(".tar.lz4"), QStringLiteral(".tar.gz"), QStringLiteral(".tar.bz2"),
        QStringLiteral(".tar.xz"),  QStringLiteral(".tar"),     QStringLiteral(".tzst"),   QStringLiteral(".txz"),
        QStringLiteral(".tbz2"),    QStringLiteral(".tbz"),     QStringLiteral(".tgz"),    QStringLiteral(".zip"),
        QStringLiteral(".cpio"),    QStringLiteral(".ar"),      QStringLiteral(".7z"),     QStringLiteral(".iso"),
        QStringLiteral(".xar"),     QStringLiteral(".rpm"),     QStringLiteral(".deb")};

    for (const QString& suffix : suffixes) {
        if (lower.endsWith(suffix)) {
            const int keep = fileName.size() - suffix.size();
            QString stem = fileName.left(keep);
            if (stem.isEmpty()) {
                stem = QStringLiteral("extracted");
            }
            return stem;
        }
    }
    return QString();
}

bool isSupportedArchive(const QString& path, QString* destinationOut) {
    QFileInfo info(path);
    const QString stem = stripArchiveExtension(info.fileName());
    if (stem.isEmpty()) {
        return false;
    }
    if (destinationOut) {
        *destinationOut = info.absolutePath() + QLatin1Char('/') + stem;
    }
    return true;
}

}  // namespace

void View::removeLibfmArchiverActions(Fm::FileMenu* menu) {
    if (!menu) {
        return;
    }
    const auto actions = menu->actions();
    for (QAction* action : actions) {
        const QString text = action->text().toLower();
        if (text.contains(QStringLiteral("compress")) || text.contains(QStringLiteral("extract here")) ||
            text.contains(QStringLiteral("extract to"))) {
            menu->removeAction(action);
            action->deleteLater();
        }
    }
}

View::View(Fm::FolderView::ViewMode mode, QWidget* parent) : Fm::FolderView(mode, parent) {
    updateFromSettings(appSettings());
}

View::~View() = default;

void View::onFileClicked(int type, const std::shared_ptr<const Fm::FileInfo>& fileInfo) {
    if (type == MiddleClick) {
        if (fileInfo && fileInfo->isDir()) {
            // fileInfo->path() should not be used directly here for virtual locations
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

void View::onOpenInHexEditor() {
    auto* menu = qobject_cast<Fm::FileMenu*>(sender()->parent());
    if (!menu) {
        return;
    }

    const auto files = menu->files();
    if (files.size() != 1 || !files.front()) {
        QMessageBox::warning(window(), tr("Hex editor"), tr("Select a single file to open in the hex editor."));
        return;
    }
    const auto& file = files.front();
    if (file->isDir()) {
        QMessageBox::warning(window(), tr("Hex editor"), tr("Hex editing is only available for files."));
        return;
    }
    if (!file->isNative()) {
        QMessageBox::warning(window(), tr("Hex editor"), tr("Only local files can be edited in hex."));
        return;
    }
    const auto localPath = file->path().localPath();
    if (!localPath) {
        QMessageBox::warning(window(), tr("Hex editor"), tr("Unable to resolve local path for selection."));
        return;
    }

    auto* editor = new HexEditorWindow();
    editor->setAttribute(Qt::WA_DeleteOnClose);
    QString error;
    if (!editor->openFile(QString::fromUtf8(localPath.get()), error)) {
        QMessageBox::warning(window(), tr("Hex editor"),
                             error.isEmpty() ? tr("Failed to open file in hex editor.") : error);
        editor->deleteLater();
        return;
    }
    editor->resize(900, 600);
    editor->show();
}

void View::onDisassembleWithCapstone() {
    auto* menu = qobject_cast<Fm::FileMenu*>(sender()->parent());
    if (!menu) {
        return;
    }

    const auto files = menu->files();
    if (files.size() != 1 || !files.front()) {
        QMessageBox::warning(window(), tr("Disassembly"), tr("Select a single file to disassemble."));
        return;
    }

    const auto& file = files.front();
    if (file->isDir()) {
        QMessageBox::warning(window(), tr("Disassembly"), tr("Disassembly is only available for files."));
        return;
    }
    if (!file->isNative()) {
        QMessageBox::warning(window(), tr("Disassembly"), tr("Only local files can be disassembled."));
        return;
    }

    const auto localPath = file->path().localPath();
    if (!localPath) {
        QMessageBox::warning(window(), tr("Disassembly"), tr("Unable to resolve local path for selection."));
        return;
    }

    auto* viewer = new DisassemblyWindow();
    QString error;
    if (!viewer->openFile(QString::fromUtf8(localPath.get()), error)) {
        QMessageBox::warning(window(), tr("Disassembly"), error.isEmpty() ? tr("Failed to disassemble file.") : error);
        viewer->deleteLater();
        return;
    }

    viewer->resize(960, 720);
    viewer->show();
}

void View::onCalculateBlake3() {
    auto* menu = qobject_cast<Fm::FileMenu*>(sender()->parent());
    if (!menu) {
        return;
    }

    const auto files = menu->files();
    if (files.empty()) {
        return;
    }

    QStringList paths;
    paths.reserve(static_cast<int>(files.size()));
    for (const auto& file : files) {
        if (!file || file->isDir()) {
            QMessageBox::warning(window(), tr("BLAKE3 checksum"),
                                 tr("Checksum calculation is only available for files."));
            return;
        }
        if (!file->isNative()) {
            QMessageBox::warning(window(), tr("BLAKE3 checksum"),
                                 tr("BLAKE3 sums can only be calculated for local files."));
            return;
        }

        const auto localPath = file->path().localPath();
        if (!localPath) {
            QMessageBox::warning(window(), tr("BLAKE3 checksum"),
                                 tr("Could not resolve a local path for the selection."));
            return;
        }
        paths << QString::fromUtf8(localPath.get());
    }

    auto& windows = checksumWindows();

    auto ensureWindow = [this, &windows](const QString& path) -> ChecksumWindowWidgets& {
        auto it = windows.find(path);
        if (it == windows.end()) {
            it = windows.insert(path, ChecksumWindowWidgets{});
        }
        ChecksumWindowWidgets& widgets = it.value();

        if (!widgets.dialog) {
            auto* dialog = new QDialog();
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog->setModal(false);
            dialog->setWindowModality(Qt::NonModal);
            dialog->setMinimumWidth(640);
            dialog->setSizeGripEnabled(true);

            auto* layout = new QVBoxLayout(dialog);
            widgets.pathLabel = new QLabel(dialog);
            widgets.pathLabel->setWordWrap(true);
            layout->addWidget(widgets.pathLabel);

            const QFont monospace = QFontDatabase::systemFont(QFontDatabase::FixedFont);

            auto* checksumBox = new QGroupBox(tr("Checksums (editable)"), dialog);
            auto* checksumLayout = new QVBoxLayout(checksumBox);
            widgets.checksumEdit = new QPlainTextEdit(checksumBox);
            widgets.checksumEdit->setFont(monospace);
            widgets.checksumEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
            widgets.checksumEdit->setMinimumHeight(260);
            widgets.checksumEdit->setReadOnly(false);
            checksumLayout->addWidget(widgets.checksumEdit);
            layout->addWidget(checksumBox);

            widgets.errorBox = new QGroupBox(tr("Errors"), dialog);
            auto* errorsLayout = new QVBoxLayout(widgets.errorBox);
            widgets.errorEdit = new QPlainTextEdit(widgets.errorBox);
            widgets.errorEdit->setReadOnly(true);
            widgets.errorEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
            widgets.errorEdit->setFont(monospace);
            errorsLayout->addWidget(widgets.errorEdit);
            layout->addWidget(widgets.errorBox);
            widgets.errorBox->setVisible(false);

            auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
            auto* copyButton = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
            QPointer<QDialog> dialogPtr(dialog);
            QPlainTextEdit* checksumEdit = widgets.checksumEdit;
            QPlainTextEdit* errorEdit = widgets.errorEdit;
            QGroupBox* errorBox = widgets.errorBox;
            connect(copyButton, &QPushButton::clicked, dialog, [this, dialogPtr, checksumEdit, errorEdit, errorBox] {
                if (!dialogPtr) {
                    return;
                }
                QString text = checksumEdit ? checksumEdit->toPlainText() : QString();
                const QString errorsText =
                    (errorEdit && errorBox && errorBox->isVisible()) ? errorEdit->toPlainText() : QString();
                if (!errorsText.isEmpty()) {
                    if (!text.isEmpty()) {
                        text += QLatin1Char('\n');
                    }
                    text += tr("Errors:\n%1").arg(errorsText);
                }
                QApplication::clipboard()->setText(text);
            });
            connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
            layout->addWidget(buttons);

            connect(dialog, &QObject::destroyed, this, [path] { checksumWindows().remove(path); });

            widgets.dialog = dialog;
        }

        return widgets;
    };

    for (const auto& path : paths) {
        QString hash;
        QString error;

        const QByteArray nativePath = QFile::encodeName(path);
        FsOps::Error err;
        std::string outHash;
        if (!FsOps::blake3_file(nativePath.constData(), outHash, err)) {
            error =
                err.message.empty() ? tr("Failed to compute BLAKE3 checksum.") : QString::fromStdString(err.message);
        }
        else {
            hash = QString::fromLatin1(outHash.c_str());
        }

        ChecksumWindowWidgets& widgets = ensureWindow(path);
        if (widgets.dialog) {
            widgets.dialog->setWindowTitle(path);
        }
        if (widgets.pathLabel) {
            widgets.pathLabel->setText(tr("Path: %1").arg(path));
        }
        if (widgets.checksumEdit) {
            const QString text = hash.isEmpty() ? QString() : QStringLiteral("%1  %2").arg(hash, path);
            widgets.checksumEdit->setPlainText(text);
        }
        if (widgets.errorBox && widgets.errorEdit) {
            widgets.errorEdit->setPlainText(error);
            widgets.errorBox->setVisible(!error.isEmpty());
        }
        if (widgets.dialog) {
            widgets.dialog->show();
            widgets.dialog->raise();
            widgets.dialog->activateWindow();
        }
    }
}

void View::onSearch() {
    // reserved for integrating a search action from the context menu
}

void View::startArchiveCompression(const QStringList& paths) {
    if (paths.isEmpty()) {
        return;
    }

    QFileInfo first(paths.first());
    QString baseDir = first.absolutePath();
    QString defaultStem = first.completeBaseName();
    if (defaultStem.isEmpty()) {
        defaultStem = QStringLiteral("archive");
    }
    QString suggested = baseDir + QLatin1Char('/') + defaultStem + QStringLiteral(".tar.zst");

    const QString dest = QFileDialog::getSaveFileName(window(), tr("Save Archive"), suggested,
                                                      tr("tar.zst archive (*.tar.zst);;Tar archive (*.tar)"));
    if (dest.isEmpty()) {
        return;
    }

    QString outputPath = dest;
    if (!outputPath.endsWith(QStringLiteral(".tar.zst")) && !outputPath.endsWith(QStringLiteral(".tar"))) {
        outputPath += QStringLiteral(".tar.zst");
    }

    auto* job = new ArchiveJob(this);
    auto* dialog = new QProgressDialog(tr("Compressing files…"), tr("Cancel"), 0, 0, window());
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    dialog->setMinimumDuration(0);
    dialog->setValue(0);
    dialog->setLabelText(tr("Compressing files…"));

    connect(dialog, &QProgressDialog::canceled, job, &ArchiveJob::cancel);
    connect(job, &ArchiveJob::progress, this, [dialog](quint64 done, quint64 total, const QString& current) {
        if (total > 0 && total <= static_cast<quint64>(std::numeric_limits<int>::max())) {
            if (dialog->maximum() != static_cast<int>(total)) {
                dialog->setMaximum(static_cast<int>(total));
            }
            dialog->setValue(static_cast<int>(std::min<quint64>(done, total)));
        }
        else {
            dialog->setMaximum(0);  // busy indicator
        }
        if (!current.isEmpty()) {
            dialog->setLabelText(tr("Compressing %1").arg(current));
        }
    });
    connect(job, &ArchiveJob::finished, this, [this, dialog, job](bool ok, const QString& error) {
        dialog->hide();
        dialog->deleteLater();
        job->deleteLater();
        if (!ok) {
            QMessageBox::warning(window(), tr("Compression failed"),
                                 error.isEmpty() ? tr("The archive could not be created.") : error);
        }
    });

    job->start(paths, outputPath);
    dialog->show();
}

void View::startArchiveExtraction(const QString& archivePath, const QString& destinationDir) {
    auto* job = new ArchiveExtractJob(this);
    auto* dialog = new QProgressDialog(tr("Extracting archive…"), tr("Cancel"), 0, 0, window());
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setAutoClose(false);
    dialog->setAutoReset(false);
    dialog->setMinimumDuration(0);
    dialog->setValue(0);
    dialog->setLabelText(tr("Extracting files…"));

    connect(dialog, &QProgressDialog::canceled, job, &ArchiveExtractJob::cancel);
    connect(job, &ArchiveExtractJob::progress, this, [dialog](quint64 done, quint64 total, const QString& current) {
        if (total > 0 && total <= static_cast<quint64>(std::numeric_limits<int>::max())) {
            if (dialog->maximum() != static_cast<int>(total)) {
                dialog->setMaximum(static_cast<int>(total));
            }
            dialog->setValue(static_cast<int>(std::min<quint64>(done, total)));
        }
        else {
            dialog->setMaximum(0);
        }
        if (!current.isEmpty()) {
            dialog->setLabelText(tr("Extracting %1").arg(current));
        }
    });
    connect(job, &ArchiveExtractJob::finished, this, [this, dialog, job](bool ok, const QString& error) {
        dialog->hide();
        dialog->deleteLater();
        job->deleteLater();
        if (!ok) {
            QMessageBox::warning(window(), tr("Extraction failed"),
                                 error.isEmpty() ? tr("The archive could not be extracted.") : error);
        }
    });

    job->start(archivePath, destinationDir);
    dialog->show();
}

void View::prepareFileMenu(Fm::FileMenu* menu) {
    Settings& settings = appSettings();
    menu->setConfirmDelete(settings.confirmDelete());
    menu->setConfirmTrash(settings.confirmTrash());
    menu->setUseTrash(settings.useTrash());

    bool allNative = true;
    bool allDirectory = true;
    bool hasDirectory = false;

    auto files = menu->files();
    for (auto& fi : files) {
        if (!fi) {
            allNative = false;
            allDirectory = false;
            continue;
        }

        if (fi->isDir()) {
            hasDirectory = true;
        }
        else {
            allDirectory = false;
        }

        if (!fi->isNative()) {
            allNative = false;
        }
    }

    const bool allFiles = !files.empty() && !hasDirectory;

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

        if (allFiles && allNative) {
            auto* action = new QAction(QIcon::fromTheme(QStringLiteral("accessories-calculator")),
                                       tr("Calculate BLAKE3 Checksum"), menu);
            connect(action, &QAction::triggered, this, &View::onCalculateBlake3);
            menu->insertAction(menu->separator3(), action);

            action = new QAction(QIcon::fromTheme(QStringLiteral("application-x-executable")),
                                 tr("Disassemble (Capstone)"), menu);
            connect(action, &QAction::triggered, this, &View::onDisassembleWithCapstone);
            menu->insertAction(menu->separator3(), action);

            action = new QAction(QIcon::fromTheme(QStringLiteral("accessories-text-editor")), tr("Open in Hex Editor"),
                                 menu);
            connect(action, &QAction::triggered, this, &View::onOpenInHexEditor);
            menu->insertAction(menu->separator3(), action);
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

    QStringList compressPaths;
    QString extractArchivePath;
    QString extractDestination;
    if (allNative && !files.empty()) {
        compressPaths.reserve(static_cast<int>(files.size()));
        for (const auto& fi : files) {
            if (!fi || !fi->isNative()) {
                compressPaths.clear();
                break;
            }
            auto localPath = fi->path().localPath();
            if (!localPath) {
                compressPaths.clear();
                break;
            }
            const QString pathStr = QString::fromUtf8(localPath.get());
            compressPaths << pathStr;
            if (files.size() == 1 && !fi->isDir() && extractArchivePath.isEmpty()) {
                QString candidateDestination;
                if (isSupportedArchive(pathStr, &candidateDestination)) {
                    extractArchivePath = pathStr;
                    extractDestination = candidateDestination;
                }
            }
        }
        if (files.size() == 1 && extractArchivePath == compressPaths.value(0)) {
            // Do not offer "Compress" when right-clicking an archive itself
            compressPaths.clear();
        }
    }

    removeLibfmArchiverActions(menu);

    if (!extractArchivePath.isEmpty() && !extractDestination.isEmpty()) {
        auto* action = new QAction(QIcon::fromTheme(QStringLiteral("archive-extract")), tr("Extract Archive"), menu);
        connect(action, &QAction::triggered, this, [this, extractArchivePath, extractDestination] {
            startArchiveExtraction(extractArchivePath, extractDestination);
        });
        menu->insertAction(menu->separator3(), action);
    }

    if (!compressPaths.isEmpty()) {
        auto* action =
            new QAction(QIcon::fromTheme(QStringLiteral("application-x-tar")), tr("Compress to Archive…"), menu);
        connect(action, &QAction::triggered, this, [this, compressPaths] { startArchiveCompression(compressPaths); });
        menu->insertAction(menu->separator3(), action);
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
