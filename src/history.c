#include "history.h"

#include <math.h>

struct ThermalSensorHistory {
    gchar *id;
    gchar *chip;
    gchar *label;
    ThermalHistorySample *samples;
    guint capacity;
    guint count;
    guint start;
};

struct ThermalHistory {
    GHashTable *sensors;
    guint capacity;
};

static void
sensor_history_free(gpointer data)
{
    ThermalSensorHistory *sensor = data;

    if (sensor == NULL)
        return;
    g_free(sensor->id);
    g_free(sensor->chip);
    g_free(sensor->label);
    g_free(sensor->samples);
    g_free(sensor);
}

static void
append_sample(ThermalSensorHistory *sensor, gint64 timestamp_us,
              gboolean valid, gdouble temperature_c)
{
    guint position;

    if (sensor->count < sensor->capacity) {
        position = (sensor->start + sensor->count) % sensor->capacity;
        sensor->count++;
    } else {
        position = sensor->start;
        sensor->start = (sensor->start + 1) % sensor->capacity;
    }
    sensor->samples[position].timestamp_us = timestamp_us;
    sensor->samples[position].temperature_c = temperature_c;
    sensor->samples[position].valid = valid;
}

static ThermalSensorHistory *
sensor_history_new(const ThermalSensorReading *reading, guint capacity)
{
    ThermalSensorHistory *sensor = g_new0(ThermalSensorHistory, 1);

    sensor->id = g_strdup(reading->id);
    sensor->chip = g_strdup(reading->chip);
    sensor->label = g_strdup(reading->label);
    sensor->samples = g_new0(ThermalHistorySample, capacity);
    sensor->capacity = capacity;
    return sensor;
}

ThermalHistory *
thermal_history_new(guint capacity)
{
    ThermalHistory *history;

    g_return_val_if_fail(capacity > 0, NULL);
    history = g_new0(ThermalHistory, 1);
    history->capacity = capacity;
    history->sensors = g_hash_table_new_full(
        g_str_hash, g_str_equal, NULL, sensor_history_free);
    return history;
}

void
thermal_history_free(ThermalHistory *history)
{
    if (history == NULL)
        return;
    g_hash_table_unref(history->sensors);
    g_free(history);
}

static void
append_missing_sample(gpointer key, gpointer value, gpointer user_data)
{
    ThermalSensorHistory *sensor = value;
    const gint64 *timestamp_us = user_data;

    (void)key;
    append_sample(sensor, *timestamp_us, FALSE, 0.0);
}

void
thermal_history_record_snapshot(ThermalHistory *history,
                                const ThermalSensorSnapshot *snapshot,
                                gint64 timestamp_us)
{
    guint i;

    g_return_if_fail(history != NULL);
    g_hash_table_foreach(history->sensors, append_missing_sample,
                         &timestamp_us);
    if (snapshot == NULL)
        return;

    for (i = 0; i < snapshot->readings->len; i++) {
        const ThermalSensorReading *reading =
            g_ptr_array_index(snapshot->readings, i);
        ThermalSensorHistory *sensor;
        guint position;

        if (reading->id == NULL || *reading->id == '\0' ||
            !isfinite(reading->temperature_c))
            continue;
        sensor = g_hash_table_lookup(history->sensors, reading->id);
        if (sensor == NULL) {
            sensor = sensor_history_new(reading, history->capacity);
            g_hash_table_insert(history->sensors, sensor->id, sensor);
            append_sample(sensor, timestamp_us, TRUE,
                          reading->temperature_c);
            continue;
        }
        g_free(sensor->chip);
        g_free(sensor->label);
        sensor->chip = g_strdup(reading->chip);
        sensor->label = g_strdup(reading->label);
        position = (sensor->start + sensor->count - 1) % sensor->capacity;
        sensor->samples[position].temperature_c = reading->temperature_c;
        sensor->samples[position].valid = TRUE;
    }
}

guint
thermal_history_sensor_count(const ThermalHistory *history)
{
    return history != NULL ? g_hash_table_size(history->sensors) : 0;
}

const ThermalSensorHistory *
thermal_history_lookup(const ThermalHistory *history, const gchar *sensor_id)
{
    if (history == NULL || sensor_id == NULL)
        return NULL;
    return g_hash_table_lookup(history->sensors, sensor_id);
}

const gchar *
thermal_sensor_history_id(const ThermalSensorHistory *sensor)
{
    return sensor != NULL ? sensor->id : NULL;
}

const gchar *
thermal_sensor_history_chip(const ThermalSensorHistory *sensor)
{
    return sensor != NULL ? sensor->chip : NULL;
}

const gchar *
thermal_sensor_history_label(const ThermalSensorHistory *sensor)
{
    return sensor != NULL ? sensor->label : NULL;
}

guint
thermal_sensor_history_sample_count(const ThermalSensorHistory *sensor)
{
    return sensor != NULL ? sensor->count : 0;
}

gboolean
thermal_sensor_history_sample_at(const ThermalSensorHistory *sensor,
                                 guint index,
                                 ThermalHistorySample *sample)
{
    guint position;

    g_return_val_if_fail(sample != NULL, FALSE);
    if (sensor == NULL || index >= sensor->count)
        return FALSE;
    position = (sensor->start + index) % sensor->capacity;
    *sample = sensor->samples[position];
    return TRUE;
}

gboolean
thermal_sensor_history_statistics(const ThermalSensorHistory *sensor,
                                  gdouble *current_c,
                                  gboolean *has_current,
                                  gdouble *minimum_c,
                                  gdouble *maximum_c)
{
    ThermalHistorySample sample;
    gboolean found = FALSE;
    guint i;

    if (sensor == NULL || sensor->count == 0)
        return FALSE;
    if (has_current != NULL) {
        thermal_sensor_history_sample_at(sensor, sensor->count - 1, &sample);
        *has_current = sample.valid;
        if (current_c != NULL && sample.valid)
            *current_c = sample.temperature_c;
    }
    for (i = 0; i < sensor->count; i++) {
        thermal_sensor_history_sample_at(sensor, i, &sample);
        if (!sample.valid)
            continue;
        if (!found) {
            if (minimum_c != NULL)
                *minimum_c = sample.temperature_c;
            if (maximum_c != NULL)
                *maximum_c = sample.temperature_c;
            found = TRUE;
        } else {
            if (minimum_c != NULL && sample.temperature_c < *minimum_c)
                *minimum_c = sample.temperature_c;
            if (maximum_c != NULL && sample.temperature_c > *maximum_c)
                *maximum_c = sample.temperature_c;
        }
    }
    return found;
}
