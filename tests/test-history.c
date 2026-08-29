#include <glib.h>

#include "history.h"

static void
reading_free(gpointer data)
{
    ThermalSensorReading *reading = data;

    g_free(reading->id);
    g_free(reading->chip);
    g_free(reading->label);
    g_free(reading);
}

static ThermalSensorSnapshot *
snapshot_new(const gchar *id, const gchar *chip, const gchar *label,
             gdouble temperature_c)
{
    ThermalSensorSnapshot *snapshot = g_new0(ThermalSensorSnapshot, 1);

    snapshot->readings = g_ptr_array_new_with_free_func(reading_free);
    if (id != NULL) {
        ThermalSensorReading *reading = g_new0(ThermalSensorReading, 1);
        reading->id = g_strdup(id);
        reading->chip = g_strdup(chip);
        reading->label = g_strdup(label);
        reading->temperature_c = temperature_c;
        g_ptr_array_add(snapshot->readings, reading);
    }
    return snapshot;
}

static void
history_tracks_gaps_and_bounds(void)
{
    ThermalHistory *history = thermal_history_new(3);
    ThermalSensorSnapshot *snapshot;
    const ThermalSensorHistory *sensor;
    ThermalHistorySample sample = { 0 };
    gdouble current = 0.0;
    gdouble minimum = 0.0;
    gdouble maximum = 0.0;
    gboolean has_current = FALSE;

    snapshot = snapshot_new("chip/temp1", "chip", "CPU", 50.0);
    thermal_history_record_snapshot(history, snapshot, 100);
    thermal_sensor_snapshot_free(snapshot);
    snapshot = snapshot_new("chip/temp1", "chip", "CPU", 60.0);
    thermal_history_record_snapshot(history, snapshot, 200);
    thermal_sensor_snapshot_free(snapshot);
    snapshot = snapshot_new(NULL, NULL, NULL, 0.0);
    thermal_history_record_snapshot(history, snapshot, 300);
    thermal_sensor_snapshot_free(snapshot);
    snapshot = snapshot_new("chip/temp1", "chip", "Processor", 70.0);
    thermal_history_record_snapshot(history, snapshot, 400);
    thermal_sensor_snapshot_free(snapshot);

    g_assert_cmpuint(thermal_history_sensor_count(history), ==, 1);
    sensor = thermal_history_lookup(history, "chip/temp1");
    g_assert_nonnull(sensor);
    g_assert_cmpstr(thermal_sensor_history_label(sensor), ==, "Processor");
    g_assert_cmpuint(thermal_sensor_history_sample_count(sensor), ==, 3);
    g_assert_true(thermal_sensor_history_sample_at(sensor, 0, &sample));
    g_assert_cmpint(sample.timestamp_us, ==, 200);
    g_assert_true(sample.valid);
    g_assert_cmpfloat(sample.temperature_c, ==, 60.0);
    g_assert_true(thermal_sensor_history_sample_at(sensor, 1, &sample));
    g_assert_false(sample.valid);
    g_assert_true(thermal_sensor_history_statistics(
        sensor, &current, &has_current, &minimum, &maximum));
    g_assert_true(has_current);
    g_assert_cmpfloat(current, ==, 70.0);
    g_assert_cmpfloat(minimum, ==, 60.0);
    g_assert_cmpfloat(maximum, ==, 70.0);

    thermal_history_free(history);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/history/gaps-and-bounds", history_tracks_gaps_and_bounds);
    return g_test_run();
}
