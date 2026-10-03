#pragma once
#include "app_types.h"

void status(Manager *m, const char *s);
Instance *selected_instance(Manager *m);
void populate(Manager *m);
void save_data(Manager *m);
void manager_load(Manager *m);
void set_window_icon(GtkWidget *window);
void scan_path(Manager *m, const char *path);
void detect_steam(Manager *m);
