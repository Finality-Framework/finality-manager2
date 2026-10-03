#include "config.h"
#include <glib/gstdio.h>
typedef struct { char *name; char *path; } Instance;

static char *config_path(void) {
  char *cwd = g_get_current_dir(); char *p = g_build_filename(cwd, ".finality-manager.properties", NULL); g_free(cwd); return p;
}
GPtrArray *config_load(char **language, gboolean *debug) {
  GPtrArray *paths = g_ptr_array_new_with_free_func(g_free); char *file = config_path(); GKeyFile *key = g_key_file_new();
  if (g_key_file_load_from_file(key, file, G_KEY_FILE_NONE, NULL)) {
    *language = g_key_file_get_string(key, "ui", "language", NULL); *debug = g_key_file_get_boolean(key, "loader", "debug", NULL);
    gsize n = 0; char **vals = g_key_file_get_string_list(key, "instances", "paths", &n, NULL); for (gsize i=0; vals && i<n; i++) g_ptr_array_add(paths, g_strdup(vals[i])); g_strfreev(vals);
  }
  g_key_file_unref(key); g_free(file); return paths;
}
gboolean config_exists(void) {
  char *file = config_path();
  gboolean exists = g_file_test(file, G_FILE_TEST_IS_REGULAR);
  g_free(file);
  return exists;
}
void config_save(const char *language, gboolean debug, GPtrArray *instances) {
  char *file = config_path(); GKeyFile *key = g_key_file_new(); g_key_file_set_string(key, "ui", "language", language ? language : "zh-CN"); g_key_file_set_boolean(key, "loader", "debug", debug);
  if (instances->len) {
    GPtrArray *unique = g_ptr_array_new();
    for (guint i = 0; i < instances->len; i++) {
      const char *path = ((Instance *)g_ptr_array_index(instances, i))->path;
      gboolean seen = FALSE;
      for (guint j = 0; j < unique->len; j++)
        if (g_strcmp0(g_ptr_array_index(unique, j), path) == 0) { seen = TRUE; break; }
      if (!seen)
        g_ptr_array_add(unique, (gpointer)path);
    }
    g_key_file_set_string_list(key, "instances", "paths", (const char * const *)unique->pdata, unique->len);
    g_ptr_array_unref(unique);
  }
  gsize len; char *data = g_key_file_to_data(key, &len, NULL); g_file_set_contents(file, data, len, NULL); g_free(data); g_key_file_unref(key); g_free(file);
}
