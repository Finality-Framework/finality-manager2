#pragma once
#include "app_types.h"

Playlist *playlist_new(const char *name);
void playlist_free(gpointer p);
/* Playlists are stored per instance in <instance>/.finality/manager/.  When the
   instance has no playlists yet a "Default" playlist holding its current mod
   state is created, so that there is always one to launch with.  *active_id
   receives the id of the active playlist (free with g_free). */
GPtrArray *playlists_load(Instance *instance, char **active_id);
void playlists_save(Instance *instance, GPtrArray *playlists,
                    const char *active_id);
Playlist *playlists_find(GPtrArray *playlists, const char *id);
char *playlist_mod_list_path(Instance *instance);
/* Enables exactly the mods of the playlist, disables the other mods of the
   instance and writes the loader mod list.  On success *mod_list_path holds the
   generated list file (free with g_free). */
gboolean playlist_apply(Instance *instance, Playlist *playlist,
                        char **mod_list_path, GError **error);
/* Writes the loader mod list definition for the playlist without changing the
   enabled mods.  On success *mod_list_path holds the generated list file (free
   with g_free). */
gboolean playlist_write_mod_list(Instance *instance, Playlist *playlist,
                                 char **mod_list_path, GError **error);
/* Writes the loader mod list definition for the playlist to an explicit path
   without changing the enabled mods. */
gboolean playlist_write_mod_list_to(Instance *instance, Playlist *playlist,
                                    const char *path, GError **error);
