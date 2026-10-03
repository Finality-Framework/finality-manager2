#include "logging_window.h"
#include "app.h"
#include "i18n.h"
#include <string.h>
typedef struct {
  Manager *manager;
  GDataInputStream *stream;
} LogReader;
typedef struct {
  Manager *manager;
  guint serial;
} ProcessWatch;
static void update_process_status(Manager *m) {
  if (!m->log_status)
    return;
  gboolean running = m->process != NULL && m->process_running;
  gtk_label_set_text(GTK_LABEL(m->log_status),
                     i18n(running ? "log.process_running" : "log.process_closed"));
  gtk_widget_remove_css_class(m->log_status, "success");
  gtk_widget_remove_css_class(m->log_status, "dim-label");
  gtk_widget_add_css_class(m->log_status, running ? "success" : "dim-label");
  if (m->log_stop)
    gtk_widget_set_sensitive(m->log_stop, running);
}
static void process_watch_done(GObject *source, GAsyncResult *result,
                               gpointer data) {
  ProcessWatch *watch = data;
  Manager *m = watch->manager;
  GError *error = NULL;
  g_subprocess_wait_finish(G_SUBPROCESS(source), result, &error);
  if (error)
    g_error_free(error);
  if (m->process_serial == watch->serial) {
    m->process_running = FALSE;
    update_process_status(m);
  }
  g_free(watch);
}
static void watch_process(Manager *m) {
  if (!m->process)
    return;
  m->process_running = TRUE;
  m->process_serial++;
  update_process_status(m);
  ProcessWatch *watch = g_new0(ProcessWatch, 1);
  watch->manager = m;
  watch->serial = m->process_serial;
  g_subprocess_wait_async(m->process, NULL, process_watch_done, watch);
}
static void append_log(Manager *m, const char *line) {
  if (!m->log_buffer)
    return;
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(m->log_buffer, &end);
  char *valid = g_utf8_make_valid(line, -1);
  GRegex *ansi = g_regex_new("\\x1b\\[[0-9;?]*[ -/]*[@-~]", 0, 0, NULL);
  char *clean = g_regex_replace(ansi, valid, -1, 0, "", 0, NULL);
  g_regex_unref(ansi);
  gtk_text_buffer_insert(m->log_buffer, &end, clean, -1);
  gtk_text_buffer_insert(m->log_buffer, &end, "\n", 1);
  g_free(clean);
  g_free(valid);
  if (m->auto_scroll && m->log_view) {
    GtkTextMark *mark = gtk_text_buffer_get_insert(m->log_buffer);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(m->log_view), mark);
  }
}
static void read_log_line(GObject *source, GAsyncResult *result,
                          gpointer data) {
  (void)source;
  LogReader *reader = data;
  GError *error = NULL;
  gsize length = 0;
  char *line = g_data_input_stream_read_line_finish(reader->stream, result,
                                                    &length, &error);
  if (line) {
    append_log(reader->manager, line);
    g_free(line);
    g_data_input_stream_read_line_async(reader->stream, G_PRIORITY_DEFAULT,
                                        NULL, read_log_line, reader);
  } else {
    if (error && !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CLOSED)) {
      append_log(reader->manager, error->message);
      g_error_free(error);
    }
    g_object_unref(reader->stream);
    g_free(reader);
  }
}
static void toggle_auto_scroll(GtkCheckButton *button, gpointer data) {
  ((Manager *)data)->auto_scroll = gtk_check_button_get_active(button);
}
static void confirm_stop(GtkButton *button, gpointer data);
static void log_window_destroy(GtkWidget *window, gpointer data) {
  Manager *m = data;
  (void)window;
  m->log_window = NULL;
  m->log_view = NULL;
  m->log_status = NULL;
  m->log_stop = NULL;
  m->log_buffer = NULL;
}
void show_log_window(Manager *m, const char *name) {
  if (m->log_window) {
    gtk_window_present(GTK_WINDOW(m->log_window));
    return;
  }
  m->log_window = gtk_window_new();
  gtk_window_set_icon_name(GTK_WINDOW(m->log_window), "logo");
  gtk_window_set_title(GTK_WINDOW(m->log_window), i18n("log.title"));
  gtk_window_set_default_size(GTK_WINDOW(m->log_window), 760, 460);
  GtkWidget *log_header = gtk_header_bar_new();
  GtkWidget *log_title = gtk_label_new(i18n("log.title"));
  gtk_header_bar_set_title_widget(GTK_HEADER_BAR(log_header), log_title);
  gtk_window_set_titlebar(GTK_WINDOW(m->log_window), log_header);
  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
  gtk_widget_set_margin_top(root, 10);
  gtk_widget_set_margin_bottom(root, 10);
  gtk_widget_set_margin_start(root, 10);
  gtk_widget_set_margin_end(root, 10);
  GtkWidget *heading = gtk_label_new(name);
  gtk_label_set_xalign(GTK_LABEL(heading), 0);
  gtk_widget_add_css_class(heading, "heading");
  gtk_box_append(GTK_BOX(root), heading);
  GtkWidget *scroll = gtk_scrolled_window_new();
  m->log_view = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(m->log_view), FALSE);
  gtk_text_view_set_monospace(GTK_TEXT_VIEW(m->log_view), TRUE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(m->log_view), GTK_WRAP_WORD_CHAR);
  m->log_buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->log_view));
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), m->log_view);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_box_append(GTK_BOX(root), scroll);
  GtkWidget *toggle =
      gtk_check_button_new_with_label(i18n("button.auto_scroll"));
  gtk_check_button_set_active(GTK_CHECK_BUTTON(toggle), TRUE);
  m->auto_scroll = TRUE;
  g_signal_connect(toggle, "toggled", G_CALLBACK(toggle_auto_scroll), m);
  gtk_box_append(GTK_BOX(root), toggle);
  GtkWidget *bottom = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *stop = gtk_button_new_with_label(i18n("button.terminate"));
  gtk_widget_add_css_class(stop, "destructive-action");
  gtk_widget_set_halign(stop, GTK_ALIGN_START);
  g_signal_connect(stop, "clicked", G_CALLBACK(confirm_stop), m);
  gtk_box_append(GTK_BOX(bottom), stop);
  m->log_stop = stop;
  m->log_status = gtk_label_new("");
  gtk_widget_set_halign(m->log_status, GTK_ALIGN_END);
  gtk_widget_set_hexpand(m->log_status, TRUE);
  gtk_box_append(GTK_BOX(bottom), m->log_status);
  gtk_box_append(GTK_BOX(root), bottom);
  gtk_window_set_child(GTK_WINDOW(m->log_window), root);
  g_signal_connect(m->log_window, "destroy", G_CALLBACK(log_window_destroy), m);
  update_process_status(m);
  gtk_window_present(GTK_WINDOW(m->log_window));
}
void logging_window_start(Manager *m, const char *name, GInputStream *stream) {
  show_log_window(m, name);
  watch_process(m);
  LogReader *reader = g_new0(LogReader, 1);
  reader->manager = m;
  reader->stream = g_data_input_stream_new(stream);
  g_data_input_stream_set_newline_type(reader->stream,
                                       G_DATA_STREAM_NEWLINE_TYPE_ANY);
  g_data_input_stream_read_line_async(reader->stream, G_PRIORITY_DEFAULT, NULL,
                                      read_log_line, reader);
}
static void stop_confirmed(GObject *source, GAsyncResult *result,
                           gpointer data) {
  Manager *m = data;
  GError *error = NULL;
  int choice =
      gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);
  if (!error && choice == 0 && m->process) {
    g_subprocess_force_exit(m->process);
    status(m, i18n("status.termination"));
  }
  if (error)
    g_error_free(error);
  g_object_unref(source);
}
static void confirm_stop(GtkButton *button, gpointer data) {
  Manager *m = data;
  (void)button;
  if (!m->process || g_subprocess_get_if_exited(m->process)) {
    status(m, i18n("status.no_process"));
    return;
  }
  /* i18n() reuses one static buffer, so the labels must be copied first. */
  char *stop_label = g_strdup(i18n("button.terminate_short"));
  char *cancel_label = g_strdup(i18n("button.cancel"));
  const char *buttons[] = {stop_label, cancel_label, NULL};
  GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", i18n("dialog.terminate"));
  gtk_alert_dialog_set_detail(dialog, i18n("dialog.terminate_detail"));
  gtk_alert_dialog_set_buttons(dialog, buttons);
  g_free(stop_label);
  g_free(cancel_label);
  gtk_alert_dialog_set_cancel_button(dialog, 1);
  gtk_alert_dialog_set_default_button(dialog, 1);
  gtk_alert_dialog_choose(dialog, GTK_WINDOW(m->log_window), NULL,
                          stop_confirmed, m);
}
