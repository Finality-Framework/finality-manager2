#include "app.h"
#include "config.h"
#include "i18n.h"
#include "playlists_window.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

static void add(Manager *m, const char *p);
extern const unsigned char _binary_res_aoh3_png_start[],
    _binary_res_aoh3_png_end[];
extern const unsigned char _binary_res_aoh2de_png_start[],
    _binary_res_aoh2de_png_end[];
static GdkTexture *embedded_texture(const unsigned char *start,
                                    const unsigned char *end) {
  GBytes *bytes = g_bytes_new_static(start, (gsize)(end - start));
  GError *error = NULL;
  GdkTexture *texture = gdk_texture_new_from_bytes(bytes, &error);
  if (error)
    g_error_free(error);
  g_bytes_unref(bytes);
  return texture;
}
void set_window_icon(GtkWidget *window) {
  gtk_window_set_icon_name(GTK_WINDOW(window), "logo");
}
static void instance_free(gpointer p) {
  Instance *i = p;
  g_free(i->name);
  g_free(i->path);
  g_free(i->appid);
  g_free(i);
}
static char *instance_appid(const char *instance) {
  char *common = g_path_get_dirname(instance);
  char *steamapps = g_path_get_dirname(common);
  char *base = g_path_get_basename(instance), *appid = NULL;
  GDir *d = g_dir_open(steamapps, 0, NULL);
  const char *name;
  while (d && (name = g_dir_read_name(d))) {
    if (!g_str_has_prefix(name, "appmanifest_") ||
        !g_str_has_suffix(name, ".acf"))
      continue;
    char *file = g_build_filename(steamapps, name, NULL), *text = NULL;
    if (g_file_get_contents(file, &text, NULL, NULL)) {
      char *needle = g_strdup_printf("\"%s\"", base);
      if (g_strstr_len(text, -1, "\"installdir\"") &&
          g_strstr_len(text, -1, needle)) {
        appid =
            g_strndup(name + strlen("appmanifest_"),
                      strlen(name) - strlen("appmanifest_") - strlen(".acf"));
        g_free(needle);
        g_free(text);
        g_free(file);
        break;
      }
      g_free(needle);
      g_free(text);
    }
    g_free(file);
  }
  if (d)
    g_dir_close(d);
  g_free(base);
  g_free(steamapps);
  g_free(common);
  return appid;
}
static gboolean is_instance(const char *p) {
  char *f = g_build_filename(p, "game.jar", NULL);
  char *appid = instance_appid(p);
  gboolean ok =
      g_file_test(f, G_FILE_TEST_IS_REGULAR) &&
      (g_strcmp0(appid, "2772750") == 0 || g_strcmp0(appid, "3381680") == 0);
  g_free(appid);
  g_free(f);
  return ok;
}
static gboolean has(Manager *m, const char *p, gboolean mp) {
  for (guint n = 0; n < m->instances->len; n++) {
    Instance *i = g_ptr_array_index(m->instances, n);
    if (i->mp == mp && !g_strcmp0(i->path, p))
      return TRUE;
  }
  return FALSE;
}
static Instance *instance_new(const char *p, gboolean mp) {
  Instance *i = g_new0(Instance, 1);
  i->path = g_strdup(p);
  i->appid = instance_appid(p);
  i->mp = mp;
  if (mp)
    i->name = g_strdup("Age of History 3 MP");
  else
    i->name =
        g_strdup(g_strcmp0(i->appid, "2772750") == 0 ? "Age of History 3"
                                                     : "Age of History 2 DE");
  return i;
}
static void add(Manager *m, const char *p) {
  if (!is_instance(p))
    return;
  char *appid = instance_appid(p);
  if (!has(m, p, FALSE))
    g_ptr_array_add(m->instances, instance_new(p, FALSE));
  if (g_strcmp0(appid, "2772750") == 0 && !has(m, p, TRUE)) {
    char *multi = g_build_filename(p, "multi.jar", NULL);
    if (g_file_test(multi, G_FILE_TEST_IS_REGULAR))
      g_ptr_array_add(m->instances, instance_new(p, TRUE));
    g_free(multi);
  }
  g_free(appid);
}
void status(Manager *m, const char *s) {
  gtk_label_set_text(GTK_LABEL(m->status), s);
}
Instance *selected_instance(Manager *m) {
  if (!m->instances || m->selected < 0 ||
      m->selected >= (int)m->instances->len)
    return NULL;
  return g_ptr_array_index(m->instances, m->selected);
}
void save_data(Manager *m) {
  config_save(m->language, m->print_debug_log, m->instances);
}
void manager_load(Manager *m) {
  if (!m->instances)
    m->instances = g_ptr_array_new_with_free_func(instance_free);
  GPtrArray *paths = config_load(&m->language, &m->print_debug_log);
  for (guint i = 0; i < paths->len; i++)
    add(m, g_ptr_array_index(paths, i));
  g_ptr_array_unref(paths);
  if (!m->language)
    m->language = g_strdup("zh-CN");
}
void populate(Manager *m) {
  GtkWidget *c;
  while ((c = gtk_widget_get_first_child(m->list)))
    gtk_list_box_remove(GTK_LIST_BOX(m->list), c);
  for (guint n = 0; n < m->instances->len; n++) {
    Instance *i = g_ptr_array_index(m->instances, n);
    GtkWidget *r = gtk_list_box_row_new(),
              *b = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12),
              *icon = gtk_image_new(),
              *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2),
              *a = gtk_label_new(i->name), *p = gtk_label_new(i->path);
    gtk_widget_set_margin_top(b, 10);
    gtk_widget_set_margin_bottom(b, 10);
    gtk_widget_set_margin_start(b, 14);
    gtk_widget_set_margin_end(b, 14);
    gtk_label_set_xalign(GTK_LABEL(a), 0);
    gtk_label_set_xalign(GTK_LABEL(p), 0);
    gtk_label_set_ellipsize(GTK_LABEL(p), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_single_line_mode(GTK_LABEL(p), TRUE);
    gtk_widget_add_css_class(a, "heading");
    gtk_widget_add_css_class(p, "dim-label");
    GdkTexture *texture =
        g_strcmp0(i->appid, "2772750") == 0
            ? embedded_texture(_binary_res_aoh3_png_start,
                               _binary_res_aoh3_png_end)
            : embedded_texture(_binary_res_aoh2de_png_start,
                               _binary_res_aoh2de_png_end);
    if (texture) {
      gtk_image_set_from_paintable(GTK_IMAGE(icon), GDK_PAINTABLE(texture));
      g_object_unref(texture);
    }
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 28);
    gtk_widget_set_valign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(labels), a);
    gtk_box_append(GTK_BOX(labels), p);
    gtk_box_append(GTK_BOX(b), icon);
    gtk_box_append(GTK_BOX(b), labels);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(r), b);
    gtk_list_box_append(GTK_LIST_BOX(m->list), r);
  }
  if (!m->instances->len) {
    GtkWidget *l = gtk_label_new(i18n("empty.instances"));
    gtk_label_set_justify(GTK_LABEL(l), GTK_JUSTIFY_CENTER);
    gtk_widget_set_margin_top(l, 36);
    gtk_list_box_append(GTK_LIST_BOX(m->list), l);
  }
  gtk_widget_set_sensitive(m->details, m->instances->len > 0);
  if (m->instances->len)
    gtk_list_box_select_row(
        GTK_LIST_BOX(m->list),
        gtk_list_box_get_row_at_index(GTK_LIST_BOX(m->list), 0));
  else
    playlists_refresh(m);
}
void scan_path(Manager *m, const char *sel) {
  guint before = m->instances->len;
  add(m, sel);
  char *common = g_build_filename(sel, "steamapps", "common", NULL);
  if (!g_file_test(common, G_FILE_TEST_IS_DIR)) {
    g_free(common);
    common = g_strdup(sel);
  }
  GDir *d = g_dir_open(common, 0, NULL);
  const char *n;
  while (d && (n = g_dir_read_name(d))) {
    char *p = g_build_filename(common, n, NULL);
    add(m, p);
    g_free(p);
  }
  if (d)
    g_dir_close(d);
  g_free(common);
  if (m->instances->len != before)
    save_data(m);
  populate(m);
}
static void detect_add_path(GPtrArray *paths, const char *value) {
  if (!value || !*value)
    return;
  for (guint i = 0; i < paths->len; i++)
    if (g_strcmp0(g_ptr_array_index(paths, i), value) == 0)
      return;
  g_ptr_array_add(paths, g_strdup(value));
}
static gboolean detect_all_digits(const char *s) {
  if (!s || !*s)
    return FALSE;
  for (; *s; s++)
    if (!g_ascii_isdigit(*s))
      return FALSE;
  return TRUE;
}
static void detect_vdf_paths(const char *file, GPtrArray *paths) {
  char *text = NULL;
  if (!g_file_get_contents(file, &text, NULL, NULL))
    return;
  GPtrArray *tokens = g_ptr_array_new_with_free_func(g_free);
  GArray *depths = g_array_new(FALSE, FALSE, sizeof(int));
  int depth = 0;
  const char *p = text;
  while (*p) {
    if (*p == '{') {
      depth++;
      p++;
    } else if (*p == '}') {
      if (depth)
        depth--;
      p++;
    } else if (*p == '"') {
      p++;
      GString *s = g_string_new(NULL);
      while (*p && *p != '"') {
        if (*p == '\\' && p[1])
          p++;
        g_string_append_c(s, *p);
        p++;
      }
      if (*p == '"')
        p++;
      g_ptr_array_add(tokens, g_string_free(s, FALSE));
      g_array_append_val(depths, depth);
    } else
      p++;
  }
  for (guint i = 0; i < tokens->len; i++) {
    const char *t = g_ptr_array_index(tokens, i);
    int d = g_array_index(depths, int, i);
    if (g_strcmp0(t, "path") == 0 && i + 1 < tokens->len)
      detect_add_path(paths, g_ptr_array_index(tokens, i + 1));
    else if (d == 1 && detect_all_digits(t) && i + 1 < tokens->len &&
             strpbrk(g_ptr_array_index(tokens, i + 1), "/\\"))
      detect_add_path(paths, g_ptr_array_index(tokens, i + 1));
  }
  g_array_unref(depths);
  g_ptr_array_unref(tokens);
  g_free(text);
}
#ifdef G_OS_WIN32
static void detect_registry_path(GPtrArray *paths, HKEY hive,
                                 const wchar_t *subkey, REGSAM view) {
  HKEY key;
  if (RegOpenKeyExW(hive, subkey, 0, KEY_QUERY_VALUE | view, &key) !=
      ERROR_SUCCESS)
    return;

  wchar_t value[32768];
  DWORD size = sizeof(value), type = 0;
  if (RegQueryValueExW(key, L"InstallPath", NULL, &type, (LPBYTE)value,
                       &size) == ERROR_SUCCESS &&
      (type == REG_SZ || type == REG_EXPAND_SZ) && size >= sizeof(wchar_t)) {
    value[(size / sizeof(wchar_t)) - 1] = L'\0';
    wchar_t expanded[32768];
    const wchar_t *path = value;
    if (type == REG_EXPAND_SZ &&
        ExpandEnvironmentStringsW(value, expanded, G_N_ELEMENTS(expanded)) >
            0)
      path = expanded;
    char *utf8 = g_utf16_to_utf8((const gunichar2 *)path, -1, NULL, NULL, NULL);
    detect_add_path(paths, utf8);
    g_free(utf8);
  }
  RegCloseKey(key);
}

static void detect_windows_paths(GPtrArray *roots) {
  static const wchar_t *const keys[] = {
      L"Software\\Valve\\Steam",
      L"Software\\Wow6432Node\\Valve\\Steam",
  };
  static const HKEY hives[] = {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE};
  static const REGSAM views[] = {KEY_WOW64_64KEY, KEY_WOW64_32KEY};

  for (guint h = 0; h < G_N_ELEMENTS(hives); h++)
    for (guint k = 0; k < G_N_ELEMENTS(keys); k++)
      for (guint v = 0; v < G_N_ELEMENTS(views); v++)
        detect_registry_path(roots, hives[h], keys[k], views[v]);

  const char *program_files = g_getenv("ProgramFiles");
  const char *program_files_x86 = g_getenv("ProgramFiles(x86)");
  const char *local_app_data = g_getenv("LOCALAPPDATA");
  const char *user_profile = g_getenv("USERPROFILE");
  const char *locations[] = {program_files, program_files_x86, local_app_data,
                             user_profile};
  for (guint i = 0; i < G_N_ELEMENTS(locations); i++) {
    if (!locations[i])
      continue;
    char *path = g_build_filename(locations[i], "Steam", NULL);
    detect_add_path(roots, path);
    g_free(path);
  }
}
#endif
void detect_steam(Manager *m) {
#ifndef G_OS_WIN32
  const char *home = g_get_home_dir();
#endif
  const char *data = g_get_user_data_dir();
  GPtrArray *roots = g_ptr_array_new_with_free_func(g_free),
            *libraries = g_ptr_array_new_with_free_func(g_free),
            *common_dirs = g_ptr_array_new_with_free_func(g_free);
  if (data)
    g_ptr_array_add(roots, g_build_filename(data, "Steam", NULL));
#ifdef G_OS_WIN32
  detect_windows_paths(roots);
#else
  if (home) {
    g_ptr_array_add(roots,
                    g_build_filename(home, ".local", "share", "Steam", NULL));
    g_ptr_array_add(roots, g_build_filename(home, ".steam", "steam", NULL));
    g_ptr_array_add(roots, g_build_filename(home, ".steam", "root", NULL));
    g_ptr_array_add(roots, g_build_filename(home, ".steam",
                                            "debian-installation", NULL));
    g_ptr_array_add(roots, g_build_filename(home, ".var", "app",
                                            "com.valvesoftware.Steam", "data",
                                            "Steam", NULL));
    g_ptr_array_add(roots, g_build_filename(home, "snap", "steam", "common",
                                             ".local", "share", "Steam", NULL));
  }
#endif
  for (guint i = 0; i < roots->len; i++) {
    const char *root = g_ptr_array_index(roots, i);
    char *vdf = g_build_filename(root, "steamapps", "libraryfolders.vdf", NULL);
    if (g_file_test(vdf, G_FILE_TEST_IS_REGULAR)) {
      g_ptr_array_add(common_dirs,
                      g_build_filename(root, "steamapps", "common", NULL));
      detect_vdf_paths(vdf, libraries);
    }
    g_free(vdf);
  }
  for (guint i = 0; i < libraries->len; i++)
    g_ptr_array_add(common_dirs,
                    g_build_filename(g_ptr_array_index(libraries, i),
                                     "steamapps", "common", NULL));
  guint found = 0;
  for (guint i = 0; i < common_dirs->len; i++) {
    const char *dir = g_ptr_array_index(common_dirs, i);
    GDir *d = g_dir_open(dir, 0, NULL);
    const char *n;
    while (d && (n = g_dir_read_name(d))) {
      char *p = g_build_filename(dir, n, NULL);
      guint before = m->instances->len;
      add(m, p);
      if (m->instances->len > before)
        found++;
      g_free(p);
    }
    if (d)
      g_dir_close(d);
  }
  g_ptr_array_unref(common_dirs);
  g_ptr_array_unref(libraries);
  g_ptr_array_unref(roots);
  if (found) {
    char *s = g_strdup_printf(i18n("status.detected"), found);
    status(m, s);
    g_free(s);
    save_data(m);
  } else
    status(m, i18n("status.detect_none"));
}
