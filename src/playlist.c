#include "playlist.h"
#include "mods.h"
#include <glib/gstdio.h>
#include <string.h>

#define PLAYLIST_FILE "playlists.properties"
#define MOD_LIST_FILE "modlist.txt"
#define STATE_GROUP "state"
#define DEFAULT_PLAYLIST "Default"

Playlist *playlist_new(const char *name) {
  Playlist *playlist = g_new0(Playlist, 1);
  playlist->id = g_uuid_string_random();
  playlist->name = g_strdup(name ? name : "");
  playlist->mods = g_ptr_array_new_with_free_func(g_free);
  return playlist;
}

void playlist_free(gpointer p) {
  Playlist *playlist = p;
  g_free(playlist->id);
  g_free(playlist->name);
  g_ptr_array_unref(playlist->mods);
  g_free(playlist);
}

Playlist *playlists_find(GPtrArray *playlists, const char *id) {
  if (!playlists || !playlists->len)
    return NULL;
  for (guint i = 0; i < playlists->len; i++) {
    Playlist *playlist = g_ptr_array_index(playlists, i);
    if (g_strcmp0(playlist->id, id) == 0)
      return playlist;
  }
  return g_ptr_array_index(playlists, 0);
}

static char *manager_dir(Instance *instance) {
  return g_build_filename(instance->path, ".finality", "manager", NULL);
}

static char *storage_path(Instance *instance) {
  char *dir = manager_dir(instance);
  char *file = g_build_filename(dir, PLAYLIST_FILE, NULL);
  g_free(dir);
  return file;
}

char *playlist_mod_list_path(Instance *instance) {
  char *dir = manager_dir(instance);
  char *file = g_build_filename(dir, MOD_LIST_FILE, NULL);
  g_free(dir);
  return file;
}

/* A playlist holding everything the instance currently enables, so the first
   launch behaves exactly like the state the user already had. */
static Playlist *default_playlist(Instance *instance) {
  Playlist *playlist = playlist_new(DEFAULT_PLAYLIST);
  GPtrArray *mods = instance_mods(instance);
  for (guint i = 0; i < mods->len; i++) {
    Mod *mod = g_ptr_array_index(mods, i);
    if (mod->enabled)
      g_ptr_array_add(playlist->mods, mod_id_from_path(instance, mod->path));
  }
  g_ptr_array_unref(mods);
  return playlist;
}

GPtrArray *playlists_load(Instance *instance, char **active_id) {
  GPtrArray *playlists = g_ptr_array_new_with_free_func(playlist_free);
  GKeyFile *key = g_key_file_new();
  char *stored_active = NULL;
  gboolean changed = FALSE;
  char *file = storage_path(instance);
  if (g_key_file_load_from_file(key, file, G_KEY_FILE_NONE, NULL)) {
    stored_active = g_key_file_get_string(key, STATE_GROUP, "active", NULL);
    gsize count = 0;
    char **groups = g_key_file_get_groups(key, &count);
    for (gsize i = 0; i < count; i++) {
      if (g_strcmp0(groups[i], STATE_GROUP) == 0)
        continue;
      char *name = g_key_file_get_string(key, groups[i], "name", NULL);
      gsize mods_len = 0;
      char **ids =
          g_key_file_get_string_list(key, groups[i], "mods", &mods_len, NULL);
      if (!name && !ids)
        continue;
      Playlist *playlist = playlist_new(name);
      char *id = g_key_file_get_string(key, groups[i], "id", NULL);
      if (id && *id) {
        g_free(playlist->id);
        playlist->id = id;
      } else {
        changed = TRUE; /* persist the generated id */
        g_free(id);
      }
      for (gsize j = 0; ids && j < mods_len; j++)
        g_ptr_array_add(playlist->mods, g_strdup(ids[j]));
      g_ptr_array_add(playlists, playlist);
      g_strfreev(ids);
      g_free(name);
    }
    g_strfreev(groups);
  }
  g_key_file_unref(key);
  g_free(file);
  if (!playlists->len) {
    g_ptr_array_add(playlists, default_playlist(instance));
    changed = TRUE;
  }
  Playlist *active = playlists_find(playlists, stored_active);
  changed = changed || g_strcmp0(stored_active, active->id) != 0;
  *active_id = g_strdup(active->id);
  g_free(stored_active);
  if (changed)
    playlists_save(instance, playlists, *active_id);
  return playlists;
}

void playlists_save(Instance *instance, GPtrArray *playlists,
                    const char *active_id) {
  GKeyFile *key = g_key_file_new();
  g_key_file_set_string(key, STATE_GROUP, "active", active_id ? active_id : "");
  for (guint i = 0; i < playlists->len; i++) {
    Playlist *playlist = g_ptr_array_index(playlists, i);
    char *group = g_strdup_printf("playlist-%u", i);
    g_key_file_set_string(key, group, "id", playlist->id);
    g_key_file_set_string(key, group, "name", playlist->name);
    g_key_file_set_string_list(key, group, "mods",
                               (const char *const *)playlist->mods->pdata,
                               playlist->mods->len);
    g_free(group);
  }
  char *dir = manager_dir(instance);
  g_mkdir_with_parents(dir, 0755);
  char *file = storage_path(instance);
  gsize len = 0;
  char *data = g_key_file_to_data(key, &len, NULL);
  g_file_set_contents(file, data, len, NULL);
  g_free(data);
  g_free(file);
  g_free(dir);
  g_key_file_unref(key);
}

/* Builds the loader mod list for a playlist from the instance mods.  When
   enabled_paths is not NULL every listed mod path is added to it so the caller
   can keep settings/ModsOff.txt in sync.  The paths added are owned by mods,
   which must stay alive while enabled_paths is used. */
static GString *build_mod_list(Instance *instance, Playlist *playlist,
                               GPtrArray *mods, GHashTable *enabled_paths) {
  GHashTable *wanted = g_hash_table_new(g_str_hash, g_str_equal);
  for (guint i = 0; i < playlist->mods->len; i++)
    g_hash_table_add(wanted, g_ptr_array_index(playlist->mods, i));
  GString *list = g_string_new(NULL);
  char *name = g_strdup(playlist->name);
  g_strdelimit(name, "\r\n", ' ');
  g_string_append(list, "[\n");
  g_free(name);
  gboolean first = TRUE;
  for (guint i = 0; i < mods->len; i++) {
    Mod *mod = g_ptr_array_index(mods, i);
    char *id = mod_id_from_path(instance, mod->path);
    if (g_hash_table_contains(wanted, id)) {
      if (enabled_paths)
        g_hash_table_add(enabled_paths, mod->path);
      gchar *escaped = g_strescape(mod->path, NULL);
      g_string_append_printf(list, "%s\"%s\"", first ? "" : ",\n", escaped);
      g_free(escaped);
      first = FALSE;
    }
    g_free(id);
  }
  g_string_append(list, "\n]\n");
  g_hash_table_unref(wanted);
  return list;
}

static gboolean write_mod_list_file(Instance *instance, GString *list,
                                    char **mod_list_path, GError **error) {
  char *dir = manager_dir(instance);
  g_mkdir_with_parents(dir, 0755);
  char *file = g_build_filename(dir, MOD_LIST_FILE, NULL);
  gboolean ok = g_file_set_contents(file, list->str, list->len, error);
  if (ok && mod_list_path)
    *mod_list_path = g_strdup(file);
  g_free(file);
  g_free(dir);
  return ok;
}

gboolean playlist_apply(Instance *instance, Playlist *playlist,
                        char **mod_list_path, GError **error) {
  GPtrArray *mods = instance_mods(instance);
  GHashTable *enabled = g_hash_table_new(g_str_hash, g_str_equal);
  GString *list = build_mod_list(instance, playlist, mods, enabled);
  mods_apply_enabled_set(instance, mods, enabled);
  gboolean ok = write_mod_list_file(instance, list, mod_list_path, error);
  g_string_free(list, TRUE);
  g_hash_table_unref(enabled);
  g_ptr_array_unref(mods);
  return ok;
}

gboolean playlist_write_mod_list(Instance *instance, Playlist *playlist,
                                 char **mod_list_path, GError **error) {
  GPtrArray *mods = instance_mods(instance);
  GString *list = build_mod_list(instance, playlist, mods, NULL);
  gboolean ok = write_mod_list_file(instance, list, mod_list_path, error);
  g_string_free(list, TRUE);
  g_ptr_array_unref(mods);
  return ok;
}

gboolean playlist_write_mod_list_to(Instance *instance, Playlist *playlist,
                                    const char *path, GError **error) {
  GPtrArray *mods = instance_mods(instance);
  GString *list = build_mod_list(instance, playlist, mods, NULL);
  gboolean ok = g_file_set_contents(path, list->str, list->len, error);
  g_string_free(list, TRUE);
  g_ptr_array_unref(mods);
  return ok;
}
