#include "main_window.h"
#include <gtk/gtk.h>

int main(int argc, char **argv) {
  Manager m = {0};
  GtkApplication *a = gtk_application_new("team.rainfall.finality.manager",
                                          G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(a, "activate", G_CALLBACK(app_activate), &m);
  int s = g_application_run(G_APPLICATION(a), argc, argv);
  if (m.instances)
    g_ptr_array_unref(m.instances);
  if (m.playlists)
    g_ptr_array_unref(m.playlists);
  g_free(m.active_playlist_id);
  g_free(m.playlists_instance_path);
  g_object_unref(a);
  return s;
}
