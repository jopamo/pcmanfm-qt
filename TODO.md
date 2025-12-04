# PCManFM-Qt TODO List

This file tracks work items, planned features, and technical debt for the PCManFM-Qt project. Maintainers and contributors can use this to coordinate development efforts.

## Modernization and Backend Work
- [x] Add a POSIX-only filesystem core (native-byte paths, explicit errors) with read/write/rename/remove/make_dir_parents/set_permissions and unit coverage
- [x] Convert `src/backends/qt/qt_fileops.*` into the thin Qt adapter that calls the POSIX core (Qt only for path conversion + signals)
- [x] Vendor libfm-qt in-tree under the `Panel::*` namespace and drop the external `find_package(fm-qt6)` usage
- [ ] Route real file mutations in app code through `IFileOps`/the core (copy/move/delete/rename/mkdir/permissions/config writes) in: `pcmanfm/mainwindow_fileops.cpp`, `pcmanfm/mainwindow.cpp`, `pcmanfm/filepropertiesdialog.cpp`, `pcmanfm/xdgdir.cpp`, `pcmanfm/application.cpp`, `pcmanfm/settings.cpp`
- [ ] Keep UI-only metadata/listing in Qt but add a guard/grep check to prevent `QFile`/`QSaveFile`/`QDir` mutations outside adapters (`backends/qt` + UI discovery)
- [ ] Expand Qt backend coverage (remote URIs, trash, volumes) without GIO once the POSIX core is in place
- [x] Replace libfm-qt archiver actions with in-process tar/tar.zst compression backed by POSIX I/O + libarchive
- [x] Add in-process tar.zst extraction (libarchive + POSIX) wired to the context menu for native archives
- [ ] Remove legacy libfm/libfm-qt code paths and includes

## UI/UX Improvements
- [ ] Harden keyboard navigation and shortcuts (focus/selection in split view, Delete handling)
- [ ] Keep View/Sort menus aligned with active tab state
- [ ] Audit menu/actions to remove unsupported legacy entries

## Stability and Technical Debt
- [ ] Audit real file operations to ensure they use the POSIX core and surface errors to UI
- [ ] Eliminate GLib/GIO runtime warnings from remaining libfm-qt usage
- [ ] Fix any build warnings introduced by backend removal
- [ ] Add error-handling/confirmation coverage for destructive ops

## Documentation and Process
- [ ] Keep HACKING.md and TODO.md aligned with the POSIX-core/Qt-adapter split as it lands
- [ ] Document backend responsibilities, allowed Qt FS usage, and the grep/CI guard for new file-mutation code
- [ ] Refresh release/build notes after major refactors

## Testing
- [ ] Add unit coverage for the POSIX filesystem core (read/write/atomic rename/mkdir/permissions, error paths)
- [ ] Add targeted tests for Qt file ops adapter (progress/cancel, rename vs copy/move semantics, error propagation)
- [ ] Add smoke tests for selection/focus rules in split view and tabs
