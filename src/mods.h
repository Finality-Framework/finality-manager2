#pragma once
#include "app_types.h"

#define MOD_KIND_LOCAL "local"
#define MOD_KIND_WORKSHOP "workshop"

Mod *mod_new(Instance *instance, const char *path, const char *source);
void mod_free(gpointer p);
gboolean mod_valid_path(const char *path);
gboolean mod_disabled(Instance *instance, const char *mod_path);
char *instance_mods_dir(Instance *instance);
char *instance_workshop_dir(Instance *instance);
/* All local and Steam Workshop mods of the instance.  The returned array owns
   its Mod entries and must be released with g_ptr_array_unref(). */
GPtrArray *instance_mods(Instance *instance);
guint instance_enabled_mod_count(Instance *instance);
void mod_set_enabled(Instance *instance, const char *mod_path, gboolean enabled);
/* Rewrites settings/ModsOff.txt so that exactly the mods of `mods` whose path
   is in `enabled_paths` stay enabled.  Entries that belong to no mod of the
   array are preserved. */
void mods_apply_enabled_set(Instance *instance, GPtrArray *mods,
                            GHashTable *enabled_paths);
char *mod_id_from_path(Instance *instance, const char *path);
char *mod_path_from_id(Instance *instance, const char *id);
