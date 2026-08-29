#include <gio/gio.h>
#include <gtk/gtk.h>

#include "config.h"
#include "history.h"
#include "indicator.h"
#include "monitor.h"
#include "preferences.h"
#include "sensors.h"
#include "thermal.h"

#define SETTINGS_ID "com.thermalert.Thermalert"

typedef struct {
    GSettings *settings;
    ThermalIndicator *indicator;
    ThermalPreferences *preferences;
    ThermalMonitor *monitor;
    ThermalSensorSnapshot *snapshot;
    ThermalHistory *history;
    ThermalState state;
    guint timer_id;
    guint startup_label_id;
    guint startup_label_phase;
    guint polling_interval;
    gboolean sensor_error_reported;
} ThermalApplication;

static gboolean
synchronize_startup_label(gpointer data)
{
    ThermalApplication *application = data;
    gboolean exact_label = (application->startup_label_phase % 2) != 0;

    thermal_indicator_sync_startup_label(application->indicator,
                                         exact_label);
    application->startup_label_phase++;
    if (application->startup_label_phase >= 6) {
        application->startup_label_id = 0;
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static ThermalThresholds
read_thresholds(GSettings *settings)
{
    ThermalThresholds thresholds;

    thresholds.warning_c =
        g_settings_get_double(settings, "warning-temperature");
    thresholds.critical_c =
        g_settings_get_double(settings, "critical-temperature");
    thresholds.hysteresis_c = g_settings_get_double(settings, "hysteresis");
    return thresholds;
}

static gboolean
poll_sensors(gpointer data)
{
    ThermalApplication *application = data;
    ThermalSensorSnapshot *snapshot;
    const ThermalSensorReading *primary;
    ThermalThresholds thresholds;
    ThermalState previous = application->state;
    GError *error = NULL;
    gchar *configured_sensor;

    snapshot = thermal_sensors_read(&error);
    if (snapshot == NULL) {
        if (!application->sensor_error_reported) {
            g_warning("Unable to read temperature sensors: %s", error->message);
            application->sensor_error_reported = TRUE;
        }
        g_clear_error(&error);
    } else {
        application->sensor_error_reported = FALSE;
    }

    thermal_sensor_snapshot_free(application->snapshot);
    application->snapshot = snapshot;
    configured_sensor = g_settings_get_string(application->settings,
                                               "primary-sensor");
    primary = thermal_sensors_select_primary(snapshot, configured_sensor);
    g_free(configured_sensor);
    thermal_history_record_snapshot(application->history, snapshot,
                                    g_get_monotonic_time());
    thresholds = read_thresholds(application->settings);
    application->state = thermal_state_evaluate(
        previous, primary != NULL,
        primary != NULL ? primary->temperature_c : 0.0, &thresholds);
    thermal_indicator_update(application->indicator, application->state,
                             application->snapshot, primary);
    if (g_settings_get_boolean(application->settings,
                               "notifications-enabled"))
        thermal_indicator_notify_transition(application->indicator, previous,
                                            application->state, primary);
    thermal_preferences_update_sensors(application->preferences,
                                       application->snapshot,
                                       thermal_sensors_select_primary(
                                           application->snapshot, NULL));
    thermal_monitor_update(application->monitor, application->history,
                           application->snapshot, primary, &thresholds);
    return G_SOURCE_CONTINUE;
}

static void
replace_timer(ThermalApplication *application)
{
    if (application->timer_id != 0) {
        g_source_remove(application->timer_id);
        application->timer_id = 0;
    }
    application->polling_interval =
        g_settings_get_uint(application->settings, "polling-interval");
    application->timer_id = g_timeout_add_seconds(
        application->polling_interval, poll_sensors, application);
}

static void
settings_changed(GSettings *settings, gchar *key, gpointer data)
{
    ThermalApplication *application = data;

    (void)settings;
    if (g_strcmp0(key, "polling-interval") == 0)
        replace_timer(application);
    poll_sensors(application);
}

static void
show_monitor(GtkMenuItem *item, gpointer data)
{
    ThermalApplication *application = data;

    (void)item;
    thermal_monitor_present(application->monitor);
}

static void
show_preferences(GtkMenuItem *item, gpointer data)
{
    ThermalApplication *application = data;

    (void)item;
    thermal_preferences_present(application->preferences);
}

static void
quit_application(GtkMenuItem *item, gpointer data)
{
    (void)item;
    (void)data;
    gtk_main_quit();
}

int
main(int argc, char **argv)
{
    ThermalApplication application = { 0 };
    const gchar *icon_directory;
    gulong settings_handler;

    if (!gtk_init_check(&argc, &argv)) {
        g_printerr("Thermalert requires a graphical GTK session.\n");
        return 1;
    }

    application.state = THERMAL_STATE_UNKNOWN;
    application.history = thermal_history_new(3600);
    application.settings = g_settings_new(SETTINGS_ID);
    application.monitor = thermal_monitor_new();
    application.preferences = thermal_preferences_new(application.settings);
    icon_directory = g_getenv("THERMALERT_ICON_DIR");
    if (icon_directory == NULL || *icon_directory == '\0')
        icon_directory = THERMALERT_ICON_DIR;
    application.indicator = thermal_indicator_new(
        icon_directory, G_CALLBACK(show_monitor), &application,
        G_CALLBACK(show_preferences), &application,
        G_CALLBACK(quit_application), &application);

    poll_sensors(&application);
    thermal_indicator_activate(application.indicator);
    application.startup_label_id = g_timeout_add(
        100, synchronize_startup_label, &application);
    replace_timer(&application);
    settings_handler = g_signal_connect(application.settings, "changed",
                                        G_CALLBACK(settings_changed),
                                        &application);
    gtk_main();

    if (application.timer_id != 0)
        g_source_remove(application.timer_id);
    if (application.startup_label_id != 0)
        g_source_remove(application.startup_label_id);
    g_signal_handler_disconnect(application.settings, settings_handler);
    thermal_sensor_snapshot_free(application.snapshot);
    thermal_history_free(application.history);
    thermal_monitor_free(application.monitor);
    thermal_preferences_free(application.preferences);
    thermal_indicator_free(application.indicator);
    g_object_unref(application.settings);
    return 0;
}
