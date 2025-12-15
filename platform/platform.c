#include "platform/platform.h"

#include <glib.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

gchar *platform_build_path_next_to_exe(const char *filename) {
  if (!filename) {
    return NULL;
  }

  wchar_t module_path[MAX_PATH];
  DWORD len = GetModuleFileNameW(NULL, module_path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return NULL;
  }

  for (DWORD i = len; i > 0; i--) {
    if (module_path[i - 1] == L'\\' || module_path[i - 1] == L'/') {
      module_path[i - 1] = L'\0';
      break;
    }
  }

  gchar *dir_utf8 = g_utf16_to_utf8((const gunichar2*)module_path, -1, NULL, NULL, NULL);
  if (!dir_utf8) {
    return NULL;
  }

  gchar *path = g_build_filename(dir_utf8, filename, NULL);
  g_free(dir_utf8);
  return path;
}

void platform_show_error(const char *title, const char *message) {
  MessageBoxA(NULL,
              message ? message : "",
              title ? title : "Error",
              MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
}

#elif defined(__linux__)

#include <unistd.h>

gchar *platform_build_path_next_to_exe(const char *filename) {
  if (!filename) {
    return NULL;
  }

  /* /proc/self/exe is available on modern Linux and points to the running binary. */
  gchar *exe_path = g_file_read_link("/proc/self/exe", NULL);
  if (!exe_path) {
    return NULL;
  }

  gchar *dir = g_path_get_dirname(exe_path);
  g_free(exe_path);
  if (!dir) {
    return NULL;
  }

  gchar *path = g_build_filename(dir, filename, NULL);
  g_free(dir);
  return path;
}

void platform_show_error(const char *title, const char *message) {
  /* Keep it simple and always available: stderr. */
  g_printerr("%s: %s\n", title ? title : "Error", message ? message : "");
}

#else

gchar *platform_build_path_next_to_exe(const char *filename) {
  (void)filename;
  return NULL;
}

void platform_show_error(const char *title, const char *message) {
  g_printerr("%s: %s\n", title ? title : "Error", message ? message : "");
}

#endif
