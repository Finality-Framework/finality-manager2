/* Headless tests for mod discovery, ModsOff.txt handling and playlists.
   Build and run with `make test`. */
#include "i18n.h"
#include "mods.h"
#include "playlist.h"
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
      failures++;                                                              \
    } else                                                                     \
      printf("ok   %s\n", #cond);                                              \
  } while (0)

static void write_file(const char *path, const char *text) {
  char *dir = g_path_get_dirname(path);
  g_mkdir_with_parents(dir, 0755);
  g_free(dir);
  g_file_set_contents(path, text, -1, NULL);
}

static char *read_file(const char *path) {
  char *text = NULL;
  g_file_get_contents(path, &text, NULL, NULL);
  return text;
}

static void remove_tree(const char *path) {
  GDir *dir = g_dir_open(path, 0, NULL);
  if (dir) {
    const char *entry;
    while ((entry = g_dir_read_name(dir))) {
      char *child = g_build_filename(path, entry, NULL);
      if (g_file_test(child, G_FILE_TEST_IS_DIR))
        remove_tree(child);
      else
        g_remove(child);
      g_free(child);
    }
    g_dir_close(dir);
  }
  g_rmdir(path);
}

static Mod *find_mod(GPtrArray *mods, const char *name) {
  for (guint i = 0; i < mods->len; i++) {
    Mod *mod = g_ptr_array_index(mods, i);
    if (g_strcmp0(mod->name, name) == 0)
      return mod;
  }
  return NULL;
}

static gboolean contains_line(const char *text, const char *line) {
  if (!text)
    return FALSE;
  char **lines = g_strsplit(text, "\n", -1);
  gboolean found = FALSE;
  for (guint i = 0; lines[i] && !found; i++)
    found = g_strcmp0(lines[i], line) == 0;
  g_strfreev(lines);
  return found;
}

/* Compares a ModsOff.txt entry ignoring the separators of the platform that
   wrote it. */
static gboolean entries_contain(const char *text, const char *needle) {
  if (!text)
    return FALSE;
  char **parts = g_strsplit(text, ";", -1);
  gboolean found = FALSE;
  for (guint i = 0; parts[i] && !found; i++) {
    char *entry = g_strdup(g_strstrip(parts[i]));
    for (char *p = entry; *p; p++)
      if (*p == '\\')
        *p = '/';
    found = strstr(entry, needle) != NULL &&
            (g_str_has_suffix(entry, needle) ||
             g_strcmp0(entry, needle) == 0 ||
             entry[strlen(needle)] == '/');
    g_free(entry);
  }
  g_strfreev(parts);
  return found;
}

int main(void) {
  i18n_set_language("en");
  GError *error = NULL;
  char *root = g_dir_make_tmp("finality-playlist-XXXXXX", &error);
  if (!root) {
    printf("cannot create temp dir: %s\n", error->message);
    g_error_free(error);
    return 1;
  }
  char *instance_path =
      g_build_filename(root, "SteamLibrary", "steamapps", "common",
                       "Age of History 3", NULL);
  char *mods = g_build_filename(instance_path, "mods", NULL);
  write_file(g_build_filename(mods, "A", "mod.txt", NULL),
             "Name: Alpha Mod\nDescription: first\n");
  /* a .zip / .jar entry is only a mod when it is a directory */
  write_file(g_build_filename(mods, "B.zip", "mod.txt", NULL),
             "Name: Beta\n");
  write_file(g_build_filename(mods, "C.jar", "mod.txt", NULL),
             "Name: Gamma\n");
  write_file(g_build_filename(mods, "D.zip", NULL), "plain file, not a mod");
  char *workshop =
      g_build_filename(root, "SteamLibrary", "steamapps", "workshop", "content",
                       "2772750", NULL);
  write_file(g_build_filename(workshop, "111", "mod.txt", NULL),
             "Name: Workshop One\n");
  write_file(g_build_filename(workshop, "222", "plugins", "x.jar", NULL), "p");
  char *mods_off =
      g_build_filename(instance_path, "settings", "ModsOff.txt", NULL);
  char *native_two = g_build_filename(workshop, "222", NULL);
  char *initial = g_strdup_printf(
      "mods/B.zip/;mods/Ghost/;D:\\SteamLibrary\\steamapps\\workshop\\"
      "content\\2772750\\111;%s",
      native_two);
  write_file(mods_off, initial);

  Instance instance = {0};
  instance.path = g_strdup(instance_path);
  instance.appid = g_strdup("2772750");

  GPtrArray *all = instance_mods(&instance);
  CHECK(all->len == 5);
  Mod *alpha = find_mod(all, "Alpha Mod");
  Mod *beta = find_mod(all, "Beta");
  Mod *gamma = find_mod(all, "Gamma");
  Mod *one = find_mod(all, "Workshop One");
  Mod *two = find_mod(all, "222");
  CHECK(find_mod(all, "D.zip") == NULL); /* a plain file is not a mod */
  CHECK(alpha && alpha->enabled);
  CHECK(beta && !beta->enabled);
  CHECK(gamma && gamma->enabled);
  CHECK(one && !one->enabled); /* disabled through a Windows path */
  CHECK(two && !two->enabled); /* disabled through a native path */
  CHECK(instance_enabled_mod_count(&instance) == 2);
  CHECK(alpha && g_strcmp0(alpha->kind, MOD_KIND_LOCAL) == 0);
  CHECK(one && g_strcmp0(one->kind, MOD_KIND_WORKSHOP) == 0);

  char *alpha_id = mod_id_from_path(&instance, alpha->path);
  char *one_id = mod_id_from_path(&instance, one->path);
  CHECK(g_strcmp0(alpha_id, "local:A") == 0);
  CHECK(g_strcmp0(one_id, "workshop:111") == 0);
  char *alpha_path = mod_path_from_id(&instance, alpha_id);
  char *one_path = mod_path_from_id(&instance, one_id);
  CHECK(g_strcmp0(alpha_path, alpha->path) == 0);
  CHECK(g_strcmp0(one_path, one->path) == 0);

  /* paths spelled the way the Windows build sees them keep their kind */
  Instance windows = {0};
  windows.path = g_strdup("D:\\Games\\Age of History 3");
  windows.appid = g_strdup("2772750");
  char *win_local = mod_id_from_path(
      &windows, "D:\\Games\\Age of History 3\\mods\\Foo.zip");
  char *win_workshop = mod_id_from_path(
      &windows,
      "D:\\Games\\SteamLibrary\\steamapps\\workshop\\content\\2772750\\123");
  CHECK(win_local && g_str_has_prefix(win_local, "local:"));
  CHECK(win_workshop && g_str_has_prefix(win_workshop, "workshop:"));
  g_free(win_local);
  g_free(win_workshop);
  g_free(windows.path);
  g_free(windows.appid);

  /* toggling one mod leaves the other entries alone */
  mod_set_enabled(&instance, gamma->path, FALSE);
  char *text = read_file(mods_off);
  CHECK(entries_contain(text, "mods/C.jar/"));
  CHECK(entries_contain(text, "mods/B.zip/"));
  CHECK(entries_contain(text, "workshop/content/2772750/111"));
  g_free(text);
  mod_set_enabled(&instance, gamma->path, TRUE);
  text = read_file(mods_off);
  CHECK(!entries_contain(text, "mods/C.jar/"));
  CHECK(entries_contain(text, "workshop/content/2772750/111"));
  g_free(text);

  /* an instance without playlists gets a Default one holding its current state */
  char *active_id = NULL;
  GPtrArray *fresh = playlists_load(&instance, &active_id);
  CHECK(fresh->len == 1);
  Playlist *fallback = g_ptr_array_index(fresh, 0);
  CHECK(g_strcmp0(fallback->name, "Default") == 0);
  CHECK(g_strcmp0(active_id, fallback->id) == 0);
  CHECK(fallback->mods->len == 2); /* A and C.jar are enabled at this point */
  gboolean has_alpha = FALSE, has_gamma = FALSE;
  for (guint i = 0; i < fallback->mods->len; i++) {
    const char *id = g_ptr_array_index(fallback->mods, i);
    has_alpha = has_alpha || g_strcmp0(id, "local:A") == 0;
    has_gamma = has_gamma || g_strcmp0(id, "local:C.jar") == 0;
  }
  CHECK(has_alpha && has_gamma);
  g_ptr_array_unref(fresh);
  g_free(active_id);
  active_id = NULL;

  /* save / load round trip, including the remembered active playlist */
  GPtrArray *playlists = g_ptr_array_new_with_free_func(playlist_free);
  Playlist *playlist = playlist_new("Test / 测试");
  g_ptr_array_add(playlist->mods, g_strdup(alpha_id));
  g_ptr_array_add(playlist->mods, g_strdup(one_id));
  g_ptr_array_add(playlists, playlist);
  Playlist *second = playlist_new("Beta");
  g_ptr_array_add(playlists, second);
  playlists_save(&instance, playlists, second->id);
  GPtrArray *loaded = playlists_load(&instance, &active_id);
  CHECK(loaded->len == 2);
  Playlist *loaded_playlist = g_ptr_array_index(loaded, 0);
  CHECK(g_strcmp0(loaded_playlist->name, "Test / 测试") == 0);
  CHECK(loaded_playlist->mods->len == 2);
  CHECK(g_strcmp0(g_ptr_array_index(loaded_playlist->mods, 0), "local:A") == 0);
  CHECK(g_strcmp0(g_ptr_array_index(loaded_playlist->mods, 1), "workshop:111") ==
        0);
  CHECK(g_strcmp0(((Playlist *)g_ptr_array_index(loaded, 1))->name, "Beta") ==
        0);
  CHECK(((Playlist *)g_ptr_array_index(loaded, 1))->mods->len == 0);
  CHECK(active_id && g_strcmp0(active_id, ((Playlist *)g_ptr_array_index(loaded, 1))->id) == 0);
  CHECK(playlists_find(loaded, active_id) == g_ptr_array_index(loaded, 1));
  /* an unknown id falls back to the first playlist */
  CHECK(playlists_find(loaded, "does-not-exist") ==
        g_ptr_array_index(loaded, 0));
  g_free(active_id);
  active_id = NULL;
  g_ptr_array_unref(loaded);

  /* apply: only Alpha and Workshop One stay enabled */
  char *list_path = NULL;
  gboolean ok = playlist_apply(&instance, playlist, &list_path, &error);
  CHECK(ok && !error);
  text = read_file(mods_off);
  CHECK(entries_contain(text, "mods/B.zip/"));
  CHECK(entries_contain(text, "mods/C.jar/"));
  CHECK(entries_contain(text, "workshop/content/2772750/222"));
  CHECK(!entries_contain(text, "mods/A/"));
  CHECK(!entries_contain(text, "workshop/content/2772750/111"));
  CHECK(entries_contain(text, "mods/Ghost/")); /* unknown entries are kept */
  char *first_pass = g_strdup(text);
  g_free(text);
  /* applying the same playlist twice stays stable */
  g_free(list_path);
  list_path = NULL;
  ok = playlist_apply(&instance, playlist, &list_path, &error);
  CHECK(ok && !error);
  char *second_pass = read_file(mods_off);
  CHECK(g_strcmp0(second_pass, first_pass) == 0);
  g_free(second_pass);
  g_free(first_pass);
  char *expected_list_path = g_build_filename(
      instance_path, ".finality", "manager", "modlist.txt", NULL);
  CHECK(list_path && g_strcmp0(list_path, expected_list_path) == 0);
  g_free(expected_list_path);
  char *list = read_file(list_path);
  CHECK(strstr(list, "[\n") != NULL);
  CHECK(strstr(list, alpha->path) != NULL);
  CHECK(strstr(list, one->path) != NULL);
  CHECK(strstr(list, gamma->path) == NULL);
  g_free(list);

  /* write_mod_list only rewrites the definition and leaves ModsOff.txt alone */
  char *before = read_file(mods_off);
  GPtrArray *only_alpha = g_ptr_array_new_with_free_func(playlist_free);
  Playlist *alpha_only = playlist_new("Alpha only");
  g_ptr_array_add(alpha_only->mods, g_strdup(alpha_id));
  g_ptr_array_add(only_alpha, alpha_only);
  g_free(list_path);
  list_path = NULL;
  ok = playlist_write_mod_list(&instance, alpha_only, &list_path, &error);
  CHECK(ok && !error);
  char *after = read_file(mods_off);
  CHECK(g_strcmp0(before, after) == 0);
  g_free(after);
  g_free(before);
  list = read_file(list_path);
  CHECK(strstr(list, alpha->path) != NULL);
  CHECK(strstr(list, one->path) == NULL);
  g_free(list);

  /* write_mod_list_to writes the same definition to an explicit path */
  char *custom_dir = g_build_filename(instance_path, ".finality", NULL);
  g_mkdir_with_parents(custom_dir, 0755);
  char *custom_list =
      g_build_filename(custom_dir, "modlist.json", NULL);
  ok = playlist_write_mod_list_to(&instance, alpha_only, custom_list, &error);
  CHECK(ok && !error);
  list = read_file(custom_list);
  CHECK(strstr(list, alpha->path) != NULL);
  CHECK(strstr(list, one->path) == NULL);
  g_free(list);
  g_free(custom_list);
  g_free(custom_dir);
  g_ptr_array_unref(only_alpha);

  g_free(list_path);
  g_free(alpha_id);
  g_free(one_id);
  g_free(alpha_path);
  g_free(one_path);
  g_ptr_array_unref(all);
  g_ptr_array_unref(playlists);
  g_free(instance.path);
  g_free(instance.appid);
  g_free(native_two);
  g_free(initial);
  g_free(workshop);
  g_free(mods_off);
  g_free(mods);
  g_free(instance_path);
  remove_tree(root);
  g_free(root);

  if (failures)
    printf("\n%d CHECK(S) FAILED\n", failures);
  else
    printf("\nall checks passed\n");
  return failures ? 1 : 0;
}
