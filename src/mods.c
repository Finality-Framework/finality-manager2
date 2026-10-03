#include "mods.h"
#include "i18n.h"
#include <glib/gstdio.h>
#include <string.h>

char *instance_mods_dir(Instance *instance) {
  if (!instance || !instance->path)
    return NULL;
  return g_build_filename(instance->path, "mods", NULL);
}

char *instance_workshop_dir(Instance *instance) {
  if (!instance || !instance->path || !instance->appid)
    return NULL;
  char *common = g_path_get_dirname(instance->path);
  char *steamapps = g_path_get_dirname(common);
  char *dir =
      g_build_filename(steamapps, "workshop", "content", instance->appid, NULL);
  g_free(common);
  g_free(steamapps);
  return dir;
}

/* Absolute paths written by the game on another platform use backslashes and
   may carry a trailing separator. */
static char *normalized(const char *path) {
  char *out = g_strdup(path ? path : "");
  for (char *p = out; *p; p++)
    if (*p == '\\')
      *p = '/';
  gsize len = strlen(out);
  while (len > 1 && out[len - 1] == '/')
    out[--len] = '\0';
  return out;
}

static const char *mod_kind_for_path(Instance *instance, const char *path) {
  char *dir = instance_mods_dir(instance);
  if (dir) {
    char *normalized_dir = normalized(dir);
    char *normalized_path = normalized(path);
    char *prefix = g_strdup_printf("%s/", normalized_dir);
    gboolean local = g_str_has_prefix(normalized_path, prefix);
    g_free(prefix);
    g_free(normalized_path);
    g_free(normalized_dir);
    g_free(dir);
    if (local)
      return MOD_KIND_LOCAL;
  }
  return MOD_KIND_WORKSHOP;
}

static char *mods_off_path(Instance *instance) {
  return g_build_filename(instance->path, "settings", "ModsOff.txt", NULL);
}

static GPtrArray *mods_off_entries(Instance *instance) {
  char *file = mods_off_path(instance), *contents = NULL;
  g_file_get_contents(file, &contents, NULL, NULL);
  GPtrArray *entries = g_ptr_array_new_with_free_func(g_free);
  char **parts = g_strsplit(contents ? contents : "", ";", -1);
  for (guint i = 0; parts[i]; i++) {
    char *entry = g_strstrip(g_strdup(parts[i]));
    if (*entry)
      g_ptr_array_add(entries, entry);
    else
      g_free(entry);
  }
  g_strfreev(parts);
  g_free(contents);
  g_free(file);
  return entries;
}

static char *mod_field(const char *text, const char *field) {
  char *needle = g_strdup_printf("%s:", field);
  const char *p = g_strstr_len(text, -1, needle);
  g_free(needle);
  if (!p)
    return NULL;
  p = strchr(p, ':') + 1;
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p == '"') {
    p++;
    const char *end = strchr(p, '"');
    return end ? g_strndup(p, end - p) : g_strdup(p);
  }
  const char *end = strpbrk(p, ",\n\r}");
  return end ? g_strndup(p, end - p) : g_strdup(p);
}

gboolean mod_valid_path(const char *path) {
  return g_file_test(path, G_FILE_TEST_IS_DIR);
}

/* A ModsOff.txt entry and a mod refer to the same mod when they name the same
   absolute path, or when a local entry is "mods/<name>" (as written by the
   loader) and a workshop entry ends in workshop/content/<appid>/<name> (the
   game spells those with the separators of the platform it writes on). */
static gboolean entry_matches(const char *entry, Instance *instance,
                              const char *kind, const char *mod_path) {
  char *e = normalized(entry), *path = normalized(mod_path);
  char *base = g_path_get_basename(path);
  gboolean match = g_strcmp0(e, path) == 0;
  if (!match && g_strcmp0(kind, MOD_KIND_LOCAL) == 0) {
    char *rel = g_strdup_printf("mods/%s", base);
    char *suffix = g_strdup_printf("/mods/%s", base);
    match = g_strcmp0(e, rel) == 0 || g_str_has_suffix(e, suffix);
    g_free(rel);
    g_free(suffix);
  } else if (!match) {
    const char *appid = instance->appid ? instance->appid : "";
    char *rel = g_strdup_printf("workshop/content/%s/%s", appid, base);
    char *suffix = g_strdup_printf("/workshop/content/%s/%s", appid, base);
    match = g_strcmp0(e, rel) == 0 || g_str_has_suffix(e, suffix);
    g_free(rel);
    g_free(suffix);
  }
  g_free(base);
  g_free(path);
  g_free(e);
  return match;
}

gboolean mod_disabled(Instance *instance, const char *mod_path) {
  GPtrArray *entries = mods_off_entries(instance);
  const char *kind = mod_kind_for_path(instance, mod_path);
  gboolean disabled = FALSE;
  for (guint i = 0; i < entries->len && !disabled; i++)
    disabled = entry_matches(g_ptr_array_index(entries, i), instance, kind,
                             mod_path);
  g_ptr_array_unref(entries);
  return disabled;
}

Mod *mod_new(Instance *instance, const char *path, const char *source) {
  Mod *mod = g_new0(Mod, 1);
  mod->path = g_strdup(path);
  mod->source = g_strdup(source);
  mod->kind = g_strdup(mod_kind_for_path(instance, path));
  char *entry = g_path_get_basename(path);
  mod->name = g_strdup(entry);
  g_free(entry);
  mod->enabled = !mod_disabled(instance, path);
  if (g_file_test(path, G_FILE_TEST_IS_DIR)) {
    char *meta_path = g_build_filename(path, "mod.txt", NULL), *meta = NULL;
    if (g_file_get_contents(meta_path, &meta, NULL, NULL)) {
      char *name = mod_field(meta, "Name"), *desc = mod_field(meta, "Description");
      if (name && *name) {
        g_free(mod->name);
        mod->name = name;
      } else
        g_free(name);
      mod->description = desc;
      g_free(meta);
    }
    g_free(meta_path);
  }
  return mod;
}

void mod_free(gpointer p) {
  Mod *mod = p;
  g_free(mod->name);
  g_free(mod->path);
  g_free(mod->description);
  g_free(mod->source);
  g_free(mod->kind);
  g_free(mod);
}

static void collect_mods(GPtrArray *mods, Instance *instance, const char *dir,
                         const char *source) {
  GDir *d = dir ? g_dir_open(dir, 0, NULL) : NULL;
  const char *entry;
  while (d && (entry = g_dir_read_name(d))) {
    char *path = g_build_filename(dir, entry, NULL);
    if (mod_valid_path(path))
      g_ptr_array_add(mods, mod_new(instance, path, source));
    g_free(path);
  }
  if (d)
    g_dir_close(d);
}

GPtrArray *instance_mods(Instance *instance) {
  GPtrArray *mods = g_ptr_array_new_with_free_func(mod_free);
  char *dir = instance_mods_dir(instance);
  char *source = g_strdup(i18n("mods.local"));
  collect_mods(mods, instance, dir, source);
  g_free(source);
  g_free(dir);
  dir = instance_workshop_dir(instance);
  source = g_strdup(i18n("mods.steam"));
  collect_mods(mods, instance, dir, source);
  g_free(source);
  g_free(dir);
  return mods;
}

guint instance_enabled_mod_count(Instance *instance) {
  GPtrArray *mods = instance_mods(instance);
  guint count = 0;
  for (guint i = 0; i < mods->len; i++)
    if (((Mod *)g_ptr_array_index(mods, i))->enabled)
      count++;
  g_ptr_array_unref(mods);
  return count;
}

static char *canonical_entry(Mod *mod) {
  if (g_strcmp0(mod->kind, MOD_KIND_LOCAL) == 0) {
    char *base = g_path_get_basename(mod->path);
    char *entry = g_strdup_printf("mods/%s/", base);
    g_free(base);
    return entry;
  }
  return g_strdup(mod->path);
}

void mods_apply_enabled_set(Instance *instance, GPtrArray *mods,
                            GHashTable *enabled_paths) {
  GPtrArray *entries = mods_off_entries(instance);
  GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
  GHashTable *handled = g_hash_table_new(g_direct_hash, g_direct_equal);
  for (guint i = 0; i < entries->len; i++) {
    const char *entry = g_ptr_array_index(entries, i);
    Mod *match = NULL;
    for (guint j = 0; j < mods->len && !match; j++) {
      Mod *mod = g_ptr_array_index(mods, j);
      if (entry_matches(entry, instance, mod->kind, mod->path))
        match = mod;
    }
    if (!match) {
      g_ptr_array_add(out, g_strdup(entry));
      continue;
    }
    if (g_hash_table_contains(enabled_paths, match->path))
      continue;
    if (g_hash_table_contains(handled, match))
      continue;
    g_hash_table_add(handled, match);
    g_ptr_array_add(out, g_strdup(entry));
  }
  for (guint i = 0; i < mods->len; i++) {
    Mod *mod = g_ptr_array_index(mods, i);
    if (g_hash_table_contains(enabled_paths, mod->path) ||
        g_hash_table_contains(handled, mod))
      continue;
    g_ptr_array_add(out, canonical_entry(mod));
  }
  GString *text = g_string_new(NULL);
  for (guint i = 0; i < out->len; i++) {
    if (i)
      g_string_append_c(text, ';');
    g_string_append(text, g_ptr_array_index(out, i));
  }
  char *settings = g_build_filename(instance->path, "settings", NULL);
  g_mkdir_with_parents(settings, 0755);
  char *file = g_build_filename(settings, "ModsOff.txt", NULL);
  g_file_set_contents(file, text->str, text->len, NULL);
  g_free(file);
  g_free(settings);
  g_string_free(text, TRUE);
  g_hash_table_unref(handled);
  g_ptr_array_unref(out);
  g_ptr_array_unref(entries);
}

void mod_set_enabled(Instance *instance, const char *mod_path, gboolean enabled) {
  GPtrArray *mods = g_ptr_array_new_with_free_func(mod_free);
  Mod *mod = g_new0(Mod, 1);
  mod->path = g_strdup(mod_path);
  mod->kind = g_strdup(mod_kind_for_path(instance, mod_path));
  g_ptr_array_add(mods, mod);
  GHashTable *enabled_paths = g_hash_table_new(g_str_hash, g_str_equal);
  if (enabled)
    g_hash_table_add(enabled_paths, mod->path);
  mods_apply_enabled_set(instance, mods, enabled_paths);
  g_hash_table_unref(enabled_paths);
  g_ptr_array_unref(mods);
}

char *mod_id_from_path(Instance *instance, const char *path) {
  const char *kind = mod_kind_for_path(instance, path);
  char *base = g_path_get_basename(path);
  char *id = g_strdup_printf("%s:%s", kind, base);
  g_free(base);
  return id;
}

char *mod_path_from_id(Instance *instance, const char *id) {
  const char *colon = strchr(id, ':');
  if (!colon)
    return NULL;
  char *kind = g_strndup(id, colon - id);
  const char *name = colon + 1;
  char *dir = NULL, *path = NULL;
  if (g_strcmp0(kind, MOD_KIND_LOCAL) == 0)
    dir = instance_mods_dir(instance);
  else if (g_strcmp0(kind, MOD_KIND_WORKSHOP) == 0)
    dir = instance_workshop_dir(instance);
  if (dir)
    path = g_build_filename(dir, name, NULL);
  g_free(dir);
  g_free(kind);
  return path;
}
