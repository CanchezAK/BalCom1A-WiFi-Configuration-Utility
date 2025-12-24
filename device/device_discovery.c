#include "device/device_discovery.h"

#include "platform/platform.h"
#include "platform/serial.h"

#include <string.h>

#define PROBE_REQUEST "FirmVersion"
#define EXPECTED_REPLY_NORM "version4.1.6uart"

#define PROBE_READ_TOTAL_TIMEOUT_MS 1000
#define PROBE_MAX_REPLY_BYTES 512

static void set_display_port(char out[32], const char *path) {
  if (!out) {
    return;
  }
  out[0] = '\0';
  if (!path) {
    return;
  }

  if (strlen(path) < 31) {
    g_strlcpy(out, path, 32);
    return;
  }

  const char *base = strrchr(path, '/');
  base = base ? (base + 1) : path;
  g_strlcpy(out, base, 32);
}

static gboolean probe_port_for_device(const char *port_name,
                                     SerialPort **out_port,
                                     gboolean *out_loopback,
                                     gboolean debug_log) {
  if (out_port) {
    *out_port = NULL;
  }
  if (out_loopback) {
    *out_loopback = FALSE;
  }

  SerialPort *p = serial_open(port_name);
  if (!p) {
    return FALSE;
  }

  if (!serial_write_all(p, PROBE_REQUEST)) {
    serial_close(p);
    return FALSE;
  }

  char buf[PROBE_MAX_REPLY_BYTES + 1];
  char norm[PROBE_MAX_REPLY_BYTES + 1];
  size_t total = 0;

  gint64 start = g_get_monotonic_time();
  while ((g_get_monotonic_time() - start) < (gint64)PROBE_READ_TOTAL_TIMEOUT_MS * 1000) {
    char chunk[128];
    size_t got = serial_read_available(p, chunk, sizeof(chunk));
    if (got == 0) {
      continue;
    }

    size_t can_copy = got;
    if (total + can_copy > PROBE_MAX_REPLY_BYTES) {
      can_copy = PROBE_MAX_REPLY_BYTES - total;
    }
    memcpy(buf + total, chunk, can_copy);
    total += can_copy;
    buf[total] = '\0';

    /* Normalize: lowercase and remove whitespace so we can match reliably. */
    size_t j = 0;
    for (size_t i = 0; i < total && j < PROBE_MAX_REPLY_BYTES; i++) {
      unsigned char c = (unsigned char)buf[i];
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
        continue;
      }
      if (c >= 'A' && c <= 'Z') {
        c = (unsigned char)(c - 'A' + 'a');
      }
      norm[j++] = (char)c;
    }
    norm[j] = '\0';

    if (strstr(norm, EXPECTED_REPLY_NORM) != NULL) {
      if (out_port) {
        *out_port = p;
      } else {
        serial_close(p);
      }
      return TRUE;
    }

    if (out_loopback && strstr(buf, PROBE_REQUEST) != NULL) {
      *out_loopback = TRUE;
    }

    if (debug_log) {
      char preview[65];
      size_t n = total < 64 ? total : 64;
      for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];
        preview[i] = (c >= 32 && c < 127) ? (char)c : '.';
      }
      preview[n] = '\0';
      g_print("[DBG] RX %s: %u bytes, preview='%s'\n", port_name, (unsigned)total, preview);
    }

    if (total >= PROBE_MAX_REPLY_BYTES) {
      break;
    }
  }

  serial_close(p);
  return FALSE;
}

gboolean device_discovery_scan_for_device(AppState *st) {
  if (!st) {
    return FALSE;
  }

  st->device_port[0] = '\0';
  st->device_port_path[0] = '\0';
  st->device_port_busy = FALSE;
  st->busy_port[0] = '\0';
  st->loopback_available = FALSE;
  st->loopback_port[0] = '\0';
  st->pending_connect_sta = FALSE;
  st->pending_ssid[0] = '\0';
  if (st->ssidSecurity) {
    g_hash_table_remove_all(st->ssidSecurity);
  }
  if (st->device) {
    serial_close(st->device);
    st->device = NULL;
  }

  GPtrArray *ports = platform_enumerate_serial_ports();

  if (st->keep_running_without_device) {
    DBG_LOG(st, "Serial ports found: %u\n", ports ? ports->len : 0);
    if (ports) {
      for (guint i = 0; i < ports->len; i++) {
        DBG_LOG(st, "  %s\n", (const char *)g_ptr_array_index(ports, i));
      }
    }
  }

  gboolean loopback_reported = FALSE;

  if (ports) {
    for (guint i = 0; i < ports->len; i++) {
      const char *port = (const char *)g_ptr_array_index(ports, i);
      if (!port) {
        continue;
      }

      DBG_LOG(st, "Probing %s...\n", port);

      SerialPort *found = NULL;
      gboolean loopback = FALSE;
      if (probe_port_for_device(port, &found, &loopback, st->keep_running_without_device)) {
        set_display_port(st->device_port, port);
        g_strlcpy(st->device_port_path, port, sizeof(st->device_port_path));
        st->device = found;
        DBG_LOG(st, "Device found on %s\n", st->device_port);
        g_ptr_array_free(ports, TRUE);
        return TRUE;
      }

      if (!st->device_port_busy && serial_last_open_was_access_denied()) {
        st->device_port_busy = TRUE;
        set_display_port(st->busy_port, port);
      }

      if (loopback && !loopback_reported) {
        loopback_reported = TRUE;
        st->loopback_available = TRUE;
        set_display_port(st->loopback_port, port);
        DBG_LOG(st, "Loopback device found\n");
      }
    }

    g_ptr_array_free(ports, TRUE);
  }

  return FALSE;
}
