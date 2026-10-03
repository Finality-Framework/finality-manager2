#include "preferences_window.h"
#include "app.h"
#include "i18n.h"
#include <gtk/gtk.h>

static void reset_data(GtkButton *button, gpointer data) {
  Manager *m = data;
  (void)button;
  g_ptr_array_set_size(m->instances, 0);
  m->selected = -1;
  g_free(m->language);
  m->language = g_strdup("zh-CN");
  save_data(m);
  populate(m);
  status(m, i18n("status.reset"));
}
static void change_language(GtkDropDown *combo, GParamSpec *pspec,
                            gpointer data) {
  Manager *m = data;
  (void)pspec;
  g_free(m->language);
  m->language =
      g_strdup(gtk_drop_down_get_selected(combo) == 1 ? "en" : "zh-CN");
  save_data(m);
  i18n_set_language(m->language);
}
static gboolean debug_switched(GtkSwitch *self, gboolean state,
                               gpointer user_data) {
  (void)self;
  Manager *m = user_data;
  m->print_debug_log = state;
  save_data(m);
  return FALSE;
}
void preferences_window_open(Manager *m) {
  GtkWidget *w = gtk_window_new();
  set_window_icon(w);
  gtk_window_set_title(GTK_WINDOW(w), i18n("preferences.title"));
  gtk_window_set_default_size(GTK_WINDOW(w), 400, 240);
  gtk_window_set_transient_for(GTK_WINDOW(w), GTK_WINDOW(m->window));
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
  gtk_widget_set_margin_top(box, 24);
  gtk_widget_set_margin_bottom(box, 24);
  gtk_widget_set_margin_start(box, 24);
  gtk_widget_set_margin_end(box, 24);
  GtkWidget *label = gtk_label_new(i18n("preferences.language"));
  gtk_label_set_xalign(GTK_LABEL(label), 0);
  gtk_box_append(GTK_BOX(box), label);
  GtkStringList *languages =
      gtk_string_list_new((const char *[]){"简体中文", "English", NULL});
  GtkWidget *combo = gtk_drop_down_new(G_LIST_MODEL(languages), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(combo),
                             g_strcmp0(m->language, "en") == 0 ? 1 : 0);
  gtk_box_append(GTK_BOX(box), combo);
  g_signal_connect(combo, "notify::selected", G_CALLBACK(change_language), m);
  GtkWidget *box2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
  GtkWidget *label_debug = gtk_label_new(i18n("preferences.debug"));
  gtk_label_set_xalign(GTK_LABEL(label_debug), 0);
  gtk_box_append(GTK_BOX(box2), label_debug);

  GtkWidget *switch_debug = gtk_switch_new();
  gtk_switch_set_active(GTK_SWITCH(switch_debug), m->print_debug_log);
  g_signal_connect(switch_debug, "state-set", G_CALLBACK(debug_switched), m);
  gtk_box_append(GTK_BOX(box2), switch_debug);
  gtk_box_append(GTK_BOX(box), box2);
  GtkWidget *reset = gtk_button_new_with_label(i18n("preferences.reset"));
  gtk_widget_add_css_class(reset, "destructive-action");
  gtk_box_append(GTK_BOX(box), reset);
  g_signal_connect(reset, "clicked", G_CALLBACK(reset_data), m);
  gtk_window_set_child(GTK_WINDOW(w), box);
  gtk_window_present(GTK_WINDOW(w));
}
