#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/video/videooverlay.h>
#include <gst/video/video-info.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "desktop_integration.h"

#define THUMB_SIZE 128
#define ZOOM_MIN 0.65
#define ZOOM_MAX 1.9
#define ZOOM_STEP 1.12
#define CACHE_LIMIT 256

typedef enum {
    ITEM_FILE,
    ITEM_DIR
} ItemKind;

typedef enum {
    VIEW_GRID,
    VIEW_LIST
} ViewMode;

typedef enum {
    CLIPBOARD_NONE,
    CLIPBOARD_COPY,
    CLIPBOARD_CUT
} ClipboardMode;

typedef struct {
    char *path;
    char *name;
    char *content_type;
    goffset size;
    time_t mtime;
    ItemKind kind;
    bool is_image;
    bool is_video;
} FileItem;

typedef struct {
    char *key;
    GdkPixbuf *pixbuf;
    guint64 tick;
} ThumbEntry;

typedef struct {
    GHashTable *map;
    GPtrArray *entries;
    guint64 tick;
} ThumbCache;

typedef struct _FolderTab FolderTab;

typedef struct {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *path_entry;
    GtkWidget *status_path;
    GtkWidget *status_counts;
    GtkWidget *status_preview;
    GtkWidget *stack;
    GtkWidget *grid_scroller;
    GtkWidget *list_scroller;
    GtkWidget *grid;
    GtkWidget *list;
    GtkWidget *view_button;
    GtkWidget *back_button;
    GtkWidget *forward_button;
    GtkWidget *up_button;
    GtkWidget *places;
    GPtrArray *tabs;
    FolderTab *active_tab;
    ThumbCache thumbs;
    GstElement *playbin;
    GstElement *video_sink;
    GtkWidget *video_widget;
    GtkWidget *video_socket;
    GtkWidget *active_video_tile;
    char *active_video_path;
    GPtrArray *hover_frames;
    guint hover_frame_pos;
    guint hover_timeout_id;
    GPtrArray *clipboard_paths;
    ClipboardMode clipboard_mode;
    GtkWidget *context_popup;
    bool drag_selecting;
    ViewMode drag_view;
    double zoom;
    ViewMode mode;
} App;

typedef struct {
    App *app;
    GPtrArray *paths;
    char *target_path;
    bool target_is_dir;
} ContextData;

typedef struct {
    App *app;
    char *name;
    char *path;
} DriveData;

typedef enum {
    SIDEBAR_ICON_HOME,
    SIDEBAR_ICON_DESKTOP,
    SIDEBAR_ICON_DOCUMENTS,
    SIDEBAR_ICON_DOWNLOADS,
    SIDEBAR_ICON_MUSIC,
    SIDEBAR_ICON_PICTURES,
    SIDEBAR_ICON_VIDEOS,
    SIDEBAR_ICON_PROJECTS,
    SIDEBAR_ICON_ROOT,
    SIDEBAR_ICON_DRIVE
} SidebarIconKind;

struct _FolderTab {
    App *app;
    char *path;
    GtkWidget *page;
    GtkWidget *label;
    GCancellable *cancel;
    GPtrArray *items;
    GPtrArray *history;
    guint history_pos;
    guint scan_generation;
    bool loading;
};

typedef struct {
    FolderTab *tab;
    GPtrArray *items;
    char *path;
    GCancellable *cancel;
    guint generation;
} ScanResult;

static const char *CSS =
    "* { border-radius: 0; outline-width: 0; }"
    "window { background: #000; color: #f0f0f0; font-family: monospace; }"
    "button { border-radius: 0; box-shadow: none; text-shadow: none; background-image: none; }"
    "entry { border-radius: 0; box-shadow: none; background-image: none; }"
    "dialog, messagedialog { background: #0a0a0a; color: #d8d8d8; border: 1px solid #777; }"
    "dialog box, messagedialog box { background: #0a0a0a; }"
    "dialog button, messagedialog button { min-height: 30px; padding: 5px 18px; background: #121212; color: #d8d8d8; border: 1px solid #555; font-family: monospace; font-size: 13px; }"
    "dialog button:hover, messagedialog button:hover { background: #2a2a2a; color: #fff; border-color: #aaa; }"
    "dialog entry { min-height: 30px; padding: 5px 10px; background: #050505; color: #f0f0f0; border: 1px solid #777; font-family: monospace; font-size: 14px; }"
    "#root { background: #000; }"
    "#sidebar { background: #000; border-right: 1px solid #777; padding: 14px 14px 0 14px; }"
    "#sidebar-title { padding: 10px 8px 14px 8px; color: #b8b8b8; font-family: monospace; font-size: 13px; letter-spacing: 1px; }"
    "#sidebar button { min-height: 38px; padding: 7px 10px; border: 0; background: transparent; color: #f2f2f2; font-family: monospace; font-size: 16px; box-shadow: none; text-shadow: none; }"
    "#sidebar button:hover { background: #151515; color: #fff; }"
    "#sidebar button:checked, #sidebar button:active { background: #2a2a2a; color: #fff; border-left: 2px solid #fff; }"
    "#toolbar { background: #000; border-bottom: 0; padding: 14px 14px 12px 14px; }"
    "#toolbar entry { min-height: 40px; padding: 4px 14px; background: #050505; color: #f4f4f4; border: 1px solid #888; font-family: monospace; font-size: 16px; }"
    "#toolbar button { min-height: 42px; min-width: 54px; padding: 4px 12px; background: #050505; color: #f0f0f0; border: 1px solid #888; font-family: monospace; font-size: 22px; box-shadow: none; text-shadow: none; }"
    "#toolbar button:hover { background: #202020; color: #fff; border-color: #fff; }"
    "notebook header { background: #000; border-bottom: 1px solid #777; }"
    "notebook tab { padding: 10px 26px; background: #000; border: 1px solid #777; border-bottom: 0; box-shadow: none; font-family: monospace; font-size: 15px; }"
    "notebook tab:checked { background: #101010; color: #fff; }"
    "scrolledwindow { background: #000; border: 0; }"
    "flowbox { background: #000; padding: 14px; }"
    "flowboxchild:selected { background: transparent; color: #ddd; }"
    "flowboxchild:selected #tile { background: #242424; border-color: #fff; }"
    "#tile { background: #050505; border: 1px solid #555; padding: 10px; }"
    "#tile:hover, #tile:selected { background: #181818; border-color: #aaa; }"
    "#thumb { background: #000; }"
    "#tile-name { color: #f2f2f2; font-family: monospace; font-size: 13px; }"
    "#tile-meta { color: #b6b6b6; font-family: monospace; font-size: 12px; }"
    "#list-header { background: #000; border-bottom: 1px solid #666; padding: 10px 24px 12px 24px; }"
    "#list-header label { color: #d8d8d8; font-family: monospace; font-size: 13px; font-weight: bold; }"
    "list { background: #000; padding: 14px; }"
    "row { padding: 0; border: 0; background: #000; }"
    "row:selected, row:hover { background: #171717; }"
    "row:selected #rowbox { background: #242424; border-color: #fff; }"
    "#rowbox { padding: 14px 16px; border: 1px solid #555; background: #080808; }"
    "#rowbox:hover { border-color: #999; }"
    "#row-name { color: #f4f4f4; font-family: monospace; font-size: 16px; }"
    "#row-sub { color: #d0d0d0; font-family: monospace; font-size: 13px; }";

static const char *CSS_EXTRA =
    "*:selected { background-color: #242424; color: #ddd; }"
    "selection { background-color: #303030; color: #eee; }"
    "#statusbar { background: #000; border-top: 1px solid #777; padding: 12px 20px; }"
    "#statusbar label { font-family: monospace; font-size: 15px; color: #e0e0e0; }"
    "#status-path { color: #f0f0f0; }"
    "#context-menu { background: #050505; border: 1px solid #888; padding: 6px; }"
    "#context-menu button { min-width: 210px; padding: 8px 12px; background: transparent; color: #eee; border: 0; font-family: monospace; font-size: 14px; }"
    "#context-menu button:hover { background: #242424; color: #fff; }"
    "#context-menu button:disabled { color: #4f4f4f; }"
    "#context-separator { min-height: 1px; background: #555; margin: 5px 2px; }";

static void app_render(App *app);
static void app_open_path(App *app, const char *path, bool new_tab);
static void app_stop_video(App *app);
static void tab_navigate(FolderTab *tab, const char *path, bool record_history);
static void show_context_menu(App *app, GtkWidget *relative_to, FileItem *target, GdkEventButton *event);
static void refresh_current(App *app);

static int grid_thumb_size(App *app) {
    return (int)(THUMB_SIZE * app->zoom + 0.5);
}

static int grid_name_chars(App *app) {
    return MAX(14, (int)(24 * app->zoom + 0.5));
}

static void file_item_free(gpointer data) {
    FileItem *item = data;
    if (!item) {
        return;
    }
    g_free(item->path);
    g_free(item->name);
    g_free(item->content_type);
    g_free(item);
}

static GPtrArray *item_array_new(void) {
    return g_ptr_array_new_with_free_func(file_item_free);
}

static bool content_type_starts(const char *type, const char *prefix) {
    return type && g_str_has_prefix(type, prefix);
}

static char *thumb_key_for_item(const FileItem *item) {
    return g_strdup_printf("%s|%" G_GINT64_FORMAT "|%ld", item->path, (gint64)item->size, (long)item->mtime);
}

static void thumb_entry_free(gpointer data) {
    ThumbEntry *entry = data;
    if (!entry) {
        return;
    }
    g_free(entry->key);
    g_clear_object(&entry->pixbuf);
    g_free(entry);
}

static void thumb_cache_init(ThumbCache *cache) {
    cache->entries = g_ptr_array_new_with_free_func(thumb_entry_free);
    cache->map = g_hash_table_new(g_str_hash, g_str_equal);
    cache->tick = 0;
}

static void pixbuf_data_free(guchar *pixels, gpointer data) {
    (void)data;
    g_free(pixels);
}

static GdkPixbuf *make_ffmpegthumbnailer_thumbnail(const char *path, guint percent) {
    if (!g_find_program_in_path("ffmpegthumbnailer")) {
        return NULL;
    }

    char *tmp = NULL;
    int fd = g_file_open_tmp("filemanager-thumb-XXXXXX.jpg", &tmp, NULL);
    if (fd < 0) {
        return NULL;
    }
    close(fd);

    char *time_arg = g_strdup_printf("%u", percent);
    char *size_arg = g_strdup_printf("%d", THUMB_SIZE);
    char *argv[] = {
        "ffmpegthumbnailer",
        "-i", (char *)path,
        "-o", tmp,
        "-s", size_arg,
        "-t", time_arg,
        "-q", "8",
        NULL
    };
    GError *error = NULL;
    gboolean ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, NULL, &error);
    g_clear_error(&error);

    GdkPixbuf *pixbuf = NULL;
    if (ok) {
        pixbuf = gdk_pixbuf_new_from_file_at_scale(tmp, THUMB_SIZE, THUMB_SIZE, TRUE, NULL);
    }
    g_unlink(tmp);
    g_free(tmp);
    g_free(time_arg);
    g_free(size_arg);
    return pixbuf;
}

static GdkPixbuf *make_video_thumbnail(const char *path) {
    GdkPixbuf *fallback = make_ffmpegthumbnailer_thumbnail(path, 10);
    if (fallback) {
        return fallback;
    }

    char *uri = g_filename_to_uri(path, NULL, NULL);
    if (!uri) {
        return NULL;
    }

    char *pipeline_desc = g_strdup_printf(
        "uridecodebin uri=\"%s\" ! queue ! videoconvert ! videoscale ! "
        "video/x-raw,format=RGB,pixel-aspect-ratio=1/1 ! "
        "appsink name=sink sync=false max-buffers=1 drop=true",
        uri
    );
    g_free(uri);

    GError *error = NULL;
    GstElement *pipeline = gst_parse_launch(pipeline_desc, &error);
    g_free(pipeline_desc);
    if (!pipeline) {
        g_clear_error(&error);
        return NULL;
    }
    g_clear_error(&error);

    GstElement *sink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if (!sink) {
        gst_object_unref(pipeline);
        return NULL;
    }

    GdkPixbuf *thumb = NULL;
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 5 * GST_SECOND);
    if (sample) {
        GstCaps *sample_caps = gst_sample_get_caps(sample);
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstVideoInfo info;
        GstMapInfo map;
        if (sample_caps && buffer && gst_video_info_from_caps(&info, sample_caps) && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            int width = GST_VIDEO_INFO_WIDTH(&info);
            int height = GST_VIDEO_INFO_HEIGHT(&info);
            int stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
            guchar *copy = g_malloc(map.size);
            memcpy(copy, map.data, map.size);
            GdkPixbuf *frame = gdk_pixbuf_new_from_data(
                copy,
                GDK_COLORSPACE_RGB,
                FALSE,
                8,
                width,
                height,
                stride,
                pixbuf_data_free,
                NULL
            );
            if (frame) {
                thumb = gdk_pixbuf_scale_simple(frame, THUMB_SIZE, THUMB_SIZE, GDK_INTERP_BILINEAR);
                g_object_unref(frame);
            } else {
                g_free(copy);
            }
            gst_buffer_unmap(buffer, &map);
        }
        gst_sample_unref(sample);
    } else {
        GstSample *preroll = gst_app_sink_try_pull_preroll(GST_APP_SINK(sink), GST_SECOND);
        if (preroll) {
            GstCaps *sample_caps = gst_sample_get_caps(preroll);
            GstBuffer *buffer = gst_sample_get_buffer(preroll);
            GstVideoInfo info;
            GstMapInfo map;
            if (sample_caps && buffer && gst_video_info_from_caps(&info, sample_caps) && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
                int width = GST_VIDEO_INFO_WIDTH(&info);
                int height = GST_VIDEO_INFO_HEIGHT(&info);
                int stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
                guchar *copy = g_malloc(map.size);
                memcpy(copy, map.data, map.size);
                GdkPixbuf *frame = gdk_pixbuf_new_from_data(copy, GDK_COLORSPACE_RGB, FALSE, 8, width, height, stride, pixbuf_data_free, NULL);
                if (frame) {
                    thumb = gdk_pixbuf_scale_simple(frame, THUMB_SIZE, THUMB_SIZE, GDK_INTERP_BILINEAR);
                    g_object_unref(frame);
                } else {
                    g_free(copy);
                }
                gst_buffer_unmap(buffer, &map);
            }
            gst_sample_unref(preroll);
        }
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(sink);
    gst_object_unref(pipeline);
    return thumb;
}

static void thumb_cache_trim(ThumbCache *cache) {
    while (cache->entries->len > CACHE_LIMIT) {
        guint oldest = 0;
        guint64 tick = G_MAXUINT64;
        for (guint i = 0; i < cache->entries->len; i++) {
            ThumbEntry *entry = g_ptr_array_index(cache->entries, i);
            if (entry->tick < tick) {
                tick = entry->tick;
                oldest = i;
            }
        }
        ThumbEntry *entry = g_ptr_array_index(cache->entries, oldest);
        g_hash_table_remove(cache->map, entry->key);
        g_ptr_array_remove_index(cache->entries, oldest);
    }
}

static const char *path_ext(const char *path) {
    const char *base = strrchr(path, G_DIR_SEPARATOR);
    const char *name = base ? base + 1 : path;
    return strrchr(name, '.');
}

static bool path_has_suffix_ci(const char *path, const char *suffix) {
    gsize path_len = strlen(path);
    gsize suffix_len = strlen(suffix);
    if (suffix_len > path_len) {
        return false;
    }
    return g_ascii_strcasecmp(path + path_len - suffix_len, suffix) == 0;
}

static void cairo_set_hex(cairo_t *cr, guint32 rgb) {
    cairo_set_source_rgb(
        cr,
        ((rgb >> 16) & 0xff) / 255.0,
        ((rgb >> 8) & 0xff) / 255.0,
        (rgb & 0xff) / 255.0
    );
}

static gboolean sidebar_icon_draw(GtkWidget *widget, cairo_t *cr, gpointer data) {
    SidebarIconKind kind = GPOINTER_TO_INT(data);
    GtkStyleContext *context = gtk_widget_get_style_context(widget);
    GdkRGBA color;
    gtk_style_context_get_color(context, gtk_style_context_get_state(context), &color);
    cairo_set_source_rgba(cr, color.red, color.green, color.blue, color.alpha);
    cairo_set_line_width(cr, 1.8);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
    cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);

    switch (kind) {
    case SIDEBAR_ICON_HOME:
        cairo_move_to(cr, 4, 12);
        cairo_line_to(cr, 12, 5);
        cairo_line_to(cr, 20, 12);
        cairo_move_to(cr, 7, 11);
        cairo_line_to(cr, 7, 20);
        cairo_line_to(cr, 17, 20);
        cairo_line_to(cr, 17, 11);
        cairo_stroke(cr);
        break;
    case SIDEBAR_ICON_DESKTOP:
        cairo_rectangle(cr, 4, 5, 16, 11);
        cairo_move_to(cr, 9, 20);
        cairo_line_to(cr, 15, 20);
        cairo_move_to(cr, 12, 16);
        cairo_line_to(cr, 12, 20);
        cairo_stroke(cr);
        break;
    case SIDEBAR_ICON_DOCUMENTS:
        cairo_move_to(cr, 6, 4);
        cairo_line_to(cr, 15, 4);
        cairo_line_to(cr, 19, 8);
        cairo_line_to(cr, 19, 20);
        cairo_line_to(cr, 6, 20);
        cairo_close_path(cr);
        cairo_move_to(cr, 15, 4);
        cairo_line_to(cr, 15, 8);
        cairo_line_to(cr, 19, 8);
        cairo_move_to(cr, 9, 12);
        cairo_line_to(cr, 16, 12);
        cairo_move_to(cr, 9, 16);
        cairo_line_to(cr, 16, 16);
        cairo_stroke(cr);
        break;
    case SIDEBAR_ICON_DOWNLOADS:
        cairo_move_to(cr, 12, 4);
        cairo_line_to(cr, 12, 15);
        cairo_move_to(cr, 8, 11);
        cairo_line_to(cr, 12, 15);
        cairo_line_to(cr, 16, 11);
        cairo_move_to(cr, 6, 20);
        cairo_line_to(cr, 18, 20);
        cairo_stroke(cr);
        break;
    case SIDEBAR_ICON_MUSIC:
        cairo_move_to(cr, 10, 6);
        cairo_line_to(cr, 18, 4);
        cairo_line_to(cr, 18, 15);
        cairo_move_to(cr, 10, 6);
        cairo_line_to(cr, 10, 17);
        cairo_arc(cr, 8, 18, 2.2, 0, 2 * G_PI);
        cairo_move_to(cr, 18, 15);
        cairo_arc(cr, 16, 16, 2.2, 0, 2 * G_PI);
        cairo_stroke(cr);
        break;
    case SIDEBAR_ICON_PICTURES:
        cairo_rectangle(cr, 4, 5, 16, 14);
        cairo_move_to(cr, 6, 17);
        cairo_line_to(cr, 10, 12);
        cairo_line_to(cr, 13, 15);
        cairo_line_to(cr, 16, 10);
        cairo_line_to(cr, 20, 17);
        cairo_arc(cr, 15.5, 9, 1.6, 0, 2 * G_PI);
        cairo_stroke(cr);
        break;
    case SIDEBAR_ICON_VIDEOS:
        cairo_rectangle(cr, 5, 5, 14, 14);
        for (int y = 8; y <= 16; y += 4) {
            cairo_move_to(cr, 5, y);
            cairo_line_to(cr, 8, y);
            cairo_move_to(cr, 16, y);
            cairo_line_to(cr, 19, y);
        }
        cairo_move_to(cr, 11, 10);
        cairo_line_to(cr, 11, 14);
        cairo_line_to(cr, 14, 12);
        cairo_close_path(cr);
        cairo_stroke(cr);
        break;
    case SIDEBAR_ICON_PROJECTS:
        cairo_move_to(cr, 4, 8);
        cairo_line_to(cr, 10, 8);
        cairo_line_to(cr, 12, 10);
        cairo_line_to(cr, 20, 10);
        cairo_line_to(cr, 20, 19);
        cairo_line_to(cr, 4, 19);
        cairo_close_path(cr);
        cairo_stroke(cr);
        break;
    case SIDEBAR_ICON_ROOT:
        cairo_rectangle(cr, 6, 4, 12, 16);
        cairo_move_to(cr, 9, 8);
        cairo_line_to(cr, 15, 8);
        cairo_move_to(cr, 9, 12);
        cairo_line_to(cr, 15, 12);
        cairo_move_to(cr, 9, 16);
        cairo_line_to(cr, 15, 16);
        cairo_stroke(cr);
        break;
    case SIDEBAR_ICON_DRIVE:
        cairo_move_to(cr, 5, 8);
        cairo_line_to(cr, 19, 8);
        cairo_line_to(cr, 17, 18);
        cairo_line_to(cr, 7, 18);
        cairo_close_path(cr);
        cairo_move_to(cr, 8, 15);
        cairo_line_to(cr, 16, 15);
        cairo_stroke(cr);
        break;
    }
    return FALSE;
}

static GtkWidget *make_sidebar_icon(SidebarIconKind kind) {
    GtkWidget *icon = gtk_drawing_area_new();
    gtk_widget_set_size_request(icon, 24, 24);
    g_signal_connect(icon, "draw", G_CALLBACK(sidebar_icon_draw), GINT_TO_POINTER(kind));
    return icon;
}

static char *file_arg_to_path(const char *arg) {
    if (g_str_has_prefix(arg, "file://")) {
        GFile *file = g_file_new_for_uri(arg);
        char *path = g_file_get_path(file);
        g_object_unref(file);
        return path ? path : g_strdup(g_get_home_dir());
    }
    return g_strdup(arg);
}

static char *self_binary_path(const char *argv0) {
    if (g_path_is_absolute(argv0)) {
        return g_strdup(argv0);
    }
    char *found = g_find_program_in_path(argv0);
    if (found) {
        return found;
    }
    char *cwd = g_get_current_dir();
    char *path = g_build_filename(cwd, argv0, NULL);
    g_free(cwd);
    return path;
}

static int handle_integration_cli(int argc, char **argv) {
    if (argc < 2 || g_strcmp0(argv[1], "--set-default") != 0) {
        return -1;
    }
    char *binary = argc > 2 ? file_arg_to_path(argv[2]) : self_binary_path(argv[0]);
    GError *error = NULL;
    if (!fm_install_user_default_file_manager(binary, &error)) {
        g_printerr("Could not set default file manager: %s\n", error ? error->message : "unknown error");
        g_clear_error(&error);
        g_free(binary);
        return 1;
    }
    g_print("Registered filemanager as the default handler for folders.\n");
    g_print("Browser upload dialogs use the desktop file chooser portal/toolkit and cannot be replaced by an inode/directory handler alone.\n");
    g_free(binary);
    return 0;
}

static void draw_doc_shape(cairo_t *cr, guint32 color) {
    cairo_set_hex(cr, color);
    cairo_move_to(cr, 38, 18);
    cairo_line_to(cr, 88, 18);
    cairo_line_to(cr, 110, 40);
    cairo_line_to(cr, 110, 110);
    cairo_line_to(cr, 38, 110);
    cairo_close_path(cr);
    cairo_fill(cr);

    cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
    cairo_move_to(cr, 88, 18);
    cairo_line_to(cr, 110, 40);
    cairo_line_to(cr, 88, 40);
    cairo_close_path(cr);
    cairo_fill(cr);
}

static void draw_center_text(cairo_t *cr, const char *text, double y, double size) {
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, size);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, text, &ext);
    cairo_move_to(cr, 64 - ext.width / 2.0 - ext.x_bearing, y);
    cairo_show_text(cr, text);
}

static GdkPixbuf *make_type_icon(const FileItem *item) {
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, THUMB_SIZE, THUMB_SIZE);
    cairo_t *cr = cairo_create(surface);
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    const char *ext = path_ext(item->path);
    const char *type = item->content_type;

    if (item->kind == ITEM_DIR) {
        cairo_set_hex(cr, 0xf2a900);
        cairo_move_to(cr, 12, 42);
        cairo_line_to(cr, 12, 32);
        cairo_line_to(cr, 52, 32);
        cairo_line_to(cr, 62, 44);
        cairo_line_to(cr, 116, 44);
        cairo_line_to(cr, 116, 102);
        cairo_line_to(cr, 12, 102);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_set_hex(cr, 0xffc21c);
        cairo_rectangle(cr, 12, 46, 104, 56);
        cairo_fill(cr);
    } else if (content_type_starts(type, "image/")) {
        cairo_set_hex(cr, 0x2fa545);
        cairo_set_line_width(cr, 8);
        cairo_rectangle(cr, 24, 28, 80, 72);
        cairo_stroke(cr);
        cairo_set_hex(cr, 0x38b449);
        cairo_move_to(cr, 30, 94);
        cairo_line_to(cr, 55, 62);
        cairo_line_to(cr, 70, 78);
        cairo_line_to(cr, 88, 52);
        cairo_line_to(cr, 102, 94);
        cairo_close_path(cr);
        cairo_fill(cr);
        cairo_arc(cr, 84, 48, 9, 0, 2 * G_PI);
        cairo_fill(cr);
    } else if (content_type_starts(type, "video/")) {
        cairo_set_hex(cr, 0x6a40b8);
        cairo_rectangle(cr, 24, 30, 80, 68);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_move_to(cr, 58, 50);
        cairo_line_to(cr, 58, 78);
        cairo_line_to(cr, 82, 64);
        cairo_close_path(cr);
        cairo_fill(cr);
        for (int i = 0; i < 4; i++) {
            cairo_rectangle(cr, 30, 38 + i * 14, 8, 8);
            cairo_rectangle(cr, 90, 38 + i * 14, 8, 8);
        }
        cairo_fill(cr);
    } else if (content_type_starts(type, "audio/")) {
        draw_doc_shape(cr, 0xe9313a);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 12);
        cairo_move_to(cr, 68, 76);
        cairo_line_to(cr, 68, 42);
        cairo_line_to(cr, 92, 36);
        cairo_line_to(cr, 92, 70);
        cairo_stroke(cr);
        cairo_arc(cr, 58, 82, 12, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_arc(cr, 84, 78, 12, 0, 2 * G_PI);
        cairo_fill(cr);
    } else if (ext && g_ascii_strcasecmp(ext, ".pdf") == 0) {
        draw_doc_shape(cr, 0xd92929);
        cairo_set_source_rgb(cr, 1, 1, 1);
        draw_center_text(cr, "PDF", 78, 23);
    } else if ((ext && (g_ascii_strcasecmp(ext, ".doc") == 0 || g_ascii_strcasecmp(ext, ".docx") == 0 || g_ascii_strcasecmp(ext, ".odt") == 0)) || content_type_starts(type, "application/vnd.openxmlformats-officedocument.wordprocessingml")) {
        draw_doc_shape(cr, 0x1e63d6);
        cairo_set_source_rgb(cr, 1, 1, 1);
        for (int i = 0; i < 4; i++) {
            cairo_rectangle(cr, 52, 48 + i * 12, 40 - i * 4, 4);
            cairo_fill(cr);
        }
    } else if (ext && (g_ascii_strcasecmp(ext, ".xls") == 0 || g_ascii_strcasecmp(ext, ".xlsx") == 0 || g_ascii_strcasecmp(ext, ".ods") == 0 || g_ascii_strcasecmp(ext, ".csv") == 0)) {
        draw_doc_shape(cr, 0x2da545);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 4);
        cairo_rectangle(cr, 48, 48, 44, 38);
        for (int i = 1; i < 3; i++) {
            cairo_move_to(cr, 48 + i * 15, 48);
            cairo_line_to(cr, 48 + i * 15, 86);
        }
        for (int i = 1; i < 3; i++) {
            cairo_move_to(cr, 48, 48 + i * 13);
            cairo_line_to(cr, 92, 48 + i * 13);
        }
        cairo_stroke(cr);
    } else if (ext && (g_ascii_strcasecmp(ext, ".ppt") == 0 || g_ascii_strcasecmp(ext, ".pptx") == 0 || g_ascii_strcasecmp(ext, ".odp") == 0)) {
        draw_doc_shape(cr, 0xf57c00);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_arc(cr, 66, 62, 18, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_set_hex(cr, 0xf57c00);
        cairo_move_to(cr, 66, 62);
        cairo_line_to(cr, 66, 42);
        cairo_line_to(cr, 86, 62);
        cairo_close_path(cr);
        cairo_fill(cr);
    } else if (ext && (g_ascii_strcasecmp(ext, ".zip") == 0 || g_ascii_strcasecmp(ext, ".tar") == 0 || g_ascii_strcasecmp(ext, ".gz") == 0 || g_ascii_strcasecmp(ext, ".xz") == 0 || g_ascii_strcasecmp(ext, ".7z") == 0 || g_ascii_strcasecmp(ext, ".rar") == 0)) {
        draw_doc_shape(cr, 0x8a5a2f);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 5);
        cairo_move_to(cr, 64, 32);
        cairo_line_to(cr, 64, 86);
        cairo_stroke(cr);
        cairo_rectangle(cr, 58, 38, 12, 7);
        cairo_rectangle(cr, 58, 52, 12, 7);
        cairo_rectangle(cr, 58, 66, 12, 7);
        cairo_fill(cr);
        cairo_arc(cr, 64, 92, 10, 0, 2 * G_PI);
        cairo_stroke(cr);
    } else if (ext && (g_ascii_strcasecmp(ext, ".html") == 0 || g_ascii_strcasecmp(ext, ".htm") == 0)) {
        draw_doc_shape(cr, 0x00a99d);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 4);
        cairo_arc(cr, 68, 68, 24, 0, 2 * G_PI);
        cairo_move_to(cr, 44, 68);
        cairo_line_to(cr, 92, 68);
        cairo_move_to(cr, 68, 44);
        cairo_line_to(cr, 68, 92);
        cairo_move_to(cr, 52, 52);
        cairo_curve_to(cr, 62, 60, 62, 76, 52, 84);
        cairo_move_to(cr, 84, 52);
        cairo_curve_to(cr, 74, 60, 74, 76, 84, 84);
        cairo_stroke(cr);
    } else if (ext && (g_ascii_strcasecmp(ext, ".json") == 0)) {
        draw_doc_shape(cr, 0xf3b300);
        cairo_set_source_rgb(cr, 1, 1, 1);
        draw_center_text(cr, "{ }", 80, 30);
    } else if (ext && (g_ascii_strcasecmp(ext, ".c") == 0 || g_ascii_strcasecmp(ext, ".h") == 0 || g_ascii_strcasecmp(ext, ".cpp") == 0 || g_ascii_strcasecmp(ext, ".py") == 0 || g_ascii_strcasecmp(ext, ".js") == 0 || g_ascii_strcasecmp(ext, ".ts") == 0 || g_ascii_strcasecmp(ext, ".css") == 0 || g_ascii_strcasecmp(ext, ".sh") == 0)) {
        draw_doc_shape(cr, 0x6a40b8);
        cairo_set_source_rgb(cr, 1, 1, 1);
        draw_center_text(cr, "</>", 78, 27);
    } else if (ext && (g_ascii_strcasecmp(ext, ".ttf") == 0 || g_ascii_strcasecmp(ext, ".otf") == 0 || g_ascii_strcasecmp(ext, ".woff") == 0 || g_ascii_strcasecmp(ext, ".woff2") == 0)) {
        draw_doc_shape(cr, 0xec407a);
        cairo_set_source_rgb(cr, 1, 1, 1);
        draw_center_text(cr, "A", 86, 52);
    } else if (ext && (g_ascii_strcasecmp(ext, ".desktop") == 0 || g_ascii_strcasecmp(ext, ".appimage") == 0 || g_ascii_strcasecmp(ext, ".exe") == 0)) {
        draw_doc_shape(cr, 0x1662d4);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_set_line_width(cr, 5);
        cairo_rectangle(cr, 42, 44, 54, 42);
        cairo_move_to(cr, 42, 56);
        cairo_line_to(cr, 96, 56);
        cairo_stroke(cr);
        cairo_arc(cr, 50, 50, 2.5, 0, 2 * G_PI);
        cairo_arc(cr, 60, 50, 2.5, 0, 2 * G_PI);
        cairo_arc(cr, 70, 50, 2.5, 0, 2 * G_PI);
        cairo_fill(cr);
    } else if (ext && (g_ascii_strcasecmp(ext, ".txt") == 0 || g_ascii_strcasecmp(ext, ".md") == 0 || g_ascii_strcasecmp(ext, ".log") == 0)) {
        draw_doc_shape(cr, 0x00a99d);
        cairo_set_source_rgb(cr, 1, 1, 1);
        for (int i = 0; i < 4; i++) {
            cairo_rectangle(cr, 50, 50 + i * 12, 42 - i * 5, 4);
            cairo_fill(cr);
        }
    } else if (ext && (g_ascii_strcasecmp(ext, ".conf") == 0 || g_ascii_strcasecmp(ext, ".ini") == 0 || g_ascii_strcasecmp(ext, ".service") == 0)) {
        cairo_set_hex(cr, 0x6f7b86);
        cairo_arc(cr, 64, 64, 30, 0, 2 * G_PI);
        cairo_set_line_width(cr, 18);
        cairo_stroke(cr);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_arc(cr, 64, 64, 13, 0, 2 * G_PI);
        cairo_fill(cr);
    } else {
        draw_doc_shape(cr, 0x2b9adf);
        cairo_set_source_rgb(cr, 1, 1, 1);
        draw_center_text(cr, "FILE", 76, 20);
    }

    cairo_destroy(cr);
    GdkPixbuf *pixbuf = gdk_pixbuf_get_from_surface(surface, 0, 0, THUMB_SIZE, THUMB_SIZE);
    cairo_surface_destroy(surface);
    return pixbuf;
}

static GdkPixbuf *thumb_cache_get(App *app, const FileItem *item) {
    char *key = thumb_key_for_item(item);
    ThumbEntry *entry = g_hash_table_lookup(app->thumbs.map, key);
    if (entry) {
        entry->tick = ++app->thumbs.tick;
        g_free(key);
        return g_object_ref(entry->pixbuf);
    }

    GError *error = NULL;
    GdkPixbuf *pixbuf = NULL;
    if (item->is_image) {
        pixbuf = gdk_pixbuf_new_from_file_at_scale(item->path, THUMB_SIZE * 2, THUMB_SIZE * 2, TRUE, &error);
        if (!pixbuf) {
            pixbuf = make_type_icon(item);
        }
    } else if (item->is_video) {
        pixbuf = make_video_thumbnail(item->path);
        if (!pixbuf) {
            pixbuf = make_type_icon(item);
        }
    } else if (item->kind == ITEM_DIR) {
        pixbuf = make_type_icon(item);
    } else {
        pixbuf = make_type_icon(item);
    }
    g_clear_error(&error);
    if (!pixbuf) {
        g_free(key);
        return NULL;
    }

    entry = g_new0(ThumbEntry, 1);
    entry->key = key;
    entry->pixbuf = g_object_ref(pixbuf);
    entry->tick = ++app->thumbs.tick;
    g_hash_table_insert(app->thumbs.map, entry->key, entry);
    g_ptr_array_add(app->thumbs.entries, entry);
    thumb_cache_trim(&app->thumbs);
    return pixbuf;
}

static GdkPixbuf *pixbuf_with_type_badge(const FileItem *item, GdkPixbuf *base, int size) {
    if (!base || (!item->is_image && !item->is_video)) {
        return base ? g_object_ref(base) : NULL;
    }

    GdkPixbuf *out = gdk_pixbuf_copy(base);
    if (!out) {
        return g_object_ref(base);
    }

    GdkPixbuf *icon = make_type_icon(item);
    if (!icon) {
        return out;
    }
    int badge_size = MAX(24, size / 3);
    GdkPixbuf *badge = gdk_pixbuf_scale_simple(icon, badge_size, badge_size, GDK_INTERP_BILINEAR);
    g_object_unref(icon);
    if (!badge) {
        return out;
    }

    int pad = MAX(3, size / 34);
    int bx = MAX(0, gdk_pixbuf_get_width(out) - badge_size - pad);
    int by = MAX(0, gdk_pixbuf_get_height(out) - badge_size - pad);
    gdk_pixbuf_composite(
        badge,
        out,
        bx,
        by,
        badge_size,
        badge_size,
        bx,
        by,
        1.0,
        1.0,
        GDK_INTERP_BILINEAR,
        230
    );
    g_object_unref(badge);
    return out;
}

static gint compare_items(gconstpointer a, gconstpointer b) {
    const FileItem *ia = *(FileItem * const *)a;
    const FileItem *ib = *(FileItem * const *)b;
    if (ia->kind != ib->kind) {
        return ia->kind == ITEM_DIR ? -1 : 1;
    }
    return g_ascii_strcasecmp(ia->name, ib->name);
}

static char *format_size(goffset size) {
    const char *units[] = {"B", "K", "M", "G", "T"};
    double value = (double)size;
    guint unit = 0;
    while (value >= 1024.0 && unit < G_N_ELEMENTS(units) - 1) {
        value /= 1024.0;
        unit++;
    }
    if (unit == 0) {
        return g_strdup_printf("%" G_GOFFSET_FORMAT " B", size);
    }
    return g_strdup_printf("%.1f%s", value, units[unit]);
}

static const char *file_type_label(const FileItem *item) {
    if (item->kind == ITEM_DIR) {
        return "Folder";
    }
    const char *ext = path_ext(item->path);
    if (item->is_image) return "Image";
    if (item->is_video) return "Video";
    if (content_type_starts(item->content_type, "audio/")) return "Audio";
    if (ext && g_ascii_strcasecmp(ext, ".desktop") == 0) return "Desktop Entry";
    if (ext && g_ascii_strcasecmp(ext, ".pdf") == 0) return "PDF";
    if (ext && (g_ascii_strcasecmp(ext, ".zip") == 0 || g_ascii_strcasecmp(ext, ".7z") == 0 || g_ascii_strcasecmp(ext, ".tar") == 0 || g_ascii_strcasecmp(ext, ".gz") == 0)) return "Archive";
    if (ext && (g_ascii_strcasecmp(ext, ".html") == 0 || g_ascii_strcasecmp(ext, ".htm") == 0)) return "HTML";
    if (ext && g_ascii_strcasecmp(ext, ".json") == 0) return "JSON";
    if (ext && (g_ascii_strcasecmp(ext, ".txt") == 0 || g_ascii_strcasecmp(ext, ".md") == 0 || g_ascii_strcasecmp(ext, ".log") == 0)) return "Text";
    if (ext && (g_ascii_strcasecmp(ext, ".c") == 0 || g_ascii_strcasecmp(ext, ".h") == 0 || g_ascii_strcasecmp(ext, ".cpp") == 0 || g_ascii_strcasecmp(ext, ".py") == 0 || g_ascii_strcasecmp(ext, ".js") == 0 || g_ascii_strcasecmp(ext, ".ts") == 0 || g_ascii_strcasecmp(ext, ".sh") == 0)) return "Code";
    return "File";
}

static char *format_mtime(time_t mtime) {
    GDateTime *dt = g_date_time_new_from_unix_local((gint64)mtime);
    if (!dt) {
        return g_strdup("-");
    }
    char *out = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    g_date_time_unref(dt);
    return out ? out : g_strdup("-");
}

static char *shorten_middle(const char *text, guint max_chars) {
    if (g_utf8_strlen(text, -1) <= max_chars) {
        return g_strdup(text);
    }
    char *head = g_utf8_substring(text, 0, (glong)(max_chars / 2));
    char *tail = g_utf8_substring(text, (glong)(g_utf8_strlen(text, -1) - max_chars / 2 + 1), -1);
    char *out = g_strdup_printf("%s...%s", head, tail);
    g_free(head);
    g_free(tail);
    return out;
}

static void apply_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);

    provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, CSS_EXTRA, -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
    );
    g_object_unref(provider);
}

static char *user_dir_value(const char *key, const char *fallback) {
    char *config = g_build_filename(g_get_user_config_dir(), "user-dirs.dirs", NULL);
    char *contents = NULL;
    gsize len = 0;
    if (!g_file_get_contents(config, &contents, &len, NULL)) {
        g_free(config);
        return g_strdup(fallback);
    }
    g_free(config);

    char *needle = g_strdup_printf("%s=\"", key);
    char *start = strstr(contents, needle);
    g_free(needle);
    if (!start) {
        g_free(contents);
        return g_strdup(fallback);
    }
    start = strchr(start, '"');
    char *end = start ? strchr(start + 1, '"') : NULL;
    if (!start || !end) {
        g_free(contents);
        return g_strdup(fallback);
    }
    char *raw = g_strndup(start + 1, (gsize)(end - start - 1));
    char *out = NULL;
    if (g_str_has_prefix(raw, "$HOME/")) {
        out = g_build_filename(g_get_home_dir(), raw + 6, NULL);
    } else if (g_strcmp0(raw, "$HOME") == 0) {
        out = g_strdup(g_get_home_dir());
    } else {
        out = g_strdup(raw);
    }
    g_free(raw);
    g_free(contents);
    return out;
}

static GtkWidget *make_label(const char *text, const char *name, float xalign) {
    GtkWidget *label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), xalign);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_MIDDLE);
    if (name) {
        gtk_widget_set_name(label, name);
    }
    return label;
}

static void update_status(App *app) {
    FolderTab *tab = app->active_tab;
    if (!tab) {
        return;
    }
    guint selected = app->mode == VIEW_GRID
        ? g_list_length(gtk_flow_box_get_selected_children(GTK_FLOW_BOX(app->grid)))
        : g_list_length(gtk_list_box_get_selected_rows(GTK_LIST_BOX(app->list)));
    char *counts = g_strdup_printf("%u selected | %u items%s", selected, tab->items ? tab->items->len : 0, tab->loading ? " | scanning" : "");
    gtk_label_set_text(GTK_LABEL(app->status_path), tab->path);
    gtk_label_set_text(GTK_LABEL(app->status_counts), counts);
    gtk_entry_set_text(GTK_ENTRY(app->path_entry), tab->path);
    if (app->back_button) {
        gtk_widget_set_sensitive(app->back_button, tab->history && tab->history_pos > 0);
    }
    if (app->forward_button) {
        gtk_widget_set_sensitive(app->forward_button, tab->history && tab->history_pos + 1 < tab->history->len);
    }
    if (app->up_button) {
        char *parent = g_path_get_dirname(tab->path);
        gtk_widget_set_sensitive(app->up_button, g_strcmp0(parent, tab->path) != 0);
        g_free(parent);
    }
    g_free(counts);
}

static void open_external(const char *path) {
    GFile *file = g_file_new_for_path(path);
    char *uri = g_file_get_uri(file);
    GError *error = NULL;
    if (!g_app_info_launch_default_for_uri(uri, NULL, &error)) {
        g_warning("open failed: %s", error ? error->message : "unknown error");
    }
    g_clear_error(&error);
    g_free(uri);
    g_object_unref(file);
}

static void show_error(App *app, const char *title, const char *detail) {
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE,
        "%s",
        title
    );
    if (detail) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", detail);
    }
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static void show_info(App *app, const char *title, const char *detail) {
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_CLOSE,
        "%s",
        title
    );
    if (detail) {
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", detail);
    }
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

static char *linux_distro_name(void) {
    char *contents = NULL;
    if (!g_file_get_contents("/etc/os-release", &contents, NULL, NULL)) {
        return g_strdup("Linux");
    }
    char **lines = g_strsplit(contents, "\n", -1);
    char *out = g_strdup("Linux");
    for (guint i = 0; lines[i]; i++) {
        if (g_str_has_prefix(lines[i], "PRETTY_NAME=")) {
            char *value = g_strdup(lines[i] + strlen("PRETTY_NAME="));
            g_strstrip(value);
            if (value[0] == '"' && value[strlen(value) - 1] == '"') {
                value[strlen(value) - 1] = '\0';
                memmove(value, value + 1, strlen(value));
            }
            g_free(out);
            out = value;
            break;
        }
    }
    g_strfreev(lines);
    g_free(contents);
    return out;
}

static char *spawn_capture(char **argv) {
    char *stdout_data = NULL;
    GError *error = NULL;
    gboolean ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &stdout_data, NULL, NULL, &error);
    g_clear_error(&error);
    if (!ok || !stdout_data) {
        g_free(stdout_data);
        return NULL;
    }
    g_strstrip(stdout_data);
    return stdout_data;
}

static bool drive_mount_info(const char *path, char **out_source, char **out_fstype, char **out_uuid) {
    char *findmnt_argv[] = {"findmnt", "-n", "-o", "SOURCE,FSTYPE,UUID", "--target", (char *)path, NULL};
    char *info = spawn_capture(findmnt_argv);
    if (!info || info[0] == '\0') {
        g_free(info);
        return false;
    }
    char **parts = g_strsplit_set(info, " \t", 0);
    char *source = NULL;
    char *fstype = NULL;
    char *uuid = NULL;
    for (guint i = 0; parts[i]; i++) {
        if (parts[i][0] == '\0') {
            continue;
        }
        if (!source) {
            source = g_strdup(parts[i]);
        } else if (!fstype) {
            fstype = g_strdup(parts[i]);
        } else if (!uuid) {
            uuid = g_strdup(parts[i]);
            break;
        }
    }
    g_strfreev(parts);
    g_free(info);
    if (source) {
        char *subvol = strchr(source, '[');
        if (subvol) {
            *subvol = '\0';
        }
    }
    if (!source || !fstype || !g_str_has_prefix(source, "/dev/")) {
        g_free(source);
        g_free(fstype);
        g_free(uuid);
        return false;
    }

    if (!uuid || uuid[0] == '\0' || g_strcmp0(uuid, "-") == 0) {
        g_free(uuid);
        char *blkid_argv[] = {"blkid", "-s", "UUID", "-o", "value", source, NULL};
        uuid = spawn_capture(blkid_argv);
    }
    if (!uuid || uuid[0] == '\0') {
        g_free(source);
        g_free(fstype);
        g_free(uuid);
        return false;
    }

    *out_source = source;
    *out_fstype = fstype;
    *out_uuid = uuid;
    return true;
}

static bool fstab_has_uuid_or_path(const char *uuid, const char *path) {
    char *contents = NULL;
    if (!g_file_get_contents("/etc/fstab", &contents, NULL, NULL)) {
        return false;
    }
    char *uuid_pattern = g_strdup_printf("UUID=%s", uuid);
    bool found = strstr(contents, uuid_pattern) != NULL || strstr(contents, path) != NULL;
    g_free(uuid_pattern);
    g_free(contents);
    return found;
}

static void run_privileged_script(App *app, const char *script, const char *success_message) {
    char *argv[] = {"pkexec", "sh", "-c", (char *)script, NULL};
    GError *error = NULL;
    char *stderr_data = NULL;
    gint status = 0;
    gboolean ok = g_spawn_sync(
        NULL,
        argv,
        NULL,
        G_SPAWN_SEARCH_PATH,
        NULL,
        NULL,
        NULL,
        &stderr_data,
        &status,
        &error
    );
    if (!ok) {
        show_error(app, "Privilege action failed", error ? error->message : "Could not start pkexec");
    } else if (!g_spawn_check_wait_status(status, &error)) {
        show_error(app, "Privilege action failed", stderr_data && stderr_data[0] ? stderr_data : (error ? error->message : "pkexec failed"));
    } else {
        show_info(app, "Automount updated", success_message);
    }
    g_free(stderr_data);
    g_clear_error(&error);
}

static GPtrArray *new_string_array(void) {
    return g_ptr_array_new_with_free_func(g_free);
}

static void clipboard_set(App *app, GPtrArray *paths, ClipboardMode mode) {
    g_clear_pointer(&app->clipboard_paths, g_ptr_array_unref);
    app->clipboard_paths = new_string_array();
    for (guint i = 0; i < paths->len; i++) {
        g_ptr_array_add(app->clipboard_paths, g_strdup(g_ptr_array_index(paths, i)));
    }
    app->clipboard_mode = mode;
}

static bool current_selection_contains(App *app, FileItem *target) {
    if (!target) {
        return false;
    }
    if (app->mode == VIEW_GRID) {
        GList *selected = gtk_flow_box_get_selected_children(GTK_FLOW_BOX(app->grid));
        for (GList *l = selected; l; l = l->next) {
            FileItem *item = g_object_get_data(G_OBJECT(l->data), "item");
            if (item == target) {
                g_list_free(selected);
                return true;
            }
        }
        g_list_free(selected);
    } else {
        GList *selected = gtk_list_box_get_selected_rows(GTK_LIST_BOX(app->list));
        for (GList *l = selected; l; l = l->next) {
            FileItem *item = g_object_get_data(G_OBJECT(l->data), "item");
            if (item == target) {
                g_list_free(selected);
                return true;
            }
        }
        g_list_free(selected);
    }
    return false;
}

static GPtrArray *context_paths(App *app, FileItem *target) {
    GPtrArray *paths = new_string_array();
    bool use_selection = current_selection_contains(app, target);
    if (use_selection && app->mode == VIEW_GRID) {
        GList *selected = gtk_flow_box_get_selected_children(GTK_FLOW_BOX(app->grid));
        for (GList *l = selected; l; l = l->next) {
            FileItem *item = g_object_get_data(G_OBJECT(l->data), "item");
            if (item) {
                g_ptr_array_add(paths, g_strdup(item->path));
            }
        }
        g_list_free(selected);
    } else if (use_selection) {
        GList *selected = gtk_list_box_get_selected_rows(GTK_LIST_BOX(app->list));
        for (GList *l = selected; l; l = l->next) {
            FileItem *item = g_object_get_data(G_OBJECT(l->data), "item");
            if (item) {
                g_ptr_array_add(paths, g_strdup(item->path));
            }
        }
        g_list_free(selected);
    }
    if (paths->len == 0 && target) {
        g_ptr_array_add(paths, g_strdup(target->path));
    }
    return paths;
}

static char *unique_destination_path(const char *dir, const char *name) {
    char *dest = g_build_filename(dir, name, NULL);
    if (!g_file_test(dest, G_FILE_TEST_EXISTS)) {
        return dest;
    }
    const char *dot = strrchr(name, '.');
    char *stem = dot ? g_strndup(name, (gsize)(dot - name)) : g_strdup(name);
    const char *ext = dot ? dot : "";
    for (guint i = 1; i < 10000; i++) {
        g_free(dest);
        char *candidate = g_strdup_printf("%s copy %u%s", stem, i, ext);
        dest = g_build_filename(dir, candidate, NULL);
        g_free(candidate);
        if (!g_file_test(dest, G_FILE_TEST_EXISTS)) {
            break;
        }
    }
    g_free(stem);
    return dest;
}

static char *basename_without_known_suffix(const char *path) {
    char *base = g_path_get_basename(path);
    const char *suffixes[] = {
        ".AppImage", ".appimage", ".tar.gz", ".tgz", ".tar.xz", ".txz",
        ".tar.bz2", ".tbz2", ".tar.zst", ".zip", ".7z", ".rar", ".gz",
        NULL
    };
    for (guint i = 0; suffixes[i]; i++) {
        if (path_has_suffix_ci(base, suffixes[i])) {
            base[strlen(base) - strlen(suffixes[i])] = '\0';
            break;
        }
    }
    return base;
}

static char *safe_command_name(const char *name) {
    GString *out = g_string_new(NULL);
    for (const char *p = name; *p; p++) {
        if (g_ascii_isalnum(*p) || *p == '-' || *p == '_') {
            g_string_append_c(out, g_ascii_tolower(*p));
        } else if (*p == ' ' || *p == '.') {
            g_string_append_c(out, '-');
        }
    }
    if (out->len == 0) {
        g_string_append(out, "appimage-app");
    }
    return g_string_free(out, FALSE);
}

static bool is_appimage_path(const char *path) {
    return path_has_suffix_ci(path, ".appimage");
}

static bool is_archive_path(const char *path) {
    const char *suffixes[] = {
        ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tbz2",
        ".tar.zst", ".zip", ".7z", ".rar", NULL
    };
    for (guint i = 0; suffixes[i]; i++) {
        if (path_has_suffix_ci(path, suffixes[i])) {
            return true;
        }
    }
    return false;
}

static char *prompt_text(App *app, const char *title, const char *action, const char *initial) {
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        title,
        GTK_WINDOW(app->window),
        GTK_DIALOG_MODAL,
        "Cancel",
        GTK_RESPONSE_CANCEL,
        action,
        GTK_RESPONSE_ACCEPT,
        NULL
    );
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), initial);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), 10);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    char *out = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
        if (text[0] != '\0') {
            out = g_strdup(text);
        }
    }
    gtk_widget_destroy(dialog);
    return out;
}

static gboolean copy_path_recursive(GFile *src, GFile *dest, GError **error) {
    GFileType type = g_file_query_file_type(src, G_FILE_QUERY_INFO_NONE, NULL);
    if (type != G_FILE_TYPE_DIRECTORY) {
        return g_file_copy(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, error);
    }

    if (!g_file_make_directory(dest, NULL, error)) {
        return false;
    }

    GFileEnumerator *en = g_file_enumerate_children(src, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NONE, NULL, error);
    if (!en) {
        return false;
    }

    GFileInfo *info = NULL;
    while ((info = g_file_enumerator_next_file(en, NULL, error)) != NULL) {
        const char *name = g_file_info_get_name(info);
        GFile *child_src = g_file_get_child(src, name);
        GFile *child_dest = g_file_get_child(dest, name);
        gboolean ok = copy_path_recursive(child_src, child_dest, error);
        g_object_unref(child_src);
        g_object_unref(child_dest);
        g_object_unref(info);
        if (!ok) {
            g_object_unref(en);
            return false;
        }
    }
    g_object_unref(en);
    return error == NULL || *error == NULL;
}

static gboolean delete_path_recursive(GFile *file, GError **error) {
    GFileType type = g_file_query_file_type(file, G_FILE_QUERY_INFO_NONE, NULL);
    if (type == G_FILE_TYPE_DIRECTORY) {
        GFileEnumerator *en = g_file_enumerate_children(file, G_FILE_ATTRIBUTE_STANDARD_NAME, G_FILE_QUERY_INFO_NONE, NULL, error);
        if (!en) {
            return false;
        }
        GFileInfo *info = NULL;
        while ((info = g_file_enumerator_next_file(en, NULL, error)) != NULL) {
            const char *name = g_file_info_get_name(info);
            GFile *child = g_file_get_child(file, name);
            gboolean ok = delete_path_recursive(child, error);
            g_object_unref(child);
            g_object_unref(info);
            if (!ok) {
                g_object_unref(en);
                return false;
            }
        }
        g_object_unref(en);
    }
    return g_file_delete(file, NULL, error);
}

static void paste_clipboard(App *app) {
    if (!app->active_tab || !app->clipboard_paths || app->clipboard_paths->len == 0) {
        return;
    }
    for (guint i = 0; i < app->clipboard_paths->len; i++) {
        const char *src_path = g_ptr_array_index(app->clipboard_paths, i);
        char *base = g_path_get_basename(src_path);
        char *dest_path = unique_destination_path(app->active_tab->path, base);
        GFile *src = g_file_new_for_path(src_path);
        GFile *dest = g_file_new_for_path(dest_path);
        GError *error = NULL;
        gboolean ok = app->clipboard_mode == CLIPBOARD_CUT
            ? g_file_move(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &error)
            : copy_path_recursive(src, dest, &error);
        if (!ok) {
            show_error(app, "Paste failed", error ? error->message : "Unknown error");
        }
        g_clear_error(&error);
        g_object_unref(src);
        g_object_unref(dest);
        g_free(dest_path);
        g_free(base);
    }
    if (app->clipboard_mode == CLIPBOARD_CUT) {
        g_clear_pointer(&app->clipboard_paths, g_ptr_array_unref);
        app->clipboard_mode = CLIPBOARD_NONE;
    }
    refresh_current(app);
}

static void activate_item(App *app, FileItem *item, bool new_tab) {
    if (item->kind == ITEM_DIR) {
        app_open_path(app, item->path, new_tab);
    } else {
        open_external(item->path);
    }
}

static void context_data_free(gpointer data) {
    ContextData *ctx = data;
    if (!ctx) {
        return;
    }
    g_clear_pointer(&ctx->paths, g_ptr_array_unref);
    g_free(ctx->target_path);
    g_free(ctx);
}

static void context_data_destroy(gpointer data, GClosure *closure) {
    (void)closure;
    context_data_free(data);
}

static ContextData *context_data_new(App *app, FileItem *target) {
    ContextData *ctx = g_new0(ContextData, 1);
    ctx->app = app;
    ctx->paths = context_paths(app, target);
    if (target) {
        ctx->target_path = g_strdup(target->path);
        ctx->target_is_dir = target->kind == ITEM_DIR;
    }
    return ctx;
}

static void menu_open(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    if (ctx->target_path) {
        app_open_path(ctx->app, ctx->target_path, false);
    }
}

static void menu_open_tab(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    if (ctx->target_path && ctx->target_is_dir) {
        app_open_path(ctx->app, ctx->target_path, true);
    }
}

static void menu_copy(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    clipboard_set(ctx->app, ctx->paths, CLIPBOARD_COPY);
}

static void menu_cut(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    clipboard_set(ctx->app, ctx->paths, CLIPBOARD_CUT);
}

static void menu_paste(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    paste_clipboard(ctx->app);
}

static void menu_trash(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    for (guint i = 0; i < ctx->paths->len; i++) {
        GFile *file = g_file_new_for_path(g_ptr_array_index(ctx->paths, i));
        GError *error = NULL;
        if (!g_file_trash(file, NULL, &error)) {
            show_error(ctx->app, "Move to trash failed", error ? error->message : "Unknown error");
        }
        g_clear_error(&error);
        g_object_unref(file);
    }
    refresh_current(ctx->app);
}

static void menu_delete(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(ctx->app->window),
        GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_CANCEL,
        "Delete permanently?"
    );
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "This cannot be undone.");
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Delete", GTK_RESPONSE_ACCEPT);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_ACCEPT) {
        return;
    }
    for (guint i = 0; i < ctx->paths->len; i++) {
        GFile *file = g_file_new_for_path(g_ptr_array_index(ctx->paths, i));
        GError *error = NULL;
        if (!delete_path_recursive(file, &error)) {
            show_error(ctx->app, "Delete failed", error ? error->message : "Unknown error");
        }
        g_clear_error(&error);
        g_object_unref(file);
    }
    refresh_current(ctx->app);
}

static void menu_rename(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    if (!ctx->target_path || ctx->paths->len != 1) {
        return;
    }
    char *old_name = g_path_get_basename(ctx->target_path);
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "Rename",
        GTK_WINDOW(ctx->app->window),
        GTK_DIALOG_MODAL,
        "Cancel",
        GTK_RESPONSE_CANCEL,
        "Rename",
        GTK_RESPONSE_ACCEPT,
        NULL
    );
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), old_name);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), 10);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        const char *new_name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (new_name[0] != '\0' && strchr(new_name, G_DIR_SEPARATOR) == NULL) {
            char *dir = g_path_get_dirname(ctx->target_path);
            char *dest_path = g_build_filename(dir, new_name, NULL);
            GFile *src = g_file_new_for_path(ctx->target_path);
            GFile *dest = g_file_new_for_path(dest_path);
            GError *error = NULL;
            if (!g_file_move(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &error)) {
                show_error(ctx->app, "Rename failed", error ? error->message : "Unknown error");
            }
            g_clear_error(&error);
            g_object_unref(src);
            g_object_unref(dest);
            g_free(dest_path);
            g_free(dir);
            refresh_current(ctx->app);
        }
    }
    gtk_widget_destroy(dialog);
    g_free(old_name);
}

static void menu_new_folder(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    if (!ctx->app->active_tab) {
        return;
    }
    GtkWidget *dialog = gtk_dialog_new_with_buttons(
        "New Folder",
        GTK_WINDOW(ctx->app->window),
        GTK_DIALOG_MODAL,
        "Cancel",
        GTK_RESPONSE_CANCEL,
        "Create",
        GTK_RESPONSE_ACCEPT,
        NULL
    );
    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), "New Folder");
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    gtk_container_set_border_width(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), 10);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), entry, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        const char *name = gtk_entry_get_text(GTK_ENTRY(entry));
        if (name[0] != '\0' && strchr(name, G_DIR_SEPARATOR) == NULL) {
            char *path = unique_destination_path(ctx->app->active_tab->path, name);
            GFile *file = g_file_new_for_path(path);
            GError *error = NULL;
            if (!g_file_make_directory(file, NULL, &error)) {
                show_error(ctx->app, "Create folder failed", error ? error->message : "Unknown error");
            }
            g_clear_error(&error);
            g_object_unref(file);
            g_free(path);
            refresh_current(ctx->app);
        }
    }
    gtk_widget_destroy(dialog);
}

static void menu_install_appimage_app(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    if (!ctx->target_path || !is_appimage_path(ctx->target_path)) {
        return;
    }
    char *base = basename_without_known_suffix(ctx->target_path);
    char *name = prompt_text(ctx->app, "Install AppImage as App", "Install", base);
    if (!name) {
        g_free(base);
        return;
    }
    char *safe = safe_command_name(name);
    char *app_dir = g_build_filename(g_get_home_dir(), ".local", "share", "filemanager", "apps", NULL);
    char *desktop_dir = g_build_filename(g_get_home_dir(), ".local", "share", "applications", NULL);
    g_mkdir_with_parents(app_dir, 0755);
    g_mkdir_with_parents(desktop_dir, 0755);
    char *app_name = g_strdup_printf("%s.AppImage", safe);
    char *dest_path = g_build_filename(app_dir, app_name, NULL);
    GFile *src = g_file_new_for_path(ctx->target_path);
    GFile *dest = g_file_new_for_path(dest_path);
    GError *error = NULL;
    if (!g_file_copy(src, dest, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &error)) {
        show_error(ctx->app, "AppImage install failed", error ? error->message : "Could not copy AppImage");
    } else {
        g_chmod(dest_path, 0755);
        char *desktop_name = g_strdup_printf("filemanager-%s.desktop", safe);
        char *desktop_path = g_build_filename(desktop_dir, desktop_name, NULL);
        char *quoted_exec = g_shell_quote(dest_path);
        char *desktop = g_strdup_printf(
            "[Desktop Entry]\n"
            "Type=Application\n"
            "Name=%s\n"
            "Exec=%s %%U\n"
            "Terminal=false\n"
            "Categories=Utility;\n",
            name,
            quoted_exec
        );
        if (!g_file_set_contents(desktop_path, desktop, -1, &error)) {
            show_error(ctx->app, "Desktop entry failed", error ? error->message : "Could not write desktop file");
        } else {
            g_chmod(desktop_path, 0644);
            show_info(ctx->app, "AppImage installed", "Installed under ~/.local/share/applications.");
        }
        g_free(desktop);
        g_free(quoted_exec);
        g_free(desktop_path);
        g_free(desktop_name);
    }
    g_clear_error(&error);
    g_object_unref(src);
    g_object_unref(dest);
    g_free(dest_path);
    g_free(app_name);
    g_free(app_dir);
    g_free(desktop_dir);
    g_free(safe);
    g_free(name);
    g_free(base);
}

static void menu_install_appimage_command(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    if (!ctx->target_path || !is_appimage_path(ctx->target_path)) {
        return;
    }
    char *base = basename_without_known_suffix(ctx->target_path);
    char *safe_base = safe_command_name(base);
    char *cmd = prompt_text(ctx->app, "Create AppImage Command", "Create", safe_base);
    if (!cmd) {
        g_free(safe_base);
        g_free(base);
        return;
    }
    char *safe = safe_command_name(cmd);
    char *bin_dir = g_build_filename(g_get_home_dir(), ".local", "bin", NULL);
    g_mkdir_with_parents(bin_dir, 0755);
    g_chmod(ctx->target_path, 0755);
    char *link_path = g_build_filename(bin_dir, safe, NULL);
    g_unlink(link_path);
    if (symlink(ctx->target_path, link_path) != 0) {
        show_error(ctx->app, "Command creation failed", "Could not create symlink in ~/.local/bin.");
    } else {
        show_info(ctx->app, "Command created", "~/.local/bin must be in PATH to run it by name.");
    }
    g_free(link_path);
    g_free(bin_dir);
    g_free(safe);
    g_free(cmd);
    g_free(safe_base);
    g_free(base);
}

static void menu_extract_archive(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    if (!ctx->target_path || !ctx->app->active_tab || !is_archive_path(ctx->target_path)) {
        return;
    }
    char *base = basename_without_known_suffix(ctx->target_path);
    char *dest_dir = unique_destination_path(ctx->app->active_tab->path, base);
    g_mkdir_with_parents(dest_dir, 0755);
    GError *error = NULL;
    gboolean ok = false;
    gint status = 0;
    char *stderr_data = NULL;
    if (path_has_suffix_ci(ctx->target_path, ".zip")) {
        char *argv[] = {"unzip", "-q", (char *)ctx->target_path, "-d", dest_dir, NULL};
        ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &stderr_data, &status, &error);
    } else if (path_has_suffix_ci(ctx->target_path, ".7z") || path_has_suffix_ci(ctx->target_path, ".rar")) {
        char *out_arg = g_strdup_printf("-o%s", dest_dir);
        char *argv[] = {"7z", "x", "-y", (char *)ctx->target_path, out_arg, NULL};
        ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &stderr_data, &status, &error);
        g_free(out_arg);
    } else {
        char *argv[] = {"tar", "-xf", (char *)ctx->target_path, "-C", dest_dir, NULL};
        ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &stderr_data, &status, &error);
    }
    if (!ok || !g_spawn_check_wait_status(status, &error)) {
        show_error(ctx->app, "Extract failed", stderr_data && stderr_data[0] ? stderr_data : (error ? error->message : "Extractor failed"));
    } else {
        refresh_current(ctx->app);
    }
    g_clear_error(&error);
    g_free(stderr_data);
    g_free(dest_dir);
    g_free(base);
}

static void menu_properties(GtkMenuItem *item, gpointer data) {
    (void)item;
    ContextData *ctx = data;
    if (!ctx->target_path) {
        return;
    }
    GFile *file = g_file_new_for_path(ctx->target_path);
    GError *error = NULL;
    GFileInfo *info = g_file_query_info(file, "standard::type,standard::size,time::modified", G_FILE_QUERY_INFO_NONE, NULL, &error);
    if (!info) {
        show_error(ctx->app, "Properties failed", error ? error->message : "Unknown error");
        g_clear_error(&error);
        g_object_unref(file);
        return;
    }
    char *size = format_size(g_file_info_get_size(info));
    char *msg = g_strdup_printf("%s\n%s\n%s", ctx->target_path, ctx->target_is_dir ? "folder" : "file", size);
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(ctx->app->window), GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Properties");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", msg);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(msg);
    g_free(size);
    g_object_unref(info);
    g_object_unref(file);
}

static void popup_destroy_after_click(GtkButton *button, gpointer data) {
    (void)button;
    gtk_widget_destroy(GTK_WIDGET(data));
}

static void append_menu_item(GtkWidget *box, GtkWidget *popup, const char *label, GCallback callback, ContextData *ctx, bool sensitive) {
    GtkWidget *item = gtk_button_new_with_label(label);
    gtk_widget_set_sensitive(item, sensitive);
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(item));
    if (GTK_IS_LABEL(child)) {
        gtk_label_set_xalign(GTK_LABEL(child), 0.0f);
    }
    ContextData *copy = g_new0(ContextData, 1);
    copy->app = ctx->app;
    copy->paths = new_string_array();
    for (guint i = 0; i < ctx->paths->len; i++) {
        g_ptr_array_add(copy->paths, g_strdup(g_ptr_array_index(ctx->paths, i)));
    }
    copy->target_path = g_strdup(ctx->target_path);
    copy->target_is_dir = ctx->target_is_dir;
    g_signal_connect_data(item, "clicked", callback, copy, context_data_destroy, 0);
    g_signal_connect(item, "clicked", G_CALLBACK(popup_destroy_after_click), popup);
    gtk_box_pack_start(GTK_BOX(box), item, FALSE, FALSE, 0);
}

static void append_separator(GtkWidget *box) {
    GtkWidget *sep = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(sep, "context-separator");
    gtk_box_pack_start(GTK_BOX(box), sep, FALSE, FALSE, 0);
}

static void context_popup_destroyed(GtkWidget *widget, gpointer data) {
    App *app = data;
    if (app->context_popup == widget) {
        app->context_popup = NULL;
    }
}

static void close_context_popup(App *app) {
    if (app->context_popup) {
        GtkWidget *popup = app->context_popup;
        app->context_popup = NULL;
        gtk_widget_destroy(popup);
    }
}

static GtkWidget *create_context_popup(App *app, GtkWidget *relative_to) {
    close_context_popup(app);
    GtkWidget *popup = gtk_popover_new(relative_to);
    gtk_popover_set_modal(GTK_POPOVER(popup), TRUE);
    gtk_popover_set_position(GTK_POPOVER(popup), GTK_POS_BOTTOM);
    g_signal_connect(popup, "destroy", G_CALLBACK(context_popup_destroyed), app);
    app->context_popup = popup;
    return popup;
}

static void present_context_popup(App *app, GtkWidget *popup, GdkEventButton *event) {
    gint x = event ? (gint)event->x : 0;
    gint y = event ? (gint)event->y : 0;
    GdkRectangle rect = {MAX(0, x), MAX(0, y), 1, 1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popup), &rect);
    gtk_widget_show_all(popup);
    gtk_popover_popup(GTK_POPOVER(popup));
    app->context_popup = popup;
}

static void show_context_menu(App *app, GtkWidget *relative_to, FileItem *target, GdkEventButton *event) {
    ContextData *ctx = context_data_new(app, target);
    GtkWidget *popup = create_context_popup(app, relative_to);
    GtkWidget *menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(menu, "context-menu");
    gtk_container_add(GTK_CONTAINER(popup), menu);
    bool has_target = target != NULL;
    bool single = ctx->paths->len == 1;
    bool is_appimage = has_target && single && ctx->target_path && is_appimage_path(ctx->target_path);
    bool is_archive = has_target && single && ctx->target_path && is_archive_path(ctx->target_path);
    append_menu_item(menu, popup, "Open", G_CALLBACK(menu_open), ctx, has_target);
    append_menu_item(menu, popup, "Open in New Tab", G_CALLBACK(menu_open_tab), ctx, has_target && target->kind == ITEM_DIR);
    if (is_appimage || is_archive) {
        append_separator(menu);
        append_menu_item(menu, popup, "Install AppImage as App", G_CALLBACK(menu_install_appimage_app), ctx, is_appimage);
        append_menu_item(menu, popup, "Create AppImage Command", G_CALLBACK(menu_install_appimage_command), ctx, is_appimage);
        append_menu_item(menu, popup, "Extract Archive Here", G_CALLBACK(menu_extract_archive), ctx, is_archive);
    }
    append_separator(menu);
    append_menu_item(menu, popup, "Cut", G_CALLBACK(menu_cut), ctx, ctx->paths->len > 0);
    append_menu_item(menu, popup, "Copy", G_CALLBACK(menu_copy), ctx, ctx->paths->len > 0);
    append_menu_item(menu, popup, "Paste", G_CALLBACK(menu_paste), ctx, app->clipboard_paths && app->clipboard_paths->len > 0);
    append_separator(menu);
    append_menu_item(menu, popup, "Rename", G_CALLBACK(menu_rename), ctx, has_target && single);
    append_menu_item(menu, popup, "Move to Trash", G_CALLBACK(menu_trash), ctx, ctx->paths->len > 0);
    append_menu_item(menu, popup, "Delete Permanently", G_CALLBACK(menu_delete), ctx, ctx->paths->len > 0);
    append_separator(menu);
    append_menu_item(menu, popup, "New Folder", G_CALLBACK(menu_new_folder), ctx, app->active_tab != NULL);
    append_menu_item(menu, popup, "Properties", G_CALLBACK(menu_properties), ctx, has_target && single);
    present_context_popup(app, popup, event);
    context_data_free(ctx);
}

static gboolean blank_context_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    App *app = data;
    if (event->type == GDK_2BUTTON_PRESS && event->button == 1) {
        if (widget == app->grid) {
            GtkFlowBoxChild *child = gtk_flow_box_get_child_at_pos(GTK_FLOW_BOX(app->grid), (gint)event->x, (gint)event->y);
            if (child) {
                FileItem *item = g_object_get_data(G_OBJECT(child), "item");
                if (item) {
                    app->drag_selecting = false;
                    activate_item(app, item, false);
                    return GDK_EVENT_STOP;
                }
            }
        } else if (widget == app->list) {
            GtkListBoxRow *row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(app->list), (gint)event->y);
            if (row) {
                FileItem *item = g_object_get_data(G_OBJECT(row), "item");
                if (item) {
                    app->drag_selecting = false;
                    activate_item(app, item, false);
                    return GDK_EVENT_STOP;
                }
            }
        }
    }
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        if (widget == app->grid) {
            GtkFlowBoxChild *child = gtk_flow_box_get_child_at_pos(GTK_FLOW_BOX(app->grid), (gint)event->x, (gint)event->y);
            if (child) {
                FileItem *item = g_object_get_data(G_OBJECT(child), "item");
                gtk_flow_box_unselect_all(GTK_FLOW_BOX(app->grid));
                gtk_flow_box_select_child(GTK_FLOW_BOX(app->grid), child);
                update_status(app);
                show_context_menu(app, GTK_WIDGET(child), item, event);
                return GDK_EVENT_STOP;
            }
        } else if (widget == app->list) {
            GtkListBoxRow *row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(app->list), (gint)event->y);
            if (row) {
                FileItem *item = g_object_get_data(G_OBJECT(row), "item");
                gtk_list_box_unselect_all(GTK_LIST_BOX(app->list));
                gtk_list_box_select_row(GTK_LIST_BOX(app->list), row);
                update_status(app);
                show_context_menu(app, GTK_WIDGET(row), item, event);
                return GDK_EVENT_STOP;
            }
        }
        show_context_menu(app, widget, NULL, event);
        return GDK_EVENT_STOP;
    }
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        close_context_popup(app);
        app->drag_selecting = true;
        app->drag_view = widget == app->list ? VIEW_LIST : VIEW_GRID;
        if (app->drag_view == VIEW_GRID) {
            gtk_flow_box_unselect_all(GTK_FLOW_BOX(app->grid));
            GtkFlowBoxChild *child = gtk_flow_box_get_child_at_pos(GTK_FLOW_BOX(app->grid), (gint)event->x, (gint)event->y);
            if (child) {
                gtk_flow_box_select_child(GTK_FLOW_BOX(app->grid), child);
            }
        } else {
            gtk_list_box_unselect_all(GTK_LIST_BOX(app->list));
            GtkListBoxRow *row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(app->list), (gint)event->y);
            if (row) {
                gtk_list_box_select_row(GTK_LIST_BOX(app->list), row);
            }
        }
        update_status(app);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static gboolean file_area_motion(GtkWidget *widget, GdkEventMotion *event, gpointer data) {
    App *app = data;
    if (!app->drag_selecting) {
        return GDK_EVENT_PROPAGATE;
    }
    if (app->drag_view == VIEW_GRID && widget == app->grid) {
        GtkFlowBoxChild *child = gtk_flow_box_get_child_at_pos(GTK_FLOW_BOX(app->grid), (gint)event->x, (gint)event->y);
        if (child) {
            gtk_flow_box_select_child(GTK_FLOW_BOX(app->grid), child);
            update_status(app);
        }
        return GDK_EVENT_STOP;
    }
    if (app->drag_view == VIEW_LIST && widget == app->list) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_y(GTK_LIST_BOX(app->list), (gint)event->y);
        if (row) {
            gtk_list_box_select_row(GTK_LIST_BOX(app->list), row);
            update_status(app);
        }
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static gboolean file_area_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    (void)widget;
    App *app = data;
    if (event->button == 1 && app->drag_selecting) {
        app->drag_selecting = false;
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static void drive_data_free(gpointer data) {
    DriveData *drive = data;
    if (!drive) {
        return;
    }
    g_free(drive->name);
    g_free(drive->path);
    g_free(drive);
}

static void drive_data_destroy(gpointer data, GClosure *closure) {
    (void)closure;
    drive_data_free(data);
}

static DriveData *drive_data_copy(DriveData *src) {
    DriveData *copy = g_new0(DriveData, 1);
    copy->app = src->app;
    copy->name = g_strdup(src->name);
    copy->path = g_strdup(src->path);
    return copy;
}

static void drive_popup_destroy_after_click(GtkButton *button, gpointer data) {
    (void)button;
    gtk_widget_destroy(GTK_WIDGET(data));
}

static void drive_enable_automount(GtkButton *button, gpointer data) {
    (void)button;
    DriveData *drive = data;
    char *source = NULL;
    char *fstype = NULL;
    char *uuid = NULL;
    if (!drive_mount_info(drive->path, &source, &fstype, &uuid)) {
        show_error(drive->app, "Automount unavailable", "Could not resolve this mount to a block device UUID.");
        return;
    }
    if (fstab_has_uuid_or_path(uuid, drive->path)) {
        show_info(drive->app, "Automount already enabled", "This drive already appears in /etc/fstab.");
        g_free(source);
        g_free(fstype);
        g_free(uuid);
        return;
    }

    char *q_uuid = g_shell_quote(uuid);
    char *q_path = g_shell_quote(drive->path);
    char *q_fstype = g_shell_quote(fstype);
    char *script = g_strdup_printf(
        "mkdir -p %s && cp /etc/fstab /etc/fstab.filemanager.bak && "
        "printf '\\n# filemanager automount\\nUUID=%%s %%s %%s defaults,nofail,x-systemd.device-timeout=10 0 2\\n' %s %s %s >> /etc/fstab",
        q_path,
        q_uuid,
        q_path,
        q_fstype
    );
    char *distro = linux_distro_name();
    char *message = g_strdup_printf("Using /etc/fstab, which is supported on %s. Authenticate to add this drive to startup mounts.", distro);
    run_privileged_script(drive->app, script, message);
    g_free(message);
    g_free(distro);
    g_free(script);
    g_free(q_uuid);
    g_free(q_path);
    g_free(q_fstype);
    g_free(source);
    g_free(fstype);
    g_free(uuid);
}

static void drive_disable_automount(GtkButton *button, gpointer data) {
    (void)button;
    DriveData *drive = data;
    char *source = NULL;
    char *fstype = NULL;
    char *uuid = NULL;
    if (!drive_mount_info(drive->path, &source, &fstype, &uuid)) {
        show_error(drive->app, "Automount unavailable", "Could not resolve this mount to a block device UUID.");
        return;
    }

    char *q_uuid = g_shell_quote(uuid);
    char *q_path = g_shell_quote(drive->path);
    char *script = g_strdup_printf(
        "cp /etc/fstab /etc/fstab.filemanager.bak && "
        "awk -v u=UUID=%s -v p=%s '$0 ~ u || $2 == p {next} {print}' /etc/fstab > /etc/fstab.filemanager.tmp && "
        "cat /etc/fstab.filemanager.tmp > /etc/fstab && rm -f /etc/fstab.filemanager.tmp",
        q_uuid,
        q_path
    );
    char *distro = linux_distro_name();
    char *message = g_strdup_printf("Using /etc/fstab, which is supported on %s. Authenticate to remove this drive from startup mounts.", distro);
    run_privileged_script(drive->app, script, message);
    g_free(message);
    g_free(distro);
    g_free(script);
    g_free(q_uuid);
    g_free(q_path);
    g_free(source);
    g_free(fstype);
    g_free(uuid);
}

static void append_drive_menu_item(GtkWidget *box, GtkWidget *popup, const char *label, GCallback callback, DriveData *drive, bool sensitive) {
    GtkWidget *item = gtk_button_new_with_label(label);
    gtk_widget_set_sensitive(item, sensitive);
    GtkWidget *child = gtk_bin_get_child(GTK_BIN(item));
    if (GTK_IS_LABEL(child)) {
        gtk_label_set_xalign(GTK_LABEL(child), 0.0f);
    }
    g_signal_connect_data(item, "clicked", callback, drive_data_copy(drive), drive_data_destroy, 0);
    g_signal_connect(item, "clicked", G_CALLBACK(drive_popup_destroy_after_click), popup);
    gtk_box_pack_start(GTK_BOX(box), item, FALSE, FALSE, 0);
}

static void show_drive_context_menu(App *app, GtkWidget *relative_to, const char *name, const char *path, GdkEventButton *event) {
    DriveData drive = {0};
    drive.app = app;
    drive.name = (char *)name;
    drive.path = (char *)path;

    GtkWidget *popup = create_context_popup(app, relative_to);
    GtkWidget *menu = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(menu, "context-menu");
    gtk_container_add(GTK_CONTAINER(popup), menu);
    append_drive_menu_item(menu, popup, "Enable Automount on Startup", G_CALLBACK(drive_enable_automount), &drive, true);
    append_drive_menu_item(menu, popup, "Disable Automount on Startup", G_CALLBACK(drive_disable_automount), &drive, true);
    present_context_popup(app, popup, event);
}

static gboolean place_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    App *app = data;
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        gboolean is_mount = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "is-mount"));
        const char *path = g_object_get_data(G_OBJECT(widget), "path");
        const char *name = g_object_get_data(G_OBJECT(widget), "place-name");
        if (is_mount && path) {
            show_drive_context_menu(app, widget, name ? name : path, path, event);
            return GDK_EVENT_STOP;
        }
    }
    return GDK_EVENT_PROPAGATE;
}

static gboolean place_event(GtkWidget *widget, GdkEvent *event, gpointer data) {
    if (event->type != GDK_BUTTON_PRESS) {
        return GDK_EVENT_PROPAGATE;
    }
    return place_button_press(widget, (GdkEventButton *)event, data);
}

static gboolean file_area_scroll(GtkWidget *widget, GdkEventScroll *event, gpointer data) {
    (void)widget;
    App *app = data;
    if ((event->state & GDK_CONTROL_MASK) == 0) {
        return GDK_EVENT_PROPAGATE;
    }

    double old_zoom = app->zoom;
    if (event->direction == GDK_SCROLL_UP || (event->direction == GDK_SCROLL_SMOOTH && event->delta_y < 0)) {
        app->zoom = MIN(ZOOM_MAX, app->zoom * ZOOM_STEP);
    } else if (event->direction == GDK_SCROLL_DOWN || (event->direction == GDK_SCROLL_SMOOTH && event->delta_y > 0)) {
        app->zoom = MAX(ZOOM_MIN, app->zoom / ZOOM_STEP);
    }
    if (old_zoom != app->zoom && app->mode == VIEW_GRID) {
        app_render(app);
    }
    return GDK_EVENT_STOP;
}

static gboolean tile_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    App *app = data;
    FileItem *item = g_object_get_data(G_OBJECT(widget), "item");
    if (!item) {
        return GDK_EVENT_PROPAGATE;
    }
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        show_context_menu(app, widget, item, event);
        return GDK_EVENT_STOP;
    }
    if (event->type == GDK_2BUTTON_PRESS && event->button == 1 && item) {
        activate_item(app, item, false);
        return GDK_EVENT_STOP;
    }
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        close_context_popup(app);
        GtkWidget *parent = gtk_widget_get_parent(widget);
        if (GTK_IS_FLOW_BOX_CHILD(parent)) {
            gtk_flow_box_unselect_all(GTK_FLOW_BOX(app->grid));
            gtk_flow_box_select_child(GTK_FLOW_BOX(app->grid), GTK_FLOW_BOX_CHILD(parent));
            update_status(app);
            return GDK_EVENT_STOP;
        }
    }
    if (event->type == GDK_BUTTON_PRESS && event->button == 2 && item->kind == ITEM_DIR) {
        activate_item(app, item, true);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static gboolean row_button_press(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    App *app = data;
    FileItem *item = g_object_get_data(G_OBJECT(widget), "item");
    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {
        show_context_menu(app, widget, item, event);
        return GDK_EVENT_STOP;
    }
    if (event->type == GDK_2BUTTON_PRESS && event->button == 1 && item) {
        activate_item(app, item, false);
        return GDK_EVENT_STOP;
    }
    if (event->type == GDK_BUTTON_PRESS && event->button == 1) {
        close_context_popup(app);
        gtk_list_box_unselect_all(GTK_LIST_BOX(app->list));
        gtk_list_box_select_row(GTK_LIST_BOX(app->list), GTK_LIST_BOX_ROW(widget));
        update_status(app);
        return GDK_EVENT_PROPAGATE;
    }
    return GDK_EVENT_PROPAGATE;
}

static gboolean row_button_release(GtkWidget *widget, GdkEventButton *event, gpointer data) {
    App *app = data;
    FileItem *item = g_object_get_data(G_OBJECT(widget), "item");
    if (event->type == GDK_2BUTTON_PRESS && event->button == 1 && item) {
        activate_item(app, item, false);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static void flow_child_activated(GtkFlowBox *box, GtkFlowBoxChild *child, gpointer data) {
    (void)box;
    App *app = data;
    FileItem *item = g_object_get_data(G_OBJECT(child), "item");
    if (item) {
        activate_item(app, item, false);
    }
}

static void list_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer data) {
    (void)box;
    App *app = data;
    FileItem *item = g_object_get_data(G_OBJECT(row), "item");
    if (item) {
        activate_item(app, item, false);
    }
}

static gboolean tile_leave(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
    (void)widget;
    (void)event;
    app_stop_video(data);
    return GDK_EVENT_PROPAGATE;
}

static gboolean hover_frame_tick(gpointer data) {
    App *app = data;
    if (!app->active_video_tile || !app->hover_frames || app->hover_frames->len == 0) {
        app->hover_timeout_id = 0;
        return G_SOURCE_REMOVE;
    }
    GtkWidget *thumb_box = g_object_get_data(G_OBJECT(app->active_video_tile), "thumb-box");
    if (!thumb_box) {
        app->hover_timeout_id = 0;
        return G_SOURCE_REMOVE;
    }
    app->hover_frame_pos = (app->hover_frame_pos + 1) % app->hover_frames->len;
    GdkPixbuf *pixbuf = g_ptr_array_index(app->hover_frames, app->hover_frame_pos);
    GList *children = gtk_container_get_children(GTK_CONTAINER(thumb_box));
    for (GList *l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
    int thumb_size = grid_thumb_size(app);
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, thumb_size, thumb_size, GDK_INTERP_BILINEAR);
    GtkWidget *image = gtk_image_new_from_pixbuf(scaled ? scaled : pixbuf);
    g_clear_object(&scaled);
    gtk_container_add(GTK_CONTAINER(thumb_box), image);
    gtk_widget_show_all(thumb_box);
    return G_SOURCE_CONTINUE;
}

static bool start_ffmpeg_hover_preview(App *app, GtkWidget *widget, FileItem *item) {
    static const guint times[] = {5, 15, 30, 45, 60, 75};
    GPtrArray *frames = g_ptr_array_new_with_free_func(g_object_unref);
    for (guint i = 0; i < G_N_ELEMENTS(times); i++) {
        GdkPixbuf *frame = make_ffmpegthumbnailer_thumbnail(item->path, times[i]);
        if (frame) {
            g_ptr_array_add(frames, frame);
        }
    }
    if (frames->len == 0) {
        g_ptr_array_unref(frames);
        return false;
    }

    app->hover_frames = frames;
    app->hover_frame_pos = 0;
    app->active_video_tile = widget;
    app->active_video_path = g_strdup(item->path);
    hover_frame_tick(app);
    app->hover_timeout_id = g_timeout_add(220, hover_frame_tick, app);
    gtk_label_set_text(GTK_LABEL(app->status_preview), "preview: video frames");
    return true;
}

static gboolean tile_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer data) {
    (void)event;
    App *app = data;
    FileItem *item = g_object_get_data(G_OBJECT(widget), "item");
    if (!item || !item->is_video) {
        return GDK_EVENT_PROPAGATE;
    }
    if (app->active_video_tile == widget) {
        return GDK_EVENT_PROPAGATE;
    }
    app_stop_video(app);
    if (start_ffmpeg_hover_preview(app, widget, item)) {
        return GDK_EVENT_PROPAGATE;
    }
    GtkWidget *socket = NULL;
    GstElement *sink = gst_element_factory_make("gtksink", NULL);
    if (sink) {
        g_object_get(sink, "widget", &socket, NULL);
        app->video_widget = socket;
    } else {
        sink = gst_element_factory_make("ximagesink", NULL);
        socket = gtk_drawing_area_new();
    }
    if (!sink || !socket) {
        g_clear_object(&sink);
        return GDK_EVENT_PROPAGATE;
    }
    app->video_sink = sink;
    int preview_size = grid_thumb_size(app);
    gtk_widget_set_size_request(socket, preview_size, preview_size);
    GtkWidget *thumb_box = g_object_get_data(G_OBJECT(widget), "thumb-box");
    GList *children = gtk_container_get_children(GTK_CONTAINER(thumb_box));
    for (GList *l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
    gtk_container_add(GTK_CONTAINER(thumb_box), socket);
    gtk_widget_show_all(thumb_box);

    char *uri = g_filename_to_uri(item->path, NULL, NULL);
    g_object_set(app->playbin, "uri", uri, "mute", TRUE, "video-sink", sink, NULL);
    g_free(uri);
    gst_element_set_state(app->playbin, GST_STATE_PLAYING);
    gtk_widget_realize(socket);
    GdkWindow *window = gtk_widget_get_window(socket);
    if (!app->video_widget && window && GDK_IS_X11_WINDOW(window)) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(app->playbin), (guintptr)GDK_WINDOW_XID(window));
    }
    app->video_socket = socket;
    app->active_video_tile = widget;
    app->active_video_path = g_strdup(item->path);
    gtk_label_set_text(GTK_LABEL(app->status_preview), "preview: video hover");
    return GDK_EVENT_PROPAGATE;
}

static GtkWidget *make_tile(App *app, FileItem *item) {
    GtkWidget *event = gtk_event_box_new();
    gtk_widget_set_name(event, "tile");
    gtk_widget_add_events(event, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK);
    gtk_widget_set_valign(event, GTK_ALIGN_START);
    g_object_set_data(G_OBJECT(event), "item", item);
    g_signal_connect(event, "enter-notify-event", G_CALLBACK(tile_enter), app);
    g_signal_connect(event, "leave-notify-event", G_CALLBACK(tile_leave), app);
    g_signal_connect(event, "button-press-event", G_CALLBACK(tile_button_press), app);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *thumb_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(thumb_box, "thumb");
    int thumb_size = grid_thumb_size(app);
    gtk_widget_set_size_request(thumb_box, thumb_size, thumb_size);
    gtk_box_set_homogeneous(GTK_BOX(thumb_box), TRUE);
    g_object_set_data(G_OBJECT(event), "thumb-box", thumb_box);

    GdkPixbuf *pixbuf = thumb_cache_get(app, item);
    GdkPixbuf *scaled = pixbuf ? gdk_pixbuf_scale_simple(pixbuf, thumb_size, thumb_size, GDK_INTERP_BILINEAR) : NULL;
    GdkPixbuf *badged = pixbuf_with_type_badge(item, scaled ? scaled : pixbuf, thumb_size);
    GtkWidget *image = gtk_image_new_from_pixbuf(badged ? badged : (scaled ? scaled : pixbuf));
    g_clear_object(&badged);
    g_clear_object(&scaled);
    if (pixbuf) {
        g_object_unref(pixbuf);
    }
    gtk_container_add(GTK_CONTAINER(thumb_box), image);

    char *short_name = shorten_middle(item->name, (guint)grid_name_chars(app));
    GtkWidget *name = make_label(short_name, "tile-name", 0.5f);
    char *size = item->kind == ITEM_DIR ? g_strdup("folder") : format_size(item->size);
    GtkWidget *meta = make_label(size, "tile-meta", 0.5f);
    gtk_box_pack_start(GTK_BOX(box), thumb_box, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), meta, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(event), box);
    g_free(short_name);
    g_free(size);
    return event;
}

static GtkWidget *make_row(App *app, FileItem *item) {
    GtkWidget *row = gtk_list_box_row_new();
    gtk_widget_add_events(row, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_object_set_data(G_OBJECT(row), "item", item);
    g_signal_connect(row, "button-press-event", G_CALLBACK(row_button_press), app);
    g_signal_connect(row, "button-release-event", G_CALLBACK(row_button_release), app);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_set_name(box, "rowbox");
    GdkPixbuf *pixbuf = thumb_cache_get(app, item);
    GdkPixbuf *scaled = pixbuf ? gdk_pixbuf_scale_simple(pixbuf, 86, 86, GDK_INTERP_BILINEAR) : NULL;
    GtkWidget *image = gtk_image_new_from_pixbuf(scaled ? scaled : pixbuf);
    g_clear_object(&scaled);
    if (pixbuf) {
        g_object_unref(pixbuf);
    }
    GtkWidget *name_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *name = make_label(item->name, "row-name", 0.0f);
    GtkWidget *type_sub = make_label(file_type_label(item), "row-sub", 0.0f);
    gtk_box_pack_start(GTK_BOX(name_box), name, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(name_box), type_sub, FALSE, FALSE, 0);
    char *size = item->kind == ITEM_DIR ? g_strdup("-") : format_size(item->size);
    char *mtime = format_mtime(item->mtime);
    GtkWidget *size_label = make_label(size, "row-sub", 1.0f);
    GtkWidget *type_label = make_label(file_type_label(item), "row-sub", 0.5f);
    GtkWidget *modified = make_label(mtime, "row-sub", 1.0f);
    gtk_widget_set_size_request(image, 86, 86);
    gtk_widget_set_size_request(size_label, 100, -1);
    gtk_widget_set_size_request(type_label, 150, -1);
    gtk_widget_set_size_request(modified, 160, -1);
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), name_box, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), size_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), type_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), modified, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(row), box);
    g_free(size);
    g_free(mtime);
    return row;
}

static void clear_container(GtkWidget *container) {
    GList *children = gtk_container_get_children(GTK_CONTAINER(container));
    for (GList *l = children; l; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
}

static void app_render(App *app) {
    FolderTab *tab = app->active_tab;
    if (!tab) {
        return;
    }
    app_stop_video(app);
    clear_container(app->grid);
    clear_container(app->list);
    for (guint i = 0; i < tab->items->len; i++) {
        FileItem *item = g_ptr_array_index(tab->items, i);
        GtkWidget *tile = make_tile(app, item);
        GtkWidget *child = gtk_flow_box_child_new();
        gtk_widget_set_valign(child, GTK_ALIGN_START);
        g_object_set_data(G_OBJECT(child), "item", item);
        gtk_container_add(GTK_CONTAINER(child), tile);
        gtk_flow_box_insert(GTK_FLOW_BOX(app->grid), child, -1);
        gtk_container_add(GTK_CONTAINER(app->list), make_row(app, item));
    }
    gtk_widget_show_all(app->grid);
    gtk_widget_show_all(app->list);
    gtk_stack_set_visible_child(GTK_STACK(app->stack), app->mode == VIEW_GRID ? app->grid_scroller : app->list_scroller);
    update_status(app);
}

static GtkWidget *build_list_header(void) {
    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 18);
    gtk_widget_set_name(header, "list-header");
    GtkWidget *name = make_label("NIMI  ▲", NULL, 0.0f);
    GtkWidget *size = make_label("KOKO", NULL, 1.0f);
    GtkWidget *type = make_label("TYYPPI", NULL, 0.5f);
    GtkWidget *modified = make_label("MUOKATTU", NULL, 1.0f);
    gtk_widget_set_size_request(size, 100, -1);
    gtk_widget_set_size_request(type, 150, -1);
    gtk_widget_set_size_request(modified, 160, -1);
    GtkWidget *spacer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(spacer, 104, -1);
    gtk_box_pack_start(GTK_BOX(header), spacer, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), name, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(header), size, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), type, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(header), modified, FALSE, FALSE, 0);
    return header;
}

static FileItem *file_item_from_info(const char *dir, GFileInfo *info) {
    const char *name = g_file_info_get_name(info);
    if (!name || g_str_has_prefix(name, ".")) {
        return NULL;
    }
    FileItem *item = g_new0(FileItem, 1);
    item->name = g_strdup(name);
    item->path = g_build_filename(dir, name, NULL);
    item->size = g_file_info_get_size(info);
    item->mtime = (time_t)g_file_info_get_attribute_uint64(info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
    item->content_type = g_strdup(g_file_info_get_content_type(info));
    item->kind = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY ? ITEM_DIR : ITEM_FILE;
    item->is_image = content_type_starts(item->content_type, "image/");
    item->is_video = content_type_starts(item->content_type, "video/");
    return item;
}

static void scan_result_free(ScanResult *result) {
    if (!result) {
        return;
    }
    if (result->items) {
        g_ptr_array_unref(result->items);
    }
    g_clear_object(&result->cancel);
    g_free(result->path);
    g_free(result);
}

static gboolean scan_finish_main(gpointer data) {
    ScanResult *result = data;
    FolderTab *tab = result->tab;
    if (tab->scan_generation != result->generation) {
        scan_result_free(result);
        return G_SOURCE_REMOVE;
    }
    g_clear_pointer(&tab->items, g_ptr_array_unref);
    tab->items = g_ptr_array_ref(result->items);
    tab->loading = false;
    if (tab->app->active_tab == tab) {
        app_render(tab->app);
    }
    scan_result_free(result);
    return G_SOURCE_REMOVE;
}

static gpointer scan_thread(gpointer data) {
    ScanResult *result = data;
    GFile *dir = g_file_new_for_path(result->path);
    GError *error = NULL;
    GFileEnumerator *en = g_file_enumerate_children(
        dir,
        G_FILE_ATTRIBUTE_STANDARD_NAME ","
        G_FILE_ATTRIBUTE_STANDARD_TYPE ","
        G_FILE_ATTRIBUTE_STANDARD_SIZE ","
        G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
        G_FILE_ATTRIBUTE_TIME_MODIFIED,
        G_FILE_QUERY_INFO_NONE,
        result->cancel,
        &error
    );
    if (en) {
        GFileInfo *info = NULL;
        while ((info = g_file_enumerator_next_file(en, result->cancel, NULL)) != NULL) {
            if (g_cancellable_is_cancelled(result->cancel)) {
                g_object_unref(info);
                break;
            }
            FileItem *item = file_item_from_info(result->path, info);
            if (item) {
                g_ptr_array_add(result->items, item);
            }
            g_object_unref(info);
        }
        g_object_unref(en);
    }
    g_clear_error(&error);
    g_object_unref(dir);
    g_ptr_array_sort(result->items, compare_items);
    g_idle_add(scan_finish_main, result);
    return NULL;
}

static void tab_start_scan(FolderTab *tab, const char *path) {
    char *canonical = g_canonicalize_filename(path, NULL);
    if (tab->cancel) {
        g_cancellable_cancel(tab->cancel);
        g_clear_object(&tab->cancel);
    }
    tab->cancel = g_cancellable_new();
    tab->scan_generation++;
    tab->loading = true;
    g_free(tab->path);
    tab->path = canonical;
    char *base = g_path_get_basename(tab->path);
    gtk_label_set_text(GTK_LABEL(tab->label), base);
    g_free(base);
    update_status(tab->app);

    ScanResult *result = g_new0(ScanResult, 1);
    result->tab = tab;
    result->path = g_strdup(tab->path);
    result->cancel = g_object_ref(tab->cancel);
    result->items = item_array_new();
    result->generation = tab->scan_generation;
    GThread *thread = g_thread_new("filemanager-scan", scan_thread, result);
    g_thread_unref(thread);
}

static void tab_navigate(FolderTab *tab, const char *path, bool record_history) {
    char *canonical = g_canonicalize_filename(path, NULL);
    if (record_history && tab->history) {
        while (tab->history->len > tab->history_pos + 1) {
            g_ptr_array_remove_index(tab->history, tab->history->len - 1);
        }
        if (tab->history->len == 0 || g_strcmp0(g_ptr_array_index(tab->history, tab->history_pos), canonical) != 0) {
            g_ptr_array_add(tab->history, g_strdup(canonical));
            tab->history_pos = tab->history->len - 1;
        }
    }
    tab_start_scan(tab, canonical);
    g_free(canonical);
}

static FolderTab *folder_tab_new(App *app, const char *path) {
    FolderTab *tab = g_new0(FolderTab, 1);
    tab->app = app;
    tab->path = g_canonicalize_filename(path, NULL);
    tab->items = item_array_new();
    tab->history = g_ptr_array_new_with_free_func(g_free);
    tab->history_pos = 0;
    tab->page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    char *base = g_path_get_basename(tab->path);
    tab->label = gtk_label_new(base);
    g_free(base);
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), tab->page, tab->label);
    g_ptr_array_add(app->tabs, tab);
    gtk_widget_show_all(app->notebook);
    tab_navigate(tab, tab->path, true);
    return tab;
}

static void folder_tab_free(gpointer data) {
    FolderTab *tab = data;
    if (!tab) {
        return;
    }
    if (tab->cancel) {
        g_cancellable_cancel(tab->cancel);
        g_object_unref(tab->cancel);
    }
    g_free(tab->path);
    g_clear_pointer(&tab->items, g_ptr_array_unref);
    g_clear_pointer(&tab->history, g_ptr_array_unref);
    g_free(tab);
}

static void app_open_path(App *app, const char *path, bool new_tab) {
    if (!g_file_test(path, G_FILE_TEST_IS_DIR)) {
        open_external(path);
        return;
    }
    if (new_tab || !app->active_tab) {
        FolderTab *tab = folder_tab_new(app, path);
        gint page = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), tab->page);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), page);
        app->active_tab = tab;
    } else {
        tab_navigate(app->active_tab, path, true);
    }
    app_render(app);
}

static void app_stop_video(App *app) {
    if (!app->active_video_tile) {
        return;
    }
    GtkWidget *tile = app->active_video_tile;
    FileItem *item = g_object_get_data(G_OBJECT(tile), "item");
    GtkWidget *thumb_box = g_object_get_data(G_OBJECT(tile), "thumb-box");
    if (app->hover_timeout_id) {
        g_source_remove(app->hover_timeout_id);
        app->hover_timeout_id = 0;
    }
    g_clear_pointer(&app->hover_frames, g_ptr_array_unref);
    app->hover_frame_pos = 0;
    if (app->playbin) {
        gst_element_set_state(app->playbin, GST_STATE_NULL);
        g_object_set(app->playbin, "video-sink", NULL, NULL);
    }
    if (app->video_widget) {
        GtkWidget *parent = gtk_widget_get_parent(app->video_widget);
        if (parent) {
            gtk_container_remove(GTK_CONTAINER(parent), app->video_widget);
        }
        g_clear_object(&app->video_widget);
    }
    if (thumb_box && item) {
        GList *children = gtk_container_get_children(GTK_CONTAINER(thumb_box));
        for (GList *l = children; l; l = l->next) {
            gtk_widget_destroy(GTK_WIDGET(l->data));
        }
        g_list_free(children);
        GdkPixbuf *pixbuf = thumb_cache_get(app, item);
        int thumb_size = grid_thumb_size(app);
        GdkPixbuf *scaled = pixbuf ? gdk_pixbuf_scale_simple(pixbuf, thumb_size, thumb_size, GDK_INTERP_BILINEAR) : NULL;
        GdkPixbuf *badged = pixbuf_with_type_badge(item, scaled ? scaled : pixbuf, thumb_size);
        GtkWidget *image = gtk_image_new_from_pixbuf(badged ? badged : (scaled ? scaled : pixbuf));
        g_clear_object(&badged);
        g_clear_object(&scaled);
        if (pixbuf) {
            g_object_unref(pixbuf);
        }
        gtk_container_add(GTK_CONTAINER(thumb_box), image);
        gtk_widget_show_all(thumb_box);
    }
    g_clear_pointer(&app->active_video_path, g_free);
    app->active_video_tile = NULL;
    app->video_socket = NULL;
    g_clear_object(&app->video_sink);
    gtk_label_set_text(GTK_LABEL(app->status_preview), "preview: idle");
}

static void notebook_switch_page(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer data) {
    (void)notebook;
    (void)page_num;
    App *app = data;
    for (guint i = 0; i < app->tabs->len; i++) {
        FolderTab *tab = g_ptr_array_index(app->tabs, i);
        if (tab->page == page) {
            app->active_tab = tab;
            app_render(app);
            break;
        }
    }
}

static void close_current_tab(App *app) {
    if (app->tabs->len <= 1 || !app->active_tab) {
        return;
    }
    FolderTab *tab = app->active_tab;
    gint page = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), tab->page);
    gtk_notebook_remove_page(GTK_NOTEBOOK(app->notebook), page);
    g_ptr_array_remove(app->tabs, tab);
    gint next = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    GtkWidget *next_page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), next);
    notebook_switch_page(GTK_NOTEBOOK(app->notebook), next_page, (guint)next, app);
}

static void path_entry_activate(GtkEntry *entry, gpointer data) {
    App *app = data;
    const char *path = gtk_entry_get_text(entry);
    app_open_path(app, path, false);
}

static void view_button_clicked(GtkButton *button, gpointer data) {
    (void)button;
    App *app = data;
    app->mode = app->mode == VIEW_GRID ? VIEW_LIST : VIEW_GRID;
    gtk_button_set_label(GTK_BUTTON(app->view_button), app->mode == VIEW_GRID ? "▦" : "☷");
    app_render(app);
}

static void refresh_current(App *app) {
    if (app->active_tab) {
        tab_start_scan(app->active_tab, app->active_tab->path);
    }
}

static void go_back(App *app) {
    FolderTab *tab = app->active_tab;
    if (!tab || !tab->history || tab->history_pos == 0) {
        return;
    }
    tab->history_pos--;
    tab_navigate(tab, g_ptr_array_index(tab->history, tab->history_pos), false);
}

static void go_forward(App *app) {
    FolderTab *tab = app->active_tab;
    if (!tab || !tab->history || tab->history_pos + 1 >= tab->history->len) {
        return;
    }
    tab->history_pos++;
    tab_navigate(tab, g_ptr_array_index(tab->history, tab->history_pos), false);
}

static void go_up(App *app) {
    if (!app->active_tab) {
        return;
    }
    char *parent = g_path_get_dirname(app->active_tab->path);
    if (g_strcmp0(parent, app->active_tab->path) != 0) {
        app_open_path(app, parent, false);
    }
    g_free(parent);
}

static gboolean key_press(GtkWidget *widget, GdkEventKey *event, gpointer data) {
    (void)widget;
    App *app = data;
    bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    bool alt = (event->state & GDK_MOD1_MASK) != 0;
    if (alt && event->keyval == GDK_KEY_Left) {
        go_back(app);
        return GDK_EVENT_STOP;
    }
    if (alt && event->keyval == GDK_KEY_Right) {
        go_forward(app);
        return GDK_EVENT_STOP;
    }
    if (alt && event->keyval == GDK_KEY_Up) {
        go_up(app);
        return GDK_EVENT_STOP;
    }
    if (ctrl && event->keyval == GDK_KEY_l) {
        gtk_widget_grab_focus(app->path_entry);
        gtk_editable_select_region(GTK_EDITABLE(app->path_entry), 0, -1);
        return GDK_EVENT_STOP;
    }
    if (ctrl && event->keyval == GDK_KEY_t) {
        app_open_path(app, app->active_tab ? app->active_tab->path : g_get_home_dir(), true);
        return GDK_EVENT_STOP;
    }
    if (ctrl && event->keyval == GDK_KEY_w) {
        close_current_tab(app);
        return GDK_EVENT_STOP;
    }
    if (event->keyval == GDK_KEY_BackSpace && app->active_tab) {
        go_up(app);
        return GDK_EVENT_STOP;
    }
    if (event->keyval == GDK_KEY_F5) {
        refresh_current(app);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static void place_clicked(GtkButton *button, gpointer data) {
    App *app = data;
    const char *path = g_object_get_data(G_OBJECT(button), "path");
    if (path) {
        app_open_path(app, path, false);
    }
}

static SidebarIconKind sidebar_icon_for_place(const char *label, bool is_mount) {
    if (is_mount) {
        return g_strcmp0(label, "Juuritiedostot") == 0 ? SIDEBAR_ICON_ROOT : SIDEBAR_ICON_DRIVE;
    }
    if (g_strcmp0(label, "Koti") == 0) return SIDEBAR_ICON_HOME;
    if (g_strcmp0(label, "Työpöytä") == 0) return SIDEBAR_ICON_DESKTOP;
    if (g_strcmp0(label, "Asiakirjat") == 0) return SIDEBAR_ICON_DOCUMENTS;
    if (g_strcmp0(label, "Lataukset") == 0) return SIDEBAR_ICON_DOWNLOADS;
    if (g_strcmp0(label, "Musiikki") == 0) return SIDEBAR_ICON_MUSIC;
    if (g_strcmp0(label, "Kuvat") == 0) return SIDEBAR_ICON_PICTURES;
    if (g_strcmp0(label, "Videot") == 0) return SIDEBAR_ICON_VIDEOS;
    if (g_strcmp0(label, "Projektit") == 0) return SIDEBAR_ICON_PROJECTS;
    return SIDEBAR_ICON_PROJECTS;
}

static void add_place_full(App *app, const char *label, const char *path, bool is_mount) {
    if (!path || !g_file_test(path, G_FILE_TEST_IS_DIR)) {
        return;
    }
    GtkWidget *button = gtk_button_new();
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *icon = make_sidebar_icon(sidebar_icon_for_place(label, is_mount));
    GtkWidget *text = make_label(label, NULL, 0.0f);
    gtk_label_set_ellipsize(GTK_LABEL(text), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), text, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(button), row);
    gtk_widget_add_events(button, GDK_BUTTON_PRESS_MASK);
    g_object_set_data_full(G_OBJECT(button), "path", g_strdup(path), g_free);
    g_object_set_data_full(G_OBJECT(button), "place-name", g_strdup(label), g_free);
    g_object_set_data(G_OBJECT(button), "is-mount", GINT_TO_POINTER(is_mount));
    g_signal_connect(button, "clicked", G_CALLBACK(place_clicked), app);
    g_signal_connect(button, "event", G_CALLBACK(place_event), app);
    g_signal_connect(button, "button-press-event", G_CALLBACK(place_button_press), app);
    gtk_box_pack_start(GTK_BOX(app->places), button, FALSE, FALSE, 0);
}

static void add_place(App *app, const char *label, const char *path) {
    add_place_full(app, label, path, false);
}

static void add_place_section(App *app, const char *label) {
    GtkWidget *title = make_label(label, "sidebar-title", 0.0f);
    gtk_box_pack_start(GTK_BOX(app->places), title, FALSE, FALSE, 0);
}

static void add_mount_places(App *app) {
    GVolumeMonitor *monitor = g_volume_monitor_get();
    GList *mounts = g_volume_monitor_get_mounts(monitor);
    for (GList *l = mounts; l; l = l->next) {
        GMount *mount = l->data;
        GFile *root = g_mount_get_root(mount);
        char *path = g_file_get_path(root);
        char *name = g_mount_get_name(mount);
        if (path) {
            add_place_full(app, name ? name : path, path, true);
        }
        g_free(name);
        g_free(path);
        g_object_unref(root);
        g_object_unref(mount);
    }
    g_list_free(mounts);
    g_object_unref(monitor);
}

static GtkWidget *build_places(App *app) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(box, "sidebar");
    gtk_widget_set_size_request(box, 265, -1);
    app->places = box;
    char *downloads_fb = g_build_filename(g_get_home_dir(), "Downloads", NULL);
    char *pictures_fb = g_build_filename(g_get_home_dir(), "Pictures", NULL);
    char *videos_fb = g_build_filename(g_get_home_dir(), "Videos", NULL);
    char *documents_fb = g_build_filename(g_get_home_dir(), "Documents", NULL);
    char *music_fb = g_build_filename(g_get_home_dir(), "Music", NULL);
    char *desktop_fb = g_build_filename(g_get_home_dir(), "Desktop", NULL);
    char *projects_fb = g_build_filename(g_get_home_dir(), "Projects", NULL);
    char *downloads = user_dir_value("XDG_DOWNLOAD_DIR", downloads_fb);
    char *pictures = user_dir_value("XDG_PICTURES_DIR", pictures_fb);
    char *videos = user_dir_value("XDG_VIDEOS_DIR", videos_fb);
    char *documents = user_dir_value("XDG_DOCUMENTS_DIR", documents_fb);
    char *music = user_dir_value("XDG_MUSIC_DIR", music_fb);
    char *desktop = user_dir_value("XDG_DESKTOP_DIR", desktop_fb);
    char *projects = user_dir_value("XDG_PROJECTS_DIR", projects_fb);

    add_place_section(app, "SIJAINNIT");
    add_place(app, "Koti", g_get_home_dir());
    add_place(app, "Työpöytä", desktop);
    add_place(app, "Asiakirjat", documents);
    add_place(app, "Lataukset", downloads);
    add_place(app, "Musiikki", music);
    add_place(app, "Kuvat", pictures);
    add_place(app, "Videot", videos);
    add_place(app, "Projektit", projects);

    add_place_section(app, "LAITTEET");
    add_place_full(app, "Juuritiedostot", "/", true);
    add_mount_places(app);

    g_free(downloads);
    g_free(pictures);
    g_free(videos);
    g_free(documents);
    g_free(music);
    g_free(desktop);
    g_free(projects);
    g_free(downloads_fb);
    g_free(pictures_fb);
    g_free(videos_fb);
    g_free(documents_fb);
    g_free(music_fb);
    g_free(desktop_fb);
    g_free(projects_fb);
    return box;
}

static GtkWidget *build_toolbar(App *app) {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_name(bar, "toolbar");
    app->back_button = gtk_button_new_with_label("←");
    app->forward_button = gtk_button_new_with_label("→");
    app->up_button = gtk_button_new_with_label("↑");
    app->path_entry = gtk_entry_new();
    GtkWidget *refresh = gtk_button_new_with_label("⟳");
    app->view_button = gtk_button_new_with_label("▦");
    gtk_box_pack_start(GTK_BOX(bar), app->back_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), app->forward_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), app->up_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), app->path_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bar), app->view_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), refresh, FALSE, FALSE, 0);
    g_signal_connect(app->path_entry, "activate", G_CALLBACK(path_entry_activate), app);
    g_signal_connect_swapped(app->back_button, "clicked", G_CALLBACK(go_back), app);
    g_signal_connect_swapped(app->forward_button, "clicked", G_CALLBACK(go_forward), app);
    g_signal_connect_swapped(app->up_button, "clicked", G_CALLBACK(go_up), app);
    g_signal_connect(app->view_button, "clicked", G_CALLBACK(view_button_clicked), app);
    g_signal_connect_swapped(refresh, "clicked", G_CALLBACK(refresh_current), app);
    return bar;
}

static GtkWidget *build_status(App *app) {
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_name(bar, "statusbar");
    app->status_path = make_label("", "status-path", 0.0f);
    app->status_counts = make_label("", NULL, 0.0f);
    app->status_preview = make_label("preview: idle", NULL, 1.0f);
    gtk_box_pack_start(GTK_BOX(bar), app->status_path, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bar), app->status_counts, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), app->status_preview, FALSE, FALSE, 0);
    return bar;
}

static void app_init_ui(App *app, const char *start_path) {
    apply_css();
    app->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app->window), "File Manager");
    gtk_window_set_default_size(GTK_WINDOW(app->window), 1250, 900);
    g_signal_connect(app->window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(app->window, "key-press-event", G_CALLBACK(key_press), app);

    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_name(root, "root");
    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    GtkWidget *places = build_places(app);
    GtkWidget *main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    app->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);
    g_signal_connect(app->notebook, "switch-page", G_CALLBACK(notebook_switch_page), app);
    app->grid_scroller = gtk_scrolled_window_new(NULL, NULL);
    app->list_scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_add_events(app->grid_scroller, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    gtk_widget_add_events(app->list_scroller, GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    g_signal_connect(app->grid_scroller, "scroll-event", G_CALLBACK(file_area_scroll), app);
    g_signal_connect(app->list_scroller, "scroll-event", G_CALLBACK(file_area_scroll), app);
    app->grid = gtk_flow_box_new();
    gtk_widget_add_events(app->grid, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    gtk_widget_set_valign(app->grid, GTK_ALIGN_START);
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(app->grid), GTK_SELECTION_MULTIPLE);
    gtk_flow_box_set_activate_on_single_click(GTK_FLOW_BOX(app->grid), FALSE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(app->grid), FALSE);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(app->grid), 12);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(app->grid), 2);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(app->grid), 8);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(app->grid), 8);
    g_signal_connect(app->grid, "child-activated", G_CALLBACK(flow_child_activated), app);
    g_signal_connect(app->grid, "button-press-event", G_CALLBACK(blank_context_press), app);
    g_signal_connect(app->grid, "motion-notify-event", G_CALLBACK(file_area_motion), app);
    g_signal_connect(app->grid, "button-release-event", G_CALLBACK(file_area_release), app);
    g_signal_connect(app->grid, "scroll-event", G_CALLBACK(file_area_scroll), app);
    g_signal_connect_swapped(app->grid, "selected-children-changed", G_CALLBACK(update_status), app);
    app->list = gtk_list_box_new();
    gtk_widget_add_events(app->list, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK | GDK_SMOOTH_SCROLL_MASK);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app->list), GTK_SELECTION_MULTIPLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(app->list), FALSE);
    g_signal_connect(app->list, "row-activated", G_CALLBACK(list_row_activated), app);
    g_signal_connect(app->list, "button-press-event", G_CALLBACK(blank_context_press), app);
    g_signal_connect(app->list, "motion-notify-event", G_CALLBACK(file_area_motion), app);
    g_signal_connect(app->list, "button-release-event", G_CALLBACK(file_area_release), app);
    g_signal_connect(app->list, "scroll-event", G_CALLBACK(file_area_scroll), app);
    g_signal_connect_swapped(app->list, "selected-rows-changed", G_CALLBACK(update_status), app);
    gtk_container_add(GTK_CONTAINER(app->grid_scroller), app->grid);
    GtkWidget *list_frame = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(list_frame), build_list_header(), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(list_frame), app->list, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(app->list_scroller), list_frame);
    app->stack = gtk_stack_new();
    gtk_stack_add_named(GTK_STACK(app->stack), app->grid_scroller, "grid");
    gtk_stack_add_named(GTK_STACK(app->stack), app->list_scroller, "list");
    gtk_box_pack_start(GTK_BOX(main), app->notebook, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main), build_toolbar(app), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main), app->stack, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(body), places, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), main, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), body, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), build_status(app), FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(app->window), root);
    app_open_path(app, start_path, true);
    gtk_widget_show_all(app->window);
}

int main(int argc, char **argv) {
    int cli_status = handle_integration_cli(argc, argv);
    if (cli_status >= 0) {
        return cli_status;
    }
    gtk_init(&argc, &argv);
    gst_init(&argc, &argv);
    App app = {0};
    app.tabs = g_ptr_array_new_with_free_func(folder_tab_free);
    app.mode = VIEW_LIST;
    app.zoom = 1.0;
    thumb_cache_init(&app.thumbs);
    app.playbin = gst_element_factory_make("playbin", "hover-playbin");

    char *start = argc > 1 ? file_arg_to_path(argv[1]) : g_strdup(g_get_home_dir());
    app_init_ui(&app, start);
    g_free(start);
    gtk_main();

    app_stop_video(&app);
    if (app.playbin) {
        gst_element_set_state(app.playbin, GST_STATE_NULL);
        gst_object_unref(app.playbin);
    }
    g_ptr_array_unref(app.tabs);
    g_hash_table_destroy(app.thumbs.map);
    g_ptr_array_unref(app.thumbs.entries);
    return 0;
}
