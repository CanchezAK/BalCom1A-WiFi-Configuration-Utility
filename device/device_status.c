#include "device/device_status.h"

#include "device/device_protocol.h"
#include "serial/serial.h"
#include "ui/ui.h"
#include "util/util.h"

#include <string.h>

static gboolean str_contains_ci(const char *haystack, const char *needle) {
  if (!haystack || !needle || needle[0] == '\0') {
    return FALSE;
  }
  size_t nlen = strlen(needle);
  for (const char *p = haystack; *p; p++) {
    size_t i = 0;
    for (; i < nlen; i++) {
      char a = p[i];
      char b = needle[i];
      if (a == '\0') {
        return FALSE;
      }
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
      if (a != b) {
        break;
      }
    }
    if (i == nlen) {
      return TRUE;
    }
  }
  return FALSE;
}

static gboolean extract_ipv4_from_line(const char *line, char out_ip[32]) {
  if (!line || !out_ip) {
    return FALSE;
  }

  for (const char *p = line; *p; p++) {
    if (*p < '0' || *p > '9') {
      continue;
    }

    const char *q = p;
    int parts[4] = {0, 0, 0, 0};
    gboolean ok = TRUE;

    for (int i = 0; i < 4; i++) {
      if (*q < '0' || *q > '9') {
        ok = FALSE;
        break;
      }
      int v = 0;
      int digits = 0;
      while (*q >= '0' && *q <= '9') {
        v = v * 10 + (*q - '0');
        q++;
        digits++;
        if (digits > 3) {
          ok = FALSE;
          break;
        }
      }
      if (!ok || v < 0 || v > 255) {
        ok = FALSE;
        break;
      }
      parts[i] = v;
      if (i < 3) {
        if (*q != '.') {
          ok = FALSE;
          break;
        }
        q++;
      }
    }

    if (ok) {
      g_snprintf(out_ip, 32, "%d.%d.%d.%d", parts[0], parts[1], parts[2], parts[3]);
      return TRUE;
    }
  }
  return FALSE;
}

static void net_set_connecting_label(AppState *st) {
  if (!st) {
    return;
  }
  int dots = (st->anim_dots % 3) + 1;
  char msg[32];
  g_snprintf(msg, sizeof(msg), "connecting%.*s", dots, "...");
  ui_set_status(st, msg);
}

static void net_set_state(AppState *st, int new_state, const char *ip_or_null) {
  if (!st) {
    return;
  }
  st->net_state = new_state;

  if (ip_or_null && ip_or_null[0] != '\0') {
    g_strlcpy(st->last_ip, ip_or_null, sizeof(st->last_ip));
  }

  switch (st->net_state) {
    case NET_STATE_CONNECTING_STA:
    case NET_STATE_CONNECTING_AP:
      st->last_ip[0] = '\0';
      st->anim_dots = 2;
      st->last_anim_us = 0;
      st->last_get_ip_us = 0;
      net_set_connecting_label(st);
      break;
    case NET_STATE_CONNECTED_STA: {
      char msg[64];
      g_snprintf(msg, sizeof(msg), "STA mode, IP addr %s", st->last_ip[0] ? st->last_ip : "?");
      ui_set_status(st, msg);
      break;
    }
    case NET_STATE_AP_MODE: {
      char msg[64];
      g_snprintf(msg, sizeof(msg), "AP mode, IP addr %s", st->last_ip[0] ? st->last_ip : "?");
      ui_set_status(st, msg);
      break;
    }
    case NET_STATE_FAILED_RETURN_AP:
      ui_set_status(st, "connect failed, return to AP mode");
      st->last_get_ip_us = 0;
      break;
    case NET_STATE_IDLE:
    default:
      break;
  }
}

static gboolean is_ap_ip(const char *ip) {
  return ip && strcmp(ip, "192.168.4.1") == 0;
}

static void device_process_status_line(AppState *st, const char *line) {
  if (!st || !line) {
    return;
  }
  char tmp_ip[32];

  if (str_contains_ci(line, "connect failed")) {
    net_set_state(st, NET_STATE_FAILED_RETURN_AP, NULL);
    return;
  }

  if (str_contains_ci(line, "ap mode")) {
    if (extract_ipv4_from_line(line, tmp_ip)) {
      net_set_state(st, NET_STATE_AP_MODE, tmp_ip);
    } else {
      if (st->net_state != NET_STATE_AP_MODE) {
        net_set_state(st, NET_STATE_CONNECTING_AP, NULL);
      }
    }
    return;
  }

  if (str_contains_ci(line, "connected")) {
    if (extract_ipv4_from_line(line, tmp_ip)) {
      net_set_state(st, is_ap_ip(tmp_ip) ? NET_STATE_AP_MODE : NET_STATE_CONNECTED_STA, tmp_ip);
    } else {
      if (st->net_state != NET_STATE_CONNECTED_STA) {
        net_set_state(st, NET_STATE_CONNECTING_STA, NULL);
      }
    }
    return;
  }

  if (extract_ipv4_from_line(line, tmp_ip)) {
    /* get_ip may return a bare IP line; infer mode by IP value.
       Also allow updating from IDLE so startup get_ip sets the label. */
    net_set_state(st, is_ap_ip(tmp_ip) ? NET_STATE_AP_MODE : NET_STATE_CONNECTED_STA, tmp_ip);
    return;
  }
}

static void device_drain_status_input(AppState *st) {
  if (!st || !st->device) {
    return;
  }

  for (;;) {
    char chunk[256];
    size_t got = serial_read_available(st->device, chunk, sizeof(chunk));
    if (got == 0) {
      break;
    }

    if (st->rx_len + got >= sizeof(st->rx_buf)) {
      st->rx_len = 0;
      st->rx_buf[0] = '\0';
    }
    memcpy(st->rx_buf + st->rx_len, chunk, got);
    st->rx_len += got;
    st->rx_buf[st->rx_len] = '\0';

    for (;;) {
      char *nl = strchr(st->rx_buf, '\n');
      char *cr = strchr(st->rx_buf, '\r');
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
      char *line = trim_ascii_inplace(st->rx_buf);
      if (line[0] != '\0') {
        DBG_LOG(st, "[DEV] RX '%s'\n", line);
        device_process_status_line(st, line);
      }
      char *rest = sep + 1;
      size_t rest_len = strlen(rest);
      memmove(st->rx_buf, rest, rest_len + 1);
      st->rx_len = rest_len;
    }
  }
}

static gboolean device_status_tick_cb(gpointer user_data) {
  AppState *st = (AppState*)user_data;
  if (!device_mode_active(st)) {
    if (st) {
      st->status_tick_id = 0;
    }
    return G_SOURCE_REMOVE;
  }

  device_drain_status_input(st);

  gint64 now = g_get_monotonic_time();

  if (st->net_state == NET_STATE_CONNECTING_STA ||
      st->net_state == NET_STATE_CONNECTING_AP ||
      st->net_state == NET_STATE_FAILED_RETURN_AP) {
    if (st->last_get_ip_us == 0 || (now - st->last_get_ip_us) > 800000) {
      (void)device_send_command(st, "get_ip");
      st->last_get_ip_us = now;
    }
  }

  if (st->net_state == NET_STATE_CONNECTING_STA || st->net_state == NET_STATE_CONNECTING_AP) {
    if (st->last_anim_us == 0 || (now - st->last_anim_us) > 250000) {
      st->anim_dots = (st->anim_dots + 1) % 3;
      st->last_anim_us = now;
      net_set_connecting_label(st);
    }
  }

  return G_SOURCE_CONTINUE;
}

void device_status_monitor_start(AppState *st) {
  if (!st || st->status_tick_id != 0 || !device_mode_active(st)) {
    return;
  }
  st->rx_len = 0;
  st->rx_buf[0] = '\0';
  st->last_ip[0] = '\0';
  st->net_state = NET_STATE_IDLE;
  st->anim_dots = 2;
  st->last_anim_us = 0;
  st->last_get_ip_us = 0;

  /* Startup: immediately query current IP and show AP/STA mode + IP.
     If the reply is slightly delayed, we poll briefly before showing the window. */
  ui_set_status(st, "STA mode, IP addr ?");
  (void)device_send_command(st, "get_ip");
  for (int i = 0; i < 20; i++) { /* ~200ms total */
    device_drain_status_input(st);
    if (st->net_state == NET_STATE_AP_MODE || st->net_state == NET_STATE_CONNECTED_STA) {
      break;
    }
    g_usleep(10000);
  }

  st->status_tick_id = g_timeout_add(100, device_status_tick_cb, st);
}

void device_status_monitor_stop(AppState *st) {
  if (!st || st->status_tick_id == 0) {
    return;
  }
  g_source_remove(st->status_tick_id);
  st->status_tick_id = 0;
}

void device_status_set_connecting_sta(AppState *st) {
  net_set_state(st, NET_STATE_CONNECTING_STA, NULL);
}

void device_status_set_connecting_ap(AppState *st) {
  net_set_state(st, NET_STATE_CONNECTING_AP, NULL);
}
