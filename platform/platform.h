#pragma once

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Returns newly allocated UTF-8 path to <exe_dir>/<filename>. Caller frees with g_free(). */
gchar *platform_build_path_next_to_exe(const char *filename);

/* Platform-native error message (Windows MessageBox, otherwise stderr). */
void platform_show_error(const char *title, const char *message);

/* Enumerate candidate serial ports.
	Returns a GPtrArray of UTF-8 strings (char*) with g_free as free func.
	Caller frees with g_ptr_array_free(arr, TRUE).
	On Windows entries look like "COM3". On Linux entries look like "/dev/ttyACM0" or "/dev/serial/by-id/...". */
GPtrArray *platform_enumerate_serial_ports(void);

#ifdef __cplusplus
}
#endif
