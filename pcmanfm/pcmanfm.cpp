/*
 * PCManFM-Qt main entry point
 * pcmanfm/pcmanfm.cpp
 */

#include "panel/panel.h"

#include "../src/core/backend_registry.h"
#include "application.h"

int main(int argc, char** argv) {
    // ensure that glib integration of Qt is not turned off
    // This fixes #168: https://github.com/lxqt/pcmanfm-qt/issues/168
    qunsetenv("QT_NO_GLIB");

    PCManFM::Application app(argc, argv);

    // Initialize backend registry after QApplication is created
    PCManFM::BackendRegistry::initDefaults();

    app.init();
    return app.exec();
}
