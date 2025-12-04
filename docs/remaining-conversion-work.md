# PCManFM-Qt: Remaining libfm/libfm-qt Conversion Work

## Overview

This document outlines the remaining work needed to complete the migration from libfm/libfm-qt to the new modular backend architecture in PCManFM-Qt. The project is currently in an intermediate state where some backend interfaces have been implemented, but core UI components still depend heavily on libfm-qt.

## Current Status

### ✅ Completed Backend Infrastructure

- **Core Interfaces**: `IFileInfo`, `IFolderModel`, `IFileOps`, `ITrashBackend`, `IVolumeBackend`, `IRemoteBackend`
- **Qt Backend**: `QtFileInfo`, `QtFolderModel`, `QtFileOps` implementations
- **Backend Registry**: `BackendRegistry` for creating backend instances
- **GIO Backends**: removed; current build is Qt/POSIX-only
- **Build System**: New backend files included in CMakeLists.txt

### ❌ Major libfm/libfm-qt Dependencies Remaining

## High Priority Components

### 1. Core UI Components

#### MainWindow and View System

**Files affected:**
- `pcmanfm/mainwindow.h/cpp`
- `pcmanfm/view.h/cpp`
- `pcmanfm/main-win.ui`

**libfm-qt dependencies:**
- `Fm::FolderView` - Main folder view component
- `Fm::SidePane` - Sidebar with places and directory tree
- `Fm::PathBar` and `Fm::PathEdit` - Path navigation
- `Fm::FolderModel` - Folder data model

**Replacement strategy:**
- Create Qt-native `FolderView` widget using `IFolderModel`
- Implement sidebar using Qt widgets and new backend interfaces
- Replace path navigation with Qt-based components

#### Application Core

**Files affected:**
- `pcmanfm/application.h/cpp`
- `pcmanfm/pcmanfm.cpp`

**libfm-qt dependencies:**
- `Fm::LibFmQt` - Main initialization
- `Fm::FileInfoList` - File list handling
- `Fm::FilePath` - Path abstraction

**Replacement strategy:**
- Remove `Fm::LibFmQt` initialization
- Use `IFileInfo` and Qt containers for file lists
- Use `QString` and `QUrl` for path handling

### 2. Settings and Configuration

**Files affected:**
- `pcmanfm/settings.h/cpp`
- `pcmanfm/preferencesdialog.cpp`

**libfm-qt dependencies:**
- `Fm::FolderConfig` - Per-folder view settings
- `Fm::FolderView::ViewMode` - View mode enumeration
- `Fm::SidePane::Mode` - Side pane mode enumeration

**Replacement strategy:**
- Implement Qt-based settings system
- Define new view mode enumerations
- Store settings in Qt settings format

## Medium Priority Components

### 3. File Operations and Menus

**Files affected:**
- `pcmanfm/view.cpp`
- `pcmanfm/mainwindow_menus.cpp`
- `pcmanfm/mainwindow_fileops.cpp`

**libfm-qt dependencies:**
- `Fm::FileMenu` and `Fm::FolderMenu` - Context menus
- `Fm::FileOperation` - File operations
- `Fm::utilities.h` - Utility functions

**Replacement strategy:**
- Create menu backend interface
- Use `IFileOps` for file operations
- Implement Qt-based utility functions

### 4. Specialized Features

**Files affected:**
- `pcmanfm/bulkrename.h/cpp`
- `pcmanfm/launcher.h/cpp`

**libfm-qt dependencies:**
- `Fm::Bookmarks` - Bookmark management
- `Fm::Terminal` - Terminal integration
- `Fm::Archiver` - Archive operations

**Replacement strategy:**
- Implement bookmark backend
- Create terminal integration backend
- Implement archive operations backend

## Missing Backend Interfaces

Based on current libfm-qt usage, these additional backend interfaces are needed:

### Required Backend Interfaces

1. **`ISidePaneBackend`**
   ```cpp
   class ISidePaneBackend {
   public:
       virtual ~ISidePaneBackend() = default;

       virtual QList<PlaceInfo> listPlaces() = 0;
       virtual QList<VolumeInfo> listVolumes() = 0;
       virtual bool addBookmark(const QString& path, const QString& name) = 0;
       virtual bool removeBookmark(const QString& path) = 0;
   };
   ```

2. **`IBookmarkBackend`**
   ```cpp
   class IBookmarkBackend {
   public:
       virtual ~IBookmarkBackend() = default;

       virtual QList<BookmarkInfo> listBookmarks() = 0;
       virtual bool addBookmark(const QString& path, const QString& name) = 0;
       virtual bool removeBookmark(const QString& path) = 0;
       virtual bool editBookmark(const QString& oldPath, const QString& newPath, const QString& newName) = 0;
   };
   ```

3. **`ITerminalBackend`**
   ```cpp
   class ITerminalBackend {
   public:
       virtual ~ITerminalBackend() = default;

       virtual bool openTerminal(const QString& workingDirectory) = 0;
       virtual QList<TerminalInfo> listAvailableTerminals() = 0;
   };
   ```

4. **`IArchiverBackend`**
   ```cpp
   class IArchiverBackend {
   public:
       virtual ~IArchiverBackend() = default;

       virtual bool extractArchive(const QString& archivePath, const QString& destination) = 0;
       virtual bool createArchive(const QStringList& files, const QString& archivePath) = 0;
       virtual QList<ArchiverInfo> listAvailableArchivers() = 0;
   };
   ```

5. **`IFileLauncherBackend`**
   ```cpp
   class IFileLauncherBackend {
   public:
       virtual ~IFileLauncherBackend() = default;

       virtual bool openFile(const QString& filePath) = 0;
       virtual bool openFiles(const QStringList& filePaths) = 0;
       virtual QList<ApplicationInfo> listApplicationsForFile(const QString& filePath) = 0;
   };
   ```

6. **`IMenuBackend`**
   ```cpp
   class IMenuBackend {
   public:
       virtual ~IMenuBackend() = default;

       virtual QMenu* createFileContextMenu(const QList<std::shared_ptr<IFileInfo>>& files, QWidget* parent) = 0;
       virtual QMenu* createFolderContextMenu(const QString& folderPath, QWidget* parent) = 0;
       virtual QMenu* createBackgroundContextMenu(const QString& folderPath, QWidget* parent) = 0;
   };
   ```

## Build System Dependencies

### Current libfm-qt Dependencies in CMake

**Main CMakeLists.txt:**
- add_subdirectory(libfm-qt) to build the vendored lib in-tree

**pcmanfm/CMakeLists.txt:**
- target_link_libraries(pcmanfm-qt Qt6::Widgets Qt6::DBus fm-qt6)

### Required Changes

1. Optionally rename the fm-qt6 target and headers to the new Panel naming.
2. Remove `LIBFM_DATA_DIR` compile definition if no longer needed.
3. Update any remaining libfm data file references.

## Conversion Strategy

### Phase 1: Core UI Replacement (High Priority)

1. **Create Qt-native FolderView**
   - Implement custom QAbstractItemView using IFolderModel
   - Support multiple view modes (Icon, List, Detailed, Thumbnail)
   - Implement sorting and filtering

2. **Replace SidePane**
   - Create Qt widget for places and volumes
   - Use IVolumeBackend for volume listing
   - Implement directory tree view

3. **Update MainWindow**
   - Replace Fm::FilePath with QString/QUrl
   - Use BackendRegistry for all backend operations
   - Remove libfm-qt includes and dependencies

### Phase 2: Settings and Configuration (Medium Priority)

1. **Implement Qt Settings System**
   - Replace Fm::FolderConfig with QSettings
   - Define new view mode enumerations
   - Implement per-folder settings persistence

2. **Update Preferences Dialog**
   - Remove libfm-qt terminal and archiver references
   - Use new backend interfaces for configuration

### Phase 3: File Operations and Menus (Medium Priority)

1. **Implement Menu Backend**
   - Create context menu generation system
   - Integrate with file launcher backend
   - Support custom actions

2. **Complete File Operations**
   - [x] Modernize "New Folder" creation (Qt-native for local paths)
   - [ ] Ensure all file operations use IFileOps
   - [ ] Implement progress reporting
   - [ ] Support cancellation

### Phase 4: Specialized Features (Lower Priority)

1. **Implement Remaining Backends**
   - Bookmark backend
   - Terminal backend
   - Archiver backend
   - File launcher backend

2. **Update Specialized Components**
   - Bulk rename functionality
   - Application launching
   - Search functionality

## Testing Strategy

### Core Functionality Tests
- [ ] Folder navigation and browsing
- [ ] File operations (copy, move, delete, rename)
- [ ] View modes and sorting
- [ ] Side pane functionality
- [ ] Settings persistence

### Backend Integration Tests
- [ ] Trash operations
- [ ] Volume mounting/unmounting
- [ ] Remote filesystem access
- [ ] Bookmark management
- [ ] Terminal integration

### Performance Tests
- [ ] Large directory loading
- [ ] File operation performance
- [ ] Memory usage
- [ ] Responsiveness

## Migration Checklist

### Phase 1: Core UI
- [ ] Create Qt-native FolderView
- [ ] Implement sidebar with places
- [ ] Replace path navigation components
- [ ] Update MainWindow to use new backends
- [ ] Remove Fm::LibFmQt initialization

### Phase 2: Settings
- [ ] Implement Qt-based settings system
- [ ] Replace Fm::FolderConfig
- [ ] Update preferences dialog
- [ ] Migrate existing settings

### Phase 3: File Operations
- [ ] Implement menu backend
- [ ] Replace context menus
- [ ] Complete file operations backend
- [ ] Update bulk rename functionality

### Phase 4: Specialized Features
- [ ] Implement bookmark backend
- [ ] Implement terminal backend
- [ ] Implement archiver backend
- [ ] Implement file launcher backend

### Final Steps
- [ ] Remove all libfm-qt includes
- [ ] Update build system to remove libfm-qt dependencies
- [ ] Run comprehensive tests
- [ ] Update documentation

## Notes and Considerations

### Performance
- The new Qt-based components should maintain or improve performance
- Consider using QFileSystemModel for initial implementation
- Implement lazy loading for large directories

### Compatibility
- Maintain backward compatibility with existing settings
- Provide migration path for user data
- Ensure feature parity with libfm-qt version

### Architecture
- Keep UI code separate from backend implementations
- Use dependency injection for backend components
- Maintain the modular backend architecture

### Documentation
- Update developer documentation
- Create API documentation for new interfaces
- Update user documentation for any changed behavior

## Conclusion

The conversion from libfm/libfm-qt to the new backend architecture is approximately 30-40% complete. The core backend infrastructure is in place, but the main UI components still heavily depend on libfm-qt. The highest priority should be given to replacing the `Fm::FolderView` and `Fm::SidePane` components with Qt-native implementations that use the new backend interfaces.

Once the core UI components are converted, the remaining work involves implementing the specialized backends and removing the final libfm-qt dependencies from the build system. The modular architecture will provide a solid foundation for future development and make the codebase more maintainable.
