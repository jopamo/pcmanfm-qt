/*
 * GIO volume backend implementation
 * src/backends/gio/gio_volumebackend.cpp
 */

#include "gio_volumebackend.h"

#include <QDebug>

namespace PCManFM {

GioVolumeBackend::GioVolumeBackend() {
    volumeMonitor_ = g_volume_monitor_get();
    if (!volumeMonitor_) {
        qWarning() << "Failed to get GIO volume monitor";
    }
}

GioVolumeBackend::~GioVolumeBackend() {
    if (volumeMonitor_) {
        g_object_unref(volumeMonitor_);
    }
}

QList<VolumeInfo> GioVolumeBackend::listVolumes() {
    QList<VolumeInfo> volumes;

    if (!volumeMonitor_) {
        return volumes;
    }

    GList* gvolumes = g_volume_monitor_get_volumes(volumeMonitor_);
    GList* iter = gvolumes;

    while (iter) {
        GVolume* volume = G_VOLUME(iter->data);

        VolumeInfo info;

        // Get volume identifier
        char* id = g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
        if (id) {
            info.id = QString::fromUtf8(id);
            g_free(id);
        }

        // Get volume name
        char* name = g_volume_get_name(volume);
        if (name) {
            info.name = QString::fromUtf8(name);
            g_free(name);
        }

        // Get device path
        GDrive* drive = g_volume_get_drive(volume);
        if (drive) {
            char* device = g_drive_get_identifier(drive, G_DRIVE_IDENTIFIER_KIND_UNIX_DEVICE);
            if (device) {
                info.device = QString::fromUtf8(device);
                g_free(device);
            }
            g_object_unref(drive);
        }

        // Get mount point
        GMount* mount = g_volume_get_mount(volume);
        if (mount) {
            GFile* root = g_mount_get_root(mount);
            if (root) {
                char* path = g_file_get_path(root);
                if (path) {
                    info.mountPoint = QString::fromUtf8(path);
                    g_free(path);
                }
                g_object_unref(root);
            }
            g_object_unref(mount);
        }

        // Check if mounted
        info.mounted = g_volume_get_mount(volume) != nullptr;

        // Check if removable
        info.removable = g_volume_can_eject(volume);

        volumes.append(info);
        g_object_unref(volume);
        iter = iter->next;
    }

    g_list_free(gvolumes);

    return volumes;
}

bool GioVolumeBackend::mount(const QString& id, QString* errorOut) {
    if (!volumeMonitor_) {
        if (errorOut) *errorOut = QStringLiteral("Volume monitor not available");
        return false;
    }

    // Find the volume by ID
    GList* gvolumes = g_volume_monitor_get_volumes(volumeMonitor_);
    GList* iter = gvolumes;
    GVolume* targetVolume = nullptr;

    while (iter && !targetVolume) {
        GVolume* volume = G_VOLUME(iter->data);
        char* volumeId = g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
        if (volumeId && QString::fromUtf8(volumeId) == id) {
            targetVolume = volume;
            g_object_ref(targetVolume);
        }
        if (volumeId) g_free(volumeId);
        g_object_unref(volume);
        iter = iter->next;
    }

    g_list_free(gvolumes);

    if (!targetVolume) {
        if (errorOut) *errorOut = QStringLiteral("Volume not found: ") + id;
        return false;
    }

    // Mount the volume
    GError* error = nullptr;
    g_volume_mount(targetVolume, G_MOUNT_MOUNT_NONE, nullptr, nullptr, nullptr, &error);

    bool success = !error;
    if (error) {
        if (errorOut) *errorOut = QString::fromUtf8(error->message);
        g_error_free(error);
    }

    g_object_unref(targetVolume);
    return success;
}

bool GioVolumeBackend::unmount(const QString& id, QString* errorOut) {
    if (!volumeMonitor_) {
        if (errorOut) *errorOut = QStringLiteral("Volume monitor not available");
        return false;
    }

    // Find the mount by volume ID
    GList* gmounts = g_volume_monitor_get_mounts(volumeMonitor_);
    GList* iter = gmounts;
    GMount* targetMount = nullptr;

    while (iter && !targetMount) {
        GMount* mount = G_MOUNT(iter->data);
        GVolume* volume = g_mount_get_volume(mount);

        if (volume) {
            char* volumeId = g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
            if (volumeId && QString::fromUtf8(volumeId) == id) {
                targetMount = mount;
                g_object_ref(targetMount);
            }
            if (volumeId) g_free(volumeId);
            g_object_unref(volume);
        }

        g_object_unref(mount);
        iter = iter->next;
    }

    g_list_free(gmounts);

    if (!targetMount) {
        if (errorOut) *errorOut = QStringLiteral("Mounted volume not found: ") + id;
        return false;
    }

    // Unmount the mount
    GError* error = nullptr;
    g_mount_unmount_with_operation(targetMount, G_MOUNT_UNMOUNT_NONE, nullptr, nullptr, nullptr, &error);

    bool success = !error;
    if (error) {
        if (errorOut) *errorOut = QString::fromUtf8(error->message);
        g_error_free(error);
    }

    g_object_unref(targetMount);
    return success;
}

bool GioVolumeBackend::eject(const QString& id, QString* errorOut) {
    if (!volumeMonitor_) {
        if (errorOut) *errorOut = QStringLiteral("Volume monitor not available");
        return false;
    }

    // Find the volume by ID
    GList* gvolumes = g_volume_monitor_get_volumes(volumeMonitor_);
    GList* iter = gvolumes;
    GVolume* targetVolume = nullptr;

    while (iter && !targetVolume) {
        GVolume* volume = G_VOLUME(iter->data);
        char* volumeId = g_volume_get_identifier(volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
        if (volumeId && QString::fromUtf8(volumeId) == id) {
            targetVolume = volume;
            g_object_ref(targetVolume);
        }
        if (volumeId) g_free(volumeId);
        g_object_unref(volume);
        iter = iter->next;
    }

    g_list_free(gvolumes);

    if (!targetVolume) {
        if (errorOut) *errorOut = QStringLiteral("Volume not found: ") + id;
        return false;
    }

    // Eject the volume
    GError* error = nullptr;
    g_volume_eject_with_operation(targetVolume, G_MOUNT_UNMOUNT_NONE, nullptr, nullptr, nullptr, &error);

    bool success = !error;
    if (error) {
        if (errorOut) *errorOut = QString::fromUtf8(error->message);
        g_error_free(error);
    }

    g_object_unref(targetVolume);
    return success;
}

}  // namespace PCManFM