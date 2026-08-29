#include <glib.h>

#include "thermal.h"

static const ThermalThresholds thresholds = { 80.0, 100.0, 5.0 };

static void
test_initial_state(void)
{
    g_assert_cmpint(thermal_state_evaluate(THERMAL_STATE_UNKNOWN, FALSE, 0.0,
                                           &thresholds),
                    ==, THERMAL_STATE_UNKNOWN);
    g_assert_cmpint(thermal_state_evaluate(THERMAL_STATE_UNKNOWN, TRUE, 79.9,
                                           &thresholds),
                    ==, THERMAL_STATE_NORMAL);
    g_assert_cmpint(thermal_state_evaluate(THERMAL_STATE_UNKNOWN, TRUE, 80.0,
                                           &thresholds),
                    ==, THERMAL_STATE_WARNING);
    g_assert_cmpint(thermal_state_evaluate(THERMAL_STATE_UNKNOWN, TRUE, 100.0,
                                           &thresholds),
                    ==, THERMAL_STATE_CRITICAL);
}

static void
test_warning_hysteresis(void)
{
    g_assert_cmpint(thermal_state_evaluate(THERMAL_STATE_WARNING, TRUE, 75.0,
                                           &thresholds),
                    ==, THERMAL_STATE_WARNING);
    g_assert_cmpint(thermal_state_evaluate(THERMAL_STATE_WARNING, TRUE, 74.9,
                                           &thresholds),
                    ==, THERMAL_STATE_NORMAL);
    g_assert_cmpint(thermal_state_evaluate(THERMAL_STATE_WARNING, TRUE, 100.0,
                                           &thresholds),
                    ==, THERMAL_STATE_CRITICAL);
}

static void
test_critical_hysteresis(void)
{
    g_assert_cmpint(thermal_state_evaluate(THERMAL_STATE_CRITICAL, TRUE, 95.0,
                                           &thresholds),
                    ==, THERMAL_STATE_CRITICAL);
    g_assert_cmpint(thermal_state_evaluate(THERMAL_STATE_CRITICAL, TRUE, 94.9,
                                           &thresholds),
                    ==, THERMAL_STATE_WARNING);
    g_assert_cmpint(thermal_state_evaluate(THERMAL_STATE_CRITICAL, TRUE, 70.0,
                                           &thresholds),
                    ==, THERMAL_STATE_NORMAL);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/thermal/initial", test_initial_state);
    g_test_add_func("/thermal/warning-hysteresis", test_warning_hysteresis);
    g_test_add_func("/thermal/critical-hysteresis", test_critical_hysteresis);
    return g_test_run();
}
