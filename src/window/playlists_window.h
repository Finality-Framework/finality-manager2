#pragma once
#include "app_types.h"

void playlists_window_open(Manager *m);
/* Loads (and if needed creates) the playlists of the selected instance and
   refreshes the playlist widgets. */
void playlists_refresh(Manager *m);
/* Opens the mod management of one playlist. */
void playlist_editor_open(Manager *m, Playlist *playlist);
