#include "playlists_window.h"
#include "app.h"
#include "i18n.h"
#include "main_window.h"
#include "mods.h"
#include "playlist.h"
#include "string_util.h"
#include <gtk/gtk.h>
#include <string.h>

typedef struct {
  GtkWidget *row;
  GtkCheckButton *check;
  char *id;
  char *name;
  char *kind;
} EditorRow;

typedef struct {
  Manager *m;
  Playlist *playlist;
  GtkWidget *window;
  GtkWidget *count;
  GtkWidget *search;
  GtkWidget *source_filter;
  GtkWidget *state_filter;
  GHashTable *checked;
  GPtrArray *rows;
} PlaylistEditor;

typedef struct {
  Manager *m;
  Playlist *playlist;
  GtkWidget *window;
  GtkWidget *entry;
} NamePrompt;

typedef struct {
  Manager *m;
  Playlist *playlist;
} RemoveRequest;

static void playlists_window_populate(Manager *m);

static void main_window_playlist_changed(Manager *m) {
  Playlist *active = playlists_find(m->playlists, m->active_playlist_id);
  main_window_set_active_playlist(m, active ? active->name : NULL);
}

static void playlists_commit(Manager *m) {
  Instance *instance = selected_instance(m);
  if (instance)
    playlists_save(instance, m->playlists, m->active_playlist_id);
  playlists_window_populate(m);
  main_window_playlist_changed(m);
}

/* --------------------- manager state for one instance --------------------- */

void playlists_refresh(Manager *m) {
  Instance *instance = selected_instance(m);
  const char *path = instance ? instance->path : NULL;
  if (!m->playlists || g_strcmp0(m->playlists_instance_path, path) != 0) {
    if (m->playlist_editor)
      gtk_window_destroy(GTK_WINDOW(m->playlist_editor));
    if (m->playlist_prompt)
      gtk_window_destroy(GTK_WINDOW(m->playlist_prompt));
    g_free(m->playlists_instance_path);
    m->playlists_instance_path = g_strdup(path);
    if (m->playlists)
      g_ptr_array_unref(m->playlists);
    g_free(m->active_playlist_id);
    m->active_playlist_id = NULL;
    m->playlists = instance
                       ? playlists_load(instance, &m->active_playlist_id)
                       : g_ptr_array_new_with_free_func(playlist_free);
  }
  playlists_window_populate(m);
  main_window_playlist_changed(m);
}

/* ------------------------------ editor ------------------------------ */

static void editor_row_free(gpointer p) {
  EditorRow *row = p;
  g_free(row->id);
  g_free(row->name);
  g_free(row->kind);
  g_free(row);
}

static void editor_update_count(PlaylistEditor *editor) {
  char *text = g_strdup_printf(i18n("playlists.selected"),
                               g_hash_table_size(editor->checked));
  gtk_label_set_text(GTK_LABEL(editor->count), text);
  g_free(text);
}

static gboolean editor_row_visible(PlaylistEditor *editor, EditorRow *row) {
  const char *text = gtk_editable_get_text(GTK_EDITABLE(editor->search));
  if (text && *text) {
    char *folded_text = g_utf8_casefold(text, -1);
    char *folded_name = g_utf8_casefold(row->name, -1);
    gboolean match = strstr(folded_name, folded_text) != NULL;
    g_free(folded_name);
    g_free(folded_text);
    if (!match)
      return FALSE;
  }
  guint source = gtk_drop_down_get_selected(GTK_DROP_DOWN(editor->source_filter));
  if (source == 1 && g_strcmp0(row->kind, MOD_KIND_LOCAL) != 0)
    return FALSE;
  if (source == 2 && g_strcmp0(row->kind, MOD_KIND_WORKSHOP) != 0)
    return FALSE;
  guint state = gtk_drop_down_get_selected(GTK_DROP_DOWN(editor->state_filter));
  gboolean active = gtk_check_button_get_active(row->check);
  if (state == 1 && !active)
    return FALSE;
  if (state == 2 && active)
    return FALSE;
  return TRUE;
}

static void editor_apply_filter(PlaylistEditor *editor) {
  for (guint i = 0; i < editor->rows->len; i++) {
    EditorRow *row = g_ptr_array_index(editor->rows, i);
    gtk_widget_set_visible(row->row, editor_row_visible(editor, row));
  }
}

static void editor_toggled(GtkCheckButton *button, gpointer data) {
  PlaylistEditor *editor = data;
  const char *id = g_object_get_data(G_OBJECT(button), "finality-mod-id");
  if (!id)
    return;
  if (gtk_check_button_get_active(button))
    g_hash_table_add(editor->checked, g_strdup(id));
  else
    g_hash_table_remove(editor->checked, id);
  editor_update_count(editor);
  editor_apply_filter(editor);
}

static void editor_select_all(GtkButton *button, gpointer data) {
  (void)button;
  PlaylistEditor *editor = data;
  for (guint i = 0; i < editor->rows->len; i++) {
    EditorRow *row = g_ptr_array_index(editor->rows, i);
    if (gtk_widget_get_visible(row->row))
      gtk_check_button_set_active(row->check, TRUE);
  }
}

static void editor_select_none(GtkButton *button, gpointer data) {
  (void)button;
  PlaylistEditor *editor = data;
  for (guint i = 0; i < editor->rows->len; i++) {
    EditorRow *row = g_ptr_array_index(editor->rows, i);
    if (gtk_widget_get_visible(row->row))
      gtk_check_button_set_active(row->check, FALSE);
  }
}

static void editor_filter_changed(GObject *object, GParamSpec *pspec,
                                  gpointer data) {
  (void)object;
  (void)pspec;
  editor_apply_filter(data);
}

static void editor_search_changed(GtkSearchEntry *entry, gpointer data) {
  (void)entry;
  editor_apply_filter(data);
}

static void editor_destroy(GtkWidget *window, gpointer data) {
  PlaylistEditor *editor = data;
  Manager *m = editor->m;
  if (m->playlist_editor == window) {
    m->playlist_editor = NULL;
    m->playlist_editor_target = NULL;
  }
  g_hash_table_unref(editor->checked);
  g_ptr_array_unref(editor->rows);
  g_free(editor);
}

static void editor_save(GtkButton *button, gpointer data) {
  (void)button;
  PlaylistEditor *editor = data;
  Manager *m = editor->m;
  Instance *instance = selected_instance(m);
  if (!instance)
    return;
  GPtrArray *mods = instance_mods(instance);
  GHashTable *known = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  GPtrArray *ids = g_ptr_array_new_with_free_func(g_free);
  for (guint i = 0; i < mods->len; i++) {
    Mod *mod = g_ptr_array_index(mods, i);
    char *id = mod_id_from_path(instance, mod->path);
    if (g_hash_table_contains(editor->checked, id))
      g_ptr_array_add(ids, g_strdup(id));
    g_hash_table_add(known, id);
  }
  /* Keep ids that are not installed right now so that unplugged libraries do
     not silently drop mods from the playlist. */
  for (guint i = 0; i < editor->playlist->mods->len; i++) {
    const char *id = g_ptr_array_index(editor->playlist->mods, i);
    if (!g_hash_table_contains(known, id))
      g_ptr_array_add(ids, g_strdup(id));
  }
  g_ptr_array_unref(mods);
  while (editor->playlist->mods->len)
    g_ptr_array_remove_index(editor->playlist->mods,
                             editor->playlist->mods->len - 1);
  for (guint i = 0; i < ids->len; i++)
    g_ptr_array_add(editor->playlist->mods,
                    g_strdup(g_ptr_array_index(ids, i)));
  g_ptr_array_unref(ids);
  g_hash_table_unref(known);
  gtk_window_destroy(GTK_WINDOW(editor->window));
  playlists_commit(m);
  status(m, i18n("status.playlist_saved"));
}

static void editor_add_row(PlaylistEditor *editor, Instance *instance, Mod *mod,
                           GtkWidget *list) {
  EditorRow *entry = g_new0(EditorRow, 1);
  entry->id = mod_id_from_path(instance, mod->path);
  entry->name = g_strdup(mod->name);
  entry->kind = g_strdup(mod->kind);
  GtkWidget *row = gtk_list_box_row_new();
  GtkWidget *check = gtk_check_button_new();
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *name = gtk_label_new(mod->name);
  GtkWidget *origin = gtk_label_new(mod->source);
  GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  char *truncated = mod->description ? truncate_lines(mod->description, 2) : NULL;
  GtkWidget *desc =
      gtk_label_new(truncated && *truncated ? truncated : i18n("mods.no_desc"));
  g_free(truncated);
  gtk_label_set_xalign(GTK_LABEL(name), 0);
  gtk_label_set_xalign(GTK_LABEL(desc), 0);
  gtk_label_set_ellipsize(GTK_LABEL(desc), PANGO_ELLIPSIZE_END);
  gtk_widget_add_css_class(name, "heading");
  gtk_widget_add_css_class(desc, "dim-label");
  gtk_widget_add_css_class(origin, "dim-label");
  gtk_box_append(GTK_BOX(box), origin);
  gtk_box_append(GTK_BOX(box), name);
  gtk_box_append(GTK_BOX(labels), box);
  gtk_box_append(GTK_BOX(labels), desc);
  g_object_set_data_full(G_OBJECT(check), "finality-mod-id", g_strdup(entry->id),
                         g_free);
  gtk_check_button_set_active(GTK_CHECK_BUTTON(check),
                              g_hash_table_contains(editor->checked, entry->id));
  gtk_check_button_set_child(GTK_CHECK_BUTTON(check), labels);
  g_signal_connect(check, "toggled", G_CALLBACK(editor_toggled), editor);
  gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), check);
  gtk_list_box_append(GTK_LIST_BOX(list), row);
  entry->row = row;
  entry->check = GTK_CHECK_BUTTON(check);
  g_ptr_array_add(editor->rows, entry);
}

static void editor_cancel(GtkButton *button, gpointer data) {
  (void)button;
  PlaylistEditor *editor = data;
  gtk_window_destroy(GTK_WINDOW(editor->window));
}

/* Takes ownership of items, like gtk_drop_down_new() does. */
static GtkWidget *editor_dropdown(GtkStringList *items, guint selected,
                                  GCallback changed, gpointer data) {
  GtkWidget *dropdown = gtk_drop_down_new(G_LIST_MODEL(items), NULL);
  gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown), selected);
  g_signal_connect(dropdown, "notify::selected", changed, data);
  return dropdown;
}

void playlist_editor_open(Manager *m, Playlist *playlist) {
  if (!playlist)
    return;
  if (m->playlist_editor && m->playlist_editor_target == playlist) {
    gtk_window_present(GTK_WINDOW(m->playlist_editor));
    return;
  }
  if (m->playlist_editor)
    gtk_window_destroy(GTK_WINDOW(m->playlist_editor));
  Instance *instance = selected_instance(m);
  if (!instance)
    return;
  PlaylistEditor *editor = g_new0(PlaylistEditor, 1);
  editor->m = m;
  editor->playlist = playlist;
  editor->checked = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  editor->rows = g_ptr_array_new_with_free_func(editor_row_free);
  for (guint i = 0; i < playlist->mods->len; i++)
    g_hash_table_add(editor->checked,
                     g_strdup(g_ptr_array_index(playlist->mods, i)));

  GtkWidget *window = gtk_window_new();
  editor->window = window;
  set_window_icon(window);
  char *title = g_strdup_printf(i18n("mods.title_for"), playlist->name);
  gtk_window_set_title(GTK_WINDOW(window), title);
  gtk_window_set_default_size(GTK_WINDOW(window), 640, 560);
  gtk_window_set_transient_for(
      GTK_WINDOW(window),
      GTK_WINDOW(m->playlist_window ? m->playlist_window : m->window));
  GtkWidget *header = gtk_header_bar_new();
  GtkWidget *header_title = gtk_label_new(title);
  gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), header_title);
  gtk_window_set_titlebar(GTK_WINDOW(window), header);
  g_free(title);

  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
  gtk_widget_set_margin_top(box, 16);
  gtk_widget_set_margin_bottom(box, 16);
  gtk_widget_set_margin_start(box, 18);
  gtk_widget_set_margin_end(box, 18);
  GtkWidget *hint = gtk_label_new(i18n("mods.hint"));
  gtk_label_set_xalign(GTK_LABEL(hint), 0);
  gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
  gtk_widget_add_css_class(hint, "dim-label");
  gtk_box_append(GTK_BOX(box), hint);
  editor->search = gtk_search_entry_new();
  gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(editor->search),
                                        i18n("mods.search"));
  g_signal_connect(editor->search, "search-changed",
                   G_CALLBACK(editor_search_changed), editor);
  gtk_box_append(GTK_BOX(box), editor->search);
  GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkStringList *sources = gtk_string_list_new(NULL);
  gtk_string_list_append(sources, i18n("mods.source_all"));
  gtk_string_list_append(sources, i18n("mods.local"));
  gtk_string_list_append(sources, i18n("mods.steam"));
  editor->source_filter = editor_dropdown(sources, 0,
                                          G_CALLBACK(editor_filter_changed),
                                          editor);
  GtkStringList *states = gtk_string_list_new(NULL);
  gtk_string_list_append(states, i18n("mods.state_all"));
  gtk_string_list_append(states, i18n("mods.state_enabled"));
  gtk_string_list_append(states, i18n("mods.state_disabled"));
  editor->state_filter = editor_dropdown(states, 0,
                                         G_CALLBACK(editor_filter_changed),
                                         editor);
  GtkWidget *all = gtk_button_new_with_label(i18n("playlists.select_all"));
  GtkWidget *none = gtk_button_new_with_label(i18n("playlists.select_none"));
  gtk_widget_set_tooltip_text(all, i18n("mods.select_filtered"));
  gtk_widget_set_tooltip_text(none, i18n("mods.select_filtered"));
  g_signal_connect(all, "clicked", G_CALLBACK(editor_select_all), editor);
  g_signal_connect(none, "clicked", G_CALLBACK(editor_select_none), editor);
  gtk_box_append(GTK_BOX(tools), editor->source_filter);
  gtk_box_append(GTK_BOX(tools), editor->state_filter);
  gtk_box_append(GTK_BOX(tools), all);
  gtk_box_append(GTK_BOX(tools), none);
  editor->count = gtk_label_new(NULL);
  gtk_widget_add_css_class(editor->count, "heading");
  gtk_widget_set_hexpand(editor->count, TRUE);
  gtk_widget_set_halign(editor->count, GTK_ALIGN_END);
  editor_update_count(editor);
  gtk_box_append(GTK_BOX(tools), editor->count);
  gtk_box_append(GTK_BOX(box), tools);
  GtkWidget *scroll = gtk_scrolled_window_new();
  GtkWidget *list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_box_append(GTK_BOX(box), scroll);
  GPtrArray *mods = instance_mods(instance);
  for (guint i = 0; i < mods->len; i++)
    editor_add_row(editor, instance, g_ptr_array_index(mods, i), list);
  g_ptr_array_unref(mods);
  if (!editor->rows->len) {
    GtkWidget *empty = gtk_label_new(i18n("playlists.no_mods"));
    gtk_widget_set_margin_top(empty, 24);
    gtk_box_append(GTK_BOX(box), empty);
  }
  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  GtkWidget *cancel = gtk_button_new_with_label(i18n("button.cancel"));
  GtkWidget *save = gtk_button_new_with_label(i18n("button.save"));
  gtk_widget_add_css_class(save, "suggested-action");
  g_signal_connect(cancel, "clicked", G_CALLBACK(editor_cancel), editor);
  g_signal_connect(save, "clicked", G_CALLBACK(editor_save), editor);
  gtk_box_append(GTK_BOX(actions), cancel);
  gtk_box_append(GTK_BOX(actions), save);
  gtk_box_append(GTK_BOX(box), actions);
  gtk_window_set_child(GTK_WINDOW(window), box);
  g_signal_connect(window, "destroy", G_CALLBACK(editor_destroy), editor);
  m->playlist_editor = window;
  m->playlist_editor_target = playlist;
  gtk_window_present(GTK_WINDOW(window));
}

/* --------------------------- name prompt ---------------------------- */

static void prompt_destroy(GtkWidget *window, gpointer data) {
  NamePrompt *prompt = data;
  (void)window;
  if (prompt->m->playlist_prompt == prompt->window)
    prompt->m->playlist_prompt = NULL;
  g_free(prompt);
}

static void prompt_commit(NamePrompt *prompt) {
  Manager *m = prompt->m;
  Playlist *target = prompt->playlist;
  if (!m->playlists)
    return;
  char *name = g_strdup(gtk_editable_get_text(GTK_EDITABLE(prompt->entry)));
  g_strstrip(name);
  if (!*name) {
    g_free(name);
    return;
  }
  Playlist *created = NULL;
  if (target) {
    g_free(target->name);
    target->name = name;
  } else {
    created = playlist_new(name);
    g_free(name);
    g_ptr_array_add(m->playlists, created);
  }
  gtk_window_destroy(GTK_WINDOW(prompt->window));
  playlists_commit(m);
  if (created)
    playlist_editor_open(m, created);
}

static void prompt_accept(GtkButton *button, gpointer data) {
  (void)button;
  prompt_commit(data);
}

static void prompt_activate(GtkEntry *entry, gpointer data) {
  (void)entry;
  prompt_commit(data);
}

static void prompt_cancel(GtkButton *button, gpointer data) {
  (void)button;
  NamePrompt *prompt = data;
  gtk_window_destroy(GTK_WINDOW(prompt->window));
}

static void name_prompt_open(Manager *m, Playlist *playlist) {
  NamePrompt *prompt = g_new0(NamePrompt, 1);
  prompt->m = m;
  prompt->playlist = playlist;
  GtkWidget *window = gtk_window_new();
  prompt->window = window;
  set_window_icon(window);
  gtk_window_set_title(GTK_WINDOW(window),
                       i18n(playlist ? "dialog.rename_playlist"
                                     : "dialog.new_playlist"));
  gtk_window_set_modal(GTK_WINDOW(window), TRUE);
  gtk_window_set_transient_for(GTK_WINDOW(window),
                               GTK_WINDOW(m->playlist_window));
  gtk_window_set_default_size(GTK_WINDOW(window), 360, -1);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 18);
  gtk_widget_set_margin_bottom(box, 18);
  gtk_widget_set_margin_start(box, 18);
  gtk_widget_set_margin_end(box, 18);
  GtkWidget *label = gtk_label_new(i18n("playlists.name"));
  gtk_label_set_xalign(GTK_LABEL(label), 0);
  gtk_box_append(GTK_BOX(box), label);
  prompt->entry = gtk_entry_new();
  if (playlist && playlist->name)
    gtk_editable_set_text(GTK_EDITABLE(prompt->entry), playlist->name);
  g_signal_connect(prompt->entry, "activate", G_CALLBACK(prompt_activate),
                   prompt);
  gtk_box_append(GTK_BOX(box), prompt->entry);
  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  GtkWidget *cancel = gtk_button_new_with_label(i18n("button.cancel"));
  GtkWidget *accept = gtk_button_new_with_label(
      i18n(playlist ? "button.ok" : "button.create"));
  gtk_widget_add_css_class(accept, "suggested-action");
  g_signal_connect(cancel, "clicked", G_CALLBACK(prompt_cancel), prompt);
  g_signal_connect(accept, "clicked", G_CALLBACK(prompt_accept), prompt);
  gtk_box_append(GTK_BOX(actions), cancel);
  gtk_box_append(GTK_BOX(actions), accept);
  gtk_box_append(GTK_BOX(box), actions);
  gtk_window_set_child(GTK_WINDOW(window), box);
  g_signal_connect(window, "destroy", G_CALLBACK(prompt_destroy), prompt);
  m->playlist_prompt = window;
  gtk_window_present(GTK_WINDOW(window));
  gtk_widget_grab_focus(prompt->entry);
}

/* ------------------------- playlist actions ------------------------- */

static void playlist_activated(GtkCheckButton *check, gpointer data) {
  Manager *m = data;
  if (!gtk_check_button_get_active(check))
    return;
  Playlist *playlist = g_object_get_data(G_OBJECT(check), "finality-playlist");
  if (!playlist)
    return;
  g_free(m->active_playlist_id);
  m->active_playlist_id = g_strdup(playlist->id);
  Instance *instance = selected_instance(m);
  if (instance)
    playlists_save(instance, m->playlists, m->active_playlist_id);
  main_window_playlist_changed(m);
}

static void playlist_new_clicked(GtkButton *button, gpointer data) {
  (void)button;
  name_prompt_open(data, NULL);
}

static void playlist_edit_clicked(GtkButton *button, gpointer data) {
  (void)button;
  Manager *m = data;
  playlist_editor_open(m, playlists_find(m->playlists, m->active_playlist_id));
}

static void playlist_rename_clicked(GtkButton *button, gpointer data) {
  (void)button;
  Manager *m = data;
  Playlist *playlist = playlists_find(m->playlists, m->active_playlist_id);
  if (playlist)
    name_prompt_open(m, playlist);
}

static void remove_confirmed(GObject *source, GAsyncResult *result,
                             gpointer data) {
  RemoveRequest *request = data;
  Manager *m = request->m;
  Playlist *playlist = request->playlist;
  GError *error = NULL;
  int choice = gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result,
                                              &error);
  if (!error && choice == 0 && m->playlists) {
    if (m->playlist_editor_target == playlist)
      gtk_window_destroy(GTK_WINDOW(m->playlist_editor));
    if (g_strcmp0(m->active_playlist_id, playlist->id) == 0) {
      g_free(m->active_playlist_id);
      m->active_playlist_id = NULL;
    }
    g_ptr_array_remove(m->playlists, playlist);
    if (!m->active_playlist_id) {
      Playlist *active = playlists_find(m->playlists, NULL);
      if (active)
        m->active_playlist_id = g_strdup(active->id);
    }
    playlists_commit(m);
    status(m, i18n("status.playlist_removed"));
  }
  if (error)
    g_error_free(error);
  g_free(request);
  g_object_unref(source);
}

static void playlist_remove_clicked(GtkButton *button, gpointer data) {
  (void)button;
  Manager *m = data;
  Playlist *playlist = playlists_find(m->playlists, m->active_playlist_id);
  if (!playlist || m->playlists->len < 2)
    return;
  RemoveRequest *request = g_new0(RemoveRequest, 1);
  request->m = m;
  request->playlist = playlist;
  /* i18n() reuses one static buffer, so the labels must be copied first. */
  char *remove_label = g_strdup(i18n("playlists.remove"));
  char *cancel_label = g_strdup(i18n("button.cancel"));
  const char *buttons[] = {remove_label, cancel_label, NULL};
  GtkAlertDialog *dialog =
      gtk_alert_dialog_new("%s", i18n("dialog.remove_playlist"));
  gtk_alert_dialog_set_detail(dialog, i18n("dialog.remove_playlist_detail"));
  gtk_alert_dialog_set_buttons(dialog, buttons);
  g_free(remove_label);
  g_free(cancel_label);
  gtk_alert_dialog_set_cancel_button(dialog, 1);
  gtk_alert_dialog_set_default_button(dialog, 1);
  /* The callback owns the caller's reference, like the other alert dialogs. */
  gtk_alert_dialog_choose(dialog, GTK_WINDOW(m->playlist_window), NULL,
                          remove_confirmed, request);
}

static void playlist_window_destroy(GtkWidget *window, gpointer data) {
  Manager *m = data;
  if (m->playlist_window != window)
    return;
  if (m->playlist_editor)
    gtk_window_destroy(GTK_WINDOW(m->playlist_editor));
  if (m->playlist_prompt)
    gtk_window_destroy(GTK_WINDOW(m->playlist_prompt));
  m->playlist_window = NULL;
  m->playlist_list = NULL;
  m->playlist_empty = NULL;
  m->playlist_remove_button = NULL;
}

static void playlists_window_populate(Manager *m) {
  if (!m->playlist_window || !m->playlists)
    return;
  GtkWidget *child;
  while ((child = gtk_widget_get_first_child(m->playlist_list)))
    gtk_list_box_remove(GTK_LIST_BOX(m->playlist_list), child);
  Playlist *active = playlists_find(m->playlists, m->active_playlist_id);
  GtkWidget *first = NULL;
  for (guint i = 0; i < m->playlists->len; i++) {
    Playlist *playlist = g_ptr_array_index(m->playlists, i);
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *check = gtk_check_button_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *labels = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *name = gtk_label_new(playlist->name);
    char *count_text =
        g_strdup_printf(i18n("playlists.mod_count"), playlist->mods->len);
    GtkWidget *count = gtk_label_new(count_text);
    gtk_label_set_xalign(GTK_LABEL(name), 0);
    gtk_label_set_xalign(GTK_LABEL(count), 0);
    gtk_widget_add_css_class(name, "heading");
    gtk_widget_add_css_class(count, "dim-label");
    gtk_box_append(GTK_BOX(labels), name);
    gtk_box_append(GTK_BOX(labels), count);
    gtk_box_append(GTK_BOX(box), labels);
    gtk_widget_set_hexpand(labels, TRUE);
    gtk_check_button_set_child(GTK_CHECK_BUTTON(check), box);
    /* One radio group across all playlists; the active one is the checked one. */
    gtk_check_button_set_group(GTK_CHECK_BUTTON(check),
                               first ? GTK_CHECK_BUTTON(first) : NULL);
    if (!first)
      first = check;
    gtk_check_button_set_active(GTK_CHECK_BUTTON(check), playlist == active);
    g_object_set_data(G_OBJECT(check), "finality-playlist", playlist);
    g_signal_connect(check, "toggled", G_CALLBACK(playlist_activated), m);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), check);
    gtk_list_box_append(GTK_LIST_BOX(m->playlist_list), row);
    g_free(count_text);
  }
  gtk_widget_set_visible(m->playlist_empty, m->playlists->len == 0);
  if (m->playlist_remove_button)
    gtk_widget_set_sensitive(m->playlist_remove_button, m->playlists->len > 1);
}

void playlists_window_open(Manager *m) {
  if (!selected_instance(m))
    return;
  playlists_refresh(m);
  if (m->playlist_window) {
    gtk_window_present(GTK_WINDOW(m->playlist_window));
    return;
  }
  GtkWidget *window = gtk_window_new();
  m->playlist_window = window;
  set_window_icon(window);
  gtk_window_set_title(GTK_WINDOW(window), i18n("playlists.title"));
  gtk_window_set_default_size(GTK_WINDOW(window), 600, 460);
  gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(m->window));
  GtkWidget *header = gtk_header_bar_new();
  GtkWidget *header_title = gtk_label_new(i18n("playlists.title"));
  gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header), header_title);
  gtk_window_set_titlebar(GTK_WINDOW(window), header);
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
  gtk_widget_set_margin_top(box, 18);
  gtk_widget_set_margin_bottom(box, 18);
  gtk_widget_set_margin_start(box, 20);
  gtk_widget_set_margin_end(box, 20);
  GtkWidget *hint = gtk_label_new(i18n("playlists.hint"));
  gtk_label_set_xalign(GTK_LABEL(hint), 0);
  gtk_label_set_wrap(GTK_LABEL(hint), TRUE);
  gtk_widget_add_css_class(hint, "dim-label");
  gtk_box_append(GTK_BOX(box), hint);
  GtkWidget *scroll = gtk_scrolled_window_new();
  m->playlist_list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(m->playlist_list),
                                  GTK_SELECTION_NONE);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), m->playlist_list);
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_box_append(GTK_BOX(box), scroll);
  m->playlist_empty = gtk_label_new(i18n("playlists.empty"));
  gtk_widget_set_margin_top(m->playlist_empty, 12);
  gtk_box_append(GTK_BOX(box), m->playlist_empty);
  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *create = gtk_button_new_with_label(i18n("playlists.new"));
  GtkWidget *edit = gtk_button_new_with_label(i18n("playlists.edit"));
  GtkWidget *rename = gtk_button_new_with_label(i18n("playlists.rename"));
  m->playlist_remove_button =
      gtk_button_new_with_label(i18n("playlists.remove"));
  gtk_widget_add_css_class(create, "suggested-action");
  gtk_widget_set_tooltip_text(m->playlist_remove_button,
                              i18n("playlists.keep_one"));
  g_signal_connect(create, "clicked", G_CALLBACK(playlist_new_clicked), m);
  g_signal_connect(edit, "clicked", G_CALLBACK(playlist_edit_clicked), m);
  g_signal_connect(rename, "clicked", G_CALLBACK(playlist_rename_clicked), m);
  g_signal_connect(m->playlist_remove_button, "clicked",
                   G_CALLBACK(playlist_remove_clicked), m);
  gtk_box_append(GTK_BOX(actions), create);
  gtk_box_append(GTK_BOX(actions), edit);
  gtk_box_append(GTK_BOX(actions), rename);
  gtk_box_append(GTK_BOX(actions), m->playlist_remove_button);
  gtk_box_append(GTK_BOX(box), actions);
  gtk_window_set_child(GTK_WINDOW(window), box);
  g_signal_connect(window, "destroy", G_CALLBACK(playlist_window_destroy), m);
  playlists_window_populate(m);
  gtk_window_present(GTK_WINDOW(window));
}
