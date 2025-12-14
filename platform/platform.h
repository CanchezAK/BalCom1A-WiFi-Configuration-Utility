#pragma once

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns newly allocated UTF-8 path to <exe_dir>/<filename>. Caller frees with g_free(). */
gchar *platform_build_path_next_to_exe(const char *filename);

/* Platform-native error message (Windows MessageBox, otherwise stderr). */
void platform_show_error(const char *title, const char *message);

#ifdef __cplusplus
}
#endif
