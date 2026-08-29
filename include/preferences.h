#ifndef THERMALERT_PREFERENCES_H
#define THERMALERT_PREFERENCES_H

#include <gio/gio.h>
#include <gtk/gtk.h>

#include "sensors.h"

typedef struct _ThermalPreferences ThermalPreferences;

ThermalPreferences *thermal_preferences_new(GSettings *settings);
void thermal_preferences_update_sensors(
    ThermalPreferences *preferences, const ThermalSensorSnapshot *snapshot,
    const ThermalSensorReading *automatic_sensor);
void thermal_preferences_present(ThermalPreferences *preferences);
void thermal_preferences_free(ThermalPreferences *preferences);

#endif
