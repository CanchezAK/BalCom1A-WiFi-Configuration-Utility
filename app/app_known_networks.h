#pragma once

#include "app/app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Loads known SSID->password pairs from disk into st->knownPasswords.
   It is OK if the file does not exist. */
gboolean app_known_networks_load(AppState *st);

/* Saves st->knownPasswords to disk. */
gboolean app_known_networks_save(AppState *st);

/* Returns saved password for SSID or NULL if unknown. */
const char *app_known_networks_lookup(AppState *st, const char *ssid);

/* Remembers SSID/password and persists to disk. */
void app_known_networks_remember(AppState *st, const char *ssid, const char *password);

/* Forgets SSID/password and persists to disk. */
void app_known_networks_forget(AppState *st, const char *ssid);

#ifdef __cplusplus
}
#endif
