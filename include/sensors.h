#ifndef THERMALERT_SENSORS_H
#define THERMALERT_SENSORS_H

#include <glib.h>

typedef struct {
    gchar *id;
    gchar *chip;
    gchar *label;
    gdouble temperature_c;
} ThermalSensorReading;

typedef struct {
    GPtrArray *readings;
    gboolean has_readings;
    gdouble highest_c;
} ThermalSensorSnapshot;

ThermalSensorSnapshot *thermal_sensors_read(GError **error);
const ThermalSensorReading *thermal_sensors_find(
    const ThermalSensorSnapshot *snapshot, const gchar *sensor_id);
const ThermalSensorReading *thermal_sensors_select_primary(
    const ThermalSensorSnapshot *snapshot, const gchar *configured_id);
void thermal_sensor_snapshot_free(ThermalSensorSnapshot *snapshot);

#endif
