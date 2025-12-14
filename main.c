/*
  Temporary refactor bridge:
  - New codebase is split into modules (serial/platform/ui/protocol/etc).
*/

#include <gtk/gtk.h>
#include <string.h>

#include "app/app_state.h"
#include "device/device_discovery.h"
#include "platform/platform.h"
#include "serial/serial.h"
#include "ui/ui.h"

int main(int argc, char **argv) {
  AppState st = {0};

  /* Force cairo renderer (avoid GPU/backends issues). Must be set before GTK init. */
  g_setenv("GSK_RENDERER", "cairo", TRUE);

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

  GtkApplication *app = gtk_application_new("local.gtk.serial.scan", G_APPLICATION_DEFAULT_FLAGS);
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
  if (st.builder) {
    g_object_unref(st.builder);
  }
  g_object_unref(app);

  g_app_state = NULL;
  return status;
}