#include "thermal.h"

#include "i18n.h"

ThermalState
thermal_state_evaluate(ThermalState previous,
                       gboolean has_reading,
                       gdouble highest_c,
                       const ThermalThresholds *thresholds)
{
    g_return_val_if_fail(thresholds != NULL, THERMAL_STATE_UNKNOWN);

    if (!has_reading)
        return THERMAL_STATE_UNKNOWN;

    switch (previous) {
    case THERMAL_STATE_CRITICAL:
        if (highest_c < thresholds->warning_c - thresholds->hysteresis_c)
            return THERMAL_STATE_NORMAL;
        if (highest_c < thresholds->critical_c - thresholds->hysteresis_c)
            return THERMAL_STATE_WARNING;
        return THERMAL_STATE_CRITICAL;
    case THERMAL_STATE_WARNING:
        if (highest_c >= thresholds->critical_c)
            return THERMAL_STATE_CRITICAL;
        if (highest_c < thresholds->warning_c - thresholds->hysteresis_c)
            return THERMAL_STATE_NORMAL;
        return THERMAL_STATE_WARNING;
    case THERMAL_STATE_NORMAL:
    case THERMAL_STATE_UNKNOWN:
    default:
        if (highest_c >= thresholds->critical_c)
            return THERMAL_STATE_CRITICAL;
        if (highest_c >= thresholds->warning_c)
            return THERMAL_STATE_WARNING;
        return THERMAL_STATE_NORMAL;
    }
}

const gchar *
thermal_state_name(ThermalState state)
{
    switch (state) {
    case THERMAL_STATE_NORMAL:
        return N_("NORMAL");
    case THERMAL_STATE_WARNING:
        return N_("WARNING");
    case THERMAL_STATE_CRITICAL:
        return N_("CRITICAL");
    case THERMAL_STATE_UNKNOWN:
    default:
        return N_("UNKNOWN");
    }
}
