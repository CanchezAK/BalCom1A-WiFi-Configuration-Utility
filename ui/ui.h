#pragma once

#include "app/app_state.h"

#define UI_FILE "glade.ui"

void ui_set_status(AppState *st, const char *text);
void ui_set_main_title(AppState *st, const char *title);

void ui_app_activate(GtkApplication *app, gpointer user_data);
