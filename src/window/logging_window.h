#pragma once
#include "app_types.h"
void show_log_window(Manager *m, const char *name);
void logging_window_start(Manager *m, const char *name, GInputStream *stream);
