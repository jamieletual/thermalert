#include <glib.h>

#include "sensors.h"

static void
test_live_enumeration(void)
{
    ThermalSensorSnapshot *snapshot;
    GError *error = NULL;
    GHashTable *ids;
    guint i;

    snapshot = thermal_sensors_read(&error);
    if (snapshot == NULL) {
        g_printerr("Sensor enumeration failed: %s\n", error->message);
        g_clear_error(&error);
        g_assert_no_error(error);
        return;
    }

    g_print("Readable temperature sensors: %u\n", snapshot->readings->len);
    ids = g_hash_table_new(g_str_hash, g_str_equal);
    for (i = 0; i < snapshot->readings->len; i++) {
        const ThermalSensorReading *reading =
            g_ptr_array_index(snapshot->readings, i);
        g_assert_nonnull(reading->id);
        g_assert_true(*reading->id != '\0');
        g_assert_false(g_hash_table_contains(ids, reading->id));
        g_hash_table_add(ids, reading->id);
        g_print("%s (%s / %s): %.1f °C\n", reading->id,
                reading->chip, reading->label,
                reading->temperature_c);
    }
    g_hash_table_unref(ids);
    thermal_sensor_snapshot_free(snapshot);
}

static ThermalSensorReading
reading(const gchar *id, const gchar *chip, const gchar *label, gdouble value)
{
    ThermalSensorReading result = { 0 };

    result.id = (gchar *)id;
    result.chip = (gchar *)chip;
    result.label = (gchar *)label;
    result.temperature_c = value;
    return result;
}

static void
assert_automatic_selection(ThermalSensorReading *readings, guint count,
                           const gchar *expected_id)
{
    ThermalSensorSnapshot snapshot = { 0 };
    const ThermalSensorReading *selected;
    guint i;

    snapshot.readings = g_ptr_array_new();
    for (i = 0; i < count; i++)
        g_ptr_array_add(snapshot.readings, &readings[i]);
    selected = thermal_sensors_select_primary(&snapshot, NULL);
    g_assert_nonnull(selected);
    g_assert_cmpstr(selected->id, ==, expected_id);
    g_ptr_array_unref(snapshot.readings);
}

static void
test_intel_package_preferred(void)
{
    ThermalSensorReading readings[] = {
        reading("core0", "coretemp-isa-0000", "Core 0", 92.0),
        reading("package", "coretemp-isa-0000", "Package id 0", 88.0),
        reading("gpu", "nouveau-pci-0100", "temp1", 95.0)
    };

    assert_automatic_selection(readings, G_N_ELEMENTS(readings), "package");
}

static void
test_amd_tctl_preferred(void)
{
    ThermalSensorReading readings[] = {
        reading("tdie", "k10temp-pci-00c3", "Tdie", 70.0),
        reading("tctl", "k10temp-pci-00c3", "Tctl", 72.0),
        reading("gpu", "amdgpu-pci-0300", "edge", 85.0)
    };

    assert_automatic_selection(readings, G_N_ELEMENTS(readings), "tctl");
}

static void
test_firmware_cpu_preferred(void)
{
    ThermalSensorReading readings[] = {
        reading("ambient", "dell_smm-isa-0000", "Ambient", 50.0),
        reading("cpu", "dell_smm-isa-0000", "CPU", 75.0),
        reading("gpu", "dell_smm-isa-0000", "GPU", 80.0)
    };

    assert_automatic_selection(readings, G_N_ELEMENTS(readings), "cpu");
}

static void
test_hottest_generic_fallback(void)
{
    ThermalSensorReading readings[] = {
        reading("cool", "acpitz-acpi-0", "temp1", 40.0),
        reading("hot", "nvme-pci-0200", "Composite", 55.0)
    };

    assert_automatic_selection(readings, G_N_ELEMENTS(readings), "hot");
}

static void
test_configured_sensor_and_missing_fallback(void)
{
    ThermalSensorSnapshot snapshot = { 0 };
    ThermalSensorReading readings[] = {
        reading("package", "coretemp-isa-0000", "Package id 0", 70.0),
        reading("gpu", "nouveau-pci-0100", "temp1", 80.0)
    };
    const ThermalSensorReading *selected;

    snapshot.readings = g_ptr_array_new();
    g_ptr_array_add(snapshot.readings, &readings[0]);
    g_ptr_array_add(snapshot.readings, &readings[1]);
    selected = thermal_sensors_select_primary(&snapshot, "gpu");
    g_assert_cmpstr(selected->id, ==, "gpu");
    selected = thermal_sensors_select_primary(&snapshot, "missing");
    g_assert_cmpstr(selected->id, ==, "package");
    g_ptr_array_unref(snapshot.readings);
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/sensors/live-enumeration", test_live_enumeration);
    g_test_add_func("/sensors/selection/intel-package",
                    test_intel_package_preferred);
    g_test_add_func("/sensors/selection/amd-tctl", test_amd_tctl_preferred);
    g_test_add_func("/sensors/selection/firmware-cpu",
                    test_firmware_cpu_preferred);
    g_test_add_func("/sensors/selection/generic-fallback",
                    test_hottest_generic_fallback);
    g_test_add_func("/sensors/selection/configured",
                    test_configured_sensor_and_missing_fallback);
    return g_test_run();
}
