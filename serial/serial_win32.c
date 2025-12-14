#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <wchar.h>

#include "serial/serial.h"

#include <string.h>

struct SerialPort {
  HANDLE h;
};

static DWORD g_last_open_serial_error = 0;

unsigned long serial_last_open_error(void) {
  return (unsigned long)g_last_open_serial_error;
}

gboolean serial_last_open_was_access_denied(void) {
  return g_last_open_serial_error == ERROR_ACCESS_DENIED;
}

SerialPort *serial_open(const char *port_utf8) {
  if (!port_utf8) {
    g_last_open_serial_error = ERROR_INVALID_PARAMETER;
    return NULL;
  }

  wchar_t path[64];
  wchar_t wcom[32];
  wcom[0] = L'\0';
  MultiByteToWideChar(CP_UTF8, 0, port_utf8, -1, wcom, (int)(sizeof(wcom) / sizeof(wcom[0])));
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
    return NULL;
  }

  g_last_open_serial_error = 0;

  SetupComm(h, 4096, 4096);

  COMMTIMEOUTS timeouts = {0};
  timeouts.ReadIntervalTimeout = 50;
  timeouts.ReadTotalTimeoutMultiplier = 0;
  timeouts.ReadTotalTimeoutConstant = 50;
  timeouts.WriteTotalTimeoutMultiplier = 0;
  timeouts.WriteTotalTimeoutConstant = 200;
  SetCommTimeouts(h, &timeouts);

  DCB dcb = {0};
  dcb.DCBlength = sizeof(DCB);
  if (!GetCommState(h, &dcb)) {
    CloseHandle(h);
    return NULL;
  }

  dcb.BaudRate = CBR_115200;
  dcb.ByteSize = 8;
  dcb.Parity   = NOPARITY;
  dcb.StopBits = ONESTOPBIT;

  /* Disable flow control so loopback tests work reliably. */
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
    return NULL;
  }

  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

  SerialPort *p = (SerialPort*)g_malloc0(sizeof(SerialPort));
  p->h = h;
  return p;
}

void serial_close(SerialPort *port) {
  if (!port) {
    return;
  }
  if (port->h != INVALID_HANDLE_VALUE) {
    CloseHandle(port->h);
    port->h = INVALID_HANDLE_VALUE;
  }
  g_free(port);
}

void serial_purge(SerialPort *port) {
  if (!port || port->h == INVALID_HANDLE_VALUE) {
    return;
  }
  PurgeComm(port->h, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

bool serial_write_all(SerialPort *port, const char *data) {
  if (!port || port->h == INVALID_HANDLE_VALUE || !data) {
    return false;
  }
  DWORD len = (DWORD)strlen(data);
  DWORD written = 0;
  if (!WriteFile(port->h, data, len, &written, NULL) || written != len) {
    return false;
  }
  FlushFileBuffers(port->h);
  return true;
}

size_t serial_read_available(SerialPort *port, char *buf, size_t cap) {
  if (!port || port->h == INVALID_HANDLE_VALUE || !buf || cap == 0) {
    return 0;
  }

  DWORD errors = 0;
  COMSTAT stat = {0};
  if (!ClearCommError(port->h, &errors, &stat)) {
    return 0;
  }
  if (stat.cbInQue == 0) {
    return 0;
  }

  DWORD to_read = stat.cbInQue;
  if (to_read > (DWORD)cap) {
    to_read = (DWORD)cap;
  }

  DWORD got = 0;
  if (!ReadFile(port->h, buf, to_read, &got, NULL)) {
    return 0;
  }
  return (size_t)got;
}
