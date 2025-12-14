#if 0

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static gchar *get_ui_file_path_next_to_exe(void) {
  wchar_t module_path[MAX_PATH];
  DWORD len = GetModuleFileNameW(NULL, module_path, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return NULL;
  }

  /* Strip filename to get directory */
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

  gchar *path = g_build_filename(dir_utf8, UI_FILE, NULL);
  g_free(dir_utf8);
  return path;
}

#define PROBE_REQUEST "FirmVersion"
/* Expected device reply (as observed in terminal logs). */
#define EXPECTED_REPLY "version 4.1.6uart"
/* Normalized form used for matching (whitespace removed, lowercased). */
#define EXPECTED_REPLY_NORM "version4.1.6uart"

/* Serial settings (adjust to your actual device) */
#define SERIAL_BAUD CBR_115200
#define SERIAL_DATABITS 8
#define SERIAL_PARITY NOPARITY
#define SERIAL_STOPBITS ONESTOPBIT

#define PROBE_READ_TOTAL_TIMEOUT_MS 1000
#define PROBE_READ_SLICE_TIMEOUT_MS 50
#define PROBE_MAX_REPLY_BYTES 512

/* Forward decls (used by loopback command helpers) */
static HANDLE open_serial_port(const char *com_name);

typedef struct AppState {
  GtkApplication *app;
  GtkBuilder *builder;

  GtkWindow *window1;
  GtkWindow *pwdDialog;
  GtkEntry  *textBox0;
  GtkLabel  *monitor0;

  GtkDropDown   *ssidStruct;
  GListStore    *ssidStore; /* owned ref; GtkStringObject items */
  GHashTable    *ssidSecurity; /* key: ssid (utf8); value: GINT_TO_POINTER(0=open,1=pwd) */

  char device_port[32]; /* "COMx" */
  HANDLE device_handle; /* kept open for entire session; INVALID_HANDLE_VALUE if none */

  gboolean device_port_busy;
  char busy_port[32]; /* first COM port that failed with ACCESS_DENIED */

  gboolean loopback_available;
  char loopback_port[32]; /* COM port where PROBE_REQUEST was echoed */

  gboolean pending_connect_sta;
  char pending_ssid[128];

  gboolean keep_running_without_device; /* debug flag */

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

static DWORD g_last_open_serial_error = 0;

static gboolean loopback_mode_active(const AppState *st) {
  return st && st->keep_running_without_device && st->loopback_available && st->device_handle == INVALID_HANDLE_VALUE;
}

static const char *get_selected_ssid(AppState *st) {
  if (!st || !st->ssidStruct) {
    return NULL;
  }
  GObject *item = gtk_drop_down_get_selected_item(st->ssidStruct);
  if (item && GTK_IS_STRING_OBJECT(item)) {
    return gtk_string_object_get_string(GTK_STRING_OBJECT(item));
  }
  return NULL;
}

static gboolean ssid_requires_password(const char *ssid) {
  if (!ssid) {
    return FALSE;
  }
  /* Prefer device-provided security info when available. */
  if (g_app_state && g_app_state->ssidSecurity) {
    gpointer v = g_hash_table_lookup(g_app_state->ssidSecurity, ssid);
    if (v != NULL) {
      return GPOINTER_TO_INT(v) != 0;
    }
  }

  /* Debug test behavior fallback (loopback mode). */
  return (strcmp(ssid, "Test SSID 1") == 0);
}

static void set_status(AppState *st, const char *text) {
  if (!st || !st->monitor0) {
    return;
  }
  gtk_label_set_text(st->monitor0, text ? text : "");
}

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
  int dots = (st->anim_dots % 3) + 1; /* 1..3 */
  char msg[32];
  g_snprintf(msg, sizeof(msg), "connecting%.*s", dots, "...");
  set_status(st, msg);
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
      st->anim_dots = 2; /* start with ... */
      st->last_anim_us = 0;
      st->last_get_ip_us = 0;
      net_set_connecting_label(st);
      break;
    case NET_STATE_CONNECTED_STA: {
      char msg[64];
      g_snprintf(msg, sizeof(msg), "connected, IP addr %s", st->last_ip[0] ? st->last_ip : "?");
      set_status(st, msg);
      break;
    }
    case NET_STATE_AP_MODE: {
      char msg[64];
      g_snprintf(msg, sizeof(msg), "AP mode, IP addr %s", st->last_ip[0] ? st->last_ip : "?");
      set_status(st, msg);
      break;
    }
    case NET_STATE_FAILED_RETURN_AP:
      set_status(st, "connect failed, return to AP mode");
      st->last_get_ip_us = 0;
      break;
    case NET_STATE_IDLE:
    default:
      break;
  }
}

static DWORD device_read_available_bytes(AppState *st, char *out, DWORD out_cap) {
  if (!st || st->device_handle == INVALID_HANDLE_VALUE || !out || out_cap == 0) {
    return 0;
  }

  DWORD errors = 0;
  COMSTAT stat = {0};
  if (!ClearCommError(st->device_handle, &errors, &stat)) {
    return 0;
  }
  if (stat.cbInQue == 0) {
    return 0;
  }

  DWORD to_read = stat.cbInQue;
  if (to_read > out_cap) {
    to_read = out_cap;
  }

  DWORD got = 0;
  if (!ReadFile(st->device_handle, out, to_read, &got, NULL)) {
    return 0;
  }
  return got;
}

static void device_process_status_line(AppState *st, const char *line) {
  if (!st || !line) {
    return;
  }
  char tmp_ip[32];

  /* High-level status messages (tolerant matching). */
  if (str_contains_ci(line, "connect failed")) {
    net_set_state(st, NET_STATE_FAILED_RETURN_AP, NULL);
    return;
  }

  if (str_contains_ci(line, "ap mode")) {
    if (extract_ipv4_from_line(line, tmp_ip)) {
      net_set_state(st, NET_STATE_AP_MODE, tmp_ip);
    } else {
      /* Wait for IP via get_ip. */
      if (st->net_state != NET_STATE_AP_MODE) {
        net_set_state(st, NET_STATE_CONNECTING_AP, NULL);
      }
    }
    return;
  }

  if (str_contains_ci(line, "connected")) {
    if (extract_ipv4_from_line(line, tmp_ip)) {
      net_set_state(st, NET_STATE_CONNECTED_STA, tmp_ip);
    } else {
      if (st->net_state != NET_STATE_CONNECTED_STA) {
        net_set_state(st, NET_STATE_CONNECTING_STA, NULL);
      }
    }
    return;
  }

  /* Direct IP line or get_ip response. */
  if (extract_ipv4_from_line(line, tmp_ip)) {
    if (st->net_state == NET_STATE_CONNECTING_AP || st->net_state == NET_STATE_FAILED_RETURN_AP) {
      net_set_state(st, NET_STATE_AP_MODE, tmp_ip);
    } else if (st->net_state == NET_STATE_CONNECTING_STA) {
      net_set_state(st, NET_STATE_CONNECTED_STA, tmp_ip);
    }
    return;
  }
}

static void device_drain_status_input(AppState *st) {
  if (!st || st->device_handle == INVALID_HANDLE_VALUE) {
    return;
  }

  char chunk[256];
  for (;;) {
    DWORD got = device_read_available_bytes(st, chunk, (DWORD)sizeof(chunk));
    if (got == 0) {
      break;
    }

    if (st->rx_len + got >= sizeof(st->rx_buf)) {
      /* Drop buffered data if it gets too large to avoid undefined parsing. */
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

  /* If we're waiting for an IP, periodically ask the device via CDC. */
  if (st->net_state == NET_STATE_CONNECTING_STA ||
      st->net_state == NET_STATE_CONNECTING_AP ||
      st->net_state == NET_STATE_FAILED_RETURN_AP) {
    if (st->last_get_ip_us == 0 || (now - st->last_get_ip_us) > 800000) {
      (void)device_write_all(st, "get_ip");
      st->last_get_ip_us = now;
    }
  }

  /* Animate connecting... so the UI doesn't look stuck. */
  if (st->net_state == NET_STATE_CONNECTING_STA || st->net_state == NET_STATE_CONNECTING_AP) {
    if (st->last_anim_us == 0 || (now - st->last_anim_us) > 250000) {
      st->anim_dots = (st->anim_dots + 1) % 3;
      st->last_anim_us = now;
      net_set_connecting_label(st);
    }
  }

  return G_SOURCE_CONTINUE;
}

static void device_status_monitor_start(AppState *st) {
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
  st->status_tick_id = g_timeout_add(100, device_status_tick_cb, st);
}

static void device_status_monitor_stop(AppState *st) {
  if (!st || st->status_tick_id == 0) {
    return;
  }
  g_source_remove(st->status_tick_id);
  st->status_tick_id = 0;
}

static gboolean device_mode_active(const AppState *st) {
  return st && st->device_handle != INVALID_HANDLE_VALUE;
}

static void clear_ssid_store(AppState *st) {
  if (!st || !st->ssidStore) {
    return;
  }
  while (g_list_model_get_n_items(G_LIST_MODEL(st->ssidStore)) > 0) {
    g_list_store_remove(st->ssidStore, 0);
  }
}

static char *trim_ascii_inplace(char *s) {
  if (!s) {
    return s;
  }
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
    s++;
  }
  size_t n = strlen(s);
  while (n > 0) {
    char c = s[n - 1];
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      s[n - 1] = '\0';
      n--;
      continue;
    }
    break;
  }
  return s;
}

static gboolean equals_token_ci(const char *a, const char *b) {
  if (!a || !b) {
    return FALSE;
  }
  while (*a && *b) {
    char ca = *a;
    char cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
    if (ca != cb) {
      return FALSE;
    }
    a++;
    b++;
  }
  return *a == '\0' && *b == '\0';
}

static gboolean device_write_all(AppState *st, const char *data) {
  if (!st || st->device_handle == INVALID_HANDLE_VALUE || !data) {
    return FALSE;
  }
  DWORD len = (DWORD)strlen(data);
  DWORD written = 0;
  if (!WriteFile(st->device_handle, data, len, &written, NULL) || written != len) {
    return FALSE;
  }
  FlushFileBuffers(st->device_handle);
  return TRUE;
}

static gboolean device_scan_networks_and_update_dropdown(AppState *st) {
  if (!device_mode_active(st) || !st->ssidStore) {
    return FALSE;
  }
  if (!st->ssidSecurity) {
    st->ssidSecurity = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  } else {
    g_hash_table_remove_all(st->ssidSecurity);
  }

  PurgeComm(st->device_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
  if (!device_write_all(st, "scan_Networks")) {
    set_status(st, "Failed to send scan_Networks");
    return FALSE;
  }

  set_status(st, "Scanning networks...");

  clear_ssid_store(st);

  /* Read lines until we see a line equal to "complete" (case-insensitive). */
  char rx[2048];
  DWORD rx_len = 0;
  DWORD start = GetTickCount();
  const DWORD total_timeout_ms = 5000;
  guint added = 0;

  while ((GetTickCount() - start) < total_timeout_ms) {
    char chunk[128];
    DWORD read = 0;
    if (!ReadFile(st->device_handle, chunk, sizeof(chunk), &read, NULL)) {
      break;
    }
    if (read == 0) {
      continue;
    }

    DWORD can_copy = read;
    if (rx_len + can_copy >= (DWORD)sizeof(rx)) {
      can_copy = (DWORD)sizeof(rx) - 1 - rx_len;
    }
    if (can_copy == 0) {
      break;
    }
    memcpy(rx + rx_len, chunk, can_copy);
    rx_len += can_copy;
    rx[rx_len] = '\0';

    /* Process complete lines */
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
          /* shift remaining bytes (after nl) */
          char *rest = sep + 1;
          size_t rest_len = strlen(rest);
          memmove(rx, rest, rest_len + 1);
          rx_len = (DWORD)rest_len;

          /* If nothing came, keep at least a placeholder in release UI. */
          if (added == 0) {
            GtkStringObject *s0 = gtk_string_object_new("No SSIDs yet");
            g_list_store_append(st->ssidStore, s0);
            g_object_unref(s0);
            set_status(st, "Scan complete: 0 networks");
          } else {
            char msg[128];
            g_snprintf(msg, sizeof(msg), "Scan complete: %u networks", added);
            set_status(st, msg);
          }
          gtk_drop_down_set_selected(st->ssidStruct, 0);
          return TRUE;
        }

        /* Parse "SSID, open/pwd" */
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

      /* shift remaining bytes (after nl) */
      char *rest = sep + 1;
      size_t rest_len = strlen(rest);
      memmove(rx, rest, rest_len + 1);
      rx_len = (DWORD)rest_len;
    }
  }

  /* Timeout: still update selection if we got something. */
  if (added > 0) {
    gtk_drop_down_set_selected(st->ssidStruct, 0);
    {
      char msg[128];
      g_snprintf(msg, sizeof(msg), "Scan timeout: %u networks", added);
      set_status(st, msg);
    }
    return TRUE;
  }

  /* Ensure dropdown isn't empty in release UI. */
  GtkStringObject *s0 = gtk_string_object_new("No SSIDs yet");
  g_list_store_append(st->ssidStore, s0);
  g_object_unref(s0);
  gtk_drop_down_set_selected(st->ssidStruct, 0);
  set_status(st, "Scan timeout: 0 networks");
  return FALSE;
}

static gboolean send_loopback_and_check_echo(AppState *st, const char *command) {
  if (!loopback_mode_active(st) || !command || !st->loopback_port[0]) {
    return FALSE;
  }

  HANDLE h = open_serial_port(st->loopback_port);
  if (h == INVALID_HANDLE_VALUE) {
    DBG_LOG(st, "[DBG] Loopback open failed (%s)\n", st->loopback_port);
    return FALSE;
  }

  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

  DWORD written = 0;
  if (!WriteFile(h, command, (DWORD)strlen(command), &written, NULL)) {
    DBG_LOG(st, "[DBG] Loopback TX failed (%s): err=%lu\n", st->loopback_port, (unsigned long)GetLastError());
    CloseHandle(h);
    return FALSE;
  }
  FlushFileBuffers(h);

  char buf[PROBE_MAX_REPLY_BYTES + 1];
  buf[0] = '\0';
  DWORD total = 0;
  DWORD start = GetTickCount();
  while ((GetTickCount() - start) < PROBE_READ_TOTAL_TIMEOUT_MS) {
    char chunk[128];
    DWORD read = 0;
    if (!ReadFile(h, chunk, sizeof(chunk), &read, NULL)) {
      break;
    }
    if (read == 0) {
      continue;
    }

    DWORD can_copy = read;
    if (total + can_copy > PROBE_MAX_REPLY_BYTES) {
      can_copy = PROBE_MAX_REPLY_BYTES - total;
    }
    memcpy(buf + total, chunk, can_copy);
    total += can_copy;
    buf[total] = '\0';

    if (total >= PROBE_MAX_REPLY_BYTES) {
      break;
    }
  }

  /* Trim CR/LF from the end and require an exact match. */
  while (total > 0 && (buf[total - 1] == '\r' || buf[total - 1] == '\n')) {
    buf[total - 1] = '\0';
    total--;
  }

  gboolean ok = (strcmp(buf, command) == 0);
  CloseHandle(h);
  return ok;
}

/* ---------- Serial helpers ---------- */

static HANDLE open_serial_port(const char *com_name) {
  wchar_t path[64];
  wchar_t wcom[32];
  wcom[0] = L'\0';
  MultiByteToWideChar(CP_UTF8, 0, com_name, -1, wcom, (int)(sizeof(wcom) / sizeof(wcom[0])));
  swprintf(path, (int)(sizeof(path) / sizeof(path[0])), L"\\\\.\\%ls", wcom);

  HANDLE h = CreateFileW(
      path,
      GENERIC_READ | GENERIC_WRITE,
      0,
      NULL,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      NULL);

  if (h == INVALID_HANDLE_VALUE) {
    g_last_open_serial_error = GetLastError();
    /* Debug-only: helps diagnose port-in-use / permission issues. */
    DBG_LOG(g_app_state, "[DBG] CreateFileW(%s) failed: err=%lu\n", com_name, (unsigned long)GetLastError());
    return INVALID_HANDLE_VALUE;
  }

  g_last_open_serial_error = 0;

  SetupComm(h, 4096, 4096);

  COMMTIMEOUTS timeouts = {0};
  timeouts.ReadIntervalTimeout = PROBE_READ_SLICE_TIMEOUT_MS;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.ReadTotalTimeoutConstant = PROBE_READ_SLICE_TIMEOUT_MS;
  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 200;
  SetCommTimeouts(h, &timeouts);

  DCB dcb = {0};
  dcb.DCBlength = sizeof(DCB);
  if (!GetCommState(h, &dcb)) {
    CloseHandle(h);
    return INVALID_HANDLE_VALUE;
  }

  dcb.BaudRate = SERIAL_BAUD;
  dcb.ByteSize = SERIAL_DATABITS;
  dcb.Parity   = SERIAL_PARITY;
  dcb.StopBits = SERIAL_STOPBITS;

  /* Be explicit: disable flow control so loopback tests work reliably. */
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDsrSensitivity = FALSE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  dcb.fTXContinueOnXoff = TRUE;
  dcb.fNull = FALSE;
  dcb.fAbortOnError = FALSE;

  dcb.fDtrControl = DTR_CONTROL_ENABLE;
  dcb.fRtsControl = RTS_CONTROL_ENABLE;

  if (!SetCommState(h, &dcb)) {
    CloseHandle(h);
    return INVALID_HANDLE_VALUE;
  }

  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
  return h;
}

static gboolean probe_port_for_device(const char *com_name, HANDLE *out_handle, gboolean *out_loopback) {
  if (out_handle) {
    *out_handle = INVALID_HANDLE_VALUE;
  }
  if (out_loopback) {
    *out_loopback = FALSE;
  }

  HANDLE h = open_serial_port(com_name);
  if (h == INVALID_HANDLE_VALUE) {
    return FALSE;
  }

  DWORD written = 0;
  if (!WriteFile(h, PROBE_REQUEST, (DWORD)strlen(PROBE_REQUEST), &written, NULL)) {
    DBG_LOG(g_app_state, "[DBG] WriteFile(%s) failed: err=%lu\n", com_name, (unsigned long)GetLastError());
    CloseHandle(h);
    return FALSE;
  }
  DBG_LOG(g_app_state, "[DBG] TX %s: '%s' (%lu bytes)\n", com_name, PROBE_REQUEST, (unsigned long)written);

  char buf[PROBE_MAX_REPLY_BYTES + 1];
  char norm[PROBE_MAX_REPLY_BYTES + 1];
  DWORD total = 0;
  DWORD start = GetTickCount();

  while ((GetTickCount() - start) < PROBE_READ_TOTAL_TIMEOUT_MS) {
    char chunk[128];
    DWORD read = 0;

    if (!ReadFile(h, chunk, sizeof(chunk), &read, NULL)) {
      break;
    }

    if (read > 0) {
      DWORD can_copy = read;
      if (total + can_copy > PROBE_MAX_REPLY_BYTES) {
        can_copy = PROBE_MAX_REPLY_BYTES - total;
      }
      memcpy(buf + total, chunk, can_copy);
      total += can_copy;
      buf[total] = '\0';

      /* Normalize: lowercase and remove whitespace so we can match reliably. */
      DWORD j = 0;
      for (DWORD i = 0; i < total && j < PROBE_MAX_REPLY_BYTES; i++) {
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
        if (out_handle) {
          *out_handle = h;
        } else {
          CloseHandle(h);
        }
        return TRUE;
      }

      /* Debug aid: detect simple loopback/echo devices (reply contains the sent request). */
      if (out_loopback && strstr(buf, PROBE_REQUEST) != NULL) {
        *out_loopback = TRUE;
      }

      /* Debug-only: show what we received (truncated). */
      if (g_app_state && g_app_state->keep_running_without_device) {
        char preview[65];
        DWORD n = total < 64 ? total : 64;
        for (DWORD i = 0; i < n; i++) {
          unsigned char c = (unsigned char)buf[i];
          preview[i] = (c >= 32 && c < 127) ? (char)c : '.';
        }
        preview[n] = '\0';
        DBG_LOG(g_app_state, "[DBG] RX %s: %lu bytes, preview='%s'\n", com_name, (unsigned long)total, preview);
      }

      if (total >= PROBE_MAX_REPLY_BYTES) {
        break;
      }
    }
  }

  CloseHandle(h);
  return FALSE;
}

/* ---------- UI helpers ---------- */

static void set_main_title(AppState *st, const char *title) {
  if (st->window1) {
    gtk_window_set_title(st->window1, title);
  }
}

static gboolean scan_for_device_blocking(AppState *st) {
  if (!st) {
    return FALSE;
  }

  st->device_port[0] = '\0';
  st->device_port_busy = FALSE;
  st->busy_port[0] = '\0';
  st->loopback_available = FALSE;
  st->loopback_port[0] = '\0';
  st->pending_connect_sta = FALSE;
  st->pending_ssid[0] = '\0';
  if (st->ssidSecurity) {
    g_hash_table_remove_all(st->ssidSecurity);
  }
  if (st->device_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(st->device_handle);
    st->device_handle = INVALID_HANDLE_VALUE;
  }

  /* Pass 1: enumerate existing COM ports (for debug output). */
  GArray *ports = g_array_new(FALSE, FALSE, sizeof(int));
  for (int i = 1; i <= 256; i++) {
    wchar_t wname[32];
    swprintf(wname, 32, L"COM%d", i);
    wchar_t target[512];
    if (QueryDosDeviceW(wname, target, (DWORD)(sizeof(target) / sizeof(target[0]))) != 0) {
      g_array_append_val(ports, i);
    }
  }

  if (st->keep_running_without_device) {
    DBG_LOG(st, "COM ports found: %u\n", ports->len);
    if (ports->len > 0) {
      DBG_LOG(st, "Ports:");
      for (guint idx = 0; idx < ports->len; idx++) {
        int port_num = g_array_index(ports, int, idx);
        DBG_LOG(st, " COM%d", port_num);
      }
      DBG_LOG(st, "\n");
    }
  }

  /* Pass 2: probe ports to find the expected device; also detect loopback. */
  gboolean loopback_reported = FALSE;
  for (guint idx = 0; idx < ports->len; idx++) {
    int port_num = g_array_index(ports, int, idx);
    char com[32];
    g_snprintf(com, sizeof(com), "COM%d", port_num);

    DBG_LOG(st, "Probing %s...\n", com);

    HANDLE found = INVALID_HANDLE_VALUE;
    gboolean loopback = FALSE;
    if (probe_port_for_device(com, &found, &loopback)) {
      g_strlcpy(st->device_port, com, sizeof(st->device_port));
      st->device_handle = found;
      DBG_LOG(st, "Device found on %s\n", st->device_port);
      g_array_free(ports, TRUE);
      return TRUE;
    }

    /* If COM port is present but access is denied, it's likely opened by another program. */
    if (!st->device_port_busy && g_last_open_serial_error == ERROR_ACCESS_DENIED) {
      st->device_port_busy = TRUE;
      g_strlcpy(st->busy_port, com, sizeof(st->busy_port));
    }
    if (loopback && !loopback_reported) {
      loopback_reported = TRUE;
      st->loopback_available = TRUE;
      g_strlcpy(st->loopback_port, com, sizeof(st->loopback_port));
      DBG_LOG(st, "Loopback device found\n");
    }
  }

  g_array_free(ports, TRUE);
  return FALSE;
}

/* ---------- Signal handlers referenced from ui/main.ui ---------- */

G_MODULE_EXPORT void on_scanNetworks_clicked(GtkButton *btn, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)btn;
  (void)st;

  /* Placeholder: show that handler is wired */
  DBG_LOG(st, "[UI] Scan Networks clicked\n");

  if (device_mode_active(st)) {
    (void)device_scan_networks_and_update_dropdown(st);
    return;
  }

  if (loopback_mode_active(st)) {
    const char *cmd = "scan_Networks";
    gboolean ok = send_loopback_and_check_echo(st, cmd);
    DBG_LOG(st, "[LOOPBACK] TX '%s' -> %s\n", cmd, ok ? "echo OK" : "echo FAILED");
    set_status(st, ok ? "Loopback: scan_Networks OK" : "Loopback: scan_Networks FAILED");
  }

  /* No action yet: device interaction will be implemented later. */
}

G_MODULE_EXPORT void on_setAP_clicked(GtkButton *btn, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)btn;
  (void)st;

  DBG_LOG(st, "[UI] Set AP Mode clicked\n");

  if (device_mode_active(st)) {
    (void)device_write_all(st, "connect_AP");
    net_set_state(st, NET_STATE_CONNECTING_AP, NULL);
    return;
  }

  if (loopback_mode_active(st)) {
    const char *cmd = "connect_AP";
    gboolean ok = send_loopback_and_check_echo(st, cmd);
    DBG_LOG(st, "[LOOPBACK] TX '%s' -> %s\n", cmd, ok ? "echo OK" : "echo FAILED");
    set_status(st, ok ? "Loopback: connect_AP OK" : "Loopback: connect_AP FAILED");
  }

  /* No action yet: device interaction will be implemented later. */
}

G_MODULE_EXPORT void on_buttonConnect_clicked(GtkButton *btn, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)btn;

  /* Debug-only: show password dialog. Normal mode: log only. */
  DBG_LOG(st, "[UI] Connect clicked\n");

  if (!st) {
    return;
  }

  const char *ssid = get_selected_ssid(st);
  if (!ssid) {
    DBG_LOG(st, "[UI] No SSID selected\n");
    set_status(st, "No SSID selected");
    return;
  }

  if (ssid_requires_password(ssid)) {
    /* Show password dialog; send only after OK. */
    st->pending_connect_sta = TRUE;
    g_strlcpy(st->pending_ssid, ssid, sizeof(st->pending_ssid));

    if (st->textBox0) {
      gtk_editable_set_text(GTK_EDITABLE(st->textBox0), "");
    }

    if (st->pwdDialog) {
      if (st->app) {
        gtk_window_set_application(st->pwdDialog, st->app);
      }
      if (st->window1) {
        gtk_window_set_transient_for(st->pwdDialog, st->window1);
      }
      gtk_window_set_modal(st->pwdDialog, TRUE);
      gtk_window_present(st->pwdDialog);
      {
        char msg[192];
        g_snprintf(msg, sizeof(msg), "Password required for: %s", ssid);
        set_status(st, msg);
      }
    } else {
      DBG_LOG(st, "[LOOPBACK] Password required but pwdDialog missing\n");
      set_status(st, "Password required (dialog missing)");
    }
    return;
  }

  /* Open network: send immediately with empty password field. */
  char cmd[256];
  g_snprintf(cmd, sizeof(cmd), "connect_sta,%s,", ssid);
  if (loopback_mode_active(st)) {
    gboolean ok = send_loopback_and_check_echo(st, cmd);
    DBG_LOG(st, "[LOOPBACK] TX '%s' -> %s\n", cmd, ok ? "echo OK" : "echo FAILED");
    set_status(st, ok ? "Loopback: connect_sta sent" : "Loopback: connect_sta FAILED");
  } else if (device_mode_active(st)) {
    (void)device_write_all(st, cmd);
    net_set_state(st, NET_STATE_CONNECTING_STA, NULL);
  }
}

G_MODULE_EXPORT void on_buttonOK_clicked(GtkButton *btn, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)btn;
  const char *pwd = NULL;
  if (st && st->textBox0) {
    pwd = gtk_editable_get_text(GTK_EDITABLE(st->textBox0));
  }
  DBG_LOG(st, "[UI] OK clicked, password='%s'\n", pwd ? pwd : "");

  if (st && st->pending_connect_sta && st->pending_ssid[0]) {
    char cmd[256];
    g_snprintf(cmd, sizeof(cmd), "connect_sta,%s,%s", st->pending_ssid, pwd ? pwd : "");
    if (loopback_mode_active(st)) {
      gboolean ok = send_loopback_and_check_echo(st, cmd);
      DBG_LOG(st, "[LOOPBACK] TX '%s' -> %s\n", cmd, ok ? "echo OK" : "echo FAILED");
      set_status(st, ok ? "Loopback: connect_sta sent" : "Loopback: connect_sta FAILED");
    } else if (device_mode_active(st)) {
      (void)device_write_all(st, cmd);
      net_set_state(st, NET_STATE_CONNECTING_STA, NULL);
    }
  }

  if (st) {
    st->pending_connect_sta = FALSE;
    st->pending_ssid[0] = '\0';
  }

  if (st && st->pwdDialog) {
    gtk_widget_set_visible(GTK_WIDGET(st->pwdDialog), FALSE);
  }
  if (st && st->window1) {
    gtk_window_present(st->window1);
  }
}

G_MODULE_EXPORT void on_buttonCancel_clicked(GtkButton *btn, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)btn;
  DBG_LOG(st, "[UI] Cancel clicked\n");

  if (st) {
    st->pending_connect_sta = FALSE;
    st->pending_ssid[0] = '\0';
  }
  if (st && st->pwdDialog) {
    gtk_widget_set_visible(GTK_WIDGET(st->pwdDialog), FALSE);
  }
  if (st && st->window1) {
    gtk_window_present(st->window1);
  }
}

/* Signal for GtkDropDown: notify::selected (declared in glade.ui) */
G_MODULE_EXPORT void on_ssidStruct_selected_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)pspec;

  if (!st || !st->keep_running_without_device) {
    return;
  }

  GtkDropDown *dd = GTK_DROP_DOWN(obj);
  guint selected = gtk_drop_down_get_selected(dd);
  const char *value = NULL;
  if (selected != GTK_INVALID_LIST_POSITION) {
    GObject *item = gtk_drop_down_get_selected_item(dd);
    if (item && GTK_IS_STRING_OBJECT(item)) {
      value = gtk_string_object_get_string(GTK_STRING_OBJECT(item));
    }
  }
  g_print("[UI] SSID selected: idx=%u value=%s\n", selected, value ? value : "(none)");
}

static gboolean on_pwd_dialog_close_request(GtkWindow *win, gpointer user_data) {
  AppState *st = resolve_state(user_data);

  if (st) {
    st->pending_connect_sta = FALSE;
    st->pending_ssid[0] = '\0';
  }

  gtk_widget_set_visible(GTK_WIDGET(win), FALSE);
  if (st && st->window1) {
    gtk_window_present(st->window1);
  }
  return TRUE; /* prevent default destroy */
}

static gboolean on_main_window_close_request(GtkWindow *win, gpointer user_data) {
  AppState *st = (AppState*)user_data;
  (void)win;

  /* Ensure we actually quit even if the window is configured to hide-on-close. */
  if (st) {
    if (st->pwdDialog) {
      gtk_window_destroy(st->pwdDialog);
      st->pwdDialog = NULL;
    }
    device_status_monitor_stop(st);
    if (st->device_handle != INVALID_HANDLE_VALUE) {
      CloseHandle(st->device_handle);
      st->device_handle = INVALID_HANDLE_VALUE;
    }
    if (st->app) {
      g_application_quit(G_APPLICATION(st->app));
    }
  }

  return FALSE; /* allow default close handling */
}

/* ---------- App lifecycle ---------- */

static void on_window_destroy(GtkWidget *w, gpointer user_data) {
  AppState *st = (AppState*)user_data;
  (void)w;

  /* Make sure auxiliary windows don't keep the app alive. */
  if (st->pwdDialog) {
    gtk_window_destroy(st->pwdDialog);
    st->pwdDialog = NULL;
  }

  device_status_monitor_stop(st);

  if (st->device_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(st->device_handle);
    st->device_handle = INVALID_HANDLE_VALUE;
  }
  /* Be explicit: ensures the process terminates even if another hidden toplevel exists. */
  if (st->app) {
    g_application_quit(G_APPLICATION(st->app));
  }
}

static void app_activate(GtkApplication *app, gpointer user_data) {
  AppState *st = (AppState*)user_data;
  st->app = app;

  if (!st->ssidSecurity) {
    st->ssidSecurity = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  }

  st->builder = gtk_builder_new();
  GError *err = NULL;

  gboolean loaded = FALSE;
  gchar *ui_path = get_ui_file_path_next_to_exe();
  if (ui_path) {
    loaded = gtk_builder_add_from_file(st->builder, ui_path, &err);
    if (!loaded) {
      if (st->keep_running_without_device) {
        g_printerr("Failed to load UI from '%s': %s\n", ui_path, err ? err->message : "unknown error");
      }
      g_clear_error(&err);
    }
  }

  if (!loaded) {
    loaded = gtk_builder_add_from_file(st->builder, UI_FILE, &err);
  }

  if (!loaded) {
    if (st->keep_running_without_device) {
      gchar *cwd = g_get_current_dir();
      g_printerr("Failed to load UI. Tried '%s' and '%s'. CWD='%s'. Error: %s\n",
                 ui_path ? ui_path : "(null)",
                 UI_FILE,
                 cwd ? cwd : "(null)",
                 err ? err->message : "unknown error");
      g_free(cwd);
    } else {
      MessageBoxA(NULL,
                  "Failed to load UI file (glade.ui).",
                  "UI load error",
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    }
    g_clear_error(&err);
    g_free(ui_path);
    return;
  }
  g_free(ui_path);

  /* Cache widgets */
  st->window1    = GTK_WINDOW(gtk_builder_get_object(st->builder, "window1"));
  st->pwdDialog  = GTK_WINDOW(gtk_builder_get_object(st->builder, "pwdDialog"));
  st->textBox0   = GTK_ENTRY (gtk_builder_get_object(st->builder, "textBox0"));
  st->ssidStruct = GTK_DROP_DOWN(gtk_builder_get_object(st->builder, "ssidStruct"));
  st->monitor0   = GTK_LABEL(gtk_builder_get_object(st->builder, "monitor0"));

  GtkButton *scanNetworks  = GTK_BUTTON(gtk_builder_get_object(st->builder, "scanNetworks"));
  GtkButton *setAP         = GTK_BUTTON(gtk_builder_get_object(st->builder, "setAP"));
  GtkButton *buttonConnect = GTK_BUTTON(gtk_builder_get_object(st->builder, "buttonConnect"));
  GtkButton *buttonOK      = GTK_BUTTON(gtk_builder_get_object(st->builder, "buttonOK"));
  GtkButton *buttonCancel  = GTK_BUTTON(gtk_builder_get_object(st->builder, "buttonCancel"));

  if (!st->window1 || !st->pwdDialog || !st->ssidStruct || !st->textBox0) {
    if (st->keep_running_without_device) {
      g_printerr("UI missing required objects (window1/pwdDialog/ssidStruct/textBox0)\n");
    } else {
      MessageBoxA(NULL,
                  "UI is missing required objects.",
                  "UI error",
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    }
    return;
  }

  gtk_window_set_application(st->window1, app);

  /* Make sure the main window close really terminates the app. */
  gtk_window_set_hide_on_close(st->window1, FALSE);
  g_signal_connect(st->window1, "close-request", G_CALLBACK(on_main_window_close_request), st);

  /* Ensure dialog isn't destroyed on close; we reuse it in debug mode. */
  if (st->pwdDialog) {
    g_signal_connect(st->pwdDialog, "close-request", G_CALLBACK(on_pwd_dialog_close_request), st);
  }

  /* Hide dialog by default */
  if (st->pwdDialog) {
    gtk_widget_set_visible(GTK_WIDGET(st->pwdDialog), FALSE);
  }

  /* Signals for buttons/dropdown are declared in glade.ui and will be resolved by name. */
  (void)scanNetworks;
  (void)setAP;
  (void)buttonConnect;
  (void)buttonOK;
  (void)buttonCancel;

  g_signal_connect(st->window1, "destroy", G_CALLBACK(on_window_destroy), st);

  /* Create model for GtkDropDown */
  st->ssidStore = g_list_store_new(GTK_TYPE_STRING_OBJECT);
  if (st->keep_running_without_device) {
    const char *items[] = {
        "Test SSID 1",
        "Test SSID 2",
        "Test SSID 3",
        "Test SSID 4",
        NULL,
    };
    for (int i = 0; items[i] != NULL; i++) {
      GtkStringObject *s = gtk_string_object_new(items[i]);
      g_list_store_append(st->ssidStore, s);
      g_object_unref(s);
    }
  } else {
    GtkStringObject *s0 = gtk_string_object_new("No SSIDs yet");
    g_list_store_append(st->ssidStore, s0);
    g_object_unref(s0);
  }
  gtk_drop_down_set_model(st->ssidStruct, G_LIST_MODEL(st->ssidStore));
  {
    GtkExpression *expr = gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, NULL, "string");
    gtk_drop_down_set_expression(st->ssidStruct, expr);
    gtk_expression_unref(expr);
  }
  gtk_drop_down_set_selected(st->ssidStruct, 0);

  if (st->device_handle != INVALID_HANDLE_VALUE && st->device_port[0] != '\0') {
    char title[128];
    g_snprintf(title, sizeof(title), "Device: %s", st->device_port);
    set_main_title(st, title);
    set_status(st, "Ready");
    device_status_monitor_start(st);
  } else {
    set_main_title(st, st->keep_running_without_device ? "Device not found (debug mode)" : "Device not connected");
    set_status(st, st->keep_running_without_device ? "Debug mode" : "Device not connected");
  }

  gtk_window_present(st->window1);
}

int main(int argc, char **argv) {
  AppState st = {0};

  /* Force cairo renderer (avoid GPU/backends issues). Must be set before GTK init. */
  g_setenv("GSK_RENDERER", "cairo", TRUE);

  g_app_state = &st;

  st.device_handle = INVALID_HANDLE_VALUE;
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
  gboolean found = scan_for_device_blocking(&st);

  /* 2) Then: decide what UI to show. */
  if (!found && !st.keep_running_without_device) {
    /* Release mode: no console output. */
    if (st.device_port_busy && st.busy_port[0]) {
      char msg[256];
      g_snprintf(msg, sizeof(msg),
                 "Cannot open %s (access denied).\n\nClose any serial monitor/terminal using this port and restart the application.",
                 st.busy_port);
      MessageBoxA(NULL,
                  msg,
                  "Serial port busy",
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    } else {
      MessageBoxA(NULL,
                  "Device not connected. Please connect the device and restart the application.",
                  "Device not connected",
                  MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    }
    return 1;
  }

  GtkApplication *app = gtk_application_new("local.gtk.serial.scan", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(app_activate), &st);

  int status = g_application_run(G_APPLICATION(app), argc_filtered, argv_filtered);
  g_free(argv_filtered);

  if (st.device_handle != INVALID_HANDLE_VALUE) {
    CloseHandle(st.device_handle);
    st.device_handle = INVALID_HANDLE_VALUE;
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

#endif