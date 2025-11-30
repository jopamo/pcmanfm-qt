/* pcmanfm/mainwindow_bookmarks.cpp */

#include <QAction>
#include <QDir>
#include <QMenu>

#include "application.h"
#include "mainwindow.h"
#include "tabpage.h"

namespace PCManFM {

void MainWindow::loadBookmarksMenu() {
    // Clear previously inserted dynamic bookmark actions
    auto* menu = ui.menu_Bookmarks;
    if (!menu) {
        return;
    }

    const auto actions = menu->actions();
    for (auto* action : actions) {
        if (!action) {
            continue;
        }

        // identify bookmark actions via a custom property
        if (action->property("pcmanfm_bookmark").toBool()) {
            menu->removeAction(action);
            action->deleteLater();
        }
    }
}

void MainWindow::onBookmarksChanged() { loadBookmarksMenu(); }

void MainWindow::onBookmarkActionTriggered() {
    const auto* action = qobject_cast<QAction*>(sender());
    if (!action) {
        return;
    }

    const QVariant data = action->data();
    if (!data.isValid()) {
        return;
    }

    const QString pathStr = data.toString();
    if (pathStr.isEmpty()) {
        return;
    }

    QDir::setCurrent(pathStr);
}

void MainWindow::on_actionAddToBookmarks_triggered() {
    auto* app = qobject_cast<Application*>(qApp);
    if (!app) {
        return;
    }

    // delegate to Application's existing bookmark editor
    app->editBookmarks();
}

}  // namespace PCManFM
