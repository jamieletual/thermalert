#ifndef THERMALERT_INDICATOR_H
#define THERMALERT_INDICATOR_H

#include <glib.h>
#include <libayatana-appindicator/app-indicator.h>

#include "sensors.h"
#include "thermal.h"

typedef struct _ThermalIndicator ThermalIndicator;

ThermalIndicator *thermal_indicator_new(const gchar *icon_directory,
                                        GCallback monitor_callback,
                                        gpointer monitor_data,
                                        GCallback preferences_callback,
                                        gpointer preferences_data,
                                        GCallback quit_callback,
                                        gpointer quit_data);
void thermal_indicator_activate(ThermalIndicator *indicator);
void thermal_indicator_sync_startup_label(ThermalIndicator *indicator,
                                          gboolean exact_label);
void thermal_indicator_update(ThermalIndicator *indicator,
                              ThermalState state,
                              const ThermalSensorSnapshot *snapshot,
                              const ThermalSensorReading *primary);
void thermal_indicator_notify_transition(ThermalIndicator *indicator,
                                         ThermalState previous,
                                         ThermalState current,
                                         const ThermalSensorReading *primary);
void thermal_indicator_free(ThermalIndicator *indicator);

#endif
