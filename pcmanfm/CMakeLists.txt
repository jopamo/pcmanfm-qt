set(pcmanfm_SRCS
    application.cpp
    pcmanfm.cpp
    mainwindow.cpp
    mainwindow_viewframe.cpp
    mainwindow_tabs.cpp
    mainwindow_fileops.cpp
    mainwindow_navigation.cpp
    mainwindow_view.cpp
    mainwindow_pathbar.cpp
    mainwindow_menus.cpp
    mainwindow_bookmarks.cpp
    mainwindow_dragdrop.cpp
    mainwindow_events.cpp
    tabpage.cpp
    tabbar.cpp
    statusbar.cpp
    view.cpp
    launcher.cpp
    preferencesdialog.cpp
    xdgdir.cpp
    autorundialog.cpp
    connectserverdialog.cpp
    settings.cpp
    bulkrename.cpp
    createlauncherdialog.cpp
    hiddenshortcutsdialog.cpp
    # New backend files
    ../src/core/ifileops.cpp
    ../src/core/backend_registry.cpp
    ../src/backends/qt/qt_fileops.cpp
    ../src/backends/qt/qt_fileinfo.cpp
    ../src/backends/qt/qt_foldermodel.cpp
    ../src/backends/gio/gio_trashbackend.cpp
    ../src/backends/gio/gio_volumebackend.cpp
    ../src/backends/gio/gio_remotebackend.cpp
    ../src/ui/filepropertiesdialog.cpp
    ../src/core/ifoldermodel.h
)

qt6_add_dbus_adaptor(pcmanfm_DBUS_SRCS
    org.pcmanfm.Application.xml
    application.h
    PCManFM::Application
    applicationadaptor
    ApplicationAdaptor
)

qt6_add_dbus_adaptor(pcmanfm_DBUS_SRCS
    org.freedesktop.FileManager1.xml
    application.h
    PCManFM::Application
    applicationadaptorfreedesktopfilemanager
    ApplicationAdaptorFreeDesktopFileManager
)

# qt6_add_dbus_adaptor() already generated the moc files. It also marked the
# files with SKIP_AUTOMOC but we still need to mark them with SKIP_AUTOGEN.
# TODO: Check if this behaviour is a CMake bug.
set_source_files_properties(${pcmanfm_DBUS_SRCS} PROPERTIES SKIP_AUTOGEN ON)

set(pcmanfm_UIS
    main-win.ui
    about.ui
    shortcuts.ui
    preferences.ui
    autorun.ui
    connect.ui
    bulk-rename.ui
)

# add translation for pcmanfm-qt
lxqt_translate_ts(QM_FILES
    UPDATE_TRANSLATIONS ${UPDATE_TRANSLATIONS}
    SOURCES ${pcmanfm_SRCS} ${pcmanfm_UIS}
    INSTALL_DIR "${CMAKE_INSTALL_DATADIR}/${PROJECT_NAME}/translations"
)

# translate desktop entry file for pcmanfm-qt
lxqt_translate_desktop(DESKTOP_FILES
    SOURCES
        pcmanfm-qt.desktop.in
    USE_YAML
)

add_executable(pcmanfm-qt
    ${pcmanfm_SRCS}
    ${pcmanfm_DBUS_SRCS}
    ${pcmanfm_UIS}
    ${QM_FILES}
    ${DESKTOP_FILES}
)

target_compile_definitions(pcmanfm-qt
    PRIVATE
        PCMANFM_DATA_DIR="${CMAKE_INSTALL_PREFIX}/share/pcmanfm-qt"
        PCMANFM_QT_VERSION="${PCMANFM_QT_VERSION}"
        LIBFM_DATA_DIR="${PKG_FM_PREFIX}/share/libfm"
)

target_include_directories(pcmanfm-qt
    PRIVATE
        "${Qt6Gui_PRIVATE_INCLUDE_DIRS}"
        "${CMAKE_CURRENT_BINARY_DIR}/pcmanfm-qt_autogen/include"
        ${GIO_INCLUDE_DIRS}
        pcmanfm
)

target_link_libraries(pcmanfm-qt
    Qt6::Widgets
    Qt6::DBus
    fm-qt6
    ${GIO_LIBRARIES}
)

install(TARGETS pcmanfm-qt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# install a desktop entry file for pcmanfm-qt and desktop preferences
install(FILES ${DESKTOP_FILES} DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
)

# prevent the generated files from being deleted during make clean
set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM true)
