#include "firmware/firmware_upgrade.h"

#include <gio/gio.h>
#include <string.h>

#include "platform/platform.h"

#if defined(_WIN32)
#include <windows.h>
#endif

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

static void post_progress(FirmwareUpgrade *up, double fraction_0_1, const char *status);

typedef struct {
  FirmwareUpgrade *up;
  const char *phase;
  double base;
  double span;
  int last_pct;
  double last_frac;
  GString *log;
} EsptoolProgressCtx;

static void esptool_handle_output_line(EsptoolProgressCtx *ctx, const char *line_utf8) {
  if (!ctx || !ctx->up || !line_utf8 || !*line_utf8) {
    return;
  }

  FirmwareUpgrade *up = ctx->up;
  const char *phase = ctx->phase ? ctx->phase : "";

  gboolean matched_progress = FALSE;

  if (up->bytes_re) {
    GMatchInfo *mi = NULL;
    if (g_regex_match(up->bytes_re, line_utf8, 0, &mi)) {
      gchar *m_read = g_match_info_fetch(mi, 1);
      gchar *m_total = g_match_info_fetch(mi, 2);
      if (m_read && m_total) {
        gdouble read_bytes = g_ascii_strtod(m_read, NULL);
        gdouble total_bytes = g_ascii_strtod(m_total, NULL);
        if (total_bytes > 0.0 && read_bytes >= 0.0) {
          gdouble frac = read_bytes / total_bytes;
          if (frac < 0.0) frac = 0.0;
          if (frac > 1.0) frac = 1.0;
          if (frac < ctx->last_frac) {
            frac = ctx->last_frac;
          }
          if (frac > ctx->last_frac) {
            ctx->last_frac = frac;
            double f = ctx->base + ctx->span * frac;
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

  if (!matched_progress && up->pct_re) {
    GMatchInfo *mi = NULL;
    if (g_regex_match(up->pct_re, line_utf8, 0, &mi)) {
      gchar *m = g_match_info_fetch(mi, 1);
      if (m) {
        int pct = atoi(m);
        if (pct >= 0 && pct <= 100) {
          if (pct < ctx->last_pct) {
            pct = ctx->last_pct;
          }
          if (pct != ctx->last_pct) {
            ctx->last_pct = pct;
            double frac = (double)pct / 100.0;
            if (frac < ctx->last_frac) {
              frac = ctx->last_frac;
            }
            if (frac > ctx->last_frac) {
              ctx->last_frac = frac;
              double f = ctx->base + ctx->span * frac;
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
}

static void esptool_consume_text_chunk(EsptoolProgressCtx *ctx, const char *text_utf8, gsize text_len) {
  if (!ctx || !text_utf8 || text_len == 0) {
    return;
  }

  /* Keep last few KB of output for diagnostics. */
  if (ctx->log && ctx->log->len < 4096) {
    gsize allowed = 4096 - ctx->log->len;
    g_string_append_len(ctx->log, text_utf8, (text_len < allowed) ? text_len : allowed);
  }

  /* Split by CR/LF to handle esptool progress updates which often overwrite the same line (\r). */
  const char *p = text_utf8;
  const char *end = text_utf8 + text_len;
  GString *line = g_string_new(NULL);
  while (p < end) {
    char c = *p++;
    if (c == '\r' || c == '\n') {
      if (line->len > 0) {
        esptool_handle_output_line(ctx, line->str);
        g_string_set_size(line, 0);
      }
      continue;
    }
    g_string_append_c(line, c);
  }
  if (line->len > 0) {
    esptool_handle_output_line(ctx, line->str);
  }
  g_string_free(line, TRUE);
}

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

static char *build_esptool_not_found_message(void) {
  GString *msg = g_string_new(NULL);
  g_string_append(msg,
                  "Утилита прошивки не найдена.\n\n"
                  "Приложение сначала ищет 'esptool' рядом с исполняемым файлом, затем пробует найти его в PATH.\n");

  const char *candidates[] = {
#if defined(_WIN32)
    "esptool.exe",
    "balcom-esptool.exe",
#endif
    "esptool",
    "balcom-esptool",
    NULL,
  };

  g_string_append(msg, "\nПроверенные пути рядом с .exe:\n");
  for (int i = 0; candidates[i] != NULL; i++) {
    char *p = platform_build_path_next_to_exe(candidates[i]);
    if (!p) {
      g_string_append_printf(msg, "- %s (failed to resolve exe directory)\n", candidates[i]);
      continue;
    }
    gboolean exists = g_file_test(p, G_FILE_TEST_EXISTS);
    g_string_append_printf(msg, "- %s (%s)\n", p, exists ? "есть" : "нет");
    g_free(p);
  }

#if defined(_WIN32)
  g_string_append(msg,
                  "\nПримечание для Windows:\n"
                  "- Если установка выполнялась через NSIS-инсталлятор, официальный архив esptool (.zip) скачивается и распаковывается во время установки (нужен доступ в интернет).\n"
                  "- Если ПК офлайн или доступ к GitHub заблокирован прокси/фаерволом, установка может завершиться без esptool.exe, и прошивка не будет работать, пока вы не положите esptool.exe в папку установки рядом с приложением.\n");
#endif

  return g_string_free(msg, FALSE);
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

  EsptoolProgressCtx ctx = {0};
  ctx.up = up;
  ctx.phase = phase;
  ctx.base = base;
  ctx.span = span;
  ctx.last_pct = -1;
  ctx.last_frac = -1.0;
  ctx.log = g_string_new(NULL);

  post_progress(up, base, phase);

#if defined(_WIN32)
  /* Windows: use CreateProcessW with CREATE_NO_WINDOW to prevent console windows. */
  if (!argv || !argv[0]) {
    if (out_err) {
      *out_err = g_strdup("Invalid esptool argv");
    }
    g_string_free(ctx.log, TRUE);
    return FALSE;
  }

  /* Build a Windows command line with basic quoting. */
  GString *cmd = g_string_new(NULL);
  for (int i = 0; argv[i] != NULL; i++) {
    const char *a = argv[i];
    if (i > 0) {
      g_string_append_c(cmd, ' ');
    }
    gboolean need_quotes = (strpbrk(a, " \t\n\v\"" ) != NULL);
    if (!need_quotes) {
      g_string_append(cmd, a);
      continue;
    }
    g_string_append_c(cmd, '"');
    /* Escape embedded quotes. (Our args normally don't contain them.) */
    for (const char *s = a; *s; s++) {
      if (*s == '"') {
        g_string_append(cmd, "\\\"");
      } else {
        g_string_append_c(cmd, *s);
      }
    }
    g_string_append_c(cmd, '"');
  }

  gunichar2 *exe_w = g_utf8_to_utf16(argv[0], -1, NULL, NULL, NULL);
  gunichar2 *cmd_w = g_utf8_to_utf16(cmd->str, -1, NULL, NULL, NULL);
  g_string_free(cmd, TRUE);

  if (!exe_w || !cmd_w) {
    if (out_err) {
      *out_err = g_strdup("Failed to build Windows command line");
    }
    g_free(exe_w);
    g_free(cmd_w);
    g_string_free(ctx.log, TRUE);
    return FALSE;
  }

  SECURITY_ATTRIBUTES sa = {0};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE out_read = NULL;
  HANDLE out_write = NULL;
  if (!CreatePipe(&out_read, &out_write, &sa, 0)) {
    if (out_err) {
      *out_err = g_strdup("Failed to create pipe");
    }
    g_free(exe_w);
    g_free(cmd_w);
    g_string_free(ctx.log, TRUE);
    return FALSE;
  }
  /* Parent read handle must not be inherited. */
  SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);

  HANDLE nul_in = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

  STARTUPINFOW si = {0};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_HIDE;
  si.hStdInput = (nul_in != INVALID_HANDLE_VALUE) ? nul_in : GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = out_write;
  si.hStdError = out_write;

  PROCESS_INFORMATION pi = {0};
  DWORD create_flags = CREATE_NO_WINDOW;
  BOOL started = CreateProcessW((LPCWSTR)exe_w,
                               (LPWSTR)cmd_w,
                               NULL,
                               NULL,
                               TRUE,
                               create_flags,
                               NULL,
                               NULL,
                               &si,
                               &pi);

  CloseHandle(out_write);
  if (nul_in != INVALID_HANDLE_VALUE) {
    CloseHandle(nul_in);
  }
  g_free(exe_w);
  g_free(cmd_w);

  if (!started) {
    DWORD e = GetLastError();
    if (out_err) {
      *out_err = g_strdup_printf("Failed to start esptool (CreateProcessW error %lu)", (unsigned long)e);
    }
    CloseHandle(out_read);
    g_string_free(ctx.log, TRUE);
    return FALSE;
  }

  char buf[4096];
  DWORD nread = 0;
  while (ReadFile(out_read, buf, (DWORD)sizeof(buf), &nread, NULL) && nread > 0) {
    esptool_consume_text_chunk(&ctx, buf, (gsize)nread);
  }
  CloseHandle(out_read);

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);

  if (exit_code != 0) {
    if (out_err) {
      const char *log_str = (ctx.log && ctx.log->len > 0) ? ctx.log->str : NULL;
      if (log_str) {
        *out_err = g_strdup_printf("esptool failed (%s). Output:\n%s", phase, log_str);
      } else {
        *out_err = g_strdup_printf("esptool failed (%s) (exit code %lu)", phase, (unsigned long)exit_code);
      }
    }
    g_string_free(ctx.log, TRUE);
    return FALSE;
  }

  g_string_free(ctx.log, TRUE);
  post_progress(up, base + span, phase);
  return TRUE;
#else
  /* Non-Windows: use GSubprocess. */
  GError *err = NULL;
  GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
  if (!launcher) {
    if (out_err) {
      *out_err = g_strdup("Failed to create subprocess launcher");
    }
    g_string_free(ctx.log, TRUE);
    return FALSE;
  }

  GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, (const gchar *const *)argv, &err);
  g_object_unref(launcher);

  if (!proc) {
    if (out_err) {
      const char *exe_path = (argv && argv[0]) ? argv[0] : "";
      if (exe_path[0] != '\0' && g_file_test(exe_path, G_FILE_TEST_EXISTS)) {
        *out_err = g_strdup_printf(
          "Failed to start esptool at '%s': %s\n\nThe file exists, but the OS could not execute it.",
          exe_path,
          err ? err->message : "unknown error");
      } else {
        *out_err = g_strdup_printf("Failed to start esptool: %s", err ? err->message : "unknown error");
      }
    }
    g_clear_error(&err);
    g_string_free(ctx.log, TRUE);
    return FALSE;
  }

  GInputStream *stdout_stream = g_subprocess_get_stdout_pipe(proc);
  char buf[4096];
  gssize n = 0;
  while ((n = g_input_stream_read(stdout_stream, buf, sizeof(buf), NULL, &err)) > 0) {
    esptool_consume_text_chunk(&ctx, buf, (gsize)n);
  }

  if (err) {
    if (out_err) {
      *out_err = g_strdup_printf("I/O error while running esptool (%s): %s", phase, err->message);
    }
    g_clear_error(&err);
    g_string_free(ctx.log, TRUE);
    g_object_unref(proc);
    return FALSE;
  }

  gboolean ok = g_subprocess_wait_check(proc, NULL, &err);
  if (!ok) {
    if (out_err) {
      const char *log_str = (ctx.log && ctx.log->len > 0) ? ctx.log->str : NULL;
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
    g_string_free(ctx.log, TRUE);
    g_object_unref(proc);
    return FALSE;
  }

  g_string_free(ctx.log, TRUE);
  g_object_unref(proc);
  post_progress(up, base + span, phase);
  return TRUE;
#endif
}

static gboolean err_suggests_invalid_choice(const char *err_msg) {
  if (!err_msg) {
    return FALSE;
  }
  /* esptool argparse message, e.g.:
     "invalid choice: 'read-flash'" */
  return (strstr(err_msg, "invalid choice") != NULL);
}

static gboolean run_esptool_step_with_fallback(FirmwareUpgrade *up,
                                              char *const *argv,
                                              int fallback_index,
                                              const char *fallback_value,
                                              const char *phase,
                                              double base,
                                              double span,
                                              char **out_err) {
  char *first_err = NULL;
  gboolean ok = run_esptool_step(up, argv, phase, base, span, &first_err);
  if (ok) {
    if (out_err) {
      *out_err = NULL;
    }
    g_free(first_err);
    return TRUE;
  }

  if (fallback_index < 0 || !fallback_value || !err_suggests_invalid_choice(first_err)) {
    if (out_err) {
      *out_err = first_err;
    } else {
      g_free(first_err);
    }
    return FALSE;
  }

  /* Try again with alternate subcommand spelling (copy argv pointer array; don't mutate caller memory). */
  int argc = 0;
  while (argv[argc] != NULL) {
    argc++;
  }
  char **argv2 = g_new0(char *, (gsize)argc + 1);
  for (int i = 0; i < argc; i++) {
    argv2[i] = (char *)argv[i];
  }
  argv2[fallback_index] = (char *)fallback_value;
  argv2[argc] = NULL;

  char *second_err = NULL;
  ok = run_esptool_step(up, argv2, phase, base, span, &second_err);
  g_free(argv2);

  g_free(first_err);
  if (ok) {
    if (out_err) {
      *out_err = NULL;
    }
    g_free(second_err);
    return TRUE;
  }

  if (out_err) {
    *out_err = second_err;
  } else {
    g_free(second_err);
  }
  return FALSE;
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
    char *msg = build_esptool_not_found_message();
    post_done(up, FALSE, msg);
    g_free(msg);
    return NULL;
  }

  {
    char *status = g_strdup_printf("Используется прошивальщик: %s", esptool);
    post_progress(up, 0.0, status);
    g_free(status);
  }

  /* Base argv: esptool --port PORT --chip esp32s3 */
  /* Use a higher baudrate to speed up flashing when possible. */
  const char *baud = "460800";
    /* Backup size: read only the first 1 MiB of flash for backup.
      NOTE: esptool's positional "size" arg for read_flash expects bytes (or ALL),
      while 1MB/2MB/... are accepted by --flash_size option. */
    const char *backup_size = "0x100000";

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
      "read_flash",
      "0",
      (char *)backup_size,
      backup_path,
      NULL,
    };

    char *step_err = NULL;
    if (!run_esptool_step_with_fallback(up,
                                        argv_backup,
                                        7, /* subcommand position */
                                        "read-flash",
                                        "Backup",
                                        0.0,
                                        0.25,
                                        &step_err)) {
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
  g_ptr_array_add(argv, g_strdup("write_flash"));

  if (spec_args) {
    g_ptr_array_add(argv, g_strdup_printf("@%s", up->spec));
  } else {
    /* merged .bin/.hex flashed at 0x0 */
    g_ptr_array_add(argv, g_strdup("0x0"));
    g_ptr_array_add(argv, g_strdup(up->spec));
  }

  g_ptr_array_add(argv, NULL);

  char *step_err = NULL;
  gboolean ok = run_esptool_step_with_fallback(up,
                                               (char *const *)argv->pdata,
                                               7, /* subcommand position */
                                               "write-flash",
                                               "Flashing",
                                               0.25,
                                               0.75,
                                               &step_err);
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
