/*

    Copyright (C) 2013  Hong Jen Yee (PCMan) <pcman.tw@gmail.com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/


#include "application.h"
#include "mainwindow.h"
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDir>
#include <QVector>
#include <QLocale>
#include <QLibraryInfo>
#include <QFile>
#include <QMessageBox>
#include <QCommandLineParser>
#include <QSocketNotifier>
#include <QFileSystemWatcher>

#include <algorithm>

#include <gio/gio.h>
#include <sys/socket.h>

#include <libfm-qt6/mountoperation.h>
#include <libfm-qt6/filepropsdialog.h>
#include <libfm-qt6/filesearchdialog.h>
#include <libfm-qt6/core/terminal.h>
#include <libfm-qt6/core/bookmarks.h>
#include <libfm-qt6/core/folderconfig.h>
#include <libfm-qt6/core/fileinfojob.h>

#include "applicationadaptor.h"
#include "applicationadaptorfreedesktopfilemanager.h"
#include "preferencesdialog.h"
#include "autorundialog.h"
#include "launcher.h"
#include "xdgdir.h"
#include "connectserverdialog.h"


namespace PCManFM {

static const char* serviceName = "org.pcmanfm.PCManFM";
static const char* ifaceName = "org.pcmanfm.Application";

int ProxyStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget, QStyleHintReturn* returnData) const {
    Application* app = static_cast<Application*>(qApp);
    if(hint == QStyle::SH_ItemView_ActivateItemOnSingleClick) {
        if (app->settings().singleClick()) {
            return true;
        }
        // follow the style
        return QCommonStyle::styleHint(hint,option,widget,returnData);
    }
    return QProxyStyle::styleHint(hint, option, widget, returnData);
}

Application::Application(int& argc, char** argv):
    QApplication(argc, argv),
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


    // QDBusConnection::sessionBus().registerObject("/org/pcmanfm/Application", this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    if(dbus.registerService(QLatin1String(serviceName))) {
        // we successfully registered the service
        isPrimaryInstance = true;
        setStyle(new ProxyStyle());
        //desktop()->installEventFilter(this);

        new ApplicationAdaptor(this);
        dbus.registerObject(QStringLiteral("/Application"), this);

        connect(this, &Application::aboutToQuit, this, &Application::onAboutToQuit);
        // aboutToQuit() is not signalled on SIGTERM, install signal handler
        installSigtermHandler();



        // We also try to register the service "org.freedesktop.FileManager1".
        // We allow queuing of our request in case another file manager has already registered it.
        static const QString fileManagerService = QStringLiteral("org.freedesktop.FileManager1");
        connect(dbus.interface(), &QDBusConnectionInterface::serviceRegistered, this, [this](const QString& service) {
                if(fileManagerService == service) {
                    QDBusConnection dbus = QDBusConnection::sessionBus();
                    disconnect(dbus.interface(), &QDBusConnectionInterface::serviceRegistered, this, nullptr);
                    new ApplicationAdaptorFreeDesktopFileManager(this);
                    if(!dbus.registerObject(QStringLiteral("/org/freedesktop/FileManager1"), this)) {
                        qDebug() << "Can't register /org/freedesktop/FileManager1:" << dbus.lastError().message();
                    }
                }
        });
        dbus.interface()->registerService(fileManagerService, QDBusConnectionInterface::QueueService);
    }
    else {
        // an service of the same name is already registered.
        // we're not the first instance
        isPrimaryInstance = false;
    }
}

Application::~Application() {
    //desktop()->removeEventFilter(this);

    if(volumeMonitor_) {
        g_signal_handlers_disconnect_by_func(volumeMonitor_, gpointer(onVolumeAdded), this);
        g_object_unref(volumeMonitor_);
    }

    // if(enableDesktopManager_)
    //   removeNativeEventFilter(this);
}

void Application::initWatch() {
    QFile file_(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/user-dirs.dirs"));
    if(! file_.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << Q_FUNC_INFO << "Could not read: " << userDirsFile_;
        userDirsFile_ = QString();
    }
    else {
        userDirsFile_ = file_.fileName();
    }

    userDirsWatcher_ = new QFileSystemWatcher(this);
    userDirsWatcher_->addPath(userDirsFile_);
}

bool Application::parseCommandLineArgs() {
    bool keepRunning = false;
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption profileOption(QStringList() << QStringLiteral("p") << QStringLiteral("profile"), tr("Name of configuration profile"), tr("PROFILE"));
    parser.addOption(profileOption);

    QCommandLineOption daemonOption(QStringList() << QStringLiteral("d") << QStringLiteral("daemon-mode"), tr("Run PCManFM-Qt as a daemon"));
    parser.addOption(daemonOption);

    QCommandLineOption quitOption(QStringList() << QStringLiteral("q") << QStringLiteral("quit"), tr("Quit PCManFM-Qt"));
    parser.addOption(quitOption);


    QCommandLineOption newWindowOption(QStringList() << QStringLiteral("n") << QStringLiteral("new-window"), tr("Open new window"));
    parser.addOption(newWindowOption);

    QCommandLineOption findFilesOption(QStringList() << QStringLiteral("f") << QStringLiteral("find-files"), tr("Open Find Files utility"));
    parser.addOption(findFilesOption);


    QCommandLineOption showPrefOption(QStringLiteral("show-pref"), tr("Open Preferences dialog on the page with the specified name") + QStringLiteral("\n") + tr("NAME") + QStringLiteral("=(behavior|display|ui|thumbnail|volume|advanced)"), tr("NAME"));
    parser.addOption(showPrefOption);

    parser.addPositionalArgument(QStringLiteral("files"), tr("Files or directories to open"), tr("[FILE1, FILE2,...]"));

    parser.process(arguments());

    if(isPrimaryInstance) {
        qDebug("isPrimaryInstance");

        if(parser.isSet(daemonOption)) {
            daemonMode_ = true;
        }
        if(parser.isSet(profileOption)) {
            profileName_ = parser.value(profileOption);
        }

        // load app config
        settings_.load(profileName_);

        // init per-folder config
        QString perFolderConfigFile = settings_.profileDir(profileName_) + QStringLiteral("/dir-settings.conf");
        Fm::FolderConfig::init(perFolderConfigFile.toLocal8Bit().constData());


        if(settings_.useFallbackIconTheme()) {
            QIcon::setThemeName(settings_.fallbackIconThemeName());
        }

        if(parser.isSet(findFilesOption)) { // file searching utility
            findFiles(parser.positionalArguments());
            keepRunning = true;
        }
        else if(parser.isSet(showPrefOption)) { // preferences dialog
            preferences(parser.value(showPrefOption));
            keepRunning = true;
        }
        else {
            {
                bool reopenLastTabs = false;
                QStringList paths = parser.positionalArguments();
                if(paths.isEmpty()) {
                    // if no path is specified and we're using daemon mode,
                    // don't open current working directory
                    if(!daemonMode_) {
                        reopenLastTabs = true; // open last tab paths only if no path is specified
                        paths.push_back(QDir::currentPath());
                    }
                }
                if(!paths.isEmpty()) {
                    launchFiles(QDir::currentPath(), paths, parser.isSet(newWindowOption), reopenLastTabs);
                }
                keepRunning = true;
            }
        }
    }
    else {
        QDBusConnection dbus = QDBusConnection::sessionBus();
        QDBusInterface iface(QLatin1String(serviceName), QStringLiteral("/Application"), QLatin1String(ifaceName), dbus, this);
        if(parser.isSet(quitOption)) {
            iface.call(QStringLiteral("quit"));
            return false;
        }

        if(parser.isSet(findFilesOption)) { // file searching utility
            iface.call(QStringLiteral("findFiles"), parser.positionalArguments());
        }
        else if(parser.isSet(showPrefOption)) { // preferences dialog
            iface.call(QStringLiteral("preferences"), parser.value(showPrefOption));
        }
        else {
            {
                bool reopenLastTabs = false;
                QStringList paths = parser.positionalArguments();
                if(paths.isEmpty()) {
                    reopenLastTabs = true; // open last tab paths only if no path is specified
                    paths.push_back(QDir::currentPath());
                }
                iface.call(QStringLiteral("launchFiles"), QDir::currentPath(), paths, parser.isSet(newWindowOption), reopenLastTabs);
            }
        }
    }
    return keepRunning;
}

void Application::init() {

    // install the translations built-into Qt itself
    if(qtTranslator.load(QStringLiteral("qt_") + QLocale::system().name(), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        installTranslator(&qtTranslator);
    }

    // install libfm-qt translator
    installTranslator(libFm_.translator());

    // install our own tranlations
    if(translator.load(QStringLiteral("pcmanfm-qt_") + QLocale::system().name(), QStringLiteral(PCMANFM_DATA_DIR) + QStringLiteral("/translations"))) {
        installTranslator(&translator);
    }
}

int Application::exec() {

    if(!parseCommandLineArgs()) {
        return 0;
    }

    if(daemonMode_) { // keep running even when there is no window opened.
        setQuitOnLastWindowClosed(false);
    }

    volumeMonitor_ = g_volume_monitor_get();
    // delay the volume manager a little because in newer versions of glib/gio there's a problem.
    // when the first volume monitor object is created, it discovers volumes asynchronously.
    // g_volume_monitor_get() immediately returns while the monitor is still discovering devices.
    // So initially g_volume_monitor_get_volumes() returns nothing, but shortly after that
    // we get volume-added signals for all of the volumes. This is not what we want.
    // So, we wait for 3 seconds here to let it finish device discovery.
    QTimer::singleShot(3000, this, &Application::initVolumeManager);

    return QCoreApplication::exec();
}



void Application::onAboutToQuit() {
    qDebug("aboutToQuit");

    settings_.save();
}

void Application::cleanPerFolderConfig() {
    // first save the perfolder config cache to have the list of all custom folders
    Fm::FolderConfig::saveCache();
    // then remove non-existent native folders from the list of custom folders
    QByteArray perFolderConfig = (settings_.profileDir(profileName_) + QStringLiteral("/dir-settings.conf"))
                                 .toLocal8Bit();
    GKeyFile* kf = g_key_file_new();
    if(g_key_file_load_from_file(kf, perFolderConfig.constData(), G_KEY_FILE_NONE, nullptr)) {
        bool removed(false);
        gchar **groups = g_key_file_get_groups(kf, nullptr);
        for(int i = 0; groups[i] != nullptr; i++) {
            const gchar *g = groups[i];
            if(Fm::FilePath::fromPathStr(g).isNative() && !QDir(QString::fromUtf8(g)).exists()) {
                g_key_file_remove_group(kf, g, nullptr);
                removed = true;
            }
        }
        g_strfreev(groups);
        if(removed) {
            g_key_file_save_to_file(kf, perFolderConfig.constData(), nullptr);
        }
    }
    g_key_file_free(kf);
}

/*bool Application::eventFilter(QObject* watched, QEvent* event) {
    if(watched == desktop()) {
        if(event->type() == QEvent::StyleChange ||
                event->type() == QEvent::ThemeChange) {
            setStyle(new ProxyStyle());
        }
    }
    return QObject::eventFilter(watched, event);
}*/

void Application::onLastWindowClosed() {

}

void Application::onSaveStateRequest(QSessionManager& /*manager*/) {

}


void Application::onFindFileAccepted() {
    Fm::FileSearchDialog* dlg = static_cast<Fm::FileSearchDialog*>(sender());
    // get search settings
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
    ConnectServerDialog* dlg = static_cast<ConnectServerDialog*>(sender());
    QString uri = dlg->uriText();
    Fm::FilePathList paths;
    paths.push_back(Fm::FilePath::fromUri(uri.toUtf8().constData()));
    MainWindow* window = MainWindow::lastActive();
    Launcher(window).launchPaths(nullptr, paths);
}

void Application::findFiles(QStringList paths) {
    // launch file searching utility.
    Fm::FileSearchDialog* dlg = new Fm::FileSearchDialog(paths);
    connect(dlg, &QDialog::accepted, this, &Application::onFindFileAccepted);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    // set search settings
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
    ConnectServerDialog* dlg = new ConnectServerDialog();
    connect(dlg, &QDialog::accepted, this, &Application::onConnectToServerAccepted);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();
}

void Application::launchFiles(const QString& cwd, const QStringList& paths, bool inNewWindow, bool reopenLastTabs) {
    Fm::FilePathList pathList;
    Fm::FilePath cwd_path;
    auto _paths = paths;

    openingLastTabs_ = reopenLastTabs && settings_.reopenLastTabs() && !settings_.tabPaths().isEmpty();
    if(openingLastTabs_) {
        _paths = settings_.tabPaths();
        // forget tab paths with next windows until the last one is closed
        settings_.setTabPaths(QStringList());
    }

    for(const QString& it : std::as_const(_paths)) {
        QByteArray pathName = it.toLocal8Bit();
        Fm::FilePath path;
        if(pathName == "~") { // special case for home dir
            path = Fm::FilePath::homeDir();
        }
        if(pathName[0] == '/') { // absolute path
            path = Fm::FilePath::fromLocalPath(pathName.constData());
        }
        else if(pathName.contains(":/")) { // URI
            path = Fm::FilePath::fromUri(pathName.constData());
        }
        else { // basename
            if(Q_UNLIKELY(!cwd_path)) {
                cwd_path = Fm::FilePath::fromLocalPath(cwd.toLocal8Bit().constData());
            }
            path = cwd_path.relativePath(pathName.constData());
        }
        pathList.push_back(std::move(path));
    }

    if(!inNewWindow && settings_.singleWindowMode()) {
        MainWindow* window = MainWindow::lastActive();
        // if there is no last active window, find the last created window
        if(window == nullptr) {
            QWidgetList windows = topLevelWidgets();
            for(int i = 0; i < windows.size(); ++i) {
                auto win = windows.at(windows.size() - 1 - i);
                if(win->inherits("PCManFM::MainWindow")) {
                    window = static_cast<MainWindow*>(win);
                    break;
                }
            }
        }
        if(window != nullptr && openingLastTabs_) {
            // other folders have been opened explicitly in this window;
            // restoring of tab split number does not make sense
            settings_.setSplitViewTabsNum(0);
        }
        auto launcher = Launcher(window);
        launcher.openInNewTab();
        launcher.launchPaths(nullptr, pathList);
    }
    else {
        Launcher(nullptr).launchPaths(nullptr, pathList);
    }

    if(openingLastTabs_) {
        openingLastTabs_ = false;

        // if none of the last tabs can be opened and there is no main window yet,
        // open the current directory
        bool hasWindow = false;
        const QWidgetList windows = topLevelWidgets();
        for(const auto& win : windows) {
            if(win->inherits("PCManFM::MainWindow")) {
                hasWindow = true;
                break;
            }
        }
        if(!hasWindow) {
            _paths.clear();
            _paths.push_back(QDir::currentPath());
            launchFiles(QDir::currentPath(), _paths, inNewWindow, false);
        }
    }
}

void Application::openFolders(Fm::FileInfoList files) {
    Launcher(nullptr).launchFiles(nullptr, std::move(files));
}

void Application::openFolderInTerminal(Fm::FilePath path) {
    if(!settings_.terminal().isEmpty()) {
        Fm::GErrorPtr err;
        auto terminalName = settings_.terminal().toUtf8();
        if(!Fm::launchTerminal(terminalName.constData(), path, err)) {
            QMessageBox::critical(nullptr, tr("Error"), err.message());
        }
    }
    else {
        // show an error message and ask the user to set the command
        QMessageBox::critical(nullptr, tr("Error"), tr("Terminal emulator is not set."));
        preferences(QStringLiteral("advanced"));
    }
}

void Application::preferences(const QString& page) {
    // open preference dialog
    if(!preferencesDialog_) {
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
 * a tab showing its content.
 */
void Application::ShowFolders(const QStringList& uriList, const QString& startupId __attribute__((unused))) {
    if(!uriList.isEmpty()) {
        launchFiles(QDir::currentPath(), uriList, false, false);
    }
}

/* This method receives a list of file:// URIs from DBus and opens windows
 * or tabs for each folder, highlighting all listed items within each.
 */
void Application::ShowItems(const QStringList& uriList, const QString& startupId __attribute__((unused))) {
    std::unordered_map<Fm::FilePath, Fm::FilePathList, Fm::FilePathHash> groups;
    Fm::FilePathList folders; // used only for keeping the original order
    for(const auto& u : uriList) {
        if(auto path = Fm::FilePath::fromPathStr(u.toStdString().c_str())) {
            if(auto parent = path.parent()) {
                auto paths = groups[parent];
                if(std::find(paths.cbegin(), paths.cend(), path) == paths.cend()) {
                    groups[parent].push_back(std::move(path));
                }
                // also remember the order of parent folders
                if(std::find(folders.cbegin(), folders.cend(), parent) == folders.cend()) {
                    folders.push_back(std::move(parent));
                }
            }
        }
    }

    if(groups.empty()) {
        return;
    }

    PCManFM::MainWindow* window = nullptr;
    if(settings_.singleWindowMode()) {
        window = MainWindow::lastActive();
        if(window == nullptr) {
            QWidgetList windows = topLevelWidgets();
            for(int i = 0; i < windows.size(); ++i) {
                auto win = windows.at(windows.size() - 1 - i);
                if(win->inherits("PCManFM::MainWindow")) {
                    window = static_cast<MainWindow*>(win);
                    break;
                }
            }
        }
    }
    if(window == nullptr) {
        window = new MainWindow();
    }

    for(const auto& folder : folders) {
        window->openFolderAndSelectFiles(groups[folder]);
    }

    window->show();
    window->raise();
    window->activateWindow();
}

/* This method receives a list of file:// URIs from DBus and
 * for each valid URI opens a property dialog showing its information
 */
void Application::ShowItemProperties(const QStringList& uriList, const QString& startupId __attribute__((unused))) {
    // FIXME: Should we add "Fm::FilePropsDialog::showForPath()" to libfm-qt, instead of doing this?
    Fm::FilePathList paths;
    for(const auto& u : uriList) {
        Fm::FilePath path = Fm::FilePath::fromPathStr(u.toStdString().c_str());
        if(path) {
            paths.push_back(std::move(path));
        }
    }
    if(paths.empty()) {
        return;
    }
    auto job = new Fm::FileInfoJob{std::move(paths)};
    job->setAutoDelete(true);
    connect(job, &Fm::FileInfoJob::finished, this, &Application::onPropJobFinished, Qt::BlockingQueuedConnection);
    job->runAsync();
}

void Application::onPropJobFinished() {
    auto job = static_cast<Fm::FileInfoJob*>(sender());
    for(auto file: job->files()) {
        auto dialog = Fm::FilePropsDialog::showForFile(std::move(file));
        dialog->raise();
        dialog->activateWindow();
    }
}


// called when Settings is changed to update UI
void Application::updateFromSettings() {
    // if(iconTheme.isEmpty())
    //  Fm::IconTheme::setThemeName(settings_.fallbackIconThemeName());

    // update main windows and desktop windows
    QWidgetList windows = this->topLevelWidgets();
    QWidgetList::iterator it;
    for(it = windows.begin(); it != windows.end(); ++it) {
        QWidget* window = *it;
        if(window->inherits("PCManFM::MainWindow")) {
            MainWindow* mainWindow = static_cast<MainWindow*>(window);
            mainWindow->updateFromSettings(settings_);
        }
    }
}


void Application::editBookmarks() {
    if(!editBookmarksialog_) {
        editBookmarksialog_ = new Fm::EditBookmarksDialog(Fm::Bookmarks::globalInstance());
    }
    editBookmarksialog_.data()->show();
}

void Application::initVolumeManager() {

    g_signal_connect(volumeMonitor_, "volume-added", G_CALLBACK(onVolumeAdded), this);

    if(settings_.mountOnStartup()) {
        /* try to automount all volumes */
        GList* vols = g_volume_monitor_get_volumes(volumeMonitor_);
        for(GList* l = vols; l; l = l->next) {
            GVolume* volume = G_VOLUME(l->data);
            if(g_volume_should_automount(volume)) {
                autoMountVolume(volume, false);
            }
            g_object_unref(volume);
        }
        g_list_free(vols);
    }
}

bool Application::autoMountVolume(GVolume* volume, bool interactive) {
    if(!g_volume_should_automount(volume) || !g_volume_can_mount(volume)) {
        return FALSE;
    }

    GMount* mount = g_volume_get_mount(volume);
    if(!mount) { // not mounted, automount is needed
        // try automount
        Fm::MountOperation* op = new Fm::MountOperation(interactive);
        op->mount(volume);
        if(!op->wait()) {
            return false;
        }
        if(!interactive) {
            return true;
        }
        mount = g_volume_get_mount(volume);
    }

    if(mount) {
        if(interactive && settings_.autoRun()) { // show autorun dialog
            AutoRunDialog* dlg = new AutoRunDialog(volume, mount);
            dlg->show();
        }
        g_object_unref(mount);
    }
    return true;
}

// static
void Application::onVolumeAdded(GVolumeMonitor* /*monitor*/, GVolume* volume, Application* pThis) {
    if(pThis->settings_.mountRemovable()) {
        pThis->autoMountVolume(volume, true);
    }
}

#if 0
bool Application::nativeEventFilter(const QByteArray& eventType, void* message, long* result) {
    if(eventType == "xcb_generic_event_t") { // XCB event
        // filter all native X11 events (xcb)
        xcb_generic_event_t* generic_event = reinterpret_cast<xcb_generic_event_t*>(message);
        // qDebug("XCB event: %d", generic_event->response_type & ~0x80);
        Q_FOREACH(DesktopWindow* window, desktopWindows_) {
        }
    }
    return false;
}
#endif



static int sigterm_fd[2];

static void sigtermHandler(int) {
    char c = 1;
    auto w = ::write(sigterm_fd[0], &c, sizeof(c));
    Q_UNUSED(w);
}

void Application::installSigtermHandler() {
    if(::socketpair(AF_UNIX, SOCK_STREAM, 0, sigterm_fd) == 0) {
        QSocketNotifier* notifier = new QSocketNotifier(sigterm_fd[1], QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, &Application::onSigtermNotified);

        struct sigaction action;
        action.sa_handler = sigtermHandler;
        ::sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        if(::sigaction(SIGTERM, &action, nullptr) != 0) {
            qWarning("Couldn't install SIGTERM handler");
        }
    }
    else {
        qWarning("Couldn't create SIGTERM socketpair");
    }
}

void Application::onSigtermNotified() {
    if(QSocketNotifier* notifier = qobject_cast<QSocketNotifier*>(sender())) {
        notifier->setEnabled(false);
        char c;
        auto r = ::read(sigterm_fd[1], &c, sizeof(c));
        Q_UNUSED(r);
        // close all windows cleanly; otherwise, we might get this warning:
        // "QBasicTimer::start: QBasicTimer can only be used with threads started with QThread"
        const auto windows = topLevelWidgets();
        for(const auto& win : windows) {
            if(win->inherits("PCManFM::MainWindow")) {
                MainWindow* mainWindow = static_cast<MainWindow*>(win);
                mainWindow->close();
            }
        }
        quit();
        notifier->setEnabled(true);
    }
}

} // namespace PCManFM
