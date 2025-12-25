#include "ui/ui.h"

#include "ui/ui_handlers.h"

#include "device/device_status.h"
#include "platform/platform.h"

static gboolean on_firmware_upgrade_close_request(GtkWindow *win, gpointer user_data);

static gboolean on_pwd_dialog_close_request(GtkWindow *win, gpointer user_data);
static gboolean on_main_window_close_request(GtkWindow *win, gpointer user_data);
static void on_window_destroy(GtkWidget *w, gpointer user_data);

void ui_set_status(AppState *st, const char *text) {
  if (!st || !st->monitor0) {
    return;
  }
  gtk_label_set_text(st->monitor0, text ? text : "");
}

void ui_set_main_title(AppState *st, const char *title) {
  if (st && st->window1) {
    gtk_window_set_title(st->window1, title);
  }
}

static gboolean on_pwd_dialog_close_request(GtkWindow *win, gpointer user_data) {
  AppState *st = resolve_state(user_data);

  if (st) {
    st->pending_connect_sta = FALSE;
    st->pending_ssid[0] = '\0';
  }

  gtk_widget_set_visible(GTK_WIDGET(win), FALSE);
  if (st && st->window1) {
    gtk_window_present(st->window1);
  }
  return TRUE;
}

static gboolean on_main_window_close_request(GtkWindow *win, gpointer user_data) {
  AppState *st = (AppState*)user_data;
  (void)win;

  if (st) {
    if (st->pwdDialog) {
      gtk_window_destroy(st->pwdDialog);
      st->pwdDialog = NULL;
    }

    if (st->firmwareUpgradeWindow) {
      gtk_window_destroy(st->firmwareUpgradeWindow);
      st->firmwareUpgradeWindow = NULL;
    }

    device_status_monitor_stop(st);

    if (st->device) {
      serial_close(st->device);
      st->device = NULL;
    }

    if (st->app) {
      g_application_quit(G_APPLICATION(st->app));
    }
  }

  return FALSE;
}

static void on_window_destroy(GtkWidget *w, gpointer user_data) {
  AppState *st = (AppState*)user_data;
  (void)w;

  if (st->pwdDialog) {
    gtk_window_destroy(st->pwdDialog);
    st->pwdDialog = NULL;
  }

  if (st->firmwareUpgradeWindow) {
    gtk_window_destroy(st->firmwareUpgradeWindow);
    st->firmwareUpgradeWindow = NULL;
  }

  device_status_monitor_stop(st);

  if (st->device) {
    serial_close(st->device);
    st->device = NULL;
  }

  if (st->app) {
    g_application_quit(G_APPLICATION(st->app));
  }
}

void ui_app_activate(GtkApplication *app, gpointer user_data) {
  AppState *st = (AppState*)user_data;
  st->app = app;

  if (!st->ssidSecurity) {
    st->ssidSecurity = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  }

  st->builder = gtk_builder_new();
  GError *err = NULL;

  /* GTK4 resolves <signal handler="..."> names via the builder scope.
     On Linux, relying on dlsym() of the main binary may require -rdynamic.
     Register callbacks explicitly to keep builds portable (WSL/Ubuntu, etc). */
  {
    GtkBuilderScope *scope = gtk_builder_cscope_new();
    GtkBuilderCScope *cscope = GTK_BUILDER_CSCOPE(scope);

    gtk_builder_cscope_add_callback_symbol(cscope, "on_scanNetworks_clicked", G_CALLBACK(on_scanNetworks_clicked));
    gtk_builder_cscope_add_callback_symbol(cscope, "on_setAP_clicked", G_CALLBACK(on_setAP_clicked));
    gtk_builder_cscope_add_callback_symbol(cscope, "on_buttonConnect_clicked", G_CALLBACK(on_buttonConnect_clicked));
    gtk_builder_cscope_add_callback_symbol(cscope, "on_buttonCancel_clicked", G_CALLBACK(on_buttonCancel_clicked));
    gtk_builder_cscope_add_callback_symbol(cscope, "on_buttonOK_clicked", G_CALLBACK(on_buttonOK_clicked));
    gtk_builder_cscope_add_callback_symbol(cscope, "on_ssidStruct_selected_changed", G_CALLBACK(on_ssidStruct_selected_changed));
    gtk_builder_cscope_add_callback_symbol(cscope, "on_firmwareUpgrade_clicked", G_CALLBACK(on_firmwareUpgrade_clicked));
    gtk_builder_cscope_add_callback_symbol(cscope, "on_firmware_upgrade_ok_clicked", G_CALLBACK(on_firmware_upgrade_ok_clicked));

    gtk_builder_set_scope(st->builder, scope);
    g_object_unref(scope);
  }

  gboolean loaded = FALSE;
  gchar *ui_path = NULL;

  /* 1) Try installed data directory (Linux .deb / standard layout). */
#ifdef BALCOM_UI_DATADIR
  ui_path = g_build_filename(BALCOM_UI_DATADIR, UI_FILE, NULL);
  if (ui_path && g_file_test(ui_path, G_FILE_TEST_EXISTS)) {
    loaded = gtk_builder_add_from_file(st->builder, ui_path, &err);
    if (!loaded) {
      if (st->keep_running_without_device) {
        g_printerr("Failed to load UI from '%s': %s\n", ui_path, err ? err->message : "unknown error");
      }
      g_clear_error(&err);
    }
  }
  g_free(ui_path);
  ui_path = NULL;
#endif

  /* 2) Try next to the executable (portable/dev builds, Windows). */
  if (!loaded) {
    ui_path = platform_build_path_next_to_exe(UI_FILE);
    if (ui_path && g_file_test(ui_path, G_FILE_TEST_EXISTS)) {
      loaded = gtk_builder_add_from_file(st->builder, ui_path, &err);
      if (!loaded) {
        if (st->keep_running_without_device) {
          g_printerr("Failed to load UI from '%s': %s\n", ui_path, err ? err->message : "unknown error");
        }
        g_clear_error(&err);
      }
    }
    g_free(ui_path);
    ui_path = NULL;
  }

  /* 3) Try relative to current working directory (dev/debug runs). */
  if (!loaded) {
    if (g_file_test(UI_FILE, G_FILE_TEST_EXISTS)) {
      loaded = gtk_builder_add_from_file(st->builder, UI_FILE, &err);
      if (!loaded) {
        if (st->keep_running_without_device) {
          g_printerr("Failed to load UI from '%s': %s\n", UI_FILE, err ? err->message : "unknown error");
        }
        g_clear_error(&err);
      }
    }
  }

  if (!loaded) {
    if (st->keep_running_without_device) {
      gchar *cwd = g_get_current_dir();
      g_printerr("Failed to load UI. Tried BALCOM_UI_DATADIR, next-to-exe, and '%s'. CWD='%s'. Error: %s\n",
                 UI_FILE,
                 cwd ? cwd : "(null)",
                 err ? err->message : "unknown error");
      g_free(cwd);
    } else {
      platform_show_error("UI load error", "Failed to load UI file (glade.ui).");
    }
    g_clear_error(&err);
    return;
  }

  st->window1    = GTK_WINDOW(gtk_builder_get_object(st->builder, "window1"));
  st->pwdDialog  = GTK_WINDOW(gtk_builder_get_object(st->builder, "pwdDialog"));
  st->textBox0   = GTK_ENTRY (gtk_builder_get_object(st->builder, "textBox0"));
  st->ssidStruct = GTK_DROP_DOWN(gtk_builder_get_object(st->builder, "ssidStruct"));
  st->monitor0   = GTK_LABEL(gtk_builder_get_object(st->builder, "monitor0"));

  st->firmwareUpgradeWindow = GTK_WINDOW(gtk_builder_get_object(st->builder, "firmwareUpgradeWindow"));
  st->firmwareUpgradeLabel = GTK_LABEL(gtk_builder_get_object(st->builder, "firmwareUpgradeLabel"));
  st->firmwareUpgradeProgress = GTK_PROGRESS_BAR(gtk_builder_get_object(st->builder, "firmwareUpgradeProgress"));
  st->firmwareUpgradeOk = GTK_BUTTON(gtk_builder_get_object(st->builder, "firmwareUpgradeOk"));

  if (!st->window1 || !st->pwdDialog || !st->ssidStruct || !st->textBox0) {
    if (st->keep_running_without_device) {
      g_printerr("UI missing required objects (window1/pwdDialog/ssidStruct/textBox0)\n");
    } else {
      platform_show_error("UI error", "UI is missing required objects.");
    }
    return;
  }

  gtk_window_set_application(st->window1, app);

  gtk_window_set_hide_on_close(st->window1, FALSE);
  g_signal_connect(st->window1, "close-request", G_CALLBACK(on_main_window_close_request), st);

  if (st->pwdDialog) {
    g_signal_connect(st->pwdDialog, "close-request", G_CALLBACK(on_pwd_dialog_close_request), st);
  }

  if (st->firmwareUpgradeWindow) {
    gtk_window_set_application(st->firmwareUpgradeWindow, app);
    gtk_window_set_transient_for(st->firmwareUpgradeWindow, st->window1);
    gtk_window_set_modal(st->firmwareUpgradeWindow, TRUE);
    g_signal_connect(st->firmwareUpgradeWindow, "close-request", G_CALLBACK(on_firmware_upgrade_close_request), st);
    gtk_widget_set_visible(GTK_WIDGET(st->firmwareUpgradeWindow), FALSE);
  }

  if (st->pwdDialog) {
    gtk_widget_set_visible(GTK_WIDGET(st->pwdDialog), FALSE);
  }

  g_signal_connect(st->window1, "destroy", G_CALLBACK(on_window_destroy), st);

  st->ssidStore = g_list_store_new(GTK_TYPE_STRING_OBJECT);
  if (st->keep_running_without_device) {
    const char *items[] = {"Test SSID 1", "Test SSID 2", "Test SSID 3", "Test SSID 4", NULL};
    for (int i = 0; items[i] != NULL; i++) {
      GtkStringObject *s = gtk_string_object_new(items[i]);
      g_list_store_append(st->ssidStore, s);
      g_object_unref(s);
    }
  } else {
    GtkStringObject *s0 = gtk_string_object_new("No SSIDs yet");
    g_list_store_append(st->ssidStore, s0);
    g_object_unref(s0);
  }

  gtk_drop_down_set_model(st->ssidStruct, G_LIST_MODEL(st->ssidStore));
  {
    GtkExpression *expr = gtk_property_expression_new(GTK_TYPE_STRING_OBJECT, NULL, "string");
    gtk_drop_down_set_expression(st->ssidStruct, expr);
    gtk_expression_unref(expr);
  }
  gtk_drop_down_set_selected(st->ssidStruct, 0);

  if (st->device && st->device_port[0] != '\0') {
    char title[128];
    g_snprintf(title, sizeof(title), "Device: %s", st->device_port);
    ui_set_main_title(st, title);
    device_status_monitor_start(st);
  } else {
    ui_set_main_title(st, st->keep_running_without_device ? "Device not found (debug mode)" : "Device not connected");
    ui_set_status(st, st->keep_running_without_device ? "Debug mode" : "Device not connected");
  }

  gtk_window_present(st->window1);
}

static gboolean on_firmware_upgrade_close_request(GtkWindow *win, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  if (st && st->firmware_upgrade_running) {
    /* Prevent closing while an upgrade is in progress. */
    return TRUE;
  }

  gtk_widget_set_visible(GTK_WIDGET(win), FALSE);
  if (st && st->window1) {
    gtk_window_present(st->window1);
  }
  return TRUE;
}
