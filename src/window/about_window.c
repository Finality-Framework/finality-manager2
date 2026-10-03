#include "about_window.h"
#include "app.h"
#include "i18n.h"
#include <gtk/gtk.h>

void about_window_open(Manager *m) {
  GtkWidget *w = gtk_window_new();
  set_window_icon(w);
  gtk_window_set_title(GTK_WINDOW(w), i18n("about.title"));
  gtk_window_set_default_size(GTK_WINDOW(w), 420, 300);
  gtk_window_set_resizable(GTK_WINDOW(w), FALSE);
  gtk_window_set_transient_for(GTK_WINDOW(w), GTK_WINDOW(m->window));
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_top(box, 28);
  gtk_widget_set_margin_bottom(box, 28);
  gtk_widget_set_margin_start(box, 28);
  gtk_widget_set_margin_end(box, 28);
  GtkWidget *name = gtk_label_new(i18n("about.name"));
  gtk_widget_add_css_class(name, "title-1");
  GtkWidget *version = gtk_label_new(i18n("about.version"));
  GtkWidget *copyright = gtk_label_new("Copyright 2026 Team Rainfall");
  gtk_widget_add_css_class(copyright, "dim-label");
  GtkWidget *close = gtk_button_new_with_label(i18n("button.close"));
  gtk_widget_set_halign(close, GTK_ALIGN_CENTER);
  g_signal_connect_swapped(close, "clicked", G_CALLBACK(gtk_window_destroy), w);
  gtk_box_append(GTK_BOX(box), name);
  gtk_box_append(GTK_BOX(box), version);
  gtk_box_append(GTK_BOX(box), copyright);
  gtk_box_append(GTK_BOX(box), close);
  gtk_window_set_child(GTK_WINDOW(w), box);
  gtk_window_present(GTK_WINDOW(w));
}
