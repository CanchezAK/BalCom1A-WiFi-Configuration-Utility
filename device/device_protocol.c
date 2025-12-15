#include "device/device_protocol.h"

#include "platform/serial.h"
#include "ui/ui.h"
#include "util/util.h"

#include <string.h>

static void clear_ssid_store(AppState *st) {
  if (!st || !st->ssidStore) {
    return;
  }
  while (g_list_model_get_n_items(G_LIST_MODEL(st->ssidStore)) > 0) {
    g_list_store_remove(st->ssidStore, 0);
  }
}

gboolean device_send_command(AppState *st, const char *cmd) {
  if (!st || !st->device || !cmd) {
    return FALSE;
  }
  return serial_write_all(st->device, cmd) ? TRUE : FALSE;
}

gboolean device_scan_networks_and_update_dropdown(AppState *st) {
  if (!device_mode_active(st) || !st->ssidStore) {
    return FALSE;
  }
  if (!st->ssidSecurity) {
    st->ssidSecurity = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  } else {
    g_hash_table_remove_all(st->ssidSecurity);
  }

  serial_purge(st->device);
  if (!device_send_command(st, "scan_Networks")) {
    ui_set_status(st, "Failed to send scan_Networks");
    return FALSE;
  }

  ui_set_status(st, "Scanning networks...");
  clear_ssid_store(st);

  char rx[2048];
  size_t rx_len = 0;
  const gint64 total_timeout_us = 5000 * 1000;
  gint64 start = g_get_monotonic_time();
  guint added = 0;

  while ((g_get_monotonic_time() - start) < total_timeout_us) {
    char chunk[128];
    size_t got = serial_read_available(st->device, chunk, sizeof(chunk));
    if (got == 0) {
      continue;
    }

    size_t can_copy = got;
    if (rx_len + can_copy >= sizeof(rx)) {
      can_copy = sizeof(rx) - 1 - rx_len;
    }
    if (can_copy == 0) {
      break;
    }

    memcpy(rx + rx_len, chunk, can_copy);
    rx_len += can_copy;
    rx[rx_len] = '\0';

    for (;;) {
      char *nl = strchr(rx, '\n');
      char *cr = strchr(rx, '\r');
      char *sep = NULL;
      if (nl && cr) {
        sep = (nl < cr) ? nl : cr;
      } else if (nl) {
        sep = nl;
      } else if (cr) {
        sep = cr;
      }

      if (!sep) {
        break;
      }

      *sep = '\0';
      char *line = trim_ascii_inplace(rx);

      if (line[0] != '\0') {
        if (equals_token_ci(line, "complete")) {
          char *rest = sep + 1;
          size_t rest_len = strlen(rest);
          memmove(rx, rest, rest_len + 1);
          rx_len = rest_len;

          if (added == 0) {
            GtkStringObject *s0 = gtk_string_object_new("No SSIDs yet");
            g_list_store_append(st->ssidStore, s0);
            g_object_unref(s0);
            ui_set_status(st, "Scan complete: 0 networks");
          } else {
            char msg[128];
            g_snprintf(msg, sizeof(msg), "Scan complete: %u networks", added);
            ui_set_status(st, msg);
          }
          gtk_drop_down_set_selected(st->ssidStruct, 0);
          return TRUE;
        }

        char *comma = strchr(line, ',');
        if (comma) {
          *comma = '\0';
          char *ssid = trim_ascii_inplace(line);
          char *sec = trim_ascii_inplace(comma + 1);

          gboolean needs_pwd = FALSE;
          if (equals_token_ci(sec, "pwd") || equals_token_ci(sec, "password")) {
            needs_pwd = TRUE;
          } else if (equals_token_ci(sec, "open")) {
            needs_pwd = FALSE;
          }

          if (ssid[0] != '\0') {
            GtkStringObject *s = gtk_string_object_new(ssid);
            g_list_store_append(st->ssidStore, s);
            g_object_unref(s);
            g_hash_table_replace(st->ssidSecurity, g_strdup(ssid), GINT_TO_POINTER(needs_pwd ? 1 : 0));
            added++;
          }
        }
      }

      char *rest = sep + 1;
      size_t rest_len = strlen(rest);
      memmove(rx, rest, rest_len + 1);
      rx_len = rest_len;
    }
  }

  if (added > 0) {
    gtk_drop_down_set_selected(st->ssidStruct, 0);
    {
      char msg[128];
      g_snprintf(msg, sizeof(msg), "Scan timeout: %u networks", added);
      ui_set_status(st, msg);
    }
    return TRUE;
  }

  GtkStringObject *s0 = gtk_string_object_new("No SSIDs yet");
  g_list_store_append(st->ssidStore, s0);
  g_object_unref(s0);
  gtk_drop_down_set_selected(st->ssidStruct, 0);
  ui_set_status(st, "Scan timeout: 0 networks");
  return FALSE;
}
