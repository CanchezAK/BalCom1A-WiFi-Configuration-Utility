#pragma once

#include <gtk/gtk.h>

/* Callback symbols referenced by ui/glade.ui.
   These are registered explicitly via GtkBuilderCScope in ui.c. */

G_MODULE_EXPORT void on_scanNetworks_clicked(GtkButton *btn, gpointer user_data);
G_MODULE_EXPORT void on_setAP_clicked(GtkButton *btn, gpointer user_data);
G_MODULE_EXPORT void on_buttonConnect_clicked(GtkButton *btn, gpointer user_data);
G_MODULE_EXPORT void on_buttonOK_clicked(GtkButton *btn, gpointer user_data);
G_MODULE_EXPORT void on_buttonCancel_clicked(GtkButton *btn, gpointer user_data);
G_MODULE_EXPORT void on_ssidStruct_selected_changed(GObject *obj, GParamSpec *pspec, gpointer user_data);
