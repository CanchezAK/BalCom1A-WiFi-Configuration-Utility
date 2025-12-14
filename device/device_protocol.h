#pragma once

#include "app/app_state.h"

/* Thin wrappers around the device text protocol over CDC serial. */
gboolean device_send_command(AppState *st, const char *cmd);

gboolean device_scan_networks_and_update_dropdown(AppState *st);
