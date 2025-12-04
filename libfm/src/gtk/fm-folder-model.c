#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "fm-config.h"
#include "fm-folder-model.h"
#include "fm-file-info.h"
#include "fm-icon-pixbuf.h"
#include "fm-thumbnail.h"
#include "fm-gtk-marshal.h"
#include "glib-compat.h"
#include <gdk/gdk.h>
#include <glib/gi18n-lib.h>
#include <string.h>
#include <gio/gio.h>
struct _FmFolderModel {
    GObject parent;
    FmFolder* folder;
    GSequence* items;
    GSequence* hidden;
    gboolean show_hidden : 1;
    FmFolderModelCol sort_col;
    FmSortMode sort_mode;
    gint stamp;
    guint theme_change_handler;
    guint icon_size;
    guint thumbnail_max;
    GList* thumbnail_requests;
    GHashTable* items_hash;
    GSList* filters;
};
typedef struct _FmFolderItem FmFolderItem;
struct _FmFolderItem {
    FmFileInfo* inf;
    GdkPixbuf* icon;
    gpointer userdata;
    gboolean is_thumbnail : 1;
    gboolean thumbnail_loading : 1;
    gboolean thumbnail_failed : 1;
    gboolean is_extra : 1;
    FmFolderModelExtraFilePos pos : 3;
};
typedef struct _FmFolderModelFilterItem {
    FmFolderModelFilterFunc func;
    gpointer user_data;
} FmFolderModelFilterItem;
enum ReloadFlags {
    RELOAD_ICONS = 1 << 0,
    RELOAD_THUMBNAILS = 1 << 1,
    RELOAD_BOTH = (RELOAD_ICONS | RELOAD_THUMBNAILS)
};
static void fm_folder_model_tree_model_init(GtkTreeModelIface* iface);
static void fm_folder_model_tree_sortable_init(GtkTreeSortableIface* iface);
static void fm_folder_model_drag_source_init(GtkTreeDragSourceIface* iface);
static void fm_folder_model_drag_dest_init(GtkTreeDragDestIface* iface);
static void fm_folder_model_dispose(GObject* object);
G_DEFINE_TYPE_WITH_CODE(FmFolderModel,
                        fm_folder_model,
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_MODEL, fm_folder_model_tree_model_init)
                            G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_SORTABLE, fm_folder_model_tree_sortable_init)
                                G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_DRAG_SOURCE, fm_folder_model_drag_source_init)
                                    G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_DRAG_DEST, fm_folder_model_drag_dest_init))
static GtkTreeModelFlags fm_folder_model_get_flags(GtkTreeModel* tree_model);
static gint fm_folder_model_get_n_columns(GtkTreeModel* tree_model);
static GType fm_folder_model_get_column_type(GtkTreeModel* tree_model, gint index);
static gboolean fm_folder_model_get_iter(GtkTreeModel* tree_model, GtkTreeIter* iter, GtkTreePath* path);
static GtkTreePath* fm_folder_model_get_path(GtkTreeModel* tree_model, GtkTreeIter* iter);
static void fm_folder_model_get_value(GtkTreeModel* tree_model, GtkTreeIter* iter, gint column, GValue* value);
static gboolean fm_folder_model_iter_next(GtkTreeModel* tree_model, GtkTreeIter* iter);
static gboolean fm_folder_model_iter_children(GtkTreeModel* tree_model, GtkTreeIter* iter, GtkTreeIter* parent);
static gboolean fm_folder_model_iter_has_child(GtkTreeModel* tree_model, GtkTreeIter* iter);
static gint fm_folder_model_iter_n_children(GtkTreeModel* tree_model, GtkTreeIter* iter);
static gboolean fm_folder_model_iter_nth_child(GtkTreeModel* tree_model,
                                               GtkTreeIter* iter,
                                               GtkTreeIter* parent,
                                               gint n);
static gboolean fm_folder_model_iter_parent(GtkTreeModel* tree_model, GtkTreeIter* iter, GtkTreeIter* child);
static gboolean fm_folder_model_get_sort_column_id(GtkTreeSortable* sortable, gint* sort_column_id, GtkSortType* order);
static void fm_folder_model_set_sort_column_id(GtkTreeSortable* sortable, gint sort_column_id, GtkSortType order);
static void fm_folder_model_set_sort_func(GtkTreeSortable* sortable,
                                          gint sort_column_id,
                                          GtkTreeIterCompareFunc sort_func,
                                          gpointer user_data,
                                          GDestroyNotify destroy);
static void fm_folder_model_set_default_sort_func(GtkTreeSortable* sortable,
                                                  GtkTreeIterCompareFunc sort_func,
                                                  gpointer user_data,
                                                  GDestroyNotify destroy);
static void fm_folder_model_do_sort(FmFolderModel* model);
static inline gboolean file_can_show(FmFolderModel* model, FmFileInfo* file);
static void on_icon_theme_changed(GtkIconTheme* theme, FmFolderModel* model);
static void on_thumbnail_loaded(FmThumbnailRequest* req, gpointer user_data);
static void on_show_thumbnail_changed(FmConfig* cfg, gpointer user_data);
static void on_thumbnail_local_changed(FmConfig* cfg, gpointer user_data);
static void on_thumbnail_max_changed(FmConfig* cfg, gpointer user_data);
typedef struct {
    FmFolderModelCol id;
    GType type;
    const char* name;
    const char* title;
    gboolean sortable;
    gint default_width;
    void (*get_value)(FmFileInfo* fi, GValue* value);
    gint (*compare)(FmFileInfo* fi1, FmFileInfo* fi2);
} FmFolderModelInfo;
static FmFolderModelInfo column_infos_raw[] = {{FM_FOLDER_MODEL_COL_NAME, 0, "name", N_("Name"), TRUE},
                                               {FM_FOLDER_MODEL_COL_DESC, 0, "desc", N_("Description"), TRUE},
                                               {FM_FOLDER_MODEL_COL_SIZE, 0, "size", N_("Size"), TRUE},
                                               {FM_FOLDER_MODEL_COL_PERM, 0, "perm", N_("Permissions"), FALSE},
                                               {FM_FOLDER_MODEL_COL_OWNER, 0, "owner", N_("Owner"), FALSE},
                                               {FM_FOLDER_MODEL_COL_MTIME, 0, "mtime", N_("Modified"), TRUE},
                                               {FM_FOLDER_MODEL_COL_DIRNAME, 0, "dirname", N_("Location"), TRUE},
                                               {FM_FOLDER_MODEL_COL_EXT, 0, "ext", N_("Extension"), TRUE},
                                               {FM_FOLDER_MODEL_COL_INFO, 0, "info", NULL, TRUE},
                                               {FM_FOLDER_MODEL_COL_ICON, 0, "icon", NULL, FALSE},
                                               {FM_FOLDER_MODEL_COL_GICON, 0, "gicon", NULL, FALSE}};
static guint column_infos_n = 0;
static FmFolderModelInfo** column_infos = NULL;
enum { ROW_DELETING, FILTER_CHANGED, N_SIGNALS };
static guint signals[N_SIGNALS];
static void fm_folder_model_init(FmFolderModel* model) {
    model->sort_mode = FM_SORT_ASCENDING;
    model->sort_col = FM_FOLDER_MODEL_COL_DEFAULT;
    model->stamp = g_random_int();
    model->theme_change_handler =
        g_signal_connect(gtk_icon_theme_get_default(), "changed", G_CALLBACK(on_icon_theme_changed), model);
    g_signal_connect(fm_config, "changed::show_thumbnail", G_CALLBACK(on_show_thumbnail_changed), model);
    g_signal_connect(fm_config, "changed::thumbnail_local", G_CALLBACK(on_thumbnail_local_changed), model);
    g_signal_connect(fm_config, "changed::thumbnail_max", G_CALLBACK(on_thumbnail_max_changed), model);
    model->thumbnail_max = fm_config->thumbnail_max << 10;
    model->items_hash = g_hash_table_new(g_direct_hash, g_direct_equal);
}
static void fm_folder_model_class_init(FmFolderModelClass* klass) {
    GObjectClass* object_class;
    fm_folder_model_parent_class = (GObjectClass*)g_type_class_peek_parent(klass);
    object_class = (GObjectClass*)klass;
    object_class->dispose = fm_folder_model_dispose;
    signals[ROW_DELETING] = g_signal_new("row-deleting", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
                                         G_STRUCT_OFFSET(FmFolderModelClass, row_deleting), NULL, NULL,
                                         fm_marshal_VOID__BOXED_BOXED_POINTER, G_TYPE_NONE, 3, GTK_TYPE_TREE_PATH,
                                         GTK_TYPE_TREE_ITER, G_TYPE_POINTER);
    signals[FILTER_CHANGED] = g_signal_new("filter-changed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST,
                                           G_STRUCT_OFFSET(FmFolderModelClass, filter_changed), NULL, NULL,
                                           g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}
static void fm_folder_model_tree_model_init(GtkTreeModelIface* iface) {
    iface->get_flags = fm_folder_model_get_flags;
    iface->get_n_columns = fm_folder_model_get_n_columns;
    iface->get_column_type = fm_folder_model_get_column_type;
    iface->get_iter = fm_folder_model_get_iter;
    iface->get_path = fm_folder_model_get_path;
    iface->get_value = fm_folder_model_get_value;
    iface->iter_next = fm_folder_model_iter_next;
    iface->iter_children = fm_folder_model_iter_children;
    iface->iter_has_child = fm_folder_model_iter_has_child;
    iface->iter_n_children = fm_folder_model_iter_n_children;
    iface->iter_nth_child = fm_folder_model_iter_nth_child;
    iface->iter_parent = fm_folder_model_iter_parent;
}
static void fm_folder_model_tree_sortable_init(GtkTreeSortableIface* iface) {
    iface->get_sort_column_id = fm_folder_model_get_sort_column_id;
    iface->set_sort_column_id = fm_folder_model_set_sort_column_id;
    iface->set_sort_func = fm_folder_model_set_sort_func;
    iface->set_default_sort_func = fm_folder_model_set_default_sort_func;
    iface->has_default_sort_func = (gboolean (*)(GtkTreeSortable*))gtk_false;
}
static gboolean _drag_source_false(GtkTreeDragSource* drag_source, GtkTreePath* path) {
    return FALSE;
}
static gboolean _drag_source_false2(GtkTreeDragSource* drag_source,
                                    GtkTreePath* path,
                                    GtkSelectionData* selection_data) {
    return FALSE;
}
static void fm_folder_model_drag_source_init(GtkTreeDragSourceIface* iface) {
    iface->row_draggable = _drag_source_false;
    iface->drag_data_get = _drag_source_false2;
    iface->drag_data_delete = _drag_source_false;
}
static gboolean _drag_dest_false(GtkTreeDragDest* drag_dest, GtkTreePath* dest, GtkSelectionData* selection_data) {
    return FALSE;
}
static void fm_folder_model_drag_dest_init(GtkTreeDragDestIface* iface) {
    iface->drag_data_received = _drag_dest_false;
    iface->row_drop_possible = _drag_dest_false;
}
static void fm_folder_model_filter_item_free(FmFolderModelFilterItem* item) {
    g_slice_free(FmFolderModelFilterItem, item);
}
static void fm_folder_model_dispose(GObject* object) {
    FmFolderModel* model = FM_FOLDER_MODEL(object);
    if (model->folder)
        fm_folder_model_set_folder(model, NULL);
    if (model->items) {
        g_sequence_free(model->items);
        model->items = NULL;
    }
    if (model->hidden) {
        g_sequence_free(model->hidden);
        model->hidden = NULL;
    }
    if (model->theme_change_handler) {
        g_signal_handler_disconnect(gtk_icon_theme_get_default(), model->theme_change_handler);
        model->theme_change_handler = 0;
    }
    g_signal_handlers_disconnect_by_func(fm_config, on_show_thumbnail_changed, model);
    g_signal_handlers_disconnect_by_func(fm_config, on_thumbnail_local_changed, model);
    g_signal_handlers_disconnect_by_func(fm_config, on_thumbnail_max_changed, model);
    if (model->thumbnail_requests) {
        g_list_foreach(model->thumbnail_requests, (GFunc)fm_thumbnail_request_cancel, NULL);
        g_list_free(model->thumbnail_requests);
        model->thumbnail_requests = NULL;
    }
    if (model->items_hash) {
        g_hash_table_destroy(model->items_hash);
        model->items_hash = NULL;
    }
    if (model->filters) {
        g_slist_free_full(model->filters, (GDestroyNotify)fm_folder_model_filter_item_free);
        model->filters = NULL;
    }
    (*G_OBJECT_CLASS(fm_folder_model_parent_class)->dispose)(object);
}
FmFolderModel* fm_folder_model_new(FmFolder* dir, gboolean show_hidden) {
    FmFolderModel* model;
    model = (FmFolderModel*)g_object_new(FM_TYPE_FOLDER_MODEL, NULL);
    model->items = NULL;
    model->hidden = NULL;
    model->show_hidden = show_hidden;
    fm_folder_model_set_folder(model, dir);
    CHECK_MODULES();
    return model;
}
static inline FmFolderItem* fm_folder_item_new(FmFileInfo* inf) {
    FmFolderItem* item = g_slice_new0(FmFolderItem);
    item->inf = fm_file_info_ref(inf);
    return item;
}
static inline void fm_folder_item_free(gpointer data) {
    FmFolderItem* item = (FmFolderItem*)data;
    if (item->icon)
        g_object_unref(item->icon);
    fm_file_info_unref(item->inf);
    g_slice_free(FmFolderItem, item);
}
static void _fm_folder_model_files_changed(FmFolder* dir, GSList* files, FmFolderModel* model) {
    GSList* l;
    for (l = files; l; l = l->next)
        fm_folder_model_file_changed(model, l->data);
}
static void _fm_folder_model_add_file(FmFolderModel* model, FmFileInfo* file) {
    if (!file_can_show(model, file))
        g_sequence_append(model->hidden, fm_folder_item_new(file));
    else
        fm_folder_model_file_created(model, file);
}
static void _fm_folder_model_files_added(FmFolder* dir, GSList* files, FmFolderModel* model) {
    GSList* l;
    for (l = files; l; l = l->next) {
        FmFileInfo* fi = FM_FILE_INFO(l->data);
        _fm_folder_model_add_file(model, fi);
    }
}
static void _fm_folder_model_files_removed(FmFolder* dir, GSList* files, FmFolderModel* model) {
    GSList* l;
    for (l = files; l; l = l->next)
        fm_folder_model_file_deleted(model, FM_FILE_INFO(l->data));
}
FmFolder* fm_folder_model_get_folder(FmFolderModel* model) {
    return model->folder;
}
FmPath* fm_folder_model_get_folder_path(FmFolderModel* model) {
    return model->folder ? fm_folder_get_path(model->folder) : NULL;
}
void fm_folder_model_set_folder(FmFolderModel* model, FmFolder* dir) {
    GSequence *new_items, *new_hidden;
    GSequenceIter *item_it, *next_item_it;
    FmFolderItem* item;
    if (model->folder == dir)
        return;
    new_items = g_sequence_new(fm_folder_item_free);
    new_hidden = g_sequence_new(fm_folder_item_free);
    if (model->items) {
        item_it = g_sequence_get_begin_iter(model->items);
        while (!g_sequence_iter_is_end(item_it)) {
            next_item_it = g_sequence_iter_next(item_it);
            item = (FmFolderItem*)g_sequence_get(item_it);
            if (item->is_extra)
                g_sequence_move(item_it, g_sequence_get_end_iter(new_items));
            item_it = next_item_it;
        }
    }
    if (model->hidden) {
        item_it = g_sequence_get_begin_iter(model->hidden);
        while (!g_sequence_iter_is_end(item_it)) {
            next_item_it = g_sequence_iter_next(item_it);
            item = (FmFolderItem*)g_sequence_get(item_it);
            if (item->is_extra)
                g_sequence_move(item_it, g_sequence_get_begin_iter(new_hidden));
            item_it = next_item_it;
        }
    }
    if (model->folder) {
        guint row_deleted_signal = g_signal_lookup("row-deleted", GTK_TYPE_TREE_MODEL);
        g_signal_handlers_disconnect_by_func(model->folder, _fm_folder_model_files_added, model);
        g_signal_handlers_disconnect_by_func(model->folder, _fm_folder_model_files_removed, model);
        g_signal_handlers_disconnect_by_func(model->folder, _fm_folder_model_files_changed, model);
        if (g_signal_has_handler_pending(model, row_deleted_signal, 0, TRUE)) {
            GtkTreePath* tp = gtk_tree_path_new_first();
            GSequenceIter* item_it = g_sequence_get_begin_iter(model->items);
            GtkTreeIter it;
            it.stamp = model->stamp;
            while (!g_sequence_iter_is_end(item_it)) {
                FmFolderItem* item = (FmFolderItem*)g_sequence_get(item_it);
                it.user_data = item_it;
                g_signal_emit(model, signals[ROW_DELETING], 0, tp, &it, item->userdata);
                gtk_tree_model_row_deleted((GtkTreeModel*)model, tp);
                item_it = g_sequence_iter_next(item_it);
            }
            gtk_tree_path_free(tp);
        }
        g_hash_table_remove_all(model->items_hash);
        g_sequence_free(model->items);
        g_sequence_free(model->hidden);
        g_object_unref(model->folder);
        model->folder = NULL;
    }
    model->items = new_items;
    model->hidden = new_hidden;
    item_it = g_sequence_get_begin_iter(model->items);
    while (!g_sequence_iter_is_end(item_it)) {
        item = (FmFolderItem*)g_sequence_get(item_it);
        g_hash_table_insert(model->items_hash, item->inf, item_it);
        item_it = g_sequence_iter_next(item_it);
    }
    if (!dir)
        return;
    model->folder = FM_FOLDER(g_object_ref(dir));
    g_signal_connect(model->folder, "files-added", G_CALLBACK(_fm_folder_model_files_added), model);
    g_signal_connect(model->folder, "files-removed", G_CALLBACK(_fm_folder_model_files_removed), model);
    g_signal_connect(model->folder, "files-changed", G_CALLBACK(_fm_folder_model_files_changed), model);
    if (fm_folder_is_loaded(model->folder) || fm_folder_is_incremental(model->folder)) {
        if (!fm_folder_is_empty(model->folder)) {
            GList* l;
            FmFileInfoList* files = fm_folder_get_files(model->folder);
            for (l = fm_file_info_list_peek_head_link(files); l; l = l->next)
                _fm_folder_model_add_file(model, FM_FILE_INFO(l->data));
        }
    }
}
static GtkTreeModelFlags fm_folder_model_get_flags(GtkTreeModel* tree_model) {
    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), (GtkTreeModelFlags)0);
    return (GTK_TREE_MODEL_LIST_ONLY | GTK_TREE_MODEL_ITERS_PERSIST);
}
static gint fm_folder_model_get_n_columns(GtkTreeModel* tree_model) {
    return (gint)column_infos_n;
}
static GType fm_folder_model_get_column_type(GtkTreeModel* tree_model, gint index) {
    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), G_TYPE_INVALID);
    if ((guint)index >= column_infos_n)
        return G_TYPE_INVALID;
    if (column_infos[index] == NULL)
        return G_TYPE_INVALID;
    return column_infos[index]->type;
}
static gboolean fm_folder_model_get_iter(GtkTreeModel* tree_model, GtkTreeIter* iter, GtkTreePath* path) {
    FmFolderModel* model;
    gint *indices, n, depth;
    GSequenceIter* items_it;
    g_assert(FM_IS_FOLDER_MODEL(tree_model));
    g_assert(path != NULL);
    model = (FmFolderModel*)tree_model;
    indices = gtk_tree_path_get_indices(path);
    depth = gtk_tree_path_get_depth(path);
    g_assert(depth == 1);
    n = indices[0];
    if (n >= g_sequence_get_length(model->items) || n < 0)
        return FALSE;
    items_it = g_sequence_get_iter_at_pos(model->items, n);
    g_assert(items_it != g_sequence_get_end_iter(model->items));
    iter->stamp = model->stamp;
    iter->user_data = items_it;
    return TRUE;
}
static GtkTreePath* fm_folder_model_get_path(GtkTreeModel* tree_model, GtkTreeIter* iter) {
    GtkTreePath* path;
    GSequenceIter* items_it;
    FmFolderModel* model = FM_FOLDER_MODEL(tree_model);
    g_return_val_if_fail(model, NULL);
    g_return_val_if_fail(iter != NULL, NULL);
    g_return_val_if_fail(iter->stamp == model->stamp, NULL);
    g_return_val_if_fail(iter->user_data != NULL, NULL);
    items_it = (GSequenceIter*)iter->user_data;
    path = gtk_tree_path_new();
    gtk_tree_path_append_index(path, g_sequence_iter_get_position(items_it));
    return path;
}
static void fm_folder_model_get_value(GtkTreeModel* tree_model, GtkTreeIter* iter, gint column, GValue* value) {
    GSequenceIter* item_it;
    FmFolderModel* model = FM_FOLDER_MODEL(tree_model);
    g_return_if_fail(iter != NULL);
    g_return_if_fail((guint)column < column_infos_n && column_infos[column] != NULL);
    g_value_init(value, column_infos[column]->type);
    item_it = (GSequenceIter*)iter->user_data;
    g_return_if_fail(item_it != NULL);
    FmFolderItem* item = (FmFolderItem*)g_sequence_get(item_it);
    FmFileInfo* info = item->inf;
    FmIcon* icon;
    mode_t mode;
    char buf[12];
    if (column >= FM_FOLDER_MODEL_N_COLS) {
        column_infos[column]->get_value(info, value);
    }
    else
        switch ((FmFolderModelCol)column) {
            case FM_FOLDER_MODEL_COL_GICON:
                icon = fm_file_info_get_icon(info);
                if (G_LIKELY(icon))
                    g_value_set_object(value, icon);
                break;
            case FM_FOLDER_MODEL_COL_ICON: {
                if (G_UNLIKELY(!item->icon)) {
                    icon = fm_file_info_get_icon(info);
                    if (!icon)
                        return;
                    if (fm_file_info_is_dir(info))
                        item->icon = fm_pixbuf_from_icon_with_fallback(icon, model->icon_size, "folder");
                    else if (fm_file_info_is_desktop_entry(info))
                        item->icon =
                            fm_pixbuf_from_icon_with_fallback(icon, model->icon_size, "application-x-executable");
                    else
                        item->icon = fm_pixbuf_from_icon(icon, model->icon_size);
                }
                g_value_set_object(value, item->icon);
                if (fm_config->show_thumbnail &&
                    (fm_path_is_native_or_trash(fm_file_info_get_path(info)) || !fm_config->thumbnail_local)) {
                    if (!item->is_thumbnail && !item->thumbnail_failed && !item->thumbnail_loading) {
                        if (fm_file_info_can_thumbnail(item->inf)) {
                            FmThumbnailRequest* req =
                                fm_thumbnail_request(item->inf, model->icon_size, on_thumbnail_loaded, model);
                            model->thumbnail_requests = g_list_prepend(model->thumbnail_requests, req);
                            item->thumbnail_loading = TRUE;
                        }
                        else {
                            item->thumbnail_failed = TRUE;
                        }
                    }
                }
                break;
            }
            case FM_FOLDER_MODEL_COL_NAME:
                g_value_set_string(value, fm_file_info_get_disp_name(info));
                break;
            case FM_FOLDER_MODEL_COL_SIZE:
                g_value_set_string(value, fm_file_info_get_disp_size(info));
                break;
            case FM_FOLDER_MODEL_COL_DESC:
                g_value_set_string(value, fm_file_info_get_desc(info));
                break;
            case FM_FOLDER_MODEL_COL_PERM:
                mode = fm_file_info_get_mode(info);
                strcpy(buf, "---------");
                if ((mode & S_IRUSR) != 0)
                    buf[0] = 'r';
                if ((mode & S_IWUSR) != 0)
                    buf[1] = 'w';
                if ((mode & (S_IXUSR | S_ISUID)) == (S_IXUSR | S_ISUID))
                    buf[2] = 's';
                else if ((mode & S_IXUSR) != 0)
                    buf[2] = 'x';
                else if ((mode & S_ISUID) != 0)
                    buf[2] = 'S';
                if ((mode & S_IRGRP) != 0)
                    buf[3] = 'r';
                if ((mode & S_IWGRP) != 0)
                    buf[4] = 'w';
                if ((mode & (S_IXGRP | S_ISGID)) == (S_IXGRP | S_ISGID))
                    buf[5] = 's';
                else if ((mode & S_IXGRP) != 0)
                    buf[5] = 'x';
                else if ((mode & S_ISGID) != 0)
                    buf[5] = 'S';
                if ((mode & S_IROTH) != 0)
                    buf[6] = 'r';
                if ((mode & S_IWOTH) != 0)
                    buf[7] = 'w';
                if ((mode & (S_IXOTH | S_ISVTX)) == (S_IXOTH | S_ISVTX))
                    buf[8] = 't';
                else if ((mode & S_IXOTH) != 0)
                    buf[8] = 'x';
                else if ((mode & S_ISVTX) != 0)
                    buf[8] = 'T';
                g_value_set_string(value, buf);
                break;
            case FM_FOLDER_MODEL_COL_OWNER:
                g_value_set_string(value, fm_file_info_get_disp_owner(info));
                break;
            case FM_FOLDER_MODEL_COL_MTIME:
                g_value_set_string(value, fm_file_info_get_disp_mtime(info));
                break;
            case FM_FOLDER_MODEL_COL_INFO:
                g_value_set_pointer(value, info);
                break;
            case FM_FOLDER_MODEL_COL_DIRNAME: {
                FmPath* path = fm_file_info_get_path(info);
                FmPath* dirpath = fm_path_get_parent(path);
                if (dirpath) {
                    char* dirname = fm_path_display_name(dirpath, TRUE);
                    g_value_set_string(value, dirname);
                    g_free(dirname);
                }
                break;
            }
            case FM_FOLDER_MODEL_COL_EXT: {
                const char* name = fm_file_info_get_disp_name(info);
                const char* str;
                if (fm_file_info_is_dir(info)) {
                    str = NULL;
                }
                else {
                    str = strrchr(name, '.');
                    if (str == name)
                        str = NULL;
                    else if (str)
                        str++;
                }
                g_value_set_string(value, str);
            } break;
            case FM_FOLDER_MODEL_N_COLS:;
        }
}
static gboolean fm_folder_model_iter_next(GtkTreeModel* tree_model, GtkTreeIter* iter) {
    GSequenceIter *item_it, *next_item_it;
    FmFolderModel* model;
    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), FALSE);
    if (iter == NULL || iter->user_data == NULL)
        return FALSE;
    model = (FmFolderModel*)tree_model;
    item_it = (GSequenceIter*)iter->user_data;
    next_item_it = g_sequence_iter_next(item_it);
    if (g_sequence_iter_is_end(next_item_it))
        return FALSE;
    iter->stamp = model->stamp;
    iter->user_data = next_item_it;
    return TRUE;
}
static gboolean fm_folder_model_iter_children(GtkTreeModel* tree_model, GtkTreeIter* iter, GtkTreeIter* parent) {
    FmFolderModel* model;
    GSequenceIter* items_it;
    g_return_val_if_fail(parent == NULL || parent->user_data != NULL, FALSE);
    if (parent)
        return FALSE;
    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), FALSE);
    model = (FmFolderModel*)tree_model;
    items_it = g_sequence_get_begin_iter(model->items);
    iter->stamp = model->stamp;
    iter->user_data = items_it;
    return TRUE;
}
static gboolean fm_folder_model_iter_has_child(GtkTreeModel* tree_model, GtkTreeIter* iter) {
    return FALSE;
}
static gint fm_folder_model_iter_n_children(GtkTreeModel* tree_model, GtkTreeIter* iter) {
    FmFolderModel* model;
    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), -1);
    g_return_val_if_fail(iter == NULL || iter->user_data != NULL, -1);
    model = (FmFolderModel*)tree_model;
    if (!iter)
        return g_sequence_get_length(model->items);
    return 0;
}
static gboolean fm_folder_model_iter_nth_child(GtkTreeModel* tree_model,
                                               GtkTreeIter* iter,
                                               GtkTreeIter* parent,
                                               gint n) {
    GSequenceIter* items_it;
    FmFolderModel* model;
    g_return_val_if_fail(FM_IS_FOLDER_MODEL(tree_model), FALSE);
    model = (FmFolderModel*)tree_model;
    if (parent)
        return FALSE;
    if (n >= g_sequence_get_length(model->items) || n < 0)
        return FALSE;
    items_it = g_sequence_get_iter_at_pos(model->items, n);
    g_assert(items_it != g_sequence_get_end_iter(model->items));
    iter->stamp = model->stamp;
    iter->user_data = items_it;
    return TRUE;
}
static gboolean fm_folder_model_iter_parent(GtkTreeModel* tree_model, GtkTreeIter* iter, GtkTreeIter* child) {
    return FALSE;
}
static gboolean fm_folder_model_get_sort_column_id(GtkTreeSortable* sortable,
                                                   gint* sort_column_id,
                                                   GtkSortType* order) {
    FmFolderModel* model = FM_FOLDER_MODEL(sortable);
    if (sort_column_id)
        *sort_column_id = model->sort_col;
    if (order)
        *order = FM_SORT_IS_ASCENDING(model->sort_mode) ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING;
    return TRUE;
}
static void fm_folder_model_set_sort_column_id(GtkTreeSortable* sortable, gint sort_column_id, GtkSortType order) {
    FmFolderModel* model = FM_FOLDER_MODEL(sortable);
    FmSortMode mode = model->sort_mode;
    mode &= ~FM_SORT_ORDER_MASK;
    if (order == GTK_SORT_ASCENDING)
        mode |= FM_SORT_ASCENDING;
    else
        mode |= FM_SORT_DESCENDING;
    model->sort_col = sort_column_id;
    model->sort_mode = mode;
    gtk_tree_sortable_sort_column_changed(sortable);
    fm_folder_model_do_sort(model);
}
static void fm_folder_model_set_sort_func(GtkTreeSortable* sortable,
                                          gint sort_column_id,
                                          GtkTreeIterCompareFunc sort_func,
                                          gpointer user_data,
                                          GDestroyNotify destroy) {
    g_warning("fm_folder_model_set_sort_func: Not supported\n");
}
static void fm_folder_model_set_default_sort_func(GtkTreeSortable* sortable,
                                                  GtkTreeIterCompareFunc sort_func,
                                                  gpointer user_data,
                                                  GDestroyNotify destroy) {
    g_warning("fm_folder_model_set_default_sort_func: Not supported\n");
}
static gint fm_folder_model_compare(gconstpointer item1, gconstpointer item2, gpointer user_data) {
    FmFolderModel* model = FM_FOLDER_MODEL(user_data);
    FmFileInfo* file1 = ((FmFolderItem*)item1)->inf;
    FmFileInfo* file2 = ((FmFolderItem*)item2)->inf;
    const char* key1;
    const char* key2;
    const char* name;
    goffset diff;
    int ret = 0;
    if (!(model->sort_mode & FM_SORT_NO_FOLDER_FIRST)) {
        ret = fm_file_info_is_dir(file2) - fm_file_info_is_dir(file1);
        if (ret)
            return ret;
    }
    if (G_UNLIKELY(((FmFolderItem*)item1)->is_extra))
        switch (((FmFolderItem*)item1)->pos) {
            case FM_FOLDER_MODEL_ITEMPOS_SORTED:
                break;
            case FM_FOLDER_MODEL_ITEMPOS_PRE:
                if (((FmFolderItem*)item2)->is_extra && ((FmFolderItem*)item2)->pos == FM_FOLDER_MODEL_ITEMPOS_PRE)
                    goto _main_sort;
                return -1;
            case FM_FOLDER_MODEL_ITEMPOS_POST:
                if (((FmFolderItem*)item2)->is_extra && ((FmFolderItem*)item2)->pos == FM_FOLDER_MODEL_ITEMPOS_POST)
                    goto _main_sort;
                return 1;
        }
    if (G_UNLIKELY(((FmFolderItem*)item2)->is_extra))
        switch (((FmFolderItem*)item2)->pos) {
            case FM_FOLDER_MODEL_ITEMPOS_SORTED:
                break;
            case FM_FOLDER_MODEL_ITEMPOS_PRE:
                return 1;
            case FM_FOLDER_MODEL_ITEMPOS_POST:
                return -1;
        }
_main_sort:
    if (model->sort_col >= FM_FOLDER_MODEL_N_COLS && model->sort_col < column_infos_n &&
        column_infos[model->sort_col]->compare) {
        ret = column_infos[model->sort_col]->compare(file1, file2);
        if (ret == 0)
            goto _sort_by_name;
    }
    else
        switch (model->sort_col) {
            case FM_FOLDER_MODEL_COL_SIZE:
                diff = fm_file_info_get_size(file1) - fm_file_info_get_size(file2);
                if (0 == diff)
                    goto _sort_by_name;
                ret = diff > 0 ? 1 : -1;
                break;
            case FM_FOLDER_MODEL_COL_MTIME:
                ret = fm_file_info_get_mtime(file1) - fm_file_info_get_mtime(file2);
                if (0 == ret)
                    goto _sort_by_name;
                break;
            case FM_FOLDER_MODEL_COL_DESC:
                ret = g_utf8_collate(fm_file_info_get_desc(file1), fm_file_info_get_desc(file2));
                if (0 == ret)
                    goto _sort_by_name;
                break;
            case FM_FOLDER_MODEL_COL_UNSORTED:
                return 0;
            case FM_FOLDER_MODEL_COL_DIRNAME: {
                FmPath* dirpath1 = fm_file_info_get_path(file1);
                FmPath* dirpath2 = fm_file_info_get_path(file2);
                dirpath1 = fm_path_get_parent(dirpath1);
                dirpath2 = fm_path_get_parent(dirpath2);
                ret = fm_path_compare(dirpath1, dirpath2);
            } break;
            case FM_FOLDER_MODEL_COL_EXT:
                name = fm_file_info_get_disp_name(file1);
                key1 = strrchr(name, '.');
                if (key1 == name)
                    key1 = NULL;
                name = fm_file_info_get_disp_name(file2);
                key2 = strrchr(name, '.');
                if (key2 == name)
                    key2 = NULL;
                ret = g_strcmp0(key1, key2);
                if (ret == 0)
                    goto _sort_by_name;
                break;
            default:
            _sort_by_name:
                if (model->sort_mode & FM_SORT_CASE_SENSITIVE) {
                    key1 = fm_file_info_get_disp_name(file1);
                    key2 = fm_file_info_get_disp_name(file2);
                }
                else {
                    key1 = fm_file_info_get_collate_key(file1);
                    key2 = fm_file_info_get_collate_key(file2);
                }
                ret = g_strcmp0(key1, key2);
                break;
        }
    return FM_SORT_IS_ASCENDING(model->sort_mode) ? ret : -ret;
}
static void fm_folder_model_do_sort(FmFolderModel* model) {
    GHashTable* old_order;
    gint* new_order;
    GSequenceIter* items_it;
    GtkTreePath* path;
    if (model->items == NULL || g_sequence_get_length(model->items) <= 1)
        return;
    old_order = g_hash_table_new(g_direct_hash, g_direct_equal);
    items_it = g_sequence_get_begin_iter(model->items);
    while (!g_sequence_iter_is_end(items_it)) {
        int i = g_sequence_iter_get_position(items_it);
        g_hash_table_insert(old_order, items_it, GINT_TO_POINTER(i));
        items_it = g_sequence_iter_next(items_it);
    }
    g_sequence_sort(model->items, fm_folder_model_compare, model);
    new_order = g_new(int, g_sequence_get_length(model->items));
    items_it = g_sequence_get_begin_iter(model->items);
    while (!g_sequence_iter_is_end(items_it)) {
        int i = g_sequence_iter_get_position(items_it);
        new_order[i] = GPOINTER_TO_INT(g_hash_table_lookup(old_order, items_it));
        items_it = g_sequence_iter_next(items_it);
    }
    g_hash_table_destroy(old_order);
    path = gtk_tree_path_new();
    gtk_tree_model_rows_reordered(GTK_TREE_MODEL(model), path, NULL, new_order);
    gtk_tree_path_free(path);
    g_free(new_order);
}
static void _fm_folder_model_insert_item(FmFolderModel* model, FmFolderItem* new_item) {
    GtkTreeIter it;
    GtkTreePath* path;
    GSequenceIter* item_it = g_sequence_insert_sorted(model->items, new_item, fm_folder_model_compare, model);
    g_hash_table_insert(model->items_hash, new_item->inf, item_it);
    it.stamp = model->stamp;
    it.user_data = item_it;
    path = gtk_tree_path_new_from_indices(g_sequence_iter_get_position(item_it), -1);
    gtk_tree_model_row_inserted(GTK_TREE_MODEL(model), path, &it);
    gtk_tree_path_free(path);
}
void fm_folder_model_file_created(FmFolderModel* model, FmFileInfo* file) {
    FmFolderItem* new_item = fm_folder_item_new(file);
    _fm_folder_model_insert_item(model, new_item);
}
gboolean fm_folder_model_extra_file_add(FmFolderModel* model, FmFileInfo* file, FmFolderModelExtraFilePos where) {
    FmFolderItem* item;
    if (g_hash_table_lookup(model->items_hash, file) != NULL)
        return FALSE;
    if (!file_can_show(model, file)) {
        GSequenceIter* seq_it = g_sequence_get_begin_iter(model->hidden);
        while (!g_sequence_iter_is_end(seq_it)) {
            item = (FmFolderItem*)g_sequence_get(seq_it);
            if (item->inf == file)
                return FALSE;
            seq_it = g_sequence_iter_next(seq_it);
        }
    }
    item = fm_folder_item_new(file);
    item->is_extra = TRUE;
    item->pos = where;
    _fm_folder_model_insert_item(model, item);
    return TRUE;
}
static inline GSequenceIter* info2iter(FmFolderModel* model, FmFileInfo* file) {
    return (GSequenceIter*)g_hash_table_lookup(model->items_hash, file);
}
void fm_folder_model_file_deleted(FmFolderModel* model, FmFileInfo* file) {
    GSequenceIter* seq_it;
    FmFolderItem* item = NULL;
    GtkTreePath* path;
    GtkTreeIter it;
    if (!file_can_show(model, file)) {
        seq_it = g_sequence_get_begin_iter(model->hidden);
        while (!g_sequence_iter_is_end(seq_it)) {
            item = (FmFolderItem*)g_sequence_get(seq_it);
            if (item->inf == file) {
                g_sequence_remove(seq_it);
                return;
            }
            seq_it = g_sequence_iter_next(seq_it);
        }
        return;
    }
    seq_it = info2iter(model, file);
    g_return_if_fail(seq_it != NULL);
    item = (FmFolderItem*)g_sequence_get(seq_it);
    path = gtk_tree_path_new_from_indices(g_sequence_iter_get_position(seq_it), -1);
    it.stamp = model->stamp;
    it.user_data = seq_it;
    g_signal_emit(model, signals[ROW_DELETING], 0, path, &it, item->userdata);
    gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), path);
    gtk_tree_path_free(path);
    g_hash_table_remove(model->items_hash, file);
    g_sequence_remove(seq_it);
}
gboolean fm_folder_model_extra_file_remove(FmFolderModel* model, FmFileInfo* file) {
    GSequenceIter* seq_it;
    FmFolderItem* item = NULL;
    GtkTreePath* path;
    GtkTreeIter it;
    gboolean is_hidden = FALSE;
    seq_it = info2iter(model, file);
    if (seq_it)
        item = (FmFolderItem*)g_sequence_get(seq_it);
    else if (!file_can_show(model, file)) {
        seq_it = g_sequence_get_begin_iter(model->hidden);
        while (!g_sequence_iter_is_end(seq_it)) {
            item = (FmFolderItem*)g_sequence_get(seq_it);
            if (item->inf == file)
                break;
            item = NULL;
            seq_it = g_sequence_iter_next(seq_it);
        }
        is_hidden = TRUE;
    }
    if (item == NULL)
        return FALSE;
    if (!item->is_extra)
        return FALSE;
    if (!is_hidden) {
        path = gtk_tree_path_new_from_indices(g_sequence_iter_get_position(seq_it), -1);
        it.stamp = model->stamp;
        it.user_data = seq_it;
        g_signal_emit(model, signals[ROW_DELETING], 0, path, &it, item->userdata);
        gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), path);
        gtk_tree_path_free(path);
        g_hash_table_remove(model->items_hash, file);
    }
    g_sequence_remove(seq_it);
    return TRUE;
}
void fm_folder_model_file_changed(FmFolderModel* model, FmFileInfo* file) {
    FmFolderItem* item = NULL;
    GSequenceIter* items_it;
    GtkTreeIter it;
    GtkTreePath* path;
    it.stamp = model->stamp;
    if (!file_can_show(model, file)) {
        items_it = info2iter(model, file);
        if (items_it) {
            gint delete_pos = g_sequence_iter_get_position(items_it);
            it.user_data = items_it;
            g_hash_table_remove(model->items_hash, file);
            g_sequence_move(items_it, g_sequence_get_begin_iter(model->hidden));
            path = gtk_tree_path_new_from_indices(delete_pos, -1);
            item = (FmFolderItem*)g_sequence_get(items_it);
            g_signal_emit(model, signals[ROW_DELETING], 0, path, &it, item->userdata);
            gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), path);
            gtk_tree_path_free(path);
        }
        return;
    }
    items_it = info2iter(model, file);
    if (!items_it) {
        items_it = g_sequence_get_begin_iter(model->hidden);
        while (!g_sequence_iter_is_end(items_it)) {
            item = (FmFolderItem*)g_sequence_get(items_it);
            if (item->inf == file) {
                GSequenceIter* insert_item_it = g_sequence_search(model->items, item, fm_folder_model_compare, model);
                it.user_data = items_it;
                g_sequence_move(items_it, insert_item_it);
                g_hash_table_insert(model->items_hash, file, items_it);
                path = gtk_tree_path_new_from_indices(g_sequence_iter_get_position(items_it), -1);
                gtk_tree_model_row_inserted(GTK_TREE_MODEL(model), path, &it);
                gtk_tree_path_free(path);
                return;
            }
            items_it = g_sequence_iter_next(items_it);
        }
        g_return_if_reached();
    }
    item = (FmFolderItem*)g_sequence_get(items_it);
    if (item->icon) {
        g_object_unref(item->icon);
        item->icon = NULL;
        item->is_thumbnail = FALSE;
    }
    it.user_data = items_it;
    path = gtk_tree_path_new_from_indices(g_sequence_iter_get_position(items_it), -1);
    gtk_tree_model_row_changed(GTK_TREE_MODEL(model), path, &it);
    gtk_tree_path_free(path);
}
static inline gboolean file_can_show(FmFolderModel* model, FmFileInfo* file) {
    if (!model->show_hidden && fm_file_info_is_hidden(file))
        return FALSE;
    if (model->filters) {
        GSList* l;
        for (l = model->filters; l; l = l->next) {
            FmFolderModelFilterItem* item = (FmFolderModelFilterItem*)l->data;
            if (!item->func(file, item->user_data))
                return FALSE;
        }
    }
    return TRUE;
}
gboolean fm_folder_model_get_show_hidden(FmFolderModel* model) {
    return model->show_hidden;
}
void fm_folder_model_set_show_hidden(FmFolderModel* model, gboolean show_hidden) {
    g_return_if_fail(model != NULL);
    if (model->show_hidden == show_hidden)
        return;
    model->show_hidden = show_hidden;
    fm_folder_model_apply_filters(model);
}
static void reload_icons(FmFolderModel* model, enum ReloadFlags flags) {
    GSequenceIter* it = g_sequence_get_begin_iter(model->items);
    GtkTreePath* tp = gtk_tree_path_new_from_indices(0, -1);
    if (model->thumbnail_requests) {
        g_list_foreach(model->thumbnail_requests, (GFunc)fm_thumbnail_request_cancel, NULL);
        g_list_free(model->thumbnail_requests);
        model->thumbnail_requests = NULL;
    }
    for (; !g_sequence_iter_is_end(it); it = g_sequence_iter_next(it)) {
        FmFolderItem* item = (FmFolderItem*)g_sequence_get(it);
        if (item->icon) {
            GtkTreeIter tree_it;
            if ((flags & RELOAD_ICONS && !item->is_thumbnail) || (flags & RELOAD_THUMBNAILS && item->is_thumbnail)) {
                g_object_unref(item->icon);
                item->icon = NULL;
                item->is_thumbnail = FALSE;
                item->thumbnail_loading = FALSE;
                tree_it.stamp = model->stamp;
                tree_it.user_data = it;
                gtk_tree_model_row_changed(GTK_TREE_MODEL(model), tp, &tree_it);
            }
        }
        gtk_tree_path_next(tp);
    }
    gtk_tree_path_free(tp);
    it = g_sequence_get_begin_iter(model->hidden);
    for (; !g_sequence_iter_is_end(it); it = g_sequence_iter_next(it)) {
        FmFolderItem* item = (FmFolderItem*)g_sequence_get(it);
        if (item->icon) {
            g_object_unref(item->icon);
            item->icon = NULL;
            item->is_thumbnail = FALSE;
            item->thumbnail_loading = FALSE;
        }
    }
}
static void on_icon_theme_changed(GtkIconTheme* theme, FmFolderModel* model) {
    reload_icons(model, RELOAD_ICONS);
}
gboolean fm_folder_model_find_iter_by_filename(FmFolderModel* model, GtkTreeIter* it, const char* name) {
    GSequenceIter* item_it = g_sequence_get_begin_iter(model->items);
    for (; !g_sequence_iter_is_end(item_it); item_it = g_sequence_iter_next(item_it)) {
        FmFolderItem* item = (FmFolderItem*)g_sequence_get(item_it);
        FmPath* path = fm_file_info_get_path(item->inf);
        if (g_strcmp0(fm_path_get_basename(path), name) == 0) {
            it->stamp = model->stamp;
            it->user_data = item_it;
            return TRUE;
        }
    }
    return FALSE;
}
static void on_thumbnail_loaded(FmThumbnailRequest* req, gpointer user_data) {
    FmFolderModel* model = (FmFolderModel*)user_data;
    FmFileInfo* fi = fm_thumbnail_request_get_file_info(req);
    GdkPixbuf* pix = fm_thumbnail_request_get_pixbuf(req);
    GtkTreeIter it;
    GSequenceIter* seq_it;
    model->thumbnail_requests = g_list_remove(model->thumbnail_requests, req);
    seq_it = info2iter(model, fi);
    if (seq_it) {
        FmFolderItem* item;
        item = (FmFolderItem*)g_sequence_get(seq_it);
        if (pix) {
            GtkTreePath* tp;
            it.stamp = model->stamp;
            it.user_data = seq_it;
            GDK_THREADS_ENTER();
            tp = fm_folder_model_get_path(GTK_TREE_MODEL(model), &it);
            if (item->icon)
                g_object_unref(item->icon);
            item->icon = g_object_ref(pix);
            item->is_thumbnail = TRUE;
            gtk_tree_model_row_changed(GTK_TREE_MODEL(model), tp, &it);
            gtk_tree_path_free(tp);
            GDK_THREADS_LEAVE();
        }
        else {
            item->thumbnail_failed = TRUE;
        }
        item->thumbnail_loading = FALSE;
        return;
    }
    g_return_if_reached();
}
void fm_folder_model_set_icon_size(FmFolderModel* model, guint icon_size) {
    if (model->icon_size == icon_size)
        return;
    model->icon_size = icon_size;
    reload_icons(model, RELOAD_BOTH);
}
guint fm_folder_model_get_icon_size(FmFolderModel* model) {
    return model->icon_size;
}
static void on_show_thumbnail_changed(FmConfig* cfg, gpointer user_data) {
    FmFolderModel* model = (FmFolderModel*)user_data;
    reload_icons(model, RELOAD_THUMBNAILS);
}
static GList* find_in_pending_thumbnail_requests(FmFolderModel* model, FmFileInfo* fi) {
    GList *reqs = model->thumbnail_requests, *l;
    for (l = reqs; l; l = l->next) {
        FmThumbnailRequest* req = (FmThumbnailRequest*)l->data;
        FmFileInfo* fi2 = fm_thumbnail_request_get_file_info(req);
        FmPath* path = fm_file_info_get_path(fi);
        FmPath* path2 = fm_file_info_get_path(fi2);
        if (path && path2 && strcmp(fm_path_get_basename(path), fm_path_get_basename(path2)) == 0)
            return l;
    }
    return NULL;
}
static void reload_thumbnail(FmFolderModel* model, GSequenceIter* seq_it, FmFolderItem* item) {
    GtkTreeIter it;
    GtkTreePath* tp;
    if (item->is_thumbnail) {
        g_object_unref(item->icon);
        item->icon = NULL;
        it.stamp = model->stamp;
        it.user_data = seq_it;
        tp = fm_folder_model_get_path(GTK_TREE_MODEL(model), &it);
        gtk_tree_model_row_changed(GTK_TREE_MODEL(model), tp, &it);
        gtk_tree_path_free(tp);
    }
}
static void on_thumbnail_local_changed(FmConfig* cfg, gpointer user_data) {
    FmFolderModel* model = (FmFolderModel*)user_data;
    FmThumbnailRequest* req;
    GList* new_reqs = NULL;
    GSequenceIter* seq_it;
    FmFileInfo* fi;
    if (cfg->thumbnail_local) {
        GList* l;
        for (l = model->thumbnail_requests; l;) {
            GList* next = l->next;
            req = (FmThumbnailRequest*)l->data;
            fi = fm_thumbnail_request_get_file_info(req);
            if (!fm_path_is_native_or_trash(fm_file_info_get_path(fi))) {
                fm_thumbnail_request_cancel(req);
                model->thumbnail_requests = g_list_delete_link(model->thumbnail_requests, l);
            }
            l = next;
        }
    }
    seq_it = g_sequence_get_begin_iter(model->items);
    while (!g_sequence_iter_is_end(seq_it)) {
        FmFolderItem* item = (FmFolderItem*)g_sequence_get(seq_it);
        fi = item->inf;
        FmPath* path = fm_file_info_get_path(fi);
        if (cfg->thumbnail_local) {
            if (!fm_path_is_native_or_trash(path))
                reload_thumbnail(model, seq_it, item);
        }
        else {
            if (!fm_path_is_native_or_trash(path)) {
                req = fm_thumbnail_request(fi, model->icon_size, on_thumbnail_loaded, model);
                new_reqs = g_list_append(new_reqs, req);
            }
        }
        seq_it = g_sequence_iter_next(seq_it);
    }
    if (new_reqs)
        model->thumbnail_requests = g_list_concat(model->thumbnail_requests, new_reqs);
}
static void on_thumbnail_max_changed(FmConfig* cfg, gpointer user_data) {
    FmFolderModel* model = (FmFolderModel*)user_data;
    FmThumbnailRequest* req;
    GList* new_reqs = NULL;
    GSequenceIter* seq_it;
    FmFileInfo* fi;
    guint thumbnail_max_bytes = fm_config->thumbnail_max << 10;
    goffset size;
    seq_it = g_sequence_get_begin_iter(model->items);
    while (!g_sequence_iter_is_end(seq_it)) {
        FmFolderItem* item = (FmFolderItem*)g_sequence_get(seq_it);
        fi = item->inf;
        if (cfg->thumbnail_max) {
            if (thumbnail_max_bytes > model->thumbnail_max) {
                size = fm_file_info_get_size(fi);
                if (size < thumbnail_max_bytes && size > model->thumbnail_max) {
                    if (!item->thumbnail_failed && fm_file_info_can_thumbnail(fi) && fm_file_info_is_image(fi)) {
                        req = fm_thumbnail_request(fi, model->icon_size, on_thumbnail_loaded, model);
                        new_reqs = g_list_append(new_reqs, req);
                    }
                }
            }
        }
        else {
            if (!item->is_thumbnail && !item->thumbnail_loading && !item->thumbnail_failed &&
                fm_file_info_can_thumbnail(fi)) {
                GList* l = find_in_pending_thumbnail_requests(model, fi);
                if (!l) {
                    req = fm_thumbnail_request(fi, model->icon_size, on_thumbnail_loaded, model);
                    new_reqs = g_list_append(new_reqs, req);
                }
            }
        }
        seq_it = g_sequence_iter_next(seq_it);
    }
    if (new_reqs)
        model->thumbnail_requests = g_list_concat(model->thumbnail_requests, new_reqs);
    model->thumbnail_max = thumbnail_max_bytes;
}
void fm_folder_model_set_item_userdata(FmFolderModel* model, GtkTreeIter* it, gpointer user_data) {
    GSequenceIter* item_it;
    FmFolderItem* item;
    g_return_if_fail(it != NULL);
    g_return_if_fail(model != NULL);
    g_return_if_fail(it->stamp == model->stamp);
    item_it = (GSequenceIter*)it->user_data;
    g_return_if_fail(item_it != NULL);
    item = (FmFolderItem*)g_sequence_get(item_it);
    item->userdata = user_data;
}
gpointer fm_folder_model_get_item_userdata(FmFolderModel* model, GtkTreeIter* it) {
    GSequenceIter* item_it;
    FmFolderItem* item;
    g_return_val_if_fail(it != NULL, NULL);
    g_return_val_if_fail(model != NULL, NULL);
    g_return_val_if_fail(it->stamp == model->stamp, NULL);
    item_it = (GSequenceIter*)it->user_data;
    g_return_val_if_fail(item_it != NULL, NULL);
    item = (FmFolderItem*)g_sequence_get(item_it);
    return item->userdata;
}
void fm_folder_model_add_filter(FmFolderModel* model, FmFolderModelFilterFunc func, gpointer user_data) {
    FmFolderModelFilterItem* item = g_slice_new(FmFolderModelFilterItem);
    item->func = func;
    item->user_data = user_data;
    model->filters = g_slist_prepend(model->filters, item);
}
void fm_folder_model_remove_filter(FmFolderModel* model, FmFolderModelFilterFunc func, gpointer user_data) {
    GSList* l;
    for (l = model->filters; l; l = l->next) {
        FmFolderModelFilterItem* item = (FmFolderModelFilterItem*)l->data;
        if (item->func == func && item->user_data == user_data) {
            model->filters = g_slist_delete_link(model->filters, l);
            fm_folder_model_filter_item_free(item);
            break;
        }
    }
}
void fm_folder_model_apply_filters(FmFolderModel* model) {
    FmFolderItem* item;
    GSList* items_to_show = NULL;
    GtkTreeIter tree_it;
    GtkTreePath* tree_path;
    GSequenceIter* item_it;
    tree_it.stamp = model->stamp;
    item_it = g_sequence_get_begin_iter(model->hidden);
    while (!g_sequence_iter_is_end(item_it)) {
        item = (FmFolderItem*)g_sequence_get(item_it);
        if (file_can_show(model, item->inf)) {
            items_to_show = g_slist_prepend(items_to_show, item_it);
        }
        item_it = g_sequence_iter_next(item_it);
    }
    item_it = g_sequence_get_begin_iter(model->items);
    while (!g_sequence_iter_is_end(item_it)) {
        GSequenceIter* next_item_it = g_sequence_iter_next(item_it);
        item = (FmFolderItem*)g_sequence_get(item_it);
        if (!file_can_show(model, item->inf)) {
            gint delete_pos = g_sequence_iter_get_position(item_it);
            tree_it.user_data = item_it;
            g_hash_table_remove(model->items_hash, item->inf);
            g_sequence_move(item_it, g_sequence_get_begin_iter(model->hidden));
            tree_path = gtk_tree_path_new_from_indices(delete_pos, -1);
            g_signal_emit(model, signals[ROW_DELETING], 0, tree_path, &tree_it, item->userdata);
            gtk_tree_model_row_deleted(GTK_TREE_MODEL(model), tree_path);
            gtk_tree_path_free(tree_path);
        }
        item_it = next_item_it;
    }
    if (items_to_show) {
        GSList* l;
        for (l = items_to_show; l; l = l->next) {
            GSequenceIter* insert_item_it;
            item_it = (GSequenceIter*)l->data;
            item = (FmFolderItem*)g_sequence_get(item_it);
            insert_item_it = g_sequence_search(model->items, item, fm_folder_model_compare, model);
            tree_it.user_data = item_it;
            g_sequence_move(item_it, insert_item_it);
            g_hash_table_insert(model->items_hash, item->inf, item_it);
            tree_path = gtk_tree_path_new_from_indices(g_sequence_iter_get_position(item_it), -1);
            gtk_tree_model_row_inserted(GTK_TREE_MODEL(model), tree_path, &tree_it);
            gtk_tree_path_free(tree_path);
        }
        g_slist_free(items_to_show);
    }
    g_signal_emit(model, signals[FILTER_CHANGED], 0);
}
void fm_folder_model_set_sort(FmFolderModel* model, FmFolderModelCol col, FmSortMode mode) {
    FmFolderModelCol old_col = model->sort_col;
    if ((guint)col >= column_infos_n)
        col = old_col;
    if (mode == FM_SORT_DEFAULT)
        mode = model->sort_mode;
    if (model->sort_mode != mode || old_col != col) {
        model->sort_mode = mode;
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(model), col,
                                             FM_SORT_IS_ASCENDING(mode) ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING);
    }
}
gboolean fm_folder_model_get_sort(FmFolderModel* model, FmFolderModelCol* col, FmSortMode* mode) {
    if (!FM_IS_FOLDER_MODEL(model))
        return FALSE;
    if (col)
        *col = model->sort_col;
    if (mode)
        *mode = model->sort_mode;
    return TRUE;
}
const char* fm_folder_model_col_get_title(FmFolderModel* model, FmFolderModelCol col_id) {
    if (G_UNLIKELY((guint)col_id >= column_infos_n || column_infos[col_id] == NULL))
        return NULL;
    return _(column_infos[col_id]->title);
}
gboolean fm_folder_model_col_is_sortable(FmFolderModel* model, FmFolderModelCol col_id) {
    if (G_UNLIKELY((guint)col_id >= column_infos_n || column_infos[col_id] == NULL))
        return FALSE;
    return column_infos[col_id]->sortable;
}
const char* fm_folder_model_col_get_name(FmFolderModelCol col_id) {
    if (G_UNLIKELY((guint)col_id >= column_infos_n || column_infos[col_id] == NULL))
        return NULL;
    return column_infos[col_id]->name;
}
FmFolderModelCol fm_folder_model_get_col_by_name(const char* str) {
    if (G_LIKELY(str != NULL)) {
        FmFolderModelCol i = 0;
        for (i = 0; i < column_infos_n; ++i) {
            if (column_infos[i] && strcmp(str, column_infos[i]->name) == 0)
                return i;
        }
    }
    return (FmFolderModelCol)-1;
}
gint fm_folder_model_col_get_default_width(FmFolderModel* model, FmFolderModelCol col_id) {
    if (G_UNLIKELY((guint)col_id >= column_infos_n || column_infos[col_id] == NULL))
        return 0;
    return column_infos[col_id]->default_width;
}
FmFolderModelCol fm_folder_model_add_custom_column(const char* name, FmFolderModelColumnInit* init) {
    FmFolderModelInfo* info;
    guint i;
    g_return_val_if_fail(name && init && init->title && init->get_type && init->get_value, FM_FOLDER_MODEL_COL_DEFAULT);
    for (i = 0; i < column_infos_n; i++)
        if (strcmp(name, column_infos[i]->name) == 0)
            return FM_FOLDER_MODEL_COL_DEFAULT;
    column_infos = g_realloc(column_infos, sizeof(FmFolderModelInfo*) * (i + 1));
    info = g_new0(FmFolderModelInfo, 1);
    column_infos[i] = info;
    column_infos_n = i + 1;
    info->type = init->get_type();
    info->name = g_strdup(name);
    info->title = g_strdup(init->title);
    info->sortable = (init->compare != NULL);
    info->default_width = init->default_width;
    info->get_value = init->get_value;
    info->compare = init->compare;
    return i;
}
gboolean fm_folder_model_col_is_valid(FmFolderModelCol col_id) {
    return col_id < column_infos_n;
}
FM_MODULE_DEFINE_TYPE(gtk_folder_col, FmFolderModelColumnInit, 1)
static gboolean fm_module_callback_gtk_folder_col(const char* name, gpointer init, int ver) {
    if (fm_folder_model_add_custom_column(name, init) == FM_FOLDER_MODEL_COL_DEFAULT)
        return FALSE;
    return TRUE;
}
void _fm_folder_model_init(void) {
    guint i;
    column_infos_n = FM_FOLDER_MODEL_N_COLS;
    column_infos = g_new0(FmFolderModelInfo*, FM_FOLDER_MODEL_N_COLS);
    for (i = 0; i < G_N_ELEMENTS(column_infos_raw); i++) {
        FmFolderModelCol id = column_infos_raw[i].id;
        column_infos[id] = &column_infos_raw[i];
    }
    column_infos[FM_FOLDER_MODEL_COL_NAME]->type = G_TYPE_STRING;
    column_infos[FM_FOLDER_MODEL_COL_DESC]->type = G_TYPE_STRING;
    column_infos[FM_FOLDER_MODEL_COL_SIZE]->type = G_TYPE_STRING;
    column_infos[FM_FOLDER_MODEL_COL_PERM]->type = G_TYPE_STRING;
    column_infos[FM_FOLDER_MODEL_COL_OWNER]->type = G_TYPE_STRING;
    column_infos[FM_FOLDER_MODEL_COL_MTIME]->type = G_TYPE_STRING;
    column_infos[FM_FOLDER_MODEL_COL_DIRNAME]->type = G_TYPE_STRING;
    column_infos[FM_FOLDER_MODEL_COL_EXT]->type = G_TYPE_STRING;
    column_infos[FM_FOLDER_MODEL_COL_INFO]->type = G_TYPE_POINTER;
    column_infos[FM_FOLDER_MODEL_COL_ICON]->type = GDK_TYPE_PIXBUF;
    column_infos[FM_FOLDER_MODEL_COL_GICON]->type = G_TYPE_ICON;
    fm_module_register_gtk_folder_col();
}
void _fm_folder_model_finalize(void) {
    FmFolderModelCol i = column_infos_n;
    fm_module_unregister_type("gtk_folder_col");
    column_infos_n = 0;
    while (i > FM_FOLDER_MODEL_N_COLS) {
        i--;
        g_free((char*)column_infos[i]->name);
        g_free((char*)column_infos[i]->title);
        g_free(column_infos[i]);
    }
    g_free(column_infos);
}
