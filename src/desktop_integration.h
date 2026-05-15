#ifndef FILEMANAGER_DESKTOP_INTEGRATION_H
#define FILEMANAGER_DESKTOP_INTEGRATION_H

#include <glib.h>

gboolean fm_install_user_default_file_manager(const char *binary_path, GError **error);

#endif
