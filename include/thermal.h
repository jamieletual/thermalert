#ifndef THERMALERT_THERMAL_H
#define THERMALERT_THERMAL_H

#include <glib.h>

typedef enum {
    THERMAL_STATE_UNKNOWN,
    THERMAL_STATE_NORMAL,
    THERMAL_STATE_WARNING,
    THERMAL_STATE_CRITICAL
} ThermalState;

typedef struct {
    gdouble warning_c;
    gdouble critical_c;
    gdouble hysteresis_c;
} ThermalThresholds;

ThermalState thermal_state_evaluate(ThermalState previous,
                                    gboolean has_reading,
                                    gdouble highest_c,
                                    const ThermalThresholds *thresholds);
const gchar *thermal_state_name(ThermalState state);

#endif
