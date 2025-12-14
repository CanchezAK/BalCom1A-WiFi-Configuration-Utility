#pragma once

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SerialPort SerialPort;

SerialPort *serial_open(const char *port_utf8);
void serial_close(SerialPort *port);

bool serial_write_all(SerialPort *port, const char *data);
size_t serial_read_available(SerialPort *port, char *buf, size_t cap);
void serial_purge(SerialPort *port);

/* For diagnostics after serial_open() fails. */
unsigned long serial_last_open_error(void);

/* True when the most recent open failure was "access denied" (port in use). */
gboolean serial_last_open_was_access_denied(void);

#ifdef __cplusplus
}
#endif
