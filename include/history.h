#ifndef THERMALERT_HISTORY_H
#define THERMALERT_HISTORY_H

#include <glib.h>

#include "sensors.h"

typedef struct ThermalHistory ThermalHistory;
typedef struct ThermalSensorHistory ThermalSensorHistory;

typedef struct {
    gint64 timestamp_us;
    gdouble temperature_c;
    gboolean valid;
} ThermalHistorySample;

ThermalHistory *thermal_history_new(guint capacity);
void thermal_history_free(ThermalHistory *history);
void thermal_history_record_snapshot(ThermalHistory *history,
                                     const ThermalSensorSnapshot *snapshot,
                                     gint64 timestamp_us);

guint thermal_history_sensor_count(const ThermalHistory *history);
const ThermalSensorHistory *thermal_history_lookup(
    const ThermalHistory *history, const gchar *sensor_id);
const gchar *thermal_sensor_history_id(const ThermalSensorHistory *sensor);
const gchar *thermal_sensor_history_chip(const ThermalSensorHistory *sensor);
const gchar *thermal_sensor_history_label(const ThermalSensorHistory *sensor);
guint thermal_sensor_history_sample_count(const ThermalSensorHistory *sensor);
gboolean thermal_sensor_history_sample_at(const ThermalSensorHistory *sensor,
                                          guint index,
                                          ThermalHistorySample *sample);
gboolean thermal_sensor_history_statistics(const ThermalSensorHistory *sensor,
                                           gdouble *current_c,
                                           gboolean *has_current,
                                           gdouble *minimum_c,
                                           gdouble *maximum_c);

#endif
