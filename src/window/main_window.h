#pragma once
#include "app_types.h"
#include <gtk/gtk.h>

void app_activate(GtkApplication *app, gpointer data);
/* Launches the selected instance. When mod_list_path is not NULL it is passed
   to Loader2 as the --mods JSON file. */
gboolean main_window_launch(Manager *m, const char *mod_list_path);
/* Shows the name of the playlist the launch button will use. */
void main_window_set_active_playlist(Manager *m, const char *name);
