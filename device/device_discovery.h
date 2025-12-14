#pragma once

#include "app/app_state.h"

/* Scans system serial ports and populates st->device (opened) and st->device_port.
   Also detects a loopback port in debug mode.
*/
gboolean device_discovery_scan_for_device(AppState *st);
