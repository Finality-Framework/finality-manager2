#pragma once
#include <gio/gio.h>
#include <gtk/gtk.h>
typedef struct { char *name; char *path; char *appid; gboolean mp; } Instance;
typedef struct { char *name; char *path; char *description; char *source; char *kind; gboolean enabled; } Mod;
/* A playlist stores the "local:<name>" / "workshop:<id>" mod ids it enables.
   id is a stable identifier used to remember the active playlist. */
typedef struct { char *id; char *name; GPtrArray *mods; } Playlist;
typedef struct {
  GtkApplication *app; GtkWidget *window, *list, *details, *playlist_line, *status;
  GtkWidget *playlist_window, *playlist_list, *playlist_empty, *playlist_remove_button;
  GtkWidget *playlist_editor; Playlist *playlist_editor_target;
  GtkWidget *playlist_prompt;
  GPtrArray *playlists; char *playlists_instance_path; char *active_playlist_id;
  GPtrArray *instances; int selected;
  GtkWidget *log_window, *log_view, *log_status, *log_stop; GtkTextBuffer *log_buffer;
  gboolean auto_scroll; GSubprocess *process; gboolean process_running; guint process_serial;
  gboolean print_debug_log; char *language;
} Manager;
