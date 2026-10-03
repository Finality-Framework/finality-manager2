#pragma once
#include <glib.h>

GPtrArray *config_load(char **language, gboolean *debug);
void config_save(const char *language, gboolean debug, GPtrArray *instances);
gboolean config_exists(void);
