#pragma once

#include <gtk/gtk.h>
#include <stdbool.h>

#include "platform/serial.h"

typedef struct AppState {
  GtkApplication *app;
  GtkBuilder *builder;

  GtkWindow *window1;
  GtkWindow *pwdDialog;
  GtkEntry  *textBox0;
  GtkLabel  *monitor0;

  GtkDropDown *ssidStruct;
  GListStore  *ssidStore;     /* owned ref; GtkStringObject items */
  GHashTable  *ssidSecurity;  /* key: ssid (utf8); value: GINT_TO_POINTER(0=open,1=pwd) */

  /* Firmware upgrade UI */
  GtkWindow *firmwareUpgradeWindow;
  GtkLabel  *firmwareUpgradeLabel;
  GtkProgressBar *firmwareUpgradeProgress;
  GtkButton *firmwareUpgradeOk;

  /* Firmware upgrade state */
  gboolean firmware_upgrade_running;
  gpointer firmware_upgrade_ctx; /* owned by firmware module */

  /*
   * Serial port identifiers:
   * - device_port_path: full system path used for opening the port and for esptool
   *   (e.g. "COM3" on Windows, "/dev/serial/by-id/..." on Linux).
   * - device_port: short label shown in the UI (may be truncated).
   */
  char device_port_path[256];
  char device_port[32];
  SerialPort *device;

  gboolean device_port_busy;
  char busy_port[32];

  gboolean loopback_available;
  char loopback_port[32];

  gboolean pending_connect_sta;
  char pending_ssid[128];

  /* Known networks remembered by the app (in-memory): SSID -> password. */
  GHashTable *knownPasswords; /* key: ssid (utf8); value: password (utf8) */

  /* Track last connect_sta attempt for save/evict logic. */
  gboolean last_connect_was_sta_attempt;
  gboolean last_connect_pwd_was_saved;
  gboolean last_connect_should_save_pwd;
  char last_connect_ssid[128];
  char last_connect_pwd[128];

  gboolean keep_running_without_device; /* --debug */

  /* Device network status monitoring (device mode only). */
  guint status_tick_id;
  gint64 last_anim_us;
  gint64 last_get_ip_us;
  int anim_dots;
  char last_ip[32];

  char rx_buf[2048];
  size_t rx_len;

  enum {
    NET_STATE_IDLE = 0,
    NET_STATE_CONNECTING_STA,
    NET_STATE_CONNECTING_AP,
    NET_STATE_CONNECTED_STA,
    NET_STATE_AP_MODE,
    NET_STATE_FAILED_RETURN_AP,
  } net_state;
} AppState;

extern AppState *g_app_state;

static inline AppState *resolve_state(gpointer user_data) {
  return user_data ? (AppState*)user_data : g_app_state;
}

#define DBG_LOG(st, fmt, ...) \
  do { \
    if ((st) && (st)->keep_running_without_device) { \
      g_print((fmt), ##__VA_ARGS__); \
    } \
  } while (0)

static inline gboolean loopback_mode_active(const AppState *st) {
  return st && st->keep_running_without_device && st->loopback_available && st->device == NULL;
}

static inline gboolean device_mode_active(const AppState *st) {
  return st && st->device != NULL;
}
