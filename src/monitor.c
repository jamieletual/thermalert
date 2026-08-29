#include "monitor.h"

#include "i18n.h"

#include <math.h>

#define GRAPH_MIN_C 0.0
#define GRAPH_MAX_C 110.0
#define GRAPH_WINDOW_US (5 * 60 * G_USEC_PER_SEC)

struct _ThermalMonitor {
    GtkWidget *window;
    GtkWidget *drawing_area;
    GtkWidget *sensor_label;
    GtkWidget *current_label;
    GtkWidget *minimum_label;
    GtkWidget *maximum_label;
    GtkWidget *sensor_box;
    const ThermalHistory *history; /* Borrowed from the application. */
    gchar *selected_id;
    GHashTable *visible_ids;
    GHashTable *sensor_buttons;
    ThermalThresholds thresholds;
};

static const gdouble trace_colors[][3] = {
    { 0.20, 0.78, 0.38 },
    { 0.25, 0.62, 0.96 },
    { 0.93, 0.44, 0.24 },
    { 0.72, 0.45, 0.91 },
    { 0.95, 0.76, 0.20 },
    { 0.20, 0.78, 0.78 },
    { 0.94, 0.38, 0.62 }
};

static const gchar *trace_color_names[] = {
    "#33c75f", "#409ef5", "#ed703d", "#b873e8",
    "#f2c233", "#33c7c7", "#f0609e"
};

static void set_temperature_label(GtkWidget *label, gboolean available,
                                  gdouble value);

static gboolean
hide_window(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    (void)event;
    (void)data;
    gtk_widget_hide(widget);
    return TRUE;
}

static void
draw_temperature_line(cairo_t *cr, gdouble y, gdouble left, gdouble right,
                      gdouble red, gdouble green, gdouble blue)
{
    const double dashes[] = { 6.0, 5.0 };

    cairo_save(cr);
    cairo_set_source_rgba(cr, red, green, blue, 0.85);
    cairo_set_line_width(cr, 1.5);
    cairo_set_dash(cr, dashes, G_N_ELEMENTS(dashes), 0.0);
    cairo_move_to(cr, left, y);
    cairo_line_to(cr, right, y);
    cairo_stroke(cr);
    cairo_restore(cr);
}

static gdouble
temperature_y(gdouble temperature, gdouble top, gdouble height)
{
    gdouble clamped = CLAMP(temperature, GRAPH_MIN_C, GRAPH_MAX_C);
    return top + height * (GRAPH_MAX_C - clamped) /
        (GRAPH_MAX_C - GRAPH_MIN_C);
}

static void
set_trace_color(cairo_t *cr, const gchar *sensor_id)
{
    guint index = g_str_hash(sensor_id) % G_N_ELEMENTS(trace_colors);

    cairo_set_source_rgb(cr, trace_colors[index][0], trace_colors[index][1],
                         trace_colors[index][2]);
}

static void
draw_sensor_trace(cairo_t *cr, const ThermalSensorHistory *sensor,
                  const gchar *sensor_id, gint64 oldest, gdouble left,
                  gdouble top, gdouble width, gdouble height)
{
    gboolean drawing = FALSE;
    gboolean has_path = FALSE;
    guint i;

    set_trace_color(cr, sensor_id);
    cairo_set_line_width(cr, 2.0);
    for (i = 0; sensor != NULL &&
                i < thermal_sensor_history_sample_count(sensor); i++) {
        ThermalHistorySample sample;
        gdouble x;
        gdouble y;

        thermal_sensor_history_sample_at(sensor, i, &sample);
        if (!sample.valid || sample.timestamp_us < oldest) {
            drawing = FALSE;
            continue;
        }
        x = left + width * (sample.timestamp_us - oldest) / GRAPH_WINDOW_US;
        y = temperature_y(sample.temperature_c, top, height);
        if (!drawing)
            cairo_move_to(cr, x, y);
        else
            cairo_line_to(cr, x, y);
        drawing = TRUE;
        has_path = TRUE;
    }
    if (has_path)
        cairo_stroke(cr);
}

static gboolean
draw_graph(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    ThermalMonitor *monitor = data;
    GtkAllocation allocation;
    const gdouble left = 48.0;
    const gdouble right_margin = 18.0;
    const gdouble top = 18.0;
    const gdouble bottom_margin = 30.0;
    gdouble width;
    gdouble height;
    gint64 now = g_get_monotonic_time();
    gint64 oldest = now - GRAPH_WINDOW_US;
    GHashTableIter iterator;
    gpointer key;
    guint i;

    gtk_widget_get_allocation(widget, &allocation);
    width = MAX(1.0, allocation.width - left - right_margin);
    height = MAX(1.0, allocation.height - top - bottom_margin);

    cairo_set_source_rgb(cr, 0.105, 0.12, 0.14);
    cairo_paint(cr);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 10.0);
    for (i = 0; i <= 100; i += 20) {
        gdouble y = temperature_y(i, top, height);
        gchar label[16];

        cairo_set_source_rgba(cr, 0.55, 0.58, 0.62, 0.7);
        cairo_move_to(cr, 7.0, y + 4.0);
        g_snprintf(label, sizeof label, "%u °C", i);
        cairo_show_text(cr, label);
        cairo_set_source_rgba(cr, 0.38, 0.41, 0.45, 0.45);
        cairo_set_line_width(cr, 1.0);
        cairo_move_to(cr, left, y);
        cairo_line_to(cr, left + width, y);
        cairo_stroke(cr);
    }
    for (i = 0; i <= 6; i++) {
        gdouble x = left + width * i / 6.0;
        cairo_set_source_rgba(cr, 0.38, 0.41, 0.45, 0.35);
        cairo_move_to(cr, x, top);
        cairo_line_to(cr, x, top + height);
        cairo_stroke(cr);
    }

    draw_temperature_line(cr,
        temperature_y(monitor->thresholds.warning_c, top, height),
        left, left + width, 0.95, 0.60, 0.12);
    draw_temperature_line(cr,
        temperature_y(monitor->thresholds.critical_c, top, height),
        left, left + width, 0.93, 0.22, 0.22);

    g_hash_table_iter_init(&iterator, monitor->visible_ids);
    while (g_hash_table_iter_next(&iterator, &key, NULL)) {
        const gchar *sensor_id = key;
        const ThermalSensorHistory *sensor =
            thermal_history_lookup(monitor->history, sensor_id);
        draw_sensor_trace(cr, sensor, sensor_id, oldest, left, top, width,
                          height);
    }

    cairo_set_source_rgba(cr, 0.72, 0.74, 0.77, 0.9);
    cairo_move_to(cr, left, top + height + 19.0);
    cairo_show_text(cr, _("5 minutes ago"));
    cairo_move_to(cr, left + width - 22.0, top + height + 19.0);
    cairo_show_text(cr, _("now"));
    return FALSE;
}

static void
update_statistics(ThermalMonitor *monitor)
{
    const ThermalSensorHistory *sensor =
        thermal_history_lookup(monitor->history, monitor->selected_id);
    gdouble current = 0.0;
    gdouble minimum = 0.0;
    gdouble maximum = 0.0;
    gboolean has_current = FALSE;
    gboolean has_statistics;

    if (sensor != NULL) {
        gchar *title = g_strdup_printf("%s — %s",
            thermal_sensor_history_label(sensor),
            thermal_sensor_history_chip(sensor));
        gtk_label_set_text(GTK_LABEL(monitor->sensor_label), title);
        g_free(title);
    } else {
        gtk_label_set_text(GTK_LABEL(monitor->sensor_label),
                           _("Select a temperature sensor"));
    }
    has_statistics = thermal_sensor_history_statistics(
        sensor, &current, &has_current, &minimum, &maximum);
    set_temperature_label(monitor->current_label, has_current, current);
    set_temperature_label(monitor->minimum_label, has_statistics, minimum);
    set_temperature_label(monitor->maximum_label, has_statistics, maximum);
}

static void
sensor_toggled(GtkToggleButton *button, gpointer data)
{
    ThermalMonitor *monitor = data;
    const gchar *sensor_id = g_object_get_data(G_OBJECT(button), "sensor-id");

    if (gtk_toggle_button_get_active(button)) {
        g_hash_table_add(monitor->visible_ids, g_strdup(sensor_id));
        g_free(monitor->selected_id);
        monitor->selected_id = g_strdup(sensor_id);
    } else {
        GHashTableIter iterator;
        gpointer key;

        g_hash_table_remove(monitor->visible_ids, sensor_id);
        if (g_strcmp0(monitor->selected_id, sensor_id) == 0) {
            g_clear_pointer(&monitor->selected_id, g_free);
            g_hash_table_iter_init(&iterator, monitor->visible_ids);
            if (g_hash_table_iter_next(&iterator, &key, NULL))
                monitor->selected_id = g_strdup(key);
        }
    }
    update_statistics(monitor);
    gtk_widget_queue_draw(monitor->drawing_area);
}

static void
add_sensor_button(ThermalMonitor *monitor,
                  const ThermalSensorReading *reading)
{
    GtkWidget *button;
    GtkWidget *label;
    gchar *escaped_label;
    gchar *escaped_chip;
    gchar *markup;
    guint color_index;

    if (g_hash_table_contains(monitor->sensor_buttons, reading->id))
        return;
    color_index = g_str_hash(reading->id) % G_N_ELEMENTS(trace_color_names);
    escaped_label = g_markup_escape_text(reading->label, -1);
    escaped_chip = g_markup_escape_text(reading->chip, -1);
    markup = g_strdup_printf(
        "<span foreground=\"%s\">●</span> %s\n"
        "<span size=\"small\" foreground=\"#888a85\">%s</span>",
        trace_color_names[color_index], escaped_label, escaped_chip);
    g_free(escaped_label);
    g_free(escaped_chip);
    button = gtk_check_button_new();
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(label), markup);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_container_add(GTK_CONTAINER(button), label);
    g_free(markup);
    gtk_widget_set_tooltip_text(button, reading->id);
    g_object_set_data_full(G_OBJECT(button), "sensor-id",
                           g_strdup(reading->id), g_free);
    g_signal_connect(button, "toggled", G_CALLBACK(sensor_toggled), monitor);
    gtk_box_pack_start(GTK_BOX(monitor->sensor_box), button, FALSE, FALSE, 2);
    g_hash_table_insert(monitor->sensor_buttons, g_strdup(reading->id), button);
    gtk_widget_show_all(button);
    if (g_strcmp0(monitor->selected_id, reading->id) == 0)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE);
}

static GtkWidget *
statistic_box(const gchar *title, GtkWidget **value_label)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *heading = gtk_label_new(title);

    gtk_style_context_add_class(gtk_widget_get_style_context(heading),
                                "dim-label");
    *value_label = gtk_label_new("—");
    gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), *value_label, FALSE, FALSE, 0);
    return box;
}

ThermalMonitor *
thermal_monitor_new(void)
{
    ThermalMonitor *monitor = g_new0(ThermalMonitor, 1);
    GtkWidget *body = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *statistics = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 32);
    GtkWidget *selector = gtk_scrolled_window_new(NULL, NULL);
    GtkWidget *selector_content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *selector_title = gtk_label_new(_("Sensors"));

    monitor->visible_ids = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                  g_free, NULL);
    monitor->sensor_buttons = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                     g_free, NULL);

    monitor->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(monitor->window),
                         _("Temperature Monitor"));
    gtk_window_set_default_size(GTK_WINDOW(monitor->window), 940, 500);
    gtk_container_set_border_width(GTK_CONTAINER(monitor->window), 18);
    g_signal_connect(monitor->window, "delete-event", G_CALLBACK(hide_window),
                     NULL);

    monitor->sensor_label = gtk_label_new(_("Waiting for sensor readings…"));
    gtk_widget_set_halign(monitor->sensor_label, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(monitor->sensor_label), "title");
    gtk_box_pack_start(GTK_BOX(content), monitor->sensor_label,
                       FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(statistics),
        statistic_box(_("Current"), &monitor->current_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(statistics),
        statistic_box(_("Minimum"), &monitor->minimum_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(statistics),
        statistic_box(_("Maximum"), &monitor->maximum_label), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), statistics, FALSE, FALSE, 0);

    monitor->drawing_area = gtk_drawing_area_new();
    gtk_widget_set_size_request(monitor->drawing_area, 500, 280);
    g_signal_connect(monitor->drawing_area, "draw", G_CALLBACK(draw_graph),
                     monitor);
    gtk_box_pack_start(GTK_BOX(content), monitor->drawing_area, TRUE, TRUE, 0);
    gtk_widget_set_size_request(selector, 220, -1);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(selector),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_halign(selector_title, GTK_ALIGN_START);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(selector_title), "title");
    monitor->sensor_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_pack_start(GTK_BOX(selector_content), selector_title,
                       FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(selector_content), monitor->sensor_box,
                       FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(selector), selector_content);
    gtk_box_pack_start(GTK_BOX(body), selector, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), content, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(monitor->window), body);
    gtk_widget_show_all(body);
    return monitor;
}

void
thermal_monitor_present(ThermalMonitor *monitor)
{
    g_return_if_fail(monitor != NULL);
    gtk_widget_show(monitor->window);
    gtk_window_present(GTK_WINDOW(monitor->window));
}

static void
set_temperature_label(GtkWidget *label, gboolean available, gdouble value)
{
    gchar *text = available ? g_strdup_printf("%.1f °C", value) : g_strdup("—");

    gtk_label_set_text(GTK_LABEL(label), text);
    g_free(text);
}

void
thermal_monitor_update(ThermalMonitor *monitor,
                       const ThermalHistory *history,
                       const ThermalSensorSnapshot *snapshot,
                       const ThermalSensorReading *primary,
                       const ThermalThresholds *thresholds)
{
    guint i;

    g_return_if_fail(monitor != NULL);
    monitor->history = history;
    if (thresholds != NULL)
        monitor->thresholds = *thresholds;
    if (monitor->selected_id == NULL && primary != NULL)
        monitor->selected_id = g_strdup(primary->id);
    for (i = 0; snapshot != NULL && i < snapshot->readings->len; i++)
        add_sensor_button(monitor, g_ptr_array_index(snapshot->readings, i));
    update_statistics(monitor);
    gtk_widget_queue_draw(monitor->drawing_area);
}

void
thermal_monitor_free(ThermalMonitor *monitor)
{
    if (monitor == NULL)
        return;
    gtk_widget_destroy(monitor->window);
    g_hash_table_unref(monitor->sensor_buttons);
    g_hash_table_unref(monitor->visible_ids);
    g_free(monitor->selected_id);
    g_free(monitor);
}
