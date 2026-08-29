#include "sensors.h"

#include <math.h>
#include <stdlib.h>

#include <sensors/sensors.h>

static void
reading_free(gpointer data)
{
    ThermalSensorReading *reading = data;

    if (reading == NULL)
        return;
    g_free(reading->id);
    g_free(reading->chip);
    g_free(reading->label);
    g_free(reading);
}

static gint
reading_compare(gconstpointer a, gconstpointer b)
{
    const ThermalSensorReading *left = *(ThermalSensorReading * const *)a;
    const ThermalSensorReading *right = *(ThermalSensorReading * const *)b;
    gint result = g_strcmp0(left->chip, right->chip);

    return result != 0 ? result : g_strcmp0(left->label, right->label);
}

ThermalSensorSnapshot *
thermal_sensors_read(GError **error)
{
    ThermalSensorSnapshot *snapshot;
    const sensors_chip_name *chip;
    gint chip_index = 0;
    gint status;

    status = sensors_init(NULL);
    if (status != 0) {
        g_set_error(error, G_FILE_ERROR, G_FILE_ERROR_FAILED,
                    "libsensors initialization failed with error %d", status);
        return NULL;
    }

    snapshot = g_new0(ThermalSensorSnapshot, 1);
    snapshot->readings = g_ptr_array_new_with_free_func(reading_free);

    while ((chip = sensors_get_detected_chips(NULL, &chip_index)) != NULL) {
        const sensors_feature *feature;
        gint feature_index = 0;
        gchar chip_name[256];

        if (sensors_snprintf_chip_name(chip_name, sizeof chip_name, chip) < 0)
            g_strlcpy(chip_name, "unknown device", sizeof chip_name);

        while ((feature = sensors_get_features(chip, &feature_index)) != NULL) {
            const sensors_subfeature *input;
            ThermalSensorReading *reading;
            gchar *label;
            gdouble value;

            if (feature->type != SENSORS_FEATURE_TEMP)
                continue;

            input = sensors_get_subfeature(chip, feature,
                                           SENSORS_SUBFEATURE_TEMP_INPUT);
            if (input == NULL || sensors_get_value(chip, input->number, &value) != 0 ||
                !isfinite(value))
                continue;

            label = sensors_get_label(chip, feature);
            reading = g_new0(ThermalSensorReading, 1);
            reading->id = g_strdup_printf("%s/%s", chip_name, feature->name);
            reading->chip = g_strdup(chip_name);
            reading->label = g_strdup(label != NULL ? label : feature->name);
            reading->temperature_c = value;
            free(label);
            g_ptr_array_add(snapshot->readings, reading);

            if (!snapshot->has_readings || value > snapshot->highest_c) {
                snapshot->has_readings = TRUE;
                snapshot->highest_c = value;
            }
        }
    }

    sensors_cleanup();
    g_ptr_array_sort(snapshot->readings, reading_compare);
    return snapshot;
}

const ThermalSensorReading *
thermal_sensors_find(const ThermalSensorSnapshot *snapshot,
                     const gchar *sensor_id)
{
    guint i;

    if (snapshot == NULL || sensor_id == NULL || *sensor_id == '\0')
        return NULL;
    for (i = 0; i < snapshot->readings->len; i++) {
        const ThermalSensorReading *reading =
            g_ptr_array_index(snapshot->readings, i);
        if (g_strcmp0(reading->id, sensor_id) == 0)
            return reading;
    }
    return NULL;
}

static guint
automatic_rank(const ThermalSensorReading *reading)
{
    if (g_ascii_strcasecmp(reading->label, "Package id 0") == 0)
        return 500;
    if (g_ascii_strcasecmp(reading->label, "Tctl") == 0)
        return 450;
    if (g_ascii_strcasecmp(reading->label, "Tdie") == 0)
        return 440;
    if (g_ascii_strcasecmp(reading->label, "CPU") == 0)
        return 400;
    if (g_ascii_strncasecmp(reading->label, "Core ", 5) == 0)
        return 300;
    return 100;
}

const ThermalSensorReading *
thermal_sensors_select_primary(const ThermalSensorSnapshot *snapshot,
                               const gchar *configured_id)
{
    const ThermalSensorReading *selected;
    guint selected_rank = 0;
    guint i;

    selected = thermal_sensors_find(snapshot, configured_id);
    if (selected != NULL)
        return selected;
    if (snapshot == NULL)
        return NULL;

    selected = NULL;
    for (i = 0; i < snapshot->readings->len; i++) {
        const ThermalSensorReading *reading =
            g_ptr_array_index(snapshot->readings, i);
        guint rank = automatic_rank(reading);

        if (selected == NULL || rank > selected_rank ||
            (rank == selected_rank &&
             reading->temperature_c > selected->temperature_c)) {
            selected = reading;
            selected_rank = rank;
        }
    }
    return selected;
}

void
thermal_sensor_snapshot_free(ThermalSensorSnapshot *snapshot)
{
    if (snapshot == NULL)
        return;
    g_ptr_array_unref(snapshot->readings);
    g_free(snapshot);
}
