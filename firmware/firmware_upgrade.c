#include "firmware/firmware_upgrade.h"

#include <gio/gio.h>
#include <string.h>

#include "platform/platform.h"

/* BALCOM_UI_DATADIR may be defined for Linux packages; see CMakeLists.txt */

struct FirmwareUpgrade {
  GThread *thread;

  char *port;
  char *spec;
  gboolean backup_first;

  FirmwareUpgradeProgressFn on_progress;
  FirmwareUpgradeDoneFn on_done;
  gpointer user_data;

  GMainContext *main_context;

  GRegex *pct_re;
  GRegex *bytes_re;
};

typedef struct {
  FirmwareUpgrade *up;
  double fraction;
  char *status;
} ProgressDispatch;

typedef struct {
  FirmwareUpgrade *up;
  gboolean success;
  char *message;
} DoneDispatch;

static gboolean dispatch_progress(gpointer data) {
  ProgressDispatch *d = (ProgressDispatch *)data;
  if (d->up->on_progress) {
    d->up->on_progress(d->up->user_data, d->fraction, d->status ? d->status : "");
  }
  g_free(d->status);
  g_free(d);
  return G_SOURCE_REMOVE;
}

static gboolean dispatch_done(gpointer data) {
  DoneDispatch *d = (DoneDispatch *)data;
  if (d->up->on_done) {
    d->up->on_done(d->up->user_data, d->success, d->message ? d->message : "");
  }
  g_free(d->message);
  g_free(d);
  return G_SOURCE_REMOVE;
}

static void post_progress(FirmwareUpgrade *up, double fraction_0_1, const char *status) {
  if (!up) {
    return;
  }
  if (fraction_0_1 < 0.0) {
    fraction_0_1 = 0.0;
  }
  if (fraction_0_1 > 1.0) {
    fraction_0_1 = 1.0;
  }

  ProgressDispatch *d = g_new0(ProgressDispatch, 1);
  d->up = up;
  d->fraction = fraction_0_1;
  d->status = g_strdup(status ? status : "");

  g_main_context_invoke(up->main_context, dispatch_progress, d);
}

static void post_done(FirmwareUpgrade *up, gboolean success, const char *message) {
  if (!up) {
    return;
  }

  DoneDispatch *d = g_new0(DoneDispatch, 1);
  d->up = up;
  d->success = success;
  d->message = g_strdup(message ? message : "");

  g_main_context_invoke(up->main_context, dispatch_done, d);
}

static char *find_esptool_program(void) {
  /* Prefer a bundled flasher next to the executable (portable installer layout). */
  {
    const char *local_candidates[] = {
#if defined(_WIN32)
      "esptool.exe",
      "balcom-esptool.exe",
#endif
      "esptool",
      "balcom-esptool",
      NULL,
    };

    for (int i = 0; local_candidates[i] != NULL; i++) {
      char *p = platform_build_path_next_to_exe(local_candidates[i]);
      if (!p) {
        continue;
      }
#if defined(_WIN32)
      /* On Windows, executable permission bits don't exist; treat an existing .exe as runnable.
         (Some GLib builds can be conservative with G_FILE_TEST_IS_EXECUTABLE.) */
      if (g_file_test(p, G_FILE_TEST_EXISTS)) {
        return p; /* already heap allocated */
      }
#else
      if (g_file_test(p, G_FILE_TEST_IS_EXECUTABLE)) {
        return p; /* already heap allocated */
      }
#endif
      g_free(p);
    }
  }

#ifdef BALCOM_UI_DATADIR
  /* Linux .deb install: we may ship tools in the same data dir as glade.ui. */
  {
    char *p = g_build_filename(BALCOM_UI_DATADIR, "esptool", NULL);
    if (p && g_file_test(p, G_FILE_TEST_IS_EXECUTABLE)) {
      return p;
    }
    g_free(p);
  }
#endif

  /* Fall back to PATH (developer machines). */
  {
    const char *candidates[] = {
      "esptool",
#if defined(_WIN32)
      "esptool.exe",
#endif
      NULL,
    };
    for (int i = 0; candidates[i] != NULL; i++) {
      char *p = g_find_program_in_path(candidates[i]);
      if (p) {
        return p;
      }
    }
  }

  return NULL;
}

static gboolean path_ends_with_ci(const char *path, const char *suffix) {
  if (!path || !suffix) {
    return FALSE;
  }
  size_t lp = strlen(path);
  size_t ls = strlen(suffix);
  if (ls > lp) {
    return FALSE;
  }
  return g_ascii_strcasecmp(path + (lp - ls), suffix) == 0;
}

static gboolean spec_is_args_file(const char *spec_path) {
  if (!spec_path) {
    return FALSE;
  }

  /* ESP-IDF typically produces 'flash_args' (no extension). */
  const char *base = g_path_get_basename(spec_path);
  gboolean is_args = FALSE;
  if (base) {
    is_args = (g_ascii_strcasecmp(base, "flash_args") == 0) || (g_ascii_strcasecmp(base, "flash_args.txt") == 0) ||
              (g_ascii_strcasecmp(base, "flash-args") == 0) || (g_ascii_strcasecmp(base, "flash-args.txt") == 0);
  }
  g_free((gpointer)base);

  if (is_args) {
    return TRUE;
  }

  return path_ends_with_ci(spec_path, ".args") || path_ends_with_ci(spec_path, ".txt");
}

static gboolean run_esptool_step(FirmwareUpgrade *up,
                                char *const *argv,
                                const char *phase,
                                double base,
                                double span,
                                char **out_err) {
  if (out_err) {
    *out_err = NULL;
  }

  GError *err = NULL;
  GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
  if (!launcher) {
    if (out_err) {
      *out_err = g_strdup("Failed to create subprocess launcher");
    }
    return FALSE;
  }

  GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv, &err);
  g_object_unref(launcher);

  if (!proc) {
    if (out_err) {
      const char *exe_path = (argv && argv[0]) ? argv[0] : "";
      if (exe_path[0] != '\0' && g_file_test(exe_path, G_FILE_TEST_EXISTS)) {
        *out_err = g_strdup_printf(
          "Failed to start esptool at '%s': %s\n\nThe file exists, but the OS could not execute it. "
          "On Windows this often means a required runtime DLL is missing (e.g. Visual C++ runtime).",
          exe_path,
          err ? err->message : "unknown error");
      } else {
        *out_err = g_strdup_printf("Failed to start esptool: %s", err ? err->message : "unknown error");
      }
    }
    g_clear_error(&err);
    return FALSE;
  }

  GInputStream *stdout_stream = g_subprocess_get_stdout_pipe(proc);
  GDataInputStream *dis = g_data_input_stream_new(stdout_stream);
  g_data_input_stream_set_newline_type(dis, G_DATA_STREAM_NEWLINE_TYPE_ANY);

  char *line = NULL;
  gsize len = 0;
  int last_pct = -1;
  double last_frac = -1.0; /* 0..1 within this phase */
  GString *log = g_string_new(NULL);

  post_progress(up, base, phase);

  while ((line = g_data_input_stream_read_line(dis, &len, NULL, &err)) != NULL) {
    if (len > 0) {
      if (log->len > 0) {
        g_string_append_c(log, '\n');
      }
      /* Guard against unbounded growth; keep last few KB of output. */
      if (log->len < 4096) {
        g_string_append(log, line);
      }
    }

    /* Prefer precise byte progress: "<read>/<total> bytes". */
    gboolean matched_progress = FALSE;
    if (up->bytes_re) {
      GMatchInfo *mi = NULL;
      if (g_regex_match(up->bytes_re, line, 0, &mi)) {
        gchar *m_read = g_match_info_fetch(mi, 1);
        gchar *m_total = g_match_info_fetch(mi, 2);
        if (m_read && m_total) {
          gdouble read_bytes = g_ascii_strtod(m_read, NULL);
          gdouble total_bytes = g_ascii_strtod(m_total, NULL);
          if (total_bytes > 0.0 && read_bytes >= 0.0) {
            gdouble frac = read_bytes / total_bytes;
            if (frac < 0.0) frac = 0.0;
            if (frac > 1.0) frac = 1.0;
            if (frac < last_frac) {
              frac = last_frac;
            }
            if (frac > last_frac) {
              last_frac = frac;
              double f = base + span * frac;
              char status[256];
              g_snprintf(status, sizeof(status), "%s: %.1f%%", phase, frac * 100.0);
              post_progress(up, f, status);
            }
            matched_progress = TRUE;
          }
        }
        g_free(m_read);
        g_free(m_total);
      }
      if (mi) {
        g_match_info_free(mi);
      }
    }

    /* Fallback: parse percentage like "(10 %)" or "10%" when byte info
       is not available on this line. */
    if (!matched_progress && up->pct_re) {
      GMatchInfo *mi = NULL;
      if (g_regex_match(up->pct_re, line, 0, &mi)) {
        gchar *m = g_match_info_fetch(mi, 1);
        if (m) {
          int pct = atoi(m);
          if (pct >= 0 && pct <= 100) {
            if (pct < last_pct) {
              pct = last_pct;
            }
            if (pct != last_pct) {
              last_pct = pct;
              double frac = (double)pct / 100.0;
              if (frac < last_frac) {
                frac = last_frac;
              }
              if (frac > last_frac) {
                last_frac = frac;
                double f = base + span * frac;
                char status[256];
                g_snprintf(status, sizeof(status), "%s: %d%%", phase, pct);
                post_progress(up, f, status);
              }
            }
          }
          g_free(m);
        }
      }
      if (mi) {
        g_match_info_free(mi);
      }
    }

    g_free(line);
  }

  if (err) {
    if (out_err) {
      *out_err = g_strdup_printf("I/O error while running esptool (%s): %s", phase, err->message);
    }
    g_clear_error(&err);
    g_string_free(log, TRUE);
    g_object_unref(dis);
    g_object_unref(proc);
    return FALSE;
  }

  g_object_unref(dis);

  gboolean ok = g_subprocess_wait_check(proc, NULL, &err);
  if (!ok) {
    if (out_err) {
      const char *log_str = (log && log->len > 0) ? log->str : NULL;
      if (log_str && err) {
        *out_err = g_strdup_printf("esptool failed (%s): %s. Output:\n%s", phase, err->message, log_str);
      } else if (log_str) {
        *out_err = g_strdup_printf("esptool failed (%s). Output:\n%s", phase, log_str);
      } else if (err) {
        *out_err = g_strdup_printf("esptool failed (%s): %s", phase, err->message);
      } else {
        *out_err = g_strdup_printf("esptool failed (%s)", phase);
      }
    }
    g_clear_error(&err);
    g_string_free(log, TRUE);
    g_object_unref(proc);
    return FALSE;
  }

  g_string_free(log, TRUE);
  g_object_unref(proc);

  post_progress(up, base + span, phase);
  return TRUE;
}

static char *make_backup_path(const char *spec_path) {
  if (!spec_path) {
    return NULL;
  }

  /* Save backup next to selected spec.
     Example:
       C:\..\merged.bin -> C:\..\merged.bin.backup.bin
       C:\..\flash_args -> C:\..\flash_args.backup.bin */
  return g_strconcat(spec_path, ".backup.bin", NULL);
}

static gpointer firmware_thread_main(gpointer user_data) {
  FirmwareUpgrade *up = (FirmwareUpgrade *)user_data;

  char *esptool = find_esptool_program();
  if (!esptool) {
    post_done(up, FALSE,
              "Flashing tool not found. Expected 'esptool' bundled next to the application (recommended), or available in PATH on this machine.");
    return NULL;
  }

  /* Base argv: esptool --port PORT --chip esp32s3 */
  /* Use a higher baudrate to speed up flashing when possible. */
  const char *baud = "460800";
    /* Backup size: read only the first 1MB of flash for backup.
      This matches typical bootloader+app region and keeps backup time manageable
      compared to reading ALL of a large flash chip. */
    const char *backup_size = "1M";

  gboolean spec_args = spec_is_args_file(up->spec);
  gboolean spec_bin = path_ends_with_ci(up->spec, ".bin") || path_ends_with_ci(up->spec, ".hex");

  if (!spec_args && !spec_bin) {
    char *msg = g_strdup("Unsupported firmware file. Please select a merged .bin/.hex, or an ESP-IDF 'flash_args' file.");
    post_done(up, FALSE, msg);
    g_free(msg);
    g_free(esptool);
    return NULL;
  }

  /* 0..0.25 = backup, 0.25..1.0 = flash */
  if (up->backup_first) {
    char *backup_path = make_backup_path(up->spec);
    if (!backup_path) {
      post_done(up, FALSE, "Failed to create backup file path");
      g_free(esptool);
      return NULL;
    }
    post_progress(up, 0.0, "Backup: reading flash (device must be in download mode)");

    char *argv_backup[] = {
      esptool,
      "--port",
      up->port,
      "--chip",
      "esp32s3",
      "-b",
      (char *)baud,
      "read-flash",
      "0",
      (char *)backup_size,
      backup_path,
      NULL,
    };

    char *step_err = NULL;
    if (!run_esptool_step(up, argv_backup, "Backup", 0.0, 0.25, &step_err)) {
      /* Backup is required: abort flashing on failure. */
      char *msg = g_strdup_printf("Backup failed. %s", step_err ? step_err : "");
      g_free(step_err);
      g_free(backup_path);
      g_free(esptool);
      post_done(up, FALSE, msg);
      g_free(msg);
      return NULL;
    }

    g_free(backup_path);
  }

  post_progress(up, 0.25, "Flashing: starting (device must be in download mode)");

  /* Build flash argv */
  GPtrArray *argv = g_ptr_array_new_with_free_func(g_free);
  g_ptr_array_add(argv, g_strdup(esptool));
  g_ptr_array_add(argv, g_strdup("--port"));
  g_ptr_array_add(argv, g_strdup(up->port));
  g_ptr_array_add(argv, g_strdup("--chip"));
  g_ptr_array_add(argv, g_strdup("esp32s3"));
  g_ptr_array_add(argv, g_strdup("-b"));
  g_ptr_array_add(argv, g_strdup(baud));
  g_ptr_array_add(argv, g_strdup("write-flash"));

  if (spec_args) {
    g_ptr_array_add(argv, g_strdup_printf("@%s", up->spec));
  } else {
    /* merged .bin/.hex flashed at 0x0 */
    g_ptr_array_add(argv, g_strdup("0x0"));
    g_ptr_array_add(argv, g_strdup(up->spec));
  }

  g_ptr_array_add(argv, NULL);

  char *step_err = NULL;
  gboolean ok = run_esptool_step(up, (char *const *)argv->pdata, "Flashing", 0.25, 0.75, &step_err);
  g_ptr_array_free(argv, TRUE);

  if (!ok) {
    char *msg = g_strdup_printf("Flashing failed. %s", step_err ? step_err : "");
    g_free(step_err);
    g_free(esptool);
    post_done(up, FALSE, msg);
    g_free(msg);
    return NULL;
  }

  g_free(esptool);
  post_done(up, TRUE, "Firmware upgrade completed successfully.");
  return NULL;
}

FirmwareUpgrade *firmware_upgrade_start(const char *port_utf8,
                                       const char *firmware_spec_path_utf8,
                                       gboolean backup_first,
                                       FirmwareUpgradeProgressFn on_progress,
                                       FirmwareUpgradeDoneFn on_done,
                                       gpointer user_data) {
  if (!port_utf8 || !firmware_spec_path_utf8) {
    return NULL;
  }

  FirmwareUpgrade *up = g_new0(FirmwareUpgrade, 1);
  up->port = g_strdup(port_utf8);
  up->spec = g_strdup(firmware_spec_path_utf8);
  up->backup_first = backup_first;
  up->on_progress = on_progress;
  up->on_done = on_done;
  up->user_data = user_data;
  up->main_context = g_main_context_ref_thread_default();
  up->pct_re = g_regex_new("([0-9]{1,3})\\s*%", 0, 0, NULL);
  up->bytes_re = g_regex_new("([0-9]+)\\s*/\\s*([0-9]+)\\s*bytes", 0, 0, NULL);

  up->thread = g_thread_new("firmware-upgrade", firmware_thread_main, up);
  return up;
}

void firmware_upgrade_free(FirmwareUpgrade *up) {
  if (!up) {
    return;
  }

  if (up->thread) {
    g_thread_join(up->thread);
    up->thread = NULL;
  }

  if (up->pct_re) {
    g_regex_unref(up->pct_re);
    up->pct_re = NULL;
  }

  if (up->bytes_re) {
    g_regex_unref(up->bytes_re);
    up->bytes_re = NULL;
  }

  if (up->main_context) {
    g_main_context_unref(up->main_context);
    up->main_context = NULL;
  }

  g_free(up->port);
  g_free(up->spec);
  g_free(up);
}
