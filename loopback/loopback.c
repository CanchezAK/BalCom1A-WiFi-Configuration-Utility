#include "loopback/loopback.h"

#include "platform/serial.h"

#include <string.h>

/* Keep these aligned with discovery probe settings. */
#define PROBE_READ_TOTAL_TIMEOUT_MS 1000
#define PROBE_MAX_REPLY_BYTES 512

static gint64 now_us(void) {
  return g_get_monotonic_time();
}

gboolean loopback_send_and_check_echo(AppState *st, const char *command) {
  if (!loopback_mode_active(st) || !command || !st->loopback_port[0]) {
    return FALSE;
  }

  SerialPort *p = serial_open(st->loopback_port);
  if (!p) {
    DBG_LOG(st, "[DBG] Loopback open failed (%s)\n", st->loopback_port);
    return FALSE;
  }

  serial_purge(p);

  if (!serial_write_all(p, command)) {
    DBG_LOG(st, "[DBG] Loopback TX failed (%s)\n", st->loopback_port);
    serial_close(p);
    return FALSE;
  }

  char buf[PROBE_MAX_REPLY_BYTES + 1];
  buf[0] = '\0';
  size_t total = 0;

  gint64 start = now_us();
  while ((now_us() - start) < (gint64)PROBE_READ_TOTAL_TIMEOUT_MS * 1000) {
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

    if (total >= PROBE_MAX_REPLY_BYTES) {
      break;
    }
  }

  while (total > 0 && (buf[total - 1] == '\r' || buf[total - 1] == '\n')) {
    buf[total - 1] = '\0';
    total--;
  }

  gboolean ok = (strcmp(buf, command) == 0);
  serial_close(p);
  return ok;
}
