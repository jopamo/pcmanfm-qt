# PCManFM-Qt Development Guide

## 1. Project Overview

PCManFM-Qt is a Qt-based file manager originally started as the Qt port of PCManFM, the file manager of LXDE. This fork removes libfm/libfm-qt and re-implements the core on top of modern C++ and Qt 6, with small, focused GLib/GIO usage where it actually helps.

This fork is:

* Qt 6 only
* Linux only
* Desktop-only (no “desktop” window, no Wayland shell integration yet)
* In active transition away from libfm/libfm-qt

## 2. Modernization Goals

The main technical goal is:

> **Replace libfm/libfm-qt with a modular backend architecture built on Qt, with tiny GIO/udisks helpers for trash, mounts, and optional remote file systems**

Concretely:

* **Qt for everything “normal”**

  * local file operations
  * directory models and views
  * metadata, MIME types, icons
  * configuration, standard paths
* **Minimal GLib/GIO**

  * trash implementation
  * volume / mount management
  * optional remote filesystems (sftp, smb, dav, etc)
* **No libfm / libfm-qt anywhere in the build**
* **Backends are swappable** behind narrow interfaces in `src/core/`

> Keep this document live. Every time architecture or backend decisions change, update this guide and sync the current TODO list so contributors can act on it. Treat `HACKING.md` + `TODO.md` as working documents, not snapshots.

## 3. Target Architecture

### 3.1 Directory Layout

```text
src/
  core/
    ifileinfo.h
    ifoldermodel.h
    ifileops.h
    itrashbackend.h
    ivolumebackend.h
    iremotebackend.h
    backend_registry.h

  backends/
    qt/
      qt_fileinfo.h/.cpp
      qt_foldermodel.h/.cpp
      qt_fileops.h/.cpp

    gio/
      gio_trashbackend.h/.cpp
      gio_volumebackend.h/.cpp
      gio_remotebackend.h/.cpp

  ui/
    application.cpp
    mainwindow.cpp
    folderview.cpp
    sidepane_places.cpp
    fileoperationdialog.cpp
    settings.cpp
    ...
```

**Architecture rules**

* `ui/` knows only about `core/*` interfaces
* `backends/qt` uses Qt and standard C++ only
* `backends/gio` uses GLib/GIO and, optionally, udisks2 via D-Bus
* libfm and libfm-qt are not used anywhere

### 3.2 Core Interfaces (Qt-centric)

These are the “contract” between UI and backends.

#### File info

```c++
// core/ifileinfo.h
class IFileInfo {
public:
    virtual ~IFileInfo() = default

    virtual QString path() const = 0
    virtual QString name() const = 0
    virtual QString displayName() const = 0

    virtual bool isDir() const = 0
    virtual bool isFile() const = 0
    virtual bool isSymlink() const = 0
    virtual bool isHidden() const = 0

    virtual qint64 size() const = 0
    virtual QDateTime lastModified() const = 0

    virtual QString mimeType() const = 0
    virtual QIcon icon() const = 0
}
```

#### Folder model

```c++
// core/ifoldermodel.h
class IFolderModel : public QAbstractItemModel {
    Q_OBJECT
public:
    using QAbstractItemModel::QAbstractItemModel
    ~IFolderModel() override = default

    virtual void setDirectory(const QString &path) = 0
    virtual QString directory() const = 0

    virtual void refresh() = 0
}
```

#### File operations

```c++
// core/ifileops.h
enum class FileOpType {
    Copy,
    Move,
    Delete
}

struct FileOpRequest {
    FileOpType type
    QStringList sources
    QString destination
    bool followSymlinks
    bool overwriteExisting
}

struct FileOpProgress {
    quint64 bytesDone
    quint64 bytesTotal
    int filesDone
    int filesTotal
    QString currentPath
}

class IFileOps : public QObject {
    Q_OBJECT
public:
    using QObject::QObject
    ~IFileOps() override = default

    virtual void start(const FileOpRequest &req) = 0
    virtual void cancel() = 0

signals:
    void progress(const FileOpProgress &info)
    void finished(bool success, const QString &errorMessage)
}
```

#### Specialized backends

```c++
// core/itrashbackend.h
class ITrashBackend {
public:
    virtual ~ITrashBackend() = default

    virtual bool moveToTrash(const QString &path, QString *errorOut) = 0
    virtual bool restore(const QString &trashId, QString *errorOut) = 0
}
```

```c++
// core/ivolumebackend.h
struct VolumeInfo {
    QString id
    QString name
    QString device
    QString mountPoint
    bool   mounted
    bool   removable
}

class IVolumeBackend {
public:
    virtual ~IVolumeBackend() = default

    virtual QList<VolumeInfo> listVolumes() = 0
    virtual bool mount(const QString &id, QString *errorOut) = 0
    virtual bool unmount(const QString &id, QString *errorOut) = 0
    virtual bool eject(const QString &id, QString *errorOut) = 0
}
```

```c++
// core/iremotebackend.h
class IRemoteBackend {
public:
    virtual ~IRemoteBackend() = default

    virtual bool isRemote(const QUrl &url) const = 0
    virtual QString mapToMountPoint(const QUrl &url) = 0
}
```

#### Backend registry

```c++
// core/backend_registry.h
class BackendRegistry {
public:
    static void initDefaults()

    static std::unique_ptr<IFileOps> createFileOps()
    static std::unique_ptr<IFolderModel> createFolderModel(QObject *parent)

    static ITrashBackend *trash()
    static IVolumeBackend *volume()
    static IRemoteBackend *remote()
}
```

UI code never instantiates backend classes directly; it always goes through `BackendRegistry`.

## 4. Backend Implementations

### 4.1 Qt backend

**QtFileInfo**

* Wraps `QFileInfo` for metadata
* Uses `QMimeDatabase` to resolve MIME type
* Uses `QIcon::fromTheme()` to resolve icons

**QtFolderModel**

* Thin wrapper around `QFileSystemModel` at first
* Forwards `QAbstractItemModel` methods to internal `QFileSystemModel`
* Can be replaced with a custom `QAbstractItemModel` later if needed

**QtFileOps**

* Uses worker thread(s) to keep UI responsive
* Uses `QFile`, `QDir`, `QSaveFile`, etc for copy, move, delete
* Emits `FileOpProgress` and `finished` signals
* Supports cancellation via an atomic flag checked inside the worker

### 4.2 GIO backend

These are intentionally small and self-contained.

**GioTrashBackend**

* Uses `g_file_trash()` to move files to trash
* Optionally implements restore later via trash metadata
* Only deals with trash functionality, not general IO

**GioVolumeBackend**

* Uses `GVolumeMonitor` to discover volumes and mounts
* Implements mount, unmount, eject
* Can be extended to use udisks2 over D-Bus for more control

**GioRemoteBackend**

* Recognizes remote URL schemes like `sftp`, `ftp`, `smb`, `dav`, `davs`
* Integrates with GVFS mounts, mapping remote URLs to local mount points
* Keeps remote-specific logic out of the Qt core

## 5. Migration Strategy

### 5.1 High-level steps

1. Add the interfaces in `src/core/` and Qt backend implementations in `src/backends/qt/`
2. Change UI code in `src/ui/` to talk to `IFileInfo`, `IFolderModel`, `IFileOps`, etc instead of libfm-qt types
3. Provide a Qt-only configuration (`BackendRegistry::initDefaults()` with only Qt backends) so local file management works without libfm
4. Add `GioTrashBackend`, `GioVolumeBackend`, and optionally `GioRemoteBackend`
5. Remove libfm/libfm-qt includes, linkage, and build options

### 5.2 Example usage in UI code

Creating a folder model:

```c++
auto model = BackendRegistry::createFolderModel(this)
model->setDirectory(path)
view->setModel(model.get())
```

Sending something to trash:

```c++
auto trash = BackendRegistry::trash()
QString error
if (!trash->moveToTrash(path, &error)) {
    // show error dialog
}
```

Starting a file operation:

```c++
FileOpRequest req
req.type = FileOpType::Copy
req.sources = selectedPaths
req.destination = targetPath
req.followSymlinks = false
req.overwriteExisting = false

auto op = BackendRegistry::createFileOps()
connect(op.get(), &IFileOps::progress, this, &MainWindow::onFileOpProgress)
connect(op.get(), &IFileOps::finished, this, &MainWindow::onFileOpFinished)
op->start(req)
```

## 6. Development Environment

### 6.1 Required dependencies

* CMake 3.18 or newer
* Qt 6.6 or newer (Widgets, DBus, LinguistTools)
* GLib and GIO for the `backends/gio` implementation
* GVFS or equivalent, for trash and remote support
* lxqt2-build-tools or local replacements as needed in your build system

libfm-qt is only needed if you are building the legacy version. The modernized fork is designed to build without libfm-qt.

### 6.2 Building

```bash
# from your home or wherever you keep sources
cd "$HOME/projects/pcmanfm-qt"

# configure out-of-source build with tests enabled
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON

# build everything
cmake --build build -j"$(nproc)"

# run all unit tests
cd build
ctest --output-on-failure
```

if you want to rerun only tests after code changes

```bash
cd "$HOME/projects/pcmanfm-qt/build"
cmake --build . -j"$(nproc)"
ctest --output-on-failure
```

and to run a single test by name (example)

```bash
ctest -R pcmanfmqt_some_component_test --output-on-failure
```

Run from build tree:

```bash
./pcmanfm-qt /path/to/directory
```

## 7. Coding Guidelines

### 7.1 Language and framework

* C++ with Qt 6
* Build system: CMake
* Primary toolkit: Qt Widgets

### 7.2 Style

* Follow Qt coding style and naming patterns where reasonable
* Use Qt’s signal/slot mechanism for events
* Prefer Qt containers (`QList`, `QVector`, `QMap`) when interacting with Qt APIs
* Use QObject parent/child ownership and RAII where appropriate
* Keep UI code and backend code separate as described above

### 7.3 Modernization rules

When touching old code that still references libfm/libfm-qt:

* Replace `libfm` file operations with `QFile`, `QDir`, `QSaveFile`, and `IFileOps`
* Replace `libfm` metadata usage with `QFileInfo` and `QMimeDatabase`
* Replace `libfm` MIME handling with Qt’s MIME APIs
* Move any new behavior into the Qt or GIO backends instead of direct usage in `ui/`
* Keep platform assumptions Linux-only (no Windows or legacy Unix shims)

## 8. File Organization

* Source lives under `src/`
* Backends are in `src/backends/`
* Core interfaces are in `src/core/`
* UI code is in `src/ui/`
* Headers use `.h`, implementations use `.cpp`
* Qt Designer forms use `.ui`
* Translation files live under `translations/`

### File header template

```c++
/*
 * Short description of this file
 * pcmanfm-qt/relative/path/to/file.cpp
 */
```

## 9. Contributing

1. Fork the repository
2. Create a feature branch
3. Make changes that follow this guide
4. Build and test
5. Submit a pull request with a clear description

### What to test

* Copy, move, delete, rename for local files
* Folder navigation, tabs, back/forward, up
* Volume listing, mount, unmount, eject (if GIO backend enabled)
* Trash behavior and error handling
* Preferences and settings
* Any new backend code under failure conditions (permissions, missing devices, etc)

### Modernization-specific checks

* No new direct libfm/libfm-qt includes
* New code uses Qt and the core interfaces instead of libfm APIs
* Behavior matches or improves on the legacy implementation where it matters

## 10. License

PCManFM-Qt is licensed under the GPLv2 or any later version. See the `LICENSE` file for details.
