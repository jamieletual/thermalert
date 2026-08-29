#include "indicator.h"

#include <gtk/gtk.h>
#include <libnotify/notify.h>

#include "config.h"

struct _ThermalIndicator {
    AppIndicator *app_indicator;
    GtkWidget *menu;
    GtkWidget *state_item;
    GtkWidget *highest_item;
    GtkWidget *sensor_separator;
    GPtrArray *sensor_rows;
    GPtrArray *sensor_chip_items;
    GCallback monitor_callback;
    gpointer monitor_data;
    GCallback preferences_callback;
    gpointer preferences_data;
    GCallback quit_callback;
    gpointer quit_data;
    gchar *panel_label;
};

typedef struct {
    gchar *chip;
    gchar *label;
    GtkWidget *item; /* Borrowed: owned by its submenu. */
} SensorRow;

static void
sensor_row_free(gpointer data)
{
    SensorRow *row = data;

    g_free(row->chip);
    g_free(row->label);
    g_free(row);
}

static const gchar *
icon_for_state(ThermalState state)
{
    switch (state) {
    case THERMAL_STATE_WARNING:
        return "thermalert-warning";
    case THERMAL_STATE_CRITICAL:
        return "thermalert-critical";
    case THERMAL_STATE_UNKNOWN:
        return "thermalert-unknown";
    case THERMAL_STATE_NORMAL:
    default:
        return "thermalert-normal";
    }
}

static GtkWidget *
informational_item(const gchar *text)
{
    GtkWidget *item = gtk_menu_item_new_with_label(text);
    gtk_widget_set_sensitive(item, FALSE);
    return item;
}

static void
show_about(GtkMenuItem *item, gpointer data)
{
    const gchar *authors[] = { "Jamie Le Tual", NULL };

    (void)item;
    (void)data;
    gtk_show_about_dialog(
        NULL,
        "program-name", "Thermalert",
        "version", THERMALERT_VERSION,
        "comments", "A lightweight temperature monitor for Ubuntu.",
        "copyright", "Copyright (C) 2026 Jamie Le Tual",
        "license-type", GTK_LICENSE_GPL_3_0,
        "authors", authors,
        "website", "https://github.com/jamieletual/thermalert",
        "website-label", "Thermalert on GitHub",
        NULL);
}

static void
build_menu(ThermalIndicator *indicator)
{
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item;

    indicator->menu = menu;
    indicator->state_item = informational_item("");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), indicator->state_item);
    indicator->highest_item = informational_item("");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), indicator->highest_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    indicator->sensor_separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), indicator->sensor_separator);
    item = gtk_menu_item_new_with_label("Temperature Monitor…");
    g_signal_connect(item, "activate", indicator->monitor_callback,
                     indicator->monitor_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    item = gtk_menu_item_new_with_label("Preferences…");
    g_signal_connect(item, "activate", indicator->preferences_callback,
                     indicator->preferences_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    item = gtk_menu_item_new_with_label("About Thermalert");
    g_signal_connect(item, "activate", G_CALLBACK(show_about), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

    item = gtk_menu_item_new_with_label("Quit");
    g_signal_connect(item, "activate", indicator->quit_callback,
                     indicator->quit_data);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    gtk_widget_show_all(menu);
    app_indicator_set_menu(indicator->app_indicator, GTK_MENU(menu));
}

static gboolean
sensor_topology_matches(ThermalIndicator *indicator,
                        const ThermalSensorSnapshot *snapshot)
{
    guint count = snapshot != NULL ? snapshot->readings->len : 0;
    guint i;

    if (indicator->sensor_rows->len != count)
        return FALSE;
    for (i = 0; i < count; i++) {
        SensorRow *row = g_ptr_array_index(indicator->sensor_rows, i);
        const ThermalSensorReading *reading =
            g_ptr_array_index(snapshot->readings, i);
        if (g_strcmp0(row->chip, reading->chip) != 0 ||
            g_strcmp0(row->label, reading->label) != 0)
            return FALSE;
    }
    return TRUE;
}

static void
rebuild_sensor_rows(ThermalIndicator *indicator,
                    const ThermalSensorSnapshot *snapshot)
{
    GtkWidget *submenu = NULL;
    const gchar *last_chip = NULL;
    guint i;

    /* Each chip item owns its submenu and rows. Destroy only those top-level
     * owners, then immediately discard every borrowed widget pointer. */
    for (i = 0; i < indicator->sensor_chip_items->len; i++)
        gtk_widget_destroy(g_ptr_array_index(indicator->sensor_chip_items, i));
    g_ptr_array_set_size(indicator->sensor_chip_items, 0);
    g_ptr_array_set_size(indicator->sensor_rows, 0);

    if (snapshot == NULL)
        return;
    for (i = 0; i < snapshot->readings->len; i++) {
        const ThermalSensorReading *reading =
            g_ptr_array_index(snapshot->readings, i);
        SensorRow *row;
        gchar *text;

        if (g_strcmp0(last_chip, reading->chip) != 0) {
            GtkWidget *chip_item = gtk_menu_item_new_with_label(reading->chip);
            submenu = gtk_menu_new();
            gtk_menu_item_set_submenu(GTK_MENU_ITEM(chip_item), submenu);
            gtk_menu_shell_insert(GTK_MENU_SHELL(indicator->menu), chip_item,
                                  3 + indicator->sensor_chip_items->len);
            g_ptr_array_add(indicator->sensor_chip_items, chip_item);
            last_chip = reading->chip;
        }
        text = g_strdup_printf("%s    %.1f °C", reading->label,
                               reading->temperature_c);
        row = g_new0(SensorRow, 1);
        row->chip = g_strdup(reading->chip);
        row->label = g_strdup(reading->label);
        row->item = informational_item(text);
        g_free(text);
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), row->item);
        g_ptr_array_add(indicator->sensor_rows, row);
    }
    gtk_widget_show_all(indicator->menu);
}

ThermalIndicator *
thermal_indicator_new(const gchar *icon_directory,
                      GCallback monitor_callback,
                      gpointer monitor_data,
                      GCallback preferences_callback,
                      gpointer preferences_data,
                      GCallback quit_callback,
                      gpointer quit_data)
{
    ThermalIndicator *indicator = g_new0(ThermalIndicator, 1);

    indicator->monitor_callback = monitor_callback;
    indicator->monitor_data = monitor_data;
    indicator->preferences_callback = preferences_callback;
    indicator->preferences_data = preferences_data;
    indicator->quit_callback = quit_callback;
    indicator->quit_data = quit_data;
    indicator->app_indicator = app_indicator_new(
        "thermalert", "thermalert-unknown", APP_INDICATOR_CATEGORY_HARDWARE);
    app_indicator_set_icon_theme_path(indicator->app_indicator, icon_directory);
    app_indicator_set_title(indicator->app_indicator, "Thermalert");
    notify_init("Thermalert");
    indicator->sensor_rows = g_ptr_array_new_with_free_func(sensor_row_free);
    indicator->sensor_chip_items = g_ptr_array_new();
    build_menu(indicator);
    indicator->panel_label = g_strdup("-- °C");
    app_indicator_set_label(indicator->app_indicator,
                            indicator->panel_label, "100.0 °C");
    return indicator;
}

void
thermal_indicator_sync_startup_label(ThermalIndicator *indicator,
                                     gboolean exact_label)
{
    gchar *variant = NULL;

    g_return_if_fail(indicator != NULL);
    if (!exact_label)
        variant = g_strconcat(indicator->panel_label, "\xE2\x80\x8B", NULL);
    app_indicator_set_label(indicator->app_indicator,
        exact_label ? indicator->panel_label : variant, "100.0 °C");
    g_free(variant);
}

void
thermal_indicator_activate(ThermalIndicator *indicator)
{
    g_return_if_fail(indicator != NULL);
    app_indicator_set_status(indicator->app_indicator,
                             APP_INDICATOR_STATUS_ACTIVE);
}

void
thermal_indicator_update(ThermalIndicator *indicator,
                         ThermalState state,
                         const ThermalSensorSnapshot *snapshot,
                         const ThermalSensorReading *primary)
{
    gchar *panel_label;
    gchar *text;
    guint i;

    g_return_if_fail(indicator != NULL);
    app_indicator_set_icon_full(indicator->app_indicator, icon_for_state(state),
                                thermal_state_name(state));
    if (primary != NULL)
        panel_label = g_strdup_printf("%.1f °C", primary->temperature_c);
    else
        panel_label = g_strdup("-- °C");
    g_free(indicator->panel_label);
    indicator->panel_label = panel_label;
    app_indicator_set_label(indicator->app_indicator,
                            indicator->panel_label, "100.0 °C");

    text = g_strdup_printf("Thermalert — %s", thermal_state_name(state));
    gtk_menu_item_set_label(GTK_MENU_ITEM(indicator->state_item), text);
    g_free(text);
    if (primary != NULL)
        text = g_strdup_printf("Primary: %s — %.1f °C", primary->label,
                               primary->temperature_c);
    else
        text = g_strdup("Primary temperature reading unavailable");
    gtk_menu_item_set_label(GTK_MENU_ITEM(indicator->highest_item), text);
    g_free(text);

    if (!sensor_topology_matches(indicator, snapshot))
        rebuild_sensor_rows(indicator, snapshot);
    for (i = 0; snapshot != NULL && i < snapshot->readings->len; i++) {
        SensorRow *row = g_ptr_array_index(indicator->sensor_rows, i);
        const ThermalSensorReading *reading =
            g_ptr_array_index(snapshot->readings, i);
        text = g_strdup_printf("%s    %.1f °C", reading->label,
                               reading->temperature_c);
        gtk_menu_item_set_label(GTK_MENU_ITEM(row->item), text);
        g_free(text);
    }
}

void
thermal_indicator_notify_transition(ThermalIndicator *indicator,
                                    ThermalState previous,
                                    ThermalState current,
                                    const ThermalSensorReading *primary)
{
    NotifyNotification *notification;
    gchar *body;
    GError *error = NULL;

    g_return_if_fail(indicator != NULL);
    if (current != THERMAL_STATE_CRITICAL || previous == THERMAL_STATE_CRITICAL)
        return;

    if (primary != NULL)
        body = g_strdup_printf("%s temperature: %.1f °C", primary->label,
                               primary->temperature_c);
    else
        body = g_strdup("A high temperature state was detected.");

    notification = notify_notification_new("Critical temperature", body,
                                           icon_for_state(current));
    notify_notification_set_urgency(notification, NOTIFY_URGENCY_CRITICAL);
    if (!notify_notification_show(notification, &error)) {
        g_warning("Could not display a desktop notification: %s",
                  error->message);
        g_clear_error(&error);
    }
    g_object_unref(notification);
    g_free(body);
}

void
thermal_indicator_free(ThermalIndicator *indicator)
{
    if (indicator == NULL)
        return;
    if (indicator->menu != NULL)
        gtk_widget_destroy(indicator->menu);
    g_ptr_array_free(indicator->sensor_rows, TRUE);
    g_ptr_array_free(indicator->sensor_chip_items, TRUE);
    g_clear_object(&indicator->app_indicator);
    if (notify_is_initted())
        notify_uninit();
    g_free(indicator->panel_label);
    g_free(indicator);
}
