#ifndef THERMALERT_MONITOR_H
#define THERMALERT_MONITOR_H

#include <gtk/gtk.h>

#include "history.h"
#include "thermal.h"

typedef struct _ThermalMonitor ThermalMonitor;

ThermalMonitor *thermal_monitor_new(void);
void thermal_monitor_present(ThermalMonitor *monitor);
void thermal_monitor_update(ThermalMonitor *monitor,
                            const ThermalHistory *history,
                            const ThermalSensorSnapshot *snapshot,
                            const ThermalSensorReading *primary,
                            const ThermalThresholds *thresholds);
void thermal_monitor_free(ThermalMonitor *monitor);

#endif
