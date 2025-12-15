#include "platform/serial.h"

#include "platform/platform.h"

#include <string.h>

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

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

  HANDLE h = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
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
  dcb.Parity = NOPARITY;
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

  SerialPort *p = (SerialPort *)g_malloc0(sizeof(SerialPort));
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

static void add_com_port(GPtrArray *arr, int port_num) {
  char com[32];
  g_snprintf(com, sizeof(com), "COM%d", port_num);
  g_ptr_array_add(arr, g_strdup(com));
}

GPtrArray *platform_enumerate_serial_ports(void) {
  GPtrArray *ports = g_ptr_array_new_with_free_func(g_free);

  for (int i = 1; i <= 256; i++) {
    wchar_t wname[32];
    swprintf(wname, 32, L"COM%d", i);
    wchar_t target[512];
    if (QueryDosDeviceW(wname, target, (DWORD)(sizeof(target) / sizeof(target[0]))) != 0) {
      add_com_port(ports, i);
    }
  }

  return ports;
}

#elif defined(__linux__) || defined(__unix__) || defined(__APPLE__)

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

struct SerialPort {
  int fd;
};

static int g_last_open_errno = 0;

unsigned long serial_last_open_error(void) {
  return (unsigned long)g_last_open_errno;
}

gboolean serial_last_open_was_access_denied(void) {
  return (g_last_open_errno == EACCES) || (g_last_open_errno == EPERM) || (g_last_open_errno == EBUSY);
}

static int configure_port(int fd) {
  struct termios tio;
  if (tcgetattr(fd, &tio) != 0) {
    return -1;
  }

  cfmakeraw(&tio);

  cfsetispeed(&tio, B115200);
  cfsetospeed(&tio, B115200);

  tio.c_cflag &= (tcflag_t)~CSIZE;
  tio.c_cflag |= CS8;
  tio.c_cflag &= (tcflag_t)~PARENB;
  tio.c_cflag &= (tcflag_t)~CSTOPB;

#ifdef CRTSCTS
  tio.c_cflag &= (tcflag_t)~CRTSCTS;
#endif
  tio.c_iflag &= (tcflag_t)~(IXON | IXOFF | IXANY);

  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSANOW, &tio) != 0) {
    return -1;
  }

  tcflush(fd, TCIOFLUSH);
  return 0;
}

static char *normalize_port_path(const char *port_utf8, char out[256]) {
  if (!port_utf8 || port_utf8[0] == '\0') {
    return NULL;
  }
  if (g_str_has_prefix(port_utf8, "/dev/")) {
    g_strlcpy(out, port_utf8, 256);
    return out;
  }
  g_snprintf(out, 256, "/dev/%s", port_utf8);
  return out;
}

SerialPort *serial_open(const char *port_utf8) {
  char path[256];
  if (!normalize_port_path(port_utf8, path)) {
    g_last_open_errno = EINVAL;
    return NULL;
  }

  int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    g_last_open_errno = errno;
    return NULL;
  }

  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) {
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  }

  if (configure_port(fd) != 0) {
    g_last_open_errno = errno ? errno : EIO;
    close(fd);
    return NULL;
  }

  g_last_open_errno = 0;

  SerialPort *p = (SerialPort *)g_malloc0(sizeof(SerialPort));
  p->fd = fd;
  return p;
}

void serial_close(SerialPort *port) {
  if (!port) {
    return;
  }
  if (port->fd >= 0) {
    close(port->fd);
    port->fd = -1;
  }
  g_free(port);
}

void serial_purge(SerialPort *port) {
  if (!port || port->fd < 0) {
    return;
  }
  tcflush(port->fd, TCIOFLUSH);
}

bool serial_write_all(SerialPort *port, const char *data) {
  if (!port || port->fd < 0 || !data) {
    return false;
  }

  size_t len = strlen(data);
  const char *p = data;
  while (len > 0) {
    ssize_t w = write(port->fd, p, len);
    if (w < 0) {
      if (errno == EINTR) {
        continue;
      }
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        g_usleep(1000);
        continue;
      }
      return false;
    }
    if (w == 0) {
      return false;
    }
    p += (size_t)w;
    len -= (size_t)w;
  }

  (void)tcdrain(port->fd);
  return true;
}

size_t serial_read_available(SerialPort *port, char *buf, size_t cap) {
  if (!port || port->fd < 0 || !buf || cap == 0) {
    return 0;
  }

  int available = 0;
  if (ioctl(port->fd, FIONREAD, &available) != 0 || available <= 0) {
    return 0;
  }

  size_t to_read = (size_t)available;
  if (to_read > cap) {
    to_read = cap;
  }

  ssize_t r = read(port->fd, buf, to_read);
  if (r < 0) {
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
      return 0;
    }
    return 0;
  }

  return (size_t)r;
}

static void add_candidate(GPtrArray *arr, const char *path) {
  if (!arr || !path || path[0] == '\0') {
    return;
  }
  g_ptr_array_add(arr, g_strdup(path));
}

GPtrArray *platform_enumerate_serial_ports(void) {
  GPtrArray *out = g_ptr_array_new_with_free_func(g_free);

  /* Prefer stable by-id symlinks when available. */
  GDir *dir = g_dir_open("/dev/serial/by-id", 0, NULL);
  if (dir) {
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
      char full[512];
      g_snprintf(full, sizeof(full), "/dev/serial/by-id/%s", name);
      add_candidate(out, full);
    }
    g_dir_close(dir);
  }

  /* Fallback: scan /dev for ttyACM* and ttyUSB* */
  dir = g_dir_open("/dev", 0, NULL);
  if (dir) {
    const char *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
      if (g_str_has_prefix(name, "ttyACM") || g_str_has_prefix(name, "ttyUSB")) {
        char full[256];
        g_snprintf(full, sizeof(full), "/dev/%s", name);
        add_candidate(out, full);
      }
    }
    g_dir_close(dir);
  }

  return out;
}

#else

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

GPtrArray *platform_enumerate_serial_ports(void) {
  return g_ptr_array_new_with_free_func(g_free);
}

#endif
