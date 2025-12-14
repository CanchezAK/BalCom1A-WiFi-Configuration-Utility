#include "serial/serial.h"

struct SerialPort {
  int unused;
};

unsigned long serial_last_open_error(void) {
  return 0;
}

gboolean serial_last_open_was_access_denied(void) {
  return FALSE;
}

SerialPort *serial_open(const char *port_utf8) {
  (void)port_utf8;
  return NULL;
}

void serial_close(SerialPort *port) {
  (void)port;
}

bool serial_write_all(SerialPort *port, const char *data) {
  (void)port;
  (void)data;
  return false;
}

size_t serial_read_available(SerialPort *port, char *buf, size_t cap) {
  (void)port;
  (void)buf;
  (void)cap;
  return 0;
}

void serial_purge(SerialPort *port) {
  (void)port;
}
