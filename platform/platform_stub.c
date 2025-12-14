#include "platform/platform.h"

#include <gtk/gtk.h>

gchar *platform_build_path_next_to_exe(const char *filename) {
  (void)filename;
  return NULL;
}

void platform_show_error(const char *title, const char *message) {
  g_printerr("%s: %s\n", title ? title : "Error", message ? message : "");
}
