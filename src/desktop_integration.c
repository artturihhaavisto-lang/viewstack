#include "desktop_integration.h"

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <errno.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

static gboolean write_text_file(const char *path, const char *content, GError **error) {
    char *dir = g_path_get_dirname(path);
    if (g_mkdir_with_parents(dir, 0755) != 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno), "Could not create %s", dir);
        g_free(dir);
        return FALSE;
    }
    g_free(dir);
    return g_file_set_contents(path, content, -1, error);
}

static gboolean set_default_for_type(const char *content_type, GError **error) {
    GDesktopAppInfo *info = g_desktop_app_info_new("filemanager.desktop");
    if (!info) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "filemanager.desktop was not found after writing it");
        return FALSE;
    }
    gboolean ok = g_app_info_set_as_default_for_type(G_APP_INFO(info), content_type, error);
    g_object_unref(info);
    return ok;
}

gboolean fm_install_user_default_file_manager(const char *binary_path, GError **error) {
    const char *home = g_get_home_dir();
    char *applications_dir = g_build_filename(home, ".local", "share", "applications", NULL);
    char *icons_dir = g_build_filename(home, ".local", "share", "icons", "hicolor", "scalable", "apps", NULL);
    char *desktop_path = g_build_filename(applications_dir, "filemanager.desktop", NULL);
    char *icon_path = g_build_filename(icons_dir, "filemanager.svg", NULL);
    char *quoted_binary = g_shell_quote(binary_path);
    char *desktop = g_strdup_printf(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=File Manager\n"
        "GenericName=File Manager\n"
        "Comment=Sharp native file manager with media previews\n"
        "Exec=%s %%U\n"
        "Icon=filemanager\n"
        "Terminal=false\n"
        "StartupNotify=true\n"
        "Categories=System;FileTools;FileManager;Utility;Core;GTK;\n"
        "MimeType=inode/directory;x-scheme-handler/file;\n",
        quoted_binary
    );
    const char *icon =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\">"
        "<rect width=\"128\" height=\"128\" fill=\"#000\"/>"
        "<path d=\"M18 37h32l9 12h51v48H18z\" fill=\"none\" stroke=\"#f2f2f2\" stroke-width=\"7\" stroke-linejoin=\"miter\"/>"
        "<path d=\"M18 53h92\" stroke=\"#f2f2f2\" stroke-width=\"7\"/>"
        "<path d=\"M34 71h60M34 84h44\" stroke=\"#bdbdbd\" stroke-width=\"6\" stroke-linecap=\"square\"/>"
        "</svg>\n";

    gboolean ok = write_text_file(desktop_path, desktop, error);
    if (ok) {
        g_chmod(desktop_path, 0644);
        ok = write_text_file(icon_path, icon, error);
    }
    if (ok) {
        g_chmod(icon_path, 0644);
        ok = set_default_for_type("inode/directory", error);
    }
    if (ok) {
        GError *scheme_error = NULL;
        if (!set_default_for_type("x-scheme-handler/file", &scheme_error)) {
            g_clear_error(&scheme_error);
        }
    }

    g_free(quoted_binary);
    g_free(desktop);
    g_free(icon_path);
    g_free(desktop_path);
    g_free(icons_dir);
    g_free(applications_dir);
    return ok;
}
