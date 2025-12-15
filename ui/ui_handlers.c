#include <gtk/gtk.h>

#include "app/app_known_networks.h"
#include "app/app_state.h"
#include "device/device_protocol.h"
#include "device/device_status.h"
#include "loopback/loopback.h"
#include "ui/ui.h"

#include <string.h>

static const char *get_selected_ssid(AppState *st) {
  if (!st || !st->ssidStruct) {
    return NULL;
  }
  GObject *item = gtk_drop_down_get_selected_item(st->ssidStruct);
  if (item && GTK_IS_STRING_OBJECT(item)) {
    return gtk_string_object_get_string(GTK_STRING_OBJECT(item));
  }
  return NULL;
}

static gboolean ssid_requires_password(AppState *st, const char *ssid) {
  if (!st || !ssid) {
    return FALSE;
  }
  if (st->ssidSecurity) {
    gpointer v = g_hash_table_lookup(st->ssidSecurity, ssid);
    if (v != NULL) {
      return GPOINTER_TO_INT(v) != 0;
    }
  }
  return (strcmp(ssid, "Test SSID 1") == 0);
}

G_MODULE_EXPORT void on_scanNetworks_clicked(GtkButton *btn, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)btn;

  DBG_LOG(st, "[UI] Scan Networks clicked\n");

  if (device_mode_active(st)) {
    (void)device_scan_networks_and_update_dropdown(st);
    return;
  }

  if (loopback_mode_active(st)) {
    const char *cmd = "scan_Networks";
    gboolean ok = loopback_send_and_check_echo(st, cmd);
    DBG_LOG(st, "[LOOPBACK] TX '%s' -> %s\n", cmd, ok ? "echo OK" : "echo FAILED");
    ui_set_status(st, ok ? "Loopback: scan_Networks OK" : "Loopback: scan_Networks FAILED");
  }
}

G_MODULE_EXPORT void on_setAP_clicked(GtkButton *btn, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)btn;

  DBG_LOG(st, "[UI] Set AP Mode clicked\n");

  if (device_mode_active(st)) {
    (void)device_send_command(st, "connect_AP");
    st->last_connect_was_sta_attempt = FALSE;
    st->last_connect_pwd_was_saved = FALSE;
    st->last_connect_should_save_pwd = FALSE;
    device_status_set_connecting_ap(st);
    return;
  }

  if (loopback_mode_active(st)) {
    const char *cmd = "connect_AP";
    gboolean ok = loopback_send_and_check_echo(st, cmd);
    DBG_LOG(st, "[LOOPBACK] TX '%s' -> %s\n", cmd, ok ? "echo OK" : "echo FAILED");
    ui_set_status(st, ok ? "Loopback: connect_AP OK" : "Loopback: connect_AP FAILED");
  }
}

G_MODULE_EXPORT void on_buttonConnect_clicked(GtkButton *btn, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)btn;

  DBG_LOG(st, "[UI] Connect clicked\n");

  if (!st) {
    return;
  }

  const char *ssid = get_selected_ssid(st);
  if (!ssid) {
    DBG_LOG(st, "[UI] No SSID selected\n");
    ui_set_status(st, "No SSID selected");
    return;
  }

  if (ssid_requires_password(st, ssid)) {
    /* If we already know a password for this SSID, do not prompt; send immediately. */
    const char *saved_pwd = app_known_networks_lookup(st, ssid);
    if (saved_pwd && saved_pwd[0] != '\0') {
      char cmd[256];
      g_snprintf(cmd, sizeof(cmd), "connect_sta,%s,%s", ssid, saved_pwd);

      st->last_connect_was_sta_attempt = TRUE;
      st->last_connect_pwd_was_saved = TRUE;
      st->last_connect_should_save_pwd = FALSE;
      g_strlcpy(st->last_connect_ssid, ssid, sizeof(st->last_connect_ssid));
      g_strlcpy(st->last_connect_pwd, saved_pwd, sizeof(st->last_connect_pwd));

      DBG_LOG(st, "[UI] Using saved password for SSID '%s'\n", ssid);

      if (device_mode_active(st)) {
        (void)device_send_command(st, cmd);
        device_status_set_connecting_sta(st);
      } else if (loopback_mode_active(st)) {
        gboolean ok = loopback_send_and_check_echo(st, cmd);
        DBG_LOG(st, "[LOOPBACK] TX '%s' -> %s\n", cmd, ok ? "echo OK" : "echo FAILED");
        ui_set_status(st, ok ? "Loopback: connect_sta sent" : "Loopback: connect_sta FAILED");
      }
      return;
    }

    st->pending_connect_sta = TRUE;
    g_strlcpy(st->pending_ssid, ssid, sizeof(st->pending_ssid));

    if (st->textBox0) {
      gtk_editable_set_text(GTK_EDITABLE(st->textBox0), "");
    }

    if (st->pwdDialog) {
      if (st->app) {
        gtk_window_set_application(st->pwdDialog, st->app);
      }
      if (st->window1) {
        gtk_window_set_transient_for(st->pwdDialog, st->window1);
      }
      gtk_window_set_modal(st->pwdDialog, TRUE);
      gtk_window_present(st->pwdDialog);
      {
        char msg[192];
        g_snprintf(msg, sizeof(msg), "Password required for: %s", ssid);
        ui_set_status(st, msg);
      }
    } else {
      DBG_LOG(st, "[UI] Password required but pwdDialog missing\n");
      ui_set_status(st, "Password required (dialog missing)");
    }
    return;
  }

  /* Open network. */
  char cmd[256];
  g_snprintf(cmd, sizeof(cmd), "connect_sta,%s,", ssid);
  if (loopback_mode_active(st)) {
    gboolean ok = loopback_send_and_check_echo(st, cmd);
    DBG_LOG(st, "[LOOPBACK] TX '%s' -> %s\n", cmd, ok ? "echo OK" : "echo FAILED");
    ui_set_status(st, ok ? "Loopback: connect_sta sent" : "Loopback: connect_sta FAILED");
  } else if (device_mode_active(st)) {
    (void)device_send_command(st, cmd);
    device_status_set_connecting_sta(st);
  }
}

G_MODULE_EXPORT void on_buttonOK_clicked(GtkButton *btn, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)btn;

  const char *pwd = NULL;
  if (st && st->textBox0) {
    pwd = gtk_editable_get_text(GTK_EDITABLE(st->textBox0));
  }
  DBG_LOG(st, "[UI] OK clicked, password='%s'\n", pwd ? pwd : "");

  if (st && st->pending_connect_sta && st->pending_ssid[0]) {
    char cmd[256];
    g_snprintf(cmd, sizeof(cmd), "connect_sta,%s,%s", st->pending_ssid, pwd ? pwd : "");

    st->last_connect_was_sta_attempt = TRUE;
    st->last_connect_pwd_was_saved = FALSE;
    st->last_connect_should_save_pwd = (pwd && pwd[0] != '\0') ? TRUE : FALSE;
    g_strlcpy(st->last_connect_ssid, st->pending_ssid, sizeof(st->last_connect_ssid));
    g_strlcpy(st->last_connect_pwd, pwd ? pwd : "", sizeof(st->last_connect_pwd));

    if (loopback_mode_active(st)) {
      gboolean ok = loopback_send_and_check_echo(st, cmd);
      DBG_LOG(st, "[LOOPBACK] TX '%s' -> %s\n", cmd, ok ? "echo OK" : "echo FAILED");
      ui_set_status(st, ok ? "Loopback: connect_sta sent" : "Loopback: connect_sta FAILED");
    } else if (device_mode_active(st)) {
      (void)device_send_command(st, cmd);
      device_status_set_connecting_sta(st);
    }
  }

  if (st) {
    st->pending_connect_sta = FALSE;
    st->pending_ssid[0] = '\0';
  }

  if (st && st->pwdDialog) {
    gtk_widget_set_visible(GTK_WIDGET(st->pwdDialog), FALSE);
  }
  if (st && st->window1) {
    gtk_window_present(st->window1);
  }
}

G_MODULE_EXPORT void on_buttonCancel_clicked(GtkButton *btn, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)btn;

  DBG_LOG(st, "[UI] Cancel clicked\n");

  if (st) {
    st->pending_connect_sta = FALSE;
    st->pending_ssid[0] = '\0';
  }
  if (st && st->pwdDialog) {
    gtk_widget_set_visible(GTK_WIDGET(st->pwdDialog), FALSE);
  }
  if (st && st->window1) {
    gtk_window_present(st->window1);
  }
}

G_MODULE_EXPORT void on_ssidStruct_selected_changed(GObject *obj, GParamSpec *pspec, gpointer user_data) {
  AppState *st = resolve_state(user_data);
  (void)pspec;

  if (!st || !st->keep_running_without_device) {
    return;
  }

  GtkDropDown *dd = GTK_DROP_DOWN(obj);
  guint selected = gtk_drop_down_get_selected(dd);
  const char *value = NULL;
  if (selected != GTK_INVALID_LIST_POSITION) {
    GObject *item = gtk_drop_down_get_selected_item(dd);
    if (item && GTK_IS_STRING_OBJECT(item)) {
      value = gtk_string_object_get_string(GTK_STRING_OBJECT(item));
    }
  }
  g_print("[UI] SSID selected: idx=%u value=%s\n", selected, value ? value : "(none)");
}
