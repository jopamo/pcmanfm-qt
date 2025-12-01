/*
 * Backend registry implementation
 * src/core/backend_registry.cpp
 */

#include "backend_registry.h"

#include <QDebug>
#include <memory>

#include "../backends/gio/gio_remotebackend.h"
#include "../backends/gio/gio_trashbackend.h"
#include "../backends/gio/gio_volumebackend.h"
#include "../backends/qt/qt_fileops.h"
#include "../backends/qt/qt_foldermodel.h"

namespace PCManFM {

namespace {

std::unique_ptr<GioTrashBackend> g_trashBackend;
std::unique_ptr<GioVolumeBackend> g_volumeBackend;
std::unique_ptr<GioRemoteBackend> g_remoteBackend;

}  // namespace

void BackendRegistry::initDefaults() {
    // initialize default trash backend on demand
    if (!g_trashBackend) {
        g_trashBackend = std::make_unique<GioTrashBackend>();
    }

    // initialize default volume backend on demand
    if (!g_volumeBackend) {
        g_volumeBackend = std::make_unique<GioVolumeBackend>();
    }

    // initialize default remote backend on demand
    if (!g_remoteBackend) {
        g_remoteBackend = std::make_unique<GioRemoteBackend>();
    }

    qDebug() << "BackendRegistry initialized";
}

std::unique_ptr<IFileOps> BackendRegistry::createFileOps() {
    return std::make_unique<QtFileOps>();
}

std::unique_ptr<IFolderModel> BackendRegistry::createFolderModel(QObject* parent) {
    return std::make_unique<QtFolderModel>(parent);
}

ITrashBackend* BackendRegistry::trash() {
    if (!g_trashBackend) {
        g_trashBackend = std::make_unique<GioTrashBackend>();
    }
    return g_trashBackend.get();
}

IVolumeBackend* BackendRegistry::volume() {
    if (!g_volumeBackend) {
        g_volumeBackend = std::make_unique<GioVolumeBackend>();
    }
    return g_volumeBackend.get();
}

IRemoteBackend* BackendRegistry::remote() {
    if (!g_remoteBackend) {
        g_remoteBackend = std::make_unique<GioRemoteBackend>();
    }
    return g_remoteBackend.get();
}

}  // namespace PCManFM
