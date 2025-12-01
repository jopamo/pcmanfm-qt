/*
 * Application implementation for PCManFM-Qt
 * pcmanfm/application.cpp
 */

#include "application.h"

// C/POSIX Headers
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

// Std Headers
#include <algorithm>
#include <unordered_map>
#include <vector>

// Glib/Gio Headers
#include <gio/gio.h>

// LibFM-Qt Headers
#include <libfm-qt6/core/bookmarks.h>
#include <libfm-qt6/core/fileinfojob.h>
#include <libfm-qt6/core/folderconfig.h>
#include <libfm-qt6/core/terminal.h>
#include <libfm-qt6/filepropsdialog.h>
#include <libfm-qt6/filesearchdialog.h>
#include <libfm-qt6/mountoperation.h>

// Qt Headers
#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QSessionManager>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>
#include <QVector>

// Local Headers
#include "applicationadaptor.h"
#include "applicationadaptorfreedesktopfilemanager.h"
#include "autorundialog.h"
#include "connectserverdialog.h"
#include "launcher.h"
#include "mainwindow.h"
#include "preferencesdialog.h"
#include "xdgdir.h"

namespace PCManFM {

static const char* serviceName = "org.pcmanfm.PCManFM";
static const char* ifaceName = "org.pcmanfm.Application";

//-----------------------------------------------------------------------------
// ProxyStyle
//-----------------------------------------------------------------------------

int ProxyStyle::styleHint(StyleHint hint,
                          const QStyleOption* option,
                          const QWidget* widget,
                          QStyleHintReturn* returnData) const {
    auto* app = qobject_cast<Application*>(qApp);

    if (hint == QStyle::SH_ItemView_ActivateItemOnSingleClick && app) {
        if (app->settings().singleClick()) {
            return true;
        }
        // fall back to the base style when single-click is disabled
        return QCommonStyle::styleHint(hint, option, widget, returnData);
    }

    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

//-----------------------------------------------------------------------------
// Signal Handling
//-----------------------------------------------------------------------------

static int sigterm_fd[2];

// Async-signal-safe handler
static void sigtermHandler(int) {
    char c = 1;
    // We ignore the return value because we are in a signal handler
    // and cannot handle errors effectively/safely here anyway.
    if (::write(sigterm_fd[0], &c, sizeof(c))) {
    }
}

//-----------------------------------------------------------------------------
// Application Class
//-----------------------------------------------------------------------------

Application::Application(int& argc, char** argv)
    : QApplication(argc, argv),
      libFm_(),
      settings_(),
      profileName_(QStringLiteral("default")),
      daemonMode_(false),
      preferencesDialog_(),
      editBookmarksialog_(),
      volumeMonitor_(nullptr),
      userDirsWatcher_(nullptr),
      openingLastTabs_(false) {
    argc_ = argc;
    argv_ = argv;

    setApplicationVersion(QStringLiteral(PCMANFM_QT_VERSION));
    setWindowIcon(QIcon::fromTheme(QStringLiteral("pcmanfm-qt")));

    QDBusConnection dbus = QDBusConnection::sessionBus();

    // Attempt to register service. If successful, we are the primary instance.
    if (dbus.registerService(QLatin1String(serviceName))) {
        isPrimaryInstance = true;

        setStyle(new ProxyStyle());

        new ApplicationAdaptor(this);
        dbus.registerObject(QStringLiteral("/Application"), this);

        connect(this, &Application::aboutToQuit, this, &Application::onAboutToQuit);

        // aboutToQuit() is not emitted on SIGTERM, so install a handler that shuts down cleanly
        installSigtermHandler();

        // also provide the standard org.freedesktop.FileManager1 interface if possible
        const QString fileManagerService = QStringLiteral("org.freedesktop.FileManager1");

        if (auto* iface = dbus.interface()) {
            connect(iface, &QDBusConnectionInterface::serviceRegistered, this,
                    [this, fileManagerService](const QString& service) {
                        if (fileManagerService == service) {
                            QDBusConnection dbusSession = QDBusConnection::sessionBus();
                            if (auto* ifaceConn = dbusSession.interface()) {
                                disconnect(ifaceConn, &QDBusConnectionInterface::serviceRegistered, this, nullptr);
                            }
                            new ApplicationAdaptorFreeDesktopFileManager(this);
                            if (!dbusSession.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), this)) {
                                qDebug() << "Can't register /org/freedesktop/FileManager1:"
                                         << dbusSession.lastError().message();
                            }
                        }
                    });
            iface->registerService(fileManagerService, QDBusConnectionInterface::QueueService);
        }
    }
    else {
        // another instance already owns org.pcmanfm.PCManFM, this one acts as a client
        isPrimaryInstance = false;
    }
}

Application::~Application() {
    if (volumeMonitor_) {
        g_signal_handlers_disconnect_by_func(volumeMonitor_, gpointer(onVolumeAdded), this);
        g_object_unref(volumeMonitor_);
    }
}

void Application::initWatch() {
    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QFile file(configDir + QStringLiteral("/user-dirs.dirs"));

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << Q_FUNC_INFO << "Could not read:" << file.fileName();
        userDirsFile_.clear();
    }
    else {
        userDirsFile_ = file.fileName();
    }

    userDirsWatcher_ = new QFileSystemWatcher(this);
    if (!userDirsFile_.isEmpty()) {
        userDirsWatcher_->addPath(userDirsFile_);
    }
}

bool Application::parseCommandLineArgs() {
    bool keepRunning = false;

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption profileOption({QStringLiteral("p"), QStringLiteral("profile")},
                                     tr("Name of configuration profile"), tr("PROFILE"));
    parser.addOption(profileOption);

    QCommandLineOption daemonOption({QStringLiteral("d"), QStringLiteral("daemon-mode")},
                                    tr("Run PCManFM-Qt as a daemon"));
    parser.addOption(daemonOption);

    QCommandLineOption quitOption({QStringLiteral("q"), QStringLiteral("quit")}, tr("Quit PCManFM-Qt"));
    parser.addOption(quitOption);

    QCommandLineOption newWindowOption({QStringLiteral("n"), QStringLiteral("new-window")}, tr("Open new window"));
    parser.addOption(newWindowOption);

    QCommandLineOption findFilesOption({QStringLiteral("f"), QStringLiteral("find-files")},
                                       tr("Open Find Files utility"));
    parser.addOption(findFilesOption);

    QCommandLineOption showPrefOption(QStringLiteral("show-pref"),
                                      tr("Open Preferences dialog on the page with the specified name") +
                                          QStringLiteral("\n") + tr("NAME") +
                                          QStringLiteral("=(behavior|display|ui|thumbnail|volume|advanced)"),
                                      tr("NAME"));
    parser.addOption(showPrefOption);

    parser.addPositionalArgument(QStringLiteral("files"), tr("Files or directories to open"), tr("[FILE1, FILE2,...]"));

    parser.process(arguments());

    if (isPrimaryInstance) {
        qDebug("isPrimaryInstance");

        if (parser.isSet(daemonOption)) {
            daemonMode_ = true;
        }
        if (parser.isSet(profileOption)) {
            profileName_ = parser.value(profileOption);
        }

        // load global application configuration
        settings_.load(profileName_);

        // initialize per-folder config backed by dir-settings.conf
        const QString perFolderConfigFile = settings_.profileDir(profileName_) + QStringLiteral("/dir-settings.conf");
        Fm::FolderConfig::init(perFolderConfigFile.toLocal8Bit().constData());

        if (settings_.useFallbackIconTheme()) {
            QIcon::setThemeName(settings_.fallbackIconThemeName());
        }

        if (parser.isSet(findFilesOption)) {
            // file searching utility
            findFiles(parser.positionalArguments());
            keepRunning = true;
        }
        else if (parser.isSet(showPrefOption)) {
            // open preferences dialog directly at a specific page
            preferences(parser.value(showPrefOption));
            keepRunning = true;
        }
        else {
            bool reopenLastTabs = false;
            QStringList paths = parser.positionalArguments();

            if (paths.isEmpty()) {
                // with daemon mode we do not automatically open the current directory
                if (!daemonMode_) {
                    reopenLastTabs = true;
                    paths.push_back(QDir::currentPath());
                }
            }

            if (!paths.isEmpty()) {
                launchFiles(QDir::currentPath(), paths, parser.isSet(newWindowOption), reopenLastTabs);
            }
            keepRunning = true;
        }
    }
    else {
        // secondary instance, forward the request to the primary via DBus
        QDBusConnection dbus = QDBusConnection::sessionBus();
        QDBusInterface iface(QLatin1String(serviceName), QStringLiteral("/Application"), QLatin1String(ifaceName), dbus,
                             this);

        if (parser.isSet(quitOption)) {
            iface.call(QStringLiteral("quit"));
            return false;
        }

        if (parser.isSet(findFilesOption)) {
            iface.call(QStringLiteral("findFiles"), parser.positionalArguments());
        }
        else if (parser.isSet(showPrefOption)) {
            iface.call(QStringLiteral("preferences"), parser.value(showPrefOption));
        }
        else {
            bool reopenLastTabs = false;
            QStringList paths = parser.positionalArguments();

            if (paths.isEmpty()) {
                // when invoked without explicit paths as a client, reuse last tabs
                reopenLastTabs = true;
                paths.push_back(QDir::currentPath());
            }

            iface.call(QStringLiteral("launchFiles"), QDir::currentPath(), paths, parser.isSet(newWindowOption),
                       reopenLastTabs);
        }
    }

    return keepRunning;
}

void Application::init() {
    // load Qt translations
    if (qtTranslator.load(QStringLiteral("qt_") + QLocale::system().name(),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        installTranslator(&qtTranslator);
    }

    // load libfm-qt translations
    installTranslator(libFm_.translator());

    // load pcmanfm-qt translations
    if (translator.load(QStringLiteral("pcmanfm-qt_") + QLocale::system().name(),
                        QStringLiteral(PCMANFM_DATA_DIR) + QStringLiteral("/translations"))) {
        installTranslator(&translator);
    }
}

int Application::exec() {
    if (!parseCommandLineArgs()) {
        return 0;
    }

    if (daemonMode_) {
        // keep running even when all windows are closed
        setQuitOnLastWindowClosed(false);
    }

    volumeMonitor_ = g_volume_monitor_get();

    // delay initialization to avoid treating initial async volume discovery as hotplug events
    QTimer::singleShot(3000, this, &Application::initVolumeManager);

    return QCoreApplication::exec();
}

void Application::onAboutToQuit() {
    qDebug("aboutToQuit");
    settings_.save();
}

void Application::cleanPerFolderConfig() {
    // flush the in-memory per-folder config so we know all currently customized folders
    Fm::FolderConfig::saveCache();

    // then remove non-existent native folders from the list of custom folders
    QByteArray perFolderConfig =
        (settings_.profileDir(profileName_) + QStringLiteral("/dir-settings.conf")).toLocal8Bit();

    GKeyFile* kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, perFolderConfig.constData(), G_KEY_FILE_NONE, nullptr)) {
        bool removed = false;
        gchar** groups = g_key_file_get_groups(kf, nullptr);

        for (int i = 0; groups[i] != nullptr; ++i) {
            const gchar* g = groups[i];

            // only clean native paths, leave virtual paths alone
            if (Fm::FilePath::fromPathStr(g).isNative() && !QDir(QString::fromUtf8(g)).exists()) {
                g_key_file_remove_group(kf, g, nullptr);
                removed = true;
            }
        }

        g_strfreev(groups);

        if (removed) {
            g_key_file_save_to_file(kf, perFolderConfig.constData(), nullptr);
        }
    }
    g_key_file_free(kf);
}

void Application::onLastWindowClosed() {
    // kept for future use if we need custom behavior when the last window closes
}

void Application::onSaveStateRequest(QSessionManager& /*manager*/) {
    // session management is not used; method kept for potential future integration
}

void Application::onFindFileAccepted() {
    // FIX: Using dynamic_cast because Fm::FileSearchDialog might miss Q_OBJECT macro
    auto* dlg = dynamic_cast<Fm::FileSearchDialog*>(sender());
    if (!dlg) {
        return;
    }

    // persist search settings for future sessions
    settings_.setSearchNameCaseInsensitive(dlg->nameCaseInsensitive());
    settings_.setsearchContentCaseInsensitive(dlg->contentCaseInsensitive());
    settings_.setSearchNameRegexp(dlg->nameRegexp());
    settings_.setSearchContentRegexp(dlg->contentRegexp());
    settings_.setSearchRecursive(dlg->recursive());
    settings_.setSearchhHidden(dlg->searchhHidden());
    settings_.addNamePattern(dlg->namePattern());
    settings_.addContentPattern(dlg->contentPattern());

    Fm::FilePathList paths;
    paths.emplace_back(dlg->searchUri());

    MainWindow* window = MainWindow::lastActive();
    Launcher(window).launchPaths(nullptr, paths);
}

void Application::onConnectToServerAccepted() {
    // FIX: Using dynamic_cast for safety here as well
    auto* dlg = dynamic_cast<ConnectServerDialog*>(sender());
    if (!dlg) {
        return;
    }

    const QString uri = dlg->uriText();

    Fm::FilePathList paths;
    paths.push_back(Fm::FilePath::fromUri(uri.toUtf8().constData()));

    MainWindow* window = MainWindow::lastActive();
    Launcher(window).launchPaths(nullptr, paths);
}

void Application::findFiles(QStringList paths) {
    // launch file searching utility with the given paths as search roots
    auto* dlg = new Fm::FileSearchDialog(paths);
    connect(dlg, &QDialog::accepted, this, &Application::onFindFileAccepted);
    dlg->setAttribute(Qt::WA_DeleteOnClose);

    // restore last used search settings
    dlg->setNameCaseInsensitive(settings_.searchNameCaseInsensitive());
    dlg->setContentCaseInsensitive(settings_.searchContentCaseInsensitive());
    dlg->setNameRegexp(settings_.searchNameRegexp());
    dlg->setContentRegexp(settings_.searchContentRegexp());
    dlg->setRecursive(settings_.searchRecursive());
    dlg->setSearchhHidden(settings_.searchhHidden());
    dlg->addNamePatterns(settings_.namePatterns());
    dlg->addContentPatterns(settings_.contentPatterns());

    dlg->show();
}

void Application::connectToServer() {
    auto* dlg = new ConnectServerDialog();
    connect(dlg, &QDialog::accepted, this, &Application::onConnectToServerAccepted);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void Application::launchFiles(const QString& cwd, const QStringList& paths, bool inNewWindow, bool reopenLastTabs) {
    Fm::FilePathList pathList;
    Fm::FilePath cwdPath;
    QStringList effectivePaths = paths;

    // optionally reopen last session tab paths if no explicit paths were supplied
    openingLastTabs_ = reopenLastTabs && settings_.reopenLastTabs() && !settings_.tabPaths().isEmpty();
    if (openingLastTabs_) {
        effectivePaths = settings_.tabPaths();
        // forget tab paths for subsequent windows until the last related window is closed
        settings_.setTabPaths(QStringList());
    }

    for (const QString& it : std::as_const(effectivePaths)) {
        const QByteArray pathName = it.toLocal8Bit();
        Fm::FilePath path;

        if (pathName == "~") {
            // home directory shortcut
            path = Fm::FilePath::homeDir();
        }
        else if (!pathName.isEmpty() && pathName[0] == '/') {
            // absolute local path
            path = Fm::FilePath::fromLocalPath(pathName.constData());
        }
        else if (pathName.contains(":/")) {
            // URI such as file://, smb://, etc
            path = Fm::FilePath::fromUri(pathName.constData());
        }
        else {
            // relative path, resolved against the caller's working directory
            if (Q_UNLIKELY(!cwdPath)) {
                cwdPath = Fm::FilePath::fromLocalPath(cwd.toLocal8Bit().constData());
            }
            path = cwdPath.relativePath(pathName.constData());
        }

        pathList.push_back(std::move(path));
    }

    if (!inNewWindow && settings_.singleWindowMode()) {
        MainWindow* window = MainWindow::lastActive();

        // if there is no last active window, find the last created MainWindow
        // Improved: using reverse iterators is cleaner than the manual loop
        if (window == nullptr) {
            const QWidgetList windows = topLevelWidgets();
            for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
                if ((*it)->inherits("PCManFM::MainWindow")) {
                    window = static_cast<MainWindow*>(*it);
                    break;
                }
            }
        }

        if (window != nullptr && openingLastTabs_) {
            // other folders have been opened explicitly in this window;
            // restoring the previous split view tab count would be misleading
            settings_.setSplitViewTabsNum(0);
        }

        Launcher launcher(window);
        launcher.openInNewTab();
        launcher.launchPaths(nullptr, pathList);
    }
    else {
        Launcher(nullptr).launchPaths(nullptr, pathList);
    }

    if (openingLastTabs_) {
        openingLastTabs_ = false;

        // if none of the last tabs could be opened and there is still no main window,
        // fall back to opening the current directory
        bool hasWindow = false;
        const QWidgetList windows = topLevelWidgets();
        for (const auto* win : windows) {
            if (win->inherits("PCManFM::MainWindow")) {
                hasWindow = true;
                break;
            }
        }

        if (!hasWindow) {
            QStringList fallbackPaths;
            fallbackPaths.push_back(QDir::currentPath());
            launchFiles(QDir::currentPath(), fallbackPaths, inNewWindow, false);
        }
    }
}

void Application::openFolders(Fm::FileInfoList files) {
    Launcher(nullptr).launchFiles(nullptr, std::move(files));
}

void Application::openFolderInTerminal(Fm::FilePath path) {
    if (!settings_.terminal().isEmpty()) {
        Fm::GErrorPtr err;
        const QByteArray terminalName = settings_.terminal().toUtf8();
        if (!Fm::launchTerminal(terminalName.constData(), path, err)) {
            QMessageBox::critical(nullptr, tr("Error"), err.message());
        }
    }
    else {
        // the terminal command is not configured yet, guide the user to the preferences
        QMessageBox::critical(nullptr, tr("Error"), tr("Terminal emulator is not set"));
        preferences(QStringLiteral("advanced"));
    }
}

void Application::preferences(const QString& page) {
    // open or reuse the preferences dialog and show the requested page
    if (!preferencesDialog_) {
        preferencesDialog_ = new PreferencesDialog(page);
    }
    else {
        preferencesDialog_.data()->selectPage(page);
    }

    preferencesDialog_.data()->show();
    preferencesDialog_.data()->raise();
    preferencesDialog_.data()->activateWindow();
}

/* This method receives a list of file:// URIs from DBus and for each URI opens
 * a tab showing its content
 */
void Application::ShowFolders(const QStringList& uriList, const QString& startupId) {
    Q_UNUSED(startupId);

    if (!uriList.isEmpty()) {
        launchFiles(QDir::currentPath(), uriList, false, false);
    }
}

/* This method receives a list of file:// URIs from DBus and opens windows
 * or tabs for each folder, highlighting all listed items within each
 */
void Application::ShowItems(const QStringList& uriList, const QString& startupId) {
    Q_UNUSED(startupId);

    std::unordered_map<Fm::FilePath, Fm::FilePathList, Fm::FilePathHash> groups;
    Fm::FilePathList folders;  // used only for preserving the original parent order

    for (const auto& u : uriList) {
        const QByteArray utf8 = u.toUtf8();
        if (auto path = Fm::FilePath::fromPathStr(utf8.constData())) {
            if (auto parent = path.parent()) {
                auto& paths = groups[parent];
                if (std::find(paths.cbegin(), paths.cend(), path) == paths.cend()) {
                    paths.push_back(std::move(path));
                }
                // remember the order of parent folders
                if (std::find(folders.cbegin(), folders.cend(), parent) == folders.cend()) {
                    folders.push_back(std::move(parent));
                }
            }
        }
    }

    if (groups.empty()) {
        return;
    }

    MainWindow* window = nullptr;

    if (settings_.singleWindowMode()) {
        window = MainWindow::lastActive();

        if (window == nullptr) {
            const QWidgetList windows = topLevelWidgets();
            for (auto it = windows.rbegin(); it != windows.rend(); ++it) {
                if ((*it)->inherits("PCManFM::MainWindow")) {
                    window = static_cast<MainWindow*>(*it);
                    break;
                }
            }
        }
    }

    if (window == nullptr) {
        window = new MainWindow();
    }

    for (const auto& folder : folders) {
        window->openFolderAndSelectFiles(groups[folder]);
    }

    window->show();
    window->raise();
    window->activateWindow();
}

/* This method receives a list of file:// URIs from DBus and
 * for each valid URI opens a property dialog showing its information
 */
void Application::ShowItemProperties(const QStringList& uriList, const QString& startupId) {
    Q_UNUSED(startupId);

    // resolve URIs into paths and show a properties dialog for each item
    Fm::FilePathList paths;
    for (const auto& u : uriList) {
        const QByteArray utf8 = u.toUtf8();
        Fm::FilePath path = Fm::FilePath::fromPathStr(utf8.constData());
        if (path) {
            paths.push_back(std::move(path));
        }
    }
    if (paths.empty()) {
        return;
    }

    auto* job = new Fm::FileInfoJob{std::move(paths)};
    job->setAutoDelete(true);
    connect(job, &Fm::FileInfoJob::finished, this, &Application::onPropJobFinished, Qt::BlockingQueuedConnection);
    job->runAsync();
}

void Application::onPropJobFinished() {
    auto* job = qobject_cast<Fm::FileInfoJob*>(sender());
    if (!job) {
        return;
    }

    for (auto file : job->files()) {
        auto* dialog = Fm::FilePropsDialog::showForFile(std::move(file));
        dialog->raise();
        dialog->activateWindow();
    }
}

// called when Settings is changed to update UI
void Application::updateFromSettings() {
    const QWidgetList windows = this->topLevelWidgets();
    for (QWidget* window : windows) {
        if (window->inherits("PCManFM::MainWindow")) {
            auto* mainWindow = static_cast<MainWindow*>(window);
            mainWindow->updateFromSettings(settings_);
        }
    }
}

void Application::editBookmarks() {
    if (!editBookmarksialog_) {
        editBookmarksialog_ = new Fm::EditBookmarksDialog(Fm::Bookmarks::globalInstance());
    }
    editBookmarksialog_.data()->show();
}

void Application::initVolumeManager() {
    g_signal_connect(volumeMonitor_, "volume-added", G_CALLBACK(onVolumeAdded), this);

    if (settings_.mountOnStartup()) {
        // try to automount all volumes that request automounting
        GList* vols = g_volume_monitor_get_volumes(volumeMonitor_);
        for (GList* l = vols; l; l = l->next) {
            GVolume* volume = G_VOLUME(l->data);
            if (g_volume_should_automount(volume)) {
                autoMountVolume(volume, false);
            }
            g_object_unref(volume);
        }
        g_list_free(vols);
    }
}

bool Application::autoMountVolume(GVolume* volume, bool interactive) {
    if (!g_volume_should_automount(volume) || !g_volume_can_mount(volume)) {
        return false;
    }

    GMount* mount = g_volume_get_mount(volume);
    if (!mount) {
        // not mounted, perform an automount
        auto* op = new Fm::MountOperation(interactive);
        op->mount(volume);
        if (!op->wait()) {
            return false;
        }
        if (!interactive) {
            return true;
        }
        mount = g_volume_get_mount(volume);
    }

    if (mount) {
        if (interactive && settings_.autoRun()) {
            // removable media may request an autorun dialog
            auto* dlg = new AutoRunDialog(volume, mount);
            dlg->show();
        }
        g_object_unref(mount);
    }

    return true;
}

// static
void Application::onVolumeAdded(GVolumeMonitor* /*monitor*/, GVolume* volume, Application* pThis) {
    if (pThis->settings_.mountRemovable()) {
        pThis->autoMountVolume(volume, true);
    }
}

void Application::installSigtermHandler() {
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sigterm_fd) == 0) {
        auto* notifier = new QSocketNotifier(sigterm_fd[1], QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, &Application::onSigtermNotified);

        struct sigaction action;
        action.sa_handler = sigtermHandler;
        ::sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;

        if (::sigaction(SIGTERM, &action, nullptr) != 0) {
            qWarning("Couldn't install SIGTERM handler");
        }
    }
    else {
        qWarning("Couldn't create SIGTERM socketpair");
    }
}

void Application::onSigtermNotified() {
    auto* notifier = qobject_cast<QSocketNotifier*>(sender());
    if (!notifier) {
        return;
    }

    notifier->setEnabled(false);

    char c;
    if (::read(sigterm_fd[1], &c, sizeof(c))) {
    }

    // close all main windows cleanly before quitting
    const auto windows = topLevelWidgets();
    for (auto* win : windows) {
        if (win->inherits("PCManFM::MainWindow")) {
            auto* mainWindow = static_cast<MainWindow*>(win);
            mainWindow->close();
        }
    }

    quit();
}

}  // namespace PCManFM
