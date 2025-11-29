/* pcmanfm/mainwindow.cpp */

#include "mainwindow.h"

#include "application.h"
#include "settings.h"

namespace PCManFM {

QPointer<MainWindow> MainWindow::lastActive_;

MainWindow::MainWindow(Fm::FilePath path)
    : pathEntry_(nullptr),
      pathBar_(nullptr),
      fsInfoLabel_(nullptr),
      fileLauncher_(this),
      rightClickIndex_(-1),
      updatingViewMenu_(false),
      menuSep_(nullptr),
      menuSpacer_(nullptr),
      activeViewFrame_(nullptr),
      splitView_(false),
      splitTabsNum_(0) {
    // Setup UI from the generated header
    ui.setupUi(this);

    // Create filesystem info label and add to status bar
    fsInfoLabel_ = new QLabel(this);
    fsInfoLabel_->setFrameShape(QFrame::NoFrame);
    fsInfoLabel_->setContentsMargins(4, 0, 4, 0);
    fsInfoLabel_->setVisible(false);
    ui.statusbar->addPermanentWidget(fsInfoLabel_);

    // Initialize the window
    auto* app = qobject_cast<Application*>(qApp);
    if (app) {
        Settings& settings = app->settings();

        // Initialize side pane
        ui.sidePane->setVisible(settings.isSidePaneVisible());
        ui.actionSidePane->setChecked(settings.isSidePaneVisible());
        ui.sidePane->setIconSize(QSize(settings.sidePaneIconSize(), settings.sidePaneIconSize()));
        ui.sidePane->setMode(settings.sidePaneMode());
        ui.sidePane->restoreHiddenPlaces(settings.getHiddenPlaces());

        // Connect side pane signals
        connect(ui.sidePane, &Fm::SidePane::chdirRequested, this, &MainWindow::onSidePaneChdirRequested);
        connect(ui.sidePane, &Fm::SidePane::openFolderInNewWindowRequested, this,
                &MainWindow::onSidePaneOpenFolderInNewWindowRequested);
        connect(ui.sidePane, &Fm::SidePane::openFolderInNewTabRequested, this,
                &MainWindow::onSidePaneOpenFolderInNewTabRequested);
        connect(ui.sidePane, &Fm::SidePane::openFolderInTerminalRequested, this,
                &MainWindow::onSidePaneOpenFolderInTerminalRequested);
        connect(ui.sidePane, &Fm::SidePane::createNewFolderRequested, this,
                &MainWindow::onSidePaneCreateNewFolderRequested);
        connect(ui.sidePane, &Fm::SidePane::modeChanged, this, &MainWindow::onSidePaneModeChanged);
        connect(ui.sidePane, &Fm::SidePane::hiddenPlaceSet, this, &MainWindow::onSettingHiddenPlace);

        // Initialize splitter
        connect(ui.splitter, &QSplitter::splitterMoved, this, &MainWindow::onSplitterMoved);
        ui.splitter->setStretchFactor(1, 1);  // only the right pane can be stretched
        ui.splitter->setSizes({settings.splitterPos(), 1});

        // Initialize view action icons
        ui.actionIconView->setIcon(QIcon::fromTheme(QLatin1String("view-list-icons"),
                                                    style()->standardIcon(QStyle::SP_FileDialogContentsView)));
        ui.actionThumbnailView->setIcon(QIcon::fromTheme(QLatin1String("view-list-icons"),
                                                         style()->standardIcon(QStyle::SP_FileDialogContentsView)));
        ui.actionCompactView->setIcon(QIcon::fromTheme(QLatin1String("view-list-details"),
                                                       style()->standardIcon(QStyle::SP_FileDialogDetailedView)));
        ui.actionDetailedList->setIcon(QIcon::fromTheme(QLatin1String("view-list-details"),
                                                        style()->standardIcon(QStyle::SP_FileDialogDetailedView)));

        updateFromSettings(settings);
    }

    // Add initial view frame
    addViewFrame(path);

    // Set window title
    setWindowTitle(QStringLiteral("PCManFM-Qt"));
}

MainWindow::~MainWindow() {
    // Destructor implementation
}

void MainWindow::updateFromSettings(Settings& settings) {
    // Apply settings to the window
    splitView_ = settings.splitView();

    // Update side pane visibility
    if (ui.sidePane) {
        ui.sidePane->setVisible(settings.isSidePaneVisible());
    }

    // Update side pane mode
    if (ui.sidePane) {
        ui.sidePane->setMode(settings.sidePaneMode());
    }

    // Update splitter position
    if (ui.splitter) {
        ui.splitter->setSizes({settings.splitterPos(), 1});
    }

    // Update menu bar visibility
    ui.menubar->setVisible(settings.showMenuBar());

    // Toolbar visibility is not configurable - always visible
}

void MainWindow::setRTLIcons(bool isRTL) {
    // RTL icons implementation
}

void MainWindow::onTabPageTitleChanged() {
    // Tab page title changed implementation
}

void MainWindow::onTabPageStatusChanged(int type, QString statusText) {
    if (type == TabPage::StatusTextFSInfo) {
        // Update filesystem info label
        if (fsInfoLabel_) {
            fsInfoLabel_->setText(statusText);
            fsInfoLabel_->setVisible(!statusText.isEmpty());
        }
    }
}

void MainWindow::onTabPageSortFilterChanged() {
    // Tab page sort filter changed implementation
}

void MainWindow::onFolderUnmounted() {
    // Folder unmounted implementation
}

void MainWindow::onTabBarClicked(int index) {
    // Tab bar clicked implementation
}

void MainWindow::tabContextMenu(const QPoint& pos) {
    // Tab context menu implementation
}

void MainWindow::on_actionNewTab_triggered() {
    // New tab action implementation
}

void MainWindow::on_actionSplitView_triggered(bool check) {
    // Split view action implementation
}

void MainWindow::on_actionPreferences_triggered() {
    // Preferences action implementation
}

void MainWindow::on_actionEditBookmarks_triggered() {
    // Edit bookmarks action implementation
}

void MainWindow::on_actionAbout_triggered() {
    // About action implementation
}

void MainWindow::on_actionHiddenShortcuts_triggered() {
    // Hidden shortcuts action implementation
}

void MainWindow::onShortcutPrevTab() {
    // Previous tab shortcut implementation
}

void MainWindow::onShortcutNextTab() {
    // Next tab shortcut implementation
}

void MainWindow::onShortcutJumpToTab() {
    // Jump to tab shortcut implementation
}

void MainWindow::onSidePaneChdirRequested(int type, const Fm::FilePath& path) {
    // Side pane chdir requested implementation
}

void MainWindow::onSidePaneOpenFolderInNewWindowRequested(const Fm::FilePath& path) {
    // Side pane open folder in new window implementation
}

void MainWindow::onSidePaneOpenFolderInNewTabRequested(const Fm::FilePath& path) {
    // Side pane open folder in new tab implementation
}

void MainWindow::onSidePaneOpenFolderInTerminalRequested(const Fm::FilePath& path) {
    // Side pane open folder in terminal implementation
}

void MainWindow::onSidePaneCreateNewFolderRequested(const Fm::FilePath& path) {
    // Side pane create new folder implementation
}

void MainWindow::onSidePaneModeChanged(Fm::SidePane::Mode mode) {
    // Side pane mode changed implementation
}

void MainWindow::on_actionSidePane_triggered(bool check) {
    // Side pane action implementation
}

void MainWindow::onSplitterMoved(int pos, int index) {
    // Splitter moved implementation
}

void MainWindow::onBackForwardContextMenu(QPoint pos) {
    // Back/forward context menu implementation
}

void MainWindow::closeLeftTabs() {
    // Close left tabs implementation
}

void MainWindow::closeRightTabs() {
    // Close right tabs implementation
}

void MainWindow::onSettingHiddenPlace(const QString& str, bool hide) {
    // Setting hidden place implementation
}

void MainWindow::on_actionCreateLauncher_triggered() {
    // Create launcher action implementation
}

void MainWindow::on_actionNewBlankFile_triggered() {
    // New blank file action implementation
}

void MainWindow::on_actionNewWin_triggered() {
    // New window action implementation
}

void MainWindow::on_actionCloseWindow_triggered() {
    // Close window action implementation
}

void MainWindow::on_actionNewFolder_triggered() {
    // New folder action implementation
}

void MainWindow::on_actionCloseTab_triggered() {
    // Close tab action implementation
}

}  // namespace PCManFM