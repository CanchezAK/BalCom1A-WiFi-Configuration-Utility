#pragma once

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FirmwareUpgrade FirmwareUpgrade;

typedef void (*FirmwareUpgradeProgressFn)(gpointer user_data, double fraction_0_1, const char *status_utf8);
typedef void (*FirmwareUpgradeDoneFn)(gpointer user_data, gboolean success, const char *message_utf8);

/* Starts a firmware upgrade flow:
   1) Optional backup: esptool read-flash 0 ALL -> backup_path
   2) Flash: esptool write-flash (based on firmware_spec_path)

   firmware_spec_path can be:
   - .bin or .hex: treated as merged image flashed at offset 0x0
   - a "flash args" file (e.g. ESP-IDF build's flash_args): will be passed as @file to write-flash

   The caller must ensure the serial port is not currently open by this process.
*/
FirmwareUpgrade *firmware_upgrade_start(const char *port_utf8,
                                       const char *firmware_spec_path_utf8,
                                       gboolean backup_first,
                                       FirmwareUpgradeProgressFn on_progress,
                                       FirmwareUpgradeDoneFn on_done,
                                       gpointer user_data);

void firmware_upgrade_free(FirmwareUpgrade *up);

#ifdef __cplusplus
}
#endif
