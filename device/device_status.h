#pragma once

#include "app/app_state.h"

void device_status_monitor_start(AppState *st);
void device_status_monitor_stop(AppState *st);

void device_status_set_connecting_sta(AppState *st);
void device_status_set_connecting_ap(AppState *st);
