/*
  Temporary refactor bridge:
  - New codebase is split into modules (serial/platform/ui/protocol/etc).
*/

#include <gtk/gtk.h>
#include <string.h>

#include "app/app_state.h"
#include "app/app_known_networks.h"
#include "device/device_discovery.h"
#include "platform/platform.h"
#include "platform/serial.h"
#include "ui/ui.h"

/* GLib/GIO enum compatibility:
  - On MSYS2/Windows, G_APPLICATION_FLAGS_NONE is deprecated in favor of G_APPLICATION_DEFAULT_FLAGS.
  - On Ubuntu/WSL in this project, G_APPLICATION_DEFAULT_FLAGS was not available in headers.
  Use the most compatible flag per platform. */
#if defined(_WIN32)
#define APP_GAPPLICATION_FLAGS G_APPLICATION_DEFAULT_FLAGS
#else
#define APP_GAPPLICATION_FLAGS G_APPLICATION_FLAGS_NONE
#endif

int main(int argc, char **argv) {
  AppState st = {0};

  /* Force cairo renderer (avoid GPU/backends issues). Must be set before GTK init. */
  g_setenv("GSK_RENDERER", "cairo", TRUE);

#if defined(_WIN32)
  /* When shipped as a portable/installed folder (bundled GTK runtime), point GTK/GLib to our local data files. */
  {
    gchar *schemas_dir = platform_build_path_next_to_exe("share/glib-2.0/schemas");
    if (schemas_dir && g_file_test(schemas_dir, G_FILE_TEST_IS_DIR)) {
      g_setenv("GSETTINGS_SCHEMA_DIR", schemas_dir, TRUE);
    }
    g_free(schemas_dir);

    /* gdk-pixbuf uses loaders.cache to locate image loaders (PNG/JPEG/SVG, etc.). */
    gchar *pixbuf_cache = platform_build_path_next_to_exe("lib/gdk-pixbuf-2.0/2.10.0/loaders.cache");
    if (pixbuf_cache && g_file_test(pixbuf_cache, G_FILE_TEST_EXISTS)) {
      g_setenv("GDK_PIXBUF_MODULE_FILE", pixbuf_cache, TRUE);
    }
    g_free(pixbuf_cache);

    gchar *pixbuf_moddir = platform_build_path_next_to_exe("lib/gdk-pixbuf-2.0/2.10.0/loaders");
    if (pixbuf_moddir && g_file_test(pixbuf_moddir, G_FILE_TEST_IS_DIR)) {
      g_setenv("GDK_PIXBUF_MODULEDIR", pixbuf_moddir, TRUE);
    }
    g_free(pixbuf_moddir);
  }
#endif

  g_app_state = &st;

  st.keep_running_without_device = FALSE;

  /* Parse our custom flag early, and remove it from argv before GTK parses options. */
  char **argv_filtered = g_new0(char*, (gsize)argc + 1);
  int argc_filtered = 0;
  argv_filtered[argc_filtered++] = argv[0];
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--debug") == 0) {
      st.keep_running_without_device = TRUE;
      continue;
    }
    argv_filtered[argc_filtered++] = argv[i];
  }
  argv_filtered[argc_filtered] = NULL;

  /* Load persisted known SSID/password pairs (used for auto-fill). */
  (void)app_known_networks_load(&st);

  /* 1) First: search for device (blocking). */
  gboolean found = device_discovery_scan_for_device(&st);

  /* 2) Then: decide what UI to show. */
  if (!found && !st.keep_running_without_device) {
    if (st.device_port_busy && st.busy_port[0]) {
      char msg[256];
      g_snprintf(msg, sizeof(msg),
                 "Cannot open %s (access denied).\n\nClose any serial monitor/terminal using this port and restart the application.",
                 st.busy_port);
      platform_show_error("Serial port busy", msg);
    } else {
      platform_show_error("Device not connected", "Device not connected. Please connect the device and restart the application.");
    }
    g_free(argv_filtered);
    g_app_state = NULL;
    return 1;
  }

  GtkApplication *app = gtk_application_new("com.balcom.balcom1a.configuration_utility", APP_GAPPLICATION_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(ui_app_activate), &st);

  int status = g_application_run(G_APPLICATION(app), argc_filtered, argv_filtered);
  g_free(argv_filtered);

  /* Extra safety: ui.c already closes on window close/destroy. */
  if (st.device) {
    serial_close(st.device);
    st.device = NULL;
  }

  if (st.ssidStore) {
    g_object_unref(st.ssidStore);
  }
  if (st.ssidSecurity) {
    g_hash_table_destroy(st.ssidSecurity);
    st.ssidSecurity = NULL;
  }
  if (st.knownPasswords) {
    g_hash_table_destroy(st.knownPasswords);
    st.knownPasswords = NULL;
  }
  if (st.builder) {
    g_object_unref(st.builder);
  }
  g_object_unref(app);

  g_app_state = NULL;
  return status;
}