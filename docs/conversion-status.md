# PCManFM-Qt Conversion Status

## Quick Status Overview

**Overall Progress:** ~35% Complete

### ✅ Completed
- Core backend interfaces (`IFileInfo`, `IFolderModel`, `IFileOps`, `ITrashBackend`, `IVolumeBackend`, `IRemoteBackend`)
- Qt backend implementations
- GIO backends removed; build is Qt/POSIX-only
- Backend registry system
- Basic build system integration
- Modernized "New Folder" creation in MainWindow (Qt-based for local paths)
- libfm-qt dependency vendored in-tree (Panel namespace) instead of find_package(fm-qt6)

### ❌ Major Dependencies Remaining

## High Priority (Blocking libfm-qt removal)

### 1. Core UI Components
- **`Fm::FolderView`** - Main file browser view
- **`Fm::SidePane`** - Sidebar with places/tree
- **`Fm::PathBar/PathEdit`** - Path navigation
- **Files:** `mainwindow.h/cpp`, `view.h/cpp`, `main-win.ui`

### 2. Application Core
- **`Fm::LibFmQt`** - Main initialization
- **`Fm::FilePath`** - Path abstraction
- **Files:** `application.h/cpp`, `pcmanfm.cpp`

## Medium Priority

### 3. Settings System
- **`Fm::FolderConfig`** - Per-folder settings
- **Files:** `settings.h/cpp`, `preferencesdialog.cpp`

### 4. File Operations & Menus
- **`Fm::FileMenu/FolderMenu`** - Context menus
- **Files:** `view.cpp`, `mainwindow_menus.cpp`

## Missing Backend Interfaces

1. **`ISidePaneBackend`** - Places and volumes
2. **`IBookmarkBackend`** - Bookmark management
3. **`ITerminalBackend`** - Terminal integration
4. **`IArchiverBackend`** - Archive operations
5. **`IFileLauncherBackend`** - Application launching
6. **`IMenuBackend`** - Context menu generation

## Build System Dependencies

```cmake
# Main CMakeLists.txt
add_subdirectory(libfm-qt)  # vendored Panel/libfm-qt build

# pcmanfm/CMakeLists.txt
target_link_libraries(pcmanfm-qt fm-qt6)
target_compile_definitions(pcmanfm-qt LIBFM_DATA_DIR=...)
```

## Next Steps Priority

### Phase 1 (Critical)
1. Create Qt-native `FolderView` using `IFolderModel`
2. Implement sidebar widget with places/volumes
3. Replace `Fm::FilePath` with Qt path handling
4. Remove `Fm::LibFmQt` initialization

### Phase 2 (Important)
1. Implement Qt-based settings system
2. Create menu backend for context menus
3. Implement remaining specialized backends

### Phase 3 (Final)
1. Remove all libfm-qt includes
2. Update build system dependencies
3. Run comprehensive testing

## Key Files to Focus On

- `pcmanfm/mainwindow.h/cpp` - Heavy libfm-qt usage
- `pcmanfm/view.h/cpp` - Inherits from Fm::FolderView
- `pcmanfm/application.h/cpp` - Core initialization
- `pcmanfm/settings.h/cpp` - Settings management

## Estimated Effort

- **Phase 1:** 2-3 weeks (core UI replacement)
- **Phase 2:** 1-2 weeks (settings & menus)
- **Phase 3:** 1 week (cleanup & testing)

**Total:** 4-6 weeks of focused development

## Success Criteria

- Builds without libfm-qt dependency
- All existing functionality preserved
- Performance maintained or improved
- Codebase uses only new backend architecture

---

*Last Updated: 2025-11-30*
*See `remaining-conversion-work.md` for detailed breakdown*
