# libfm and libfm-qt Usage Analysis

This document provides a comprehensive analysis of libfm and libfm-qt function usage in the pcmanfm-qt codebase. This analysis is intended to guide the modernization effort outlined in `CLAUDE.md`.

**Date**: 2025-12-05
**Analysis Scope**: All C/C++ source files in `/home/me/projects/pcmanfm-qt/`

## Overview

The project currently vendors both `libfm` (C library) and `libfm-qt` (C++ Qt wrapper) in-tree. The main application uses the `Panel::` namespace (aliased to `Fm::`) as an abstraction layer over libfm-qt. The modernization goal is to replace this dependency with a Qt-only backend architecture.

## 1. Direct libfm (C Library) Function Usage

### 1.1 Global Configuration Functions

| Function | Location | Usage |
|----------|----------|-------|
| `fm_config_init()` | `libfm-qt/src/libfmqt.cpp:67` | Initializes libfm's global configuration structure during libfm-qt initialization |
| `fm_config` (global variable) | Various files | Shared configuration between pcmanfm-qt and libfm/libfm-qt |

**Configuration fields accessed from `fm_config`**:

| Field | Location | Purpose |
|-------|----------|---------|
| `fm_config->si_unit` | `pcmanfm/tabpage.cpp:60,536,752` | SI unit preference for file size formatting |
| `fm_config->no_usb_trash` | `pcmanfm/settings.h:328` | Disable trash for USB devices |
| `fm_config->quick_exec` | `pcmanfm/settings.h:340` | Quick execution mode |
| `fm_config->backup_as_hidden` | `pcmanfm/settings.h:403-404` | Backup files as hidden |
| `fm_config->only_user_templates` | `pcmanfm/settings.h:427` | Restrict templates to user directory |
| `fm_config->template_type_once` | `pcmanfm/settings.h:434` | Template type setting |
| `fm_config->template_run_app` | `pcmanfm/settings.h:441` | Template application execution |
| `fm_config->thumbnail_local` | `libfm-qt/src/core/thumbnailjob.cpp:312-313` | Local thumbnail generation |
| `fm_config->thumbnail_max` | `libfm-qt/src/core/thumbnailjob.cpp:319-320` | Maximum thumbnail file size |
| `fm_config->external_thumbnail_max` | `libfm-qt/src/core/thumbnailjob.cpp:326-327` | Maximum external thumbnail size |
| `fm_config->big_icon_size` | `libfm-qt/src/renamedialog.cpp:45` | Icon size configuration |

### 1.2 Application Launching

| Function | Location | Usage |
|----------|----------|-------|
| `fm_app_launch_context_new_for_widget()` | `libfm-qt/src/filelauncher.cpp:42,51,58` | Creates Gtk application launch context for file launching |

### 1.3 Internal libfm Functions (Vendored)

These functions are used internally within the vendored libfm-qt code:

- `_fm_vfs_search_new_for_uri()` – VFS search URI implementation (`libfm-qt/src/libfmqt.cpp:44`)
- `_fm_vfs_menu_new_for_uri()` – VFS menu URI implementation (`libfm-qt/src/libfmqt.cpp:45`)
- Various `fm_path_*` functions (path manipulation)
- Various `fm_file_info_*` functions (file metadata)
- Various `fm_dir_list_job_*` functions (directory listing)

## 2. libfm-qt (C++ Library) Class Usage

The main application uses the `Panel::` namespace defined in `src/panel/panel.h`. All libfm-qt usage goes through these aliases.

### 2.1 Core Data Types (Aliases)

| Panel Namespace | Original Type | Purpose |
|-----------------|---------------|---------|
| `Panel::FilePath` | `Fm::FilePath` | Path representation |
| `Panel::FileInfo` | `Fm::FileInfo` | File information |
| `Panel::FileInfoList` | `Fm::FileInfoList` | List of file info objects |
| `Panel::FilePathList` | `Fm::FilePathList` | List of file paths |
| `Panel::GErrorPtr` | `Fm::GErrorPtr` | GLib error pointer wrapper |
| `Panel::GAppInfoPtr` | `Fm::GAppInfoPtr` | GLib application info wrapper |
| `Panel::FilePathHash` | `Fm::FilePathHash` | Hash for file paths |

### 2.2 Folder and File Management

| Panel Namespace | Original Type | Purpose |
|-----------------|---------------|---------|
| `Panel::Folder` | `Fm::Folder` | Directory management |
| `Panel::FolderModel` | `Fm::FolderModel` | Qt model for folder contents |
| `Panel::ProxyFolderModel` | `Fm::ProxyFolderModel` | Proxy model for filtering/sorting |
| `Panel::CachedFolderModel` | `Fm::CachedFolderModel` | Cached folder model |
| `Panel::FolderView` | `Fm::FolderView` | UI view component for folders |
| `Panel::FileInfoJob` | `Fm::FileInfoJob` | Background job for file info |
| `Panel::Job` | `Fm::Job` | Base job class |

### 2.3 UI Components

| Panel Namespace | Original Type | Purpose |
|-----------------|---------------|---------|
| `Panel::FolderMenu` | `Fm::FolderMenu` | Context menu for folders |
| `Panel::FileMenu` | `Fm::FileMenu` | Context menu for files |
| `Panel::FileLauncher` | `Fm::FileLauncher` | File/application launcher |
| `Panel::FileOperation` | `Fm::FileOperation` | File operations (copy/move/delete) |
| `Panel::PathBar` | `Fm::PathBar` | Path navigation bar |
| `Panel::PathEdit` | `Fm::PathEdit` | Path editing widget |
| `Panel::SidePane` | `Fm::SidePane` | Side panel UI component |
| `Panel::CreateNewMenu` | `Fm::CreateNewMenu` | "Create new" menu |

### 2.4 Dialogs

| Panel Namespace | Original Type | Purpose |
|-----------------|---------------|---------|
| `Panel::FilePropsDialog` | `Fm::FilePropsDialog` | File properties dialog |
| `Panel::FileSearchDialog` | `Fm::FileSearchDialog` | File search dialog |
| `Panel::EditBookmarksDialog` | `Fm::EditBookmarksDialog` | Bookmark editing dialog |

### 2.5 Configuration and Metadata

| Panel Namespace | Original Type | Purpose |
|-----------------|---------------|---------|
| `Panel::Bookmarks` | `Fm::Bookmarks` | Bookmark management |
| `Panel::BookmarkItem` | `Fm::BookmarkItem` | Individual bookmark |
| `Panel::BrowseHistory` | `Fm::BrowseHistory` | Navigation history |
| `Panel::BrowseHistoryItem` | `Fm::BrowseHistoryItem` | History item |
| `Panel::FolderConfig` | `Fm::FolderConfig` | Folder-specific configuration |
| `Panel::IconInfo` | `Fm::IconInfo` | Icon metadata |
| `Panel::MimeType` | `Fm::MimeType` | MIME type handling |

### 2.6 Specialized Functionality

| Panel Namespace | Original Type | Purpose |
|-----------------|---------------|---------|
| `Panel::ThumbnailJob` | `Fm::ThumbnailJob` | Thumbnail generation |
| `Panel::Archiver` | `Fm::Archiver` | Archive operations |
| `Panel::LibFmQt` | `Fm::LibFmQt` | Library initialization |

### 2.7 Utility Functions (Imported via `using` declarations)

These functions are imported directly from the `Fm::` namespace:

| Function | Usage Location | Purpose |
|----------|----------------|---------|
| `Panel::formatFileSize()` | `pcmanfm/tabpage.cpp:60` | Format file sizes with SI unit preference |
| `Panel::changeFileName()` | - | File renaming utility |
| `Panel::launchTerminal()` | - | Terminal launching |
| `Panel::setDefaultTerminal()` | - | Default terminal configuration |
| `Panel::allKnownTerminals()` | - | List available terminals |
| `Panel::internalTerminals()` | - | Internal terminal handlers |

## 3. Key Architectural Insights

### 3.1 Vendored Dependencies
- The project vendors both `libfm` and `libfm-qt` in-tree (under `libfm/` and `libfm-qt/` directories)
- This allows for internal modifications but creates a tight coupling

### 3.2 Abstraction Layer
- The `Panel::` namespace provides a clean abstraction over libfm-qt
- This abstraction makes migration to a Qt-only backend theoretically easier
- However, the `fm_config` global variable breaks this abstraction

### 3.3 Configuration Sharing
- The `fm_config` global variable is the primary shared state between pcmanfm-qt and libfm/libfm-qt
- This creates a direct dependency that must be broken during migration

### 3.4 File System Operations
- File operations (copy, move, delete) go through `Panel::FileOperation` (`Fm::FileOperation`)
- This in turn uses libfm's job system and file operations backend
- Thumbnail generation uses `Panel::ThumbnailJob` (`Fm::ThumbnailJob`)

### 3.5 UI Components
- Most UI components (FolderView, PathBar, SidePane) are libfm-qt widgets
- These would need to be reimplemented in pure Qt

## 4. Migration Priority Areas

Based on the analysis, here are the priority areas for migration:

### **High Priority**
1. **`fm_config` global variable** – Replace with Qt-based configuration system
2. **File operations backend** – Replace libfm's job system with Qt-based implementation
3. **Application launching** – Replace `fm_app_launch_context_new_for_widget()` with Qt equivalent

### **Medium Priority**
4. **Folder model and views** – Reimplement `FolderModel`, `FolderView` in pure Qt
5. **Thumbnail generation** – Replace libfm thumbnailer with Qt-based solution
6. **Path handling** – Replace `FilePath` with `QString`/`QUrl`

### **Lower Priority**
7. **UI widgets** – Reimplement PathBar, SidePane, etc.
8. **Dialogs** – Reimplement FilePropsDialog, FileSearchDialog
9. **Utility functions** – Replace formatFileSize, terminal functions

## 5. Files with Direct Dependencies

### Main application files with libfm dependencies:
- `src/panel/panel.h` – Includes all libfm-qt headers, defines `Panel::` namespace
- `pcmanfm/settings.h` – Accesses `fm_config` fields
- `pcmanfm/tabpage.cpp` – Uses `fm_config->si_unit` with `Panel::formatFileSize()`
- `pcmanfm/application.h` – Uses `Panel::LibFmQt`

### libfm-qt source files interfacing with libfm:
- `libfm-qt/src/libfmqt.cpp` – Calls `fm_config_init()`
- `libfm-qt/src/filelauncher.cpp` – Calls `fm_app_launch_context_new_for_widget()`
- `libfm-qt/src/core/dirlistjob.cpp` – Uses `FmPath`, `FmFileInfo`, `FmDirListJob` types
- `libfm-qt/src/core/thumbnailjob.cpp` – Accesses `fm_config` thumbnail settings
- `libfm-qt/src/core/trashjob.cpp` – Checks `fm_config->no_usb_trash`

## 6. Recommendations for Modernization

1. **Start with configuration** – Create a Qt-based `Config` class to replace `fm_config`
2. **Implement core interfaces** – Create `IFileOps`, `IFolderModel` as specified in `CLAUDE.md`
3. **Build Qt backends** – Implement `backends/qt/` with POSIX filesystem core
4. **Migrate UI gradually** – Replace libfm-qt widgets one by one
5. **Maintain compatibility** – Keep `Panel::` namespace during transition
6. **Remove vendored code** – Once migration complete, remove `libfm/` and `libfm-qt/` directories

## 7. Testing Strategy

When migrating each component:
1. Test file operations (copy, move, delete) with various file types
2. Verify configuration persistence
3. Test thumbnail generation for images
4. Verify application launching works
5. Ensure UI components behave identically

---

*This analysis provides a roadmap for the modernization effort. Refer to `CLAUDE.md` for the target architecture and `HACKING.md`/`TODO.md` for current progress.*