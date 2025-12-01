/*
 * GIO-based trash backend implementation
 * src/backends/gio/gio_trashbackend.cpp
 */

#include "gio_trashbackend.h"

#include <gio/gio.h>

#include <QDebug>
#include <QString>

namespace PCManFM {

bool GioTrashBackend::moveToTrash(const QString& path, QString* errorOut) {
    const QByteArray utf8Path = path.toUtf8();
    GFile* file = g_file_new_for_path(utf8Path.constData());
    if (!file) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to create GFile for path: %1").arg(path);
        }
        return false;
    }

    GError* error = nullptr;
    const gboolean success = g_file_trash(file, nullptr, &error);

    if (error) {
        if (errorOut) {
            *errorOut = QString::fromUtf8(error->message);
        }
        g_error_free(error);
    }

    g_object_unref(file);
    return success == TRUE;
}

bool GioTrashBackend::restore(const QString& trashId, QString* errorOut) {
    if (trashId.isEmpty()) {
        if (errorOut) {
            *errorOut = QStringLiteral("Invalid trash id");
        }
        return false;
    }

    const QByteArray utf8Id = trashId.toUtf8();

    GFile* trashedFile = nullptr;
    if (trashId.startsWith(QStringLiteral("trash://"))) {
        trashedFile = g_file_new_for_uri(utf8Id.constData());
    }
    else {
        trashedFile = g_file_new_for_path(utf8Id.constData());
    }

    if (!trashedFile) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to create GFile for trashed item: %1").arg(trashId);
        }
        return false;
    }

    GError* error = nullptr;
    GFileInfo* info = g_file_query_info(trashedFile, "trash::orig-path,standard::display-name", G_FILE_QUERY_INFO_NONE,
                                        nullptr, &error);

    if (!info) {
        if (error) {
            if (errorOut) {
                *errorOut = QString::fromUtf8(error->message);
            }
            g_error_free(error);
        }
        else if (errorOut) {
            *errorOut = QStringLiteral("Failed to query trash metadata for: %1").arg(trashId);
        }

        g_object_unref(trashedFile);
        return false;
    }

    const char* origPathC = g_file_info_get_attribute_string(info, "trash::orig-path");
    if (!origPathC || origPathC[0] == '\0') {
        if (errorOut) {
            *errorOut = QStringLiteral("Original path metadata is missing for trashed item: %1").arg(trashId);
        }

        g_object_unref(info);
        g_object_unref(trashedFile);
        return false;
    }

    GFile* destFile = g_file_new_for_path(origPathC);
    if (!destFile) {
        if (errorOut) {
            *errorOut = QStringLiteral("Failed to create destination GFile for original path: %1")
                            .arg(QString::fromUtf8(origPathC));
        }

        g_object_unref(info);
        g_object_unref(trashedFile);
        return false;
    }

    error = nullptr;
    const gboolean moved = g_file_move(
        trashedFile, destFile,
        static_cast<GFileCopyFlags>(G_FILE_COPY_ALL_METADATA | G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_OVERWRITE),
        nullptr, nullptr, nullptr, &error);

    if (!moved) {
        if (error) {
            if (errorOut) {
                *errorOut = QString::fromUtf8(error->message);
            }
            g_error_free(error);
        }
        else if (errorOut) {
            *errorOut = QStringLiteral("Failed to restore trashed item to original location");
        }
    }

    g_object_unref(destFile);
    g_object_unref(info);
    g_object_unref(trashedFile);

    return moved == TRUE;
}

}  // namespace PCManFM
