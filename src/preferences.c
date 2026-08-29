#include "preferences.h"

#include <errno.h>

#include <glib/gstdio.h>

#include "i18n.h"

#define AUTOSTART_DESKTOP_FILE "com.thermalert.Thermalert-autostart.desktop"
#define AUTOSTART_CONTENT \
    "[Desktop Entry]\n" \
    "Type=Application\n" \
    "Name=Thermalert\n" \
    "Comment=Monitor hardware temperatures from the system status area\n" \
    "Comment[fr]=Surveiller les températures matérielles dans la zone d’état du système\n" \
    "Exec=thermalert\n" \
    "Icon=thermalert-normal\n" \
    "Terminal=false\n" \
    "StartupNotify=false\n" \
    "X-GNOME-Autostart-enabled=true\n" \
    "OnlyShowIn=GNOME;Unity;\n"

struct _ThermalPreferences {
    GSettings *settings;
    GtkWidget *window;
    GtkSpinButton *warning;
    GtkSpinButton *critical;
    GtkSpinButton *hysteresis;
    GtkSpinButton *interval;
    GtkComboBoxText *sensor;
    GtkToggleButton *autostart;
    GtkToggleButton *notifications;
    gchar *sensor_signature;
    gchar *sensor_configured_id;
    gchar *automatic_sensor_id;
    GtkWidget *validation;
    gulong settings_handler;
    gboolean syncing;
};

static void preferences_sync(ThermalPreferences *preferences);

#define AUTOMATIC_SENSOR_ID "__automatic__"

static gboolean
hide_window(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    (void)event;
    (void)data;
    gtk_widget_hide(widget);
    return TRUE;
}

static void
value_changed(GtkSpinButton *spin, gpointer data)
{
    ThermalPreferences *preferences = data;
    gdouble warning;
    gdouble critical;

    (void)spin;
    if (preferences->syncing)
        return;

    warning = gtk_spin_button_get_value(preferences->warning);
    critical = gtk_spin_button_get_value(preferences->critical);
    if (critical <= warning) {
        gtk_label_set_text(GTK_LABEL(preferences->validation),
                           _("Critical temperature must be higher than warning."));
        gtk_widget_show(preferences->validation);
        return;
    }

    gtk_widget_hide(preferences->validation);
    g_settings_set_double(preferences->settings, "warning-temperature", warning);
    g_settings_set_double(preferences->settings, "critical-temperature", critical);
    g_settings_set_double(preferences->settings, "hysteresis",
                          gtk_spin_button_get_value(preferences->hysteresis));
    g_settings_set_uint(preferences->settings, "polling-interval",
                        (guint)gtk_spin_button_get_value_as_int(preferences->interval));
}

static void
sensor_changed(GtkComboBox *combo, gpointer data)
{
    ThermalPreferences *preferences = data;
    const gchar *active_id;

    if (preferences->syncing)
        return;
    active_id = gtk_combo_box_get_active_id(combo);
    if (active_id == NULL)
        return;
    g_settings_set_string(preferences->settings, "primary-sensor",
                          g_strcmp0(active_id, AUTOMATIC_SENSOR_ID) == 0
                              ? "" : active_id);
}

static gchar *
autostart_path(void)
{
    return g_build_filename(g_get_user_config_dir(), "autostart",
                            AUTOSTART_DESKTOP_FILE, NULL);
}

static void
sync_autostart(ThermalPreferences *preferences)
{
    gchar *path = autostart_path();

    preferences->syncing = TRUE;
    gtk_toggle_button_set_active(preferences->autostart,
                                 g_file_test(path, G_FILE_TEST_IS_REGULAR));
    preferences->syncing = FALSE;
    g_free(path);
}

static void
autostart_toggled(GtkToggleButton *button, gpointer data)
{
    ThermalPreferences *preferences = data;
    gboolean enabled;
    gchar *directory;
    gchar *path;
    GError *error = NULL;

    if (preferences->syncing)
        return;
    enabled = gtk_toggle_button_get_active(button);
    directory = g_build_filename(g_get_user_config_dir(), "autostart", NULL);
    path = g_build_filename(directory, AUTOSTART_DESKTOP_FILE, NULL);

    if (enabled) {
        if (g_mkdir_with_parents(directory, 0700) != 0)
            g_set_error(&error, G_FILE_ERROR, g_file_error_from_errno(errno),
                        _("Could not create %s: %s"), directory,
                        g_strerror(errno));
        else
            g_file_set_contents(path, AUTOSTART_CONTENT, -1, &error);
    } else if (g_remove(path) != 0 && errno != ENOENT) {
        g_set_error(&error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    _("Could not remove %s: %s"), path, g_strerror(errno));
    }

    if (error != NULL) {
        gtk_label_set_text(GTK_LABEL(preferences->validation), error->message);
        gtk_widget_show(preferences->validation);
        g_clear_error(&error);
        sync_autostart(preferences);
    } else {
        gtk_widget_hide(preferences->validation);
    }
    g_free(path);
    g_free(directory);
}

static void
notifications_toggled(GtkToggleButton *button, gpointer data)
{
    ThermalPreferences *preferences = data;

    if (preferences->syncing)
        return;
    g_settings_set_boolean(preferences->settings, "notifications-enabled",
                           gtk_toggle_button_get_active(button));
}

static void
settings_changed(GSettings *settings, gchar *key, gpointer data)
{
    (void)settings;
    (void)key;
    preferences_sync(data);
}

static GtkWidget *
add_spin_row(GtkGrid *grid,
             gint row,
             const gchar *label_text,
             gdouble minimum,
             gdouble maximum,
             gdouble step,
             GtkSpinButton **spin_out)
{
    GtkWidget *label = gtk_label_new(label_text);
    GtkWidget *spin = gtk_spin_button_new_with_range(minimum, maximum, step);

    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_set_hexpand(spin, TRUE);
    gtk_grid_attach(grid, label, 0, row, 1, 1);
    gtk_grid_attach(grid, spin, 1, row, 1, 1);
    *spin_out = GTK_SPIN_BUTTON(spin);
    return spin;
}

static void
preferences_sync(ThermalPreferences *preferences)
{
    preferences->syncing = TRUE;
    gtk_spin_button_set_value(
        preferences->warning,
        g_settings_get_double(preferences->settings, "warning-temperature"));
    gtk_spin_button_set_value(
        preferences->critical,
        g_settings_get_double(preferences->settings, "critical-temperature"));
    gtk_spin_button_set_value(
        preferences->hysteresis,
        g_settings_get_double(preferences->settings, "hysteresis"));
    gtk_spin_button_set_value(
        preferences->interval,
        g_settings_get_uint(preferences->settings, "polling-interval"));
    gtk_toggle_button_set_active(
        preferences->notifications,
        g_settings_get_boolean(preferences->settings,
                               "notifications-enabled"));
    preferences->syncing = FALSE;
    gtk_widget_hide(preferences->validation);
}

ThermalPreferences *
thermal_preferences_new(GSettings *settings)
{
    ThermalPreferences *preferences;
    GtkWidget *grid;
    GtkWidget *label;
    GtkWidget *spin;

    g_return_val_if_fail(G_IS_SETTINGS(settings), NULL);
    preferences = g_new0(ThermalPreferences, 1);
    preferences->settings = g_object_ref(settings);
    preferences->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(preferences->window),
                         _("Thermalert Preferences"));
    gtk_window_set_default_size(GTK_WINDOW(preferences->window), 360, -1);
    gtk_container_set_border_width(GTK_CONTAINER(preferences->window), 18);
    g_signal_connect(preferences->window, "delete-event",
                     G_CALLBACK(hide_window), preferences);

    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 18);
    gtk_container_add(GTK_CONTAINER(preferences->window), grid);

    label = gtk_label_new(_("Primary temperature sensor"));
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    preferences->sensor = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_widget_set_hexpand(GTK_WIDGET(preferences->sensor), TRUE);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(preferences->sensor), 1, 0, 1, 1);

    preferences->autostart = GTK_TOGGLE_BUTTON(gtk_check_button_new_with_label(
        _("Start Thermalert automatically when I log in")));
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(preferences->autostart),
                    0, 1, 2, 1);

    preferences->notifications = GTK_TOGGLE_BUTTON(
        gtk_check_button_new_with_label(
            _("Enable critical-temperature notifications")));
    gtk_grid_attach(GTK_GRID(grid), GTK_WIDGET(preferences->notifications),
                    0, 2, 2, 1);

    spin = add_spin_row(GTK_GRID(grid), 3, _("Warning temperature (°C)"),
                        1.0, 149.0, 1.0, &preferences->warning);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 1);
    spin = add_spin_row(GTK_GRID(grid), 4, _("Critical temperature (°C)"),
                        2.0, 150.0, 1.0, &preferences->critical);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 1);
    spin = add_spin_row(GTK_GRID(grid), 5, _("Hysteresis (°C)"),
                        0.0, 30.0, 1.0, &preferences->hysteresis);
    gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin), 1);
    add_spin_row(GTK_GRID(grid), 6, _("Polling interval (seconds)"),
                 1.0, 300.0, 1.0, &preferences->interval);

    preferences->validation = gtk_label_new(NULL);
    gtk_widget_set_halign(preferences->validation, GTK_ALIGN_START);
    gtk_label_set_line_wrap(GTK_LABEL(preferences->validation), TRUE);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(preferences->validation), "error");
    gtk_grid_attach(GTK_GRID(grid), preferences->validation, 0, 7, 2, 1);

    g_signal_connect(preferences->sensor, "changed",
                     G_CALLBACK(sensor_changed), preferences);
    g_signal_connect(preferences->autostart, "toggled",
                     G_CALLBACK(autostart_toggled), preferences);
    g_signal_connect(preferences->notifications, "toggled",
                     G_CALLBACK(notifications_toggled), preferences);
    g_signal_connect(preferences->warning, "value-changed",
                     G_CALLBACK(value_changed), preferences);
    g_signal_connect(preferences->critical, "value-changed",
                     G_CALLBACK(value_changed), preferences);
    g_signal_connect(preferences->hysteresis, "value-changed",
                     G_CALLBACK(value_changed), preferences);
    g_signal_connect(preferences->interval, "value-changed",
                     G_CALLBACK(value_changed), preferences);
    preferences->settings_handler = g_signal_connect(
        settings, "changed", G_CALLBACK(settings_changed), preferences);
    preferences_sync(preferences);
    sync_autostart(preferences);
    return preferences;
}

void
thermal_preferences_update_sensors(
    ThermalPreferences *preferences, const ThermalSensorSnapshot *snapshot,
    const ThermalSensorReading *automatic_sensor)
{
    gchar *configured_id;
    gchar *automatic_label;
    gboolean configured_available;
    GString *signature;
    guint i;

    g_return_if_fail(preferences != NULL);
    configured_id = g_settings_get_string(preferences->settings,
                                           "primary-sensor");
    signature = g_string_new(NULL);
    for (i = 0; snapshot != NULL && i < snapshot->readings->len; i++) {
        const ThermalSensorReading *reading =
            g_ptr_array_index(snapshot->readings, i);
        g_string_append_len(signature, reading->id, -1);
        g_string_append_c(signature, '\n');
    }
    if (g_strcmp0(preferences->sensor_signature, signature->str) == 0 &&
        g_strcmp0(preferences->sensor_configured_id, configured_id) == 0 &&
        g_strcmp0(preferences->automatic_sensor_id,
                  automatic_sensor != NULL ? automatic_sensor->id : NULL) == 0) {
        g_string_free(signature, TRUE);
        g_free(configured_id);
        return;
    }
    configured_available = thermal_sensors_find(snapshot, configured_id) != NULL;
    if (automatic_sensor != NULL)
        automatic_label = g_strdup_printf(_("Automatic — %s (%s)"),
                                          automatic_sensor->label,
                                          automatic_sensor->chip);
    else
        automatic_label = g_strdup(_("Automatic — no sensors available"));

    preferences->syncing = TRUE;
    gtk_combo_box_text_remove_all(preferences->sensor);
    gtk_combo_box_text_append(preferences->sensor, AUTOMATIC_SENSOR_ID,
                              automatic_label);
    for (i = 0; snapshot != NULL && i < snapshot->readings->len; i++) {
        const ThermalSensorReading *reading =
            g_ptr_array_index(snapshot->readings, i);
        gchar *label = g_strdup_printf("%s — %s", reading->label,
                                       reading->chip);
        gtk_combo_box_text_append(preferences->sensor, reading->id, label);
        g_free(label);
    }
    if (*configured_id != '\0' && !configured_available) {
        gchar *label = g_strdup_printf(_("Unavailable — %s"), configured_id);
        gtk_combo_box_text_append(preferences->sensor, configured_id, label);
        g_free(label);
    }
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(preferences->sensor),
                                *configured_id != '\0'
                                    ? configured_id : AUTOMATIC_SENSOR_ID);
    preferences->syncing = FALSE;

    g_free(preferences->sensor_signature);
    preferences->sensor_signature = g_string_free(signature, FALSE);
    g_free(preferences->sensor_configured_id);
    preferences->sensor_configured_id = g_strdup(configured_id);
    g_free(preferences->automatic_sensor_id);
    preferences->automatic_sensor_id =
        automatic_sensor != NULL ? g_strdup(automatic_sensor->id) : NULL;

    g_free(automatic_label);
    g_free(configured_id);
}

void
thermal_preferences_present(ThermalPreferences *preferences)
{
    g_return_if_fail(preferences != NULL);
    sync_autostart(preferences);
    gtk_widget_show_all(preferences->window);
    if (gtk_window_is_active(GTK_WINDOW(preferences->window)))
        gtk_window_present(GTK_WINDOW(preferences->window));
    else
        gtk_window_present_with_time(GTK_WINDOW(preferences->window),
                                     gtk_get_current_event_time());
    if (gtk_label_get_text(GTK_LABEL(preferences->validation))[0] == '\0')
        gtk_widget_hide(preferences->validation);
}

void
thermal_preferences_free(ThermalPreferences *preferences)
{
    if (preferences == NULL)
        return;
    if (preferences->settings_handler != 0)
        g_signal_handler_disconnect(preferences->settings,
                                    preferences->settings_handler);
    gtk_widget_destroy(preferences->window);
    g_object_unref(preferences->settings);
    g_free(preferences->sensor_signature);
    g_free(preferences->sensor_configured_id);
    g_free(preferences->automatic_sensor_id);
    g_free(preferences);
}
