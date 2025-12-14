#include "util/util.h"

#include <string.h>

char *trim_ascii_inplace(char *s) {
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

gboolean equals_token_ci(const char *a, const char *b) {
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
