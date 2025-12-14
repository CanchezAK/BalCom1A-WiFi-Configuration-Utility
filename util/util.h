#pragma once

#include <gtk/gtk.h>

char *trim_ascii_inplace(char *s);
gboolean equals_token_ci(const char *a, const char *b);
