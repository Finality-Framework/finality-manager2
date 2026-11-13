#include "main_window.h"
#include "about_window.h"
#include "app.h"
#include "config.h"
#include "i18n.h"
#include "loader_jar.h"
#include "logging_window.h"
#include "mods.h"
#include "playlist.h"
#include "playlists_window.h"
#include "preferences_window.h"
#include "win32_compat.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <shlobj.h>
#include <windows.h>
#define JAVA_BUNDLED "java.exe"
#define JAVA_COMMAND "java.exe"
#else
#define JAVA_BUNDLED "java"
#define JAVA_COMMAND "java"
#endif

extern const unsigned char _binary_res_logo_png_start[],
    _binary_res_logo_png_end[];

static void register_embedded_logo(void) {
  char *dir = g_build_filename(g_get_tmp_dir(), "finality-manager-icons", NULL);
  g_mkdir_with_parents(dir, 0700);
  char *path = g_build_filename(dir, "logo.png", NULL);
  if (!g_file_test(path, G_FILE_TEST_IS_REGULAR))
    g_file_set_contents(path, (const char *)_binary_res_logo_png_start,
                        (gssize)(_binary_res_logo_png_end -
                                 _binary_res_logo_png_start),
                        NULL);
  GtkIconTheme *theme =
      gtk_icon_theme_get_for_display(gdk_display_get_default());
  gtk_icon_theme_add_search_path(theme, dir);
  g_free(path);
  g_free(dir);
}
static void detect_steam_clicked(GtkButton *b, gpointer data) {
  (void)b;
  detect_steam(data);
  populate(data);
}
static void manage_mods_clicked(GtkButton *b, gpointer data) {
  (void)b;
  Manager *m = data;
  Playlist *playlist = playlists_find(m->playlists, m->active_playlist_id);
  if (!playlist) {
    status(m, i18n("status.no_playlist"));
    return;
  }
  playlist_editor_open(m, playlist);
}
static void playlists_clicked(GtkButton *b, gpointer data) {
  (void)b;
  playlists_window_open(data);
}
static void preferences_clicked(GtkButton *b, gpointer data) {
  (void)b;
  preferences_window_open(data);
}
static void about_clicked(GtkButton *b, gpointer data) {
  (void)b;
  about_window_open(data);
}
static void delete_instance(GtkButton *button, gpointer data) {
  Manager *m = data;
  (void)button;
  if (m->selected < 0 || m->selected >= (int)m->instances->len)
    return;
  g_ptr_array_remove_index(m->instances, m->selected);
  m->selected = -1;
  save_data(m);
  populate(m);
}
#ifndef G_OS_WIN32
static void chosen(GObject *o, GAsyncResult *r, gpointer data) {
  Manager *m = data;
  GError *e = NULL;
  GFile *f = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(o), r, &e);
  if (f) {
    char *p = g_file_get_path(f);
    scan_path(m, p);
    g_free(p);
    g_object_unref(f);
  }
  if (e) {
    if (!g_error_matches(e, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      status(m, e->message);
    g_error_free(e);
  }
}
#endif
static void choose(GtkButton *b, gpointer data) {
  Manager *m = data;
  (void)b;
#ifdef G_OS_WIN32
  wchar_t *title = g_utf8_to_utf16(i18n("dialog.choose"), -1, NULL, NULL, NULL);
  BROWSEINFOW bi = {0};
  bi.lpszTitle = title;
  bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
  LPITEMIDLIST item = SHBrowseForFolderW(&bi);
  if (item) {
    wchar_t path[32768];
    if (SHGetPathFromIDListW(item, path)) {
      char *selected_path =
          g_utf16_to_utf8((const gunichar2 *)path, -1, NULL, NULL, NULL);
      if (selected_path) {
        scan_path(m, selected_path);
        g_free(selected_path);
      }
    }
    CoTaskMemFree(item);
  }
  g_free(title);
#else
  GtkFileDialog *d = gtk_file_dialog_new();
  gtk_file_dialog_set_title(d, i18n("dialog.choose"));
  gtk_file_dialog_select_folder(d, GTK_WINDOW(m->window), NULL, chosen, m);
  g_object_unref(d);
#endif
}
static void selected(GtkListBox *b, GtkListBoxRow *r, gpointer data) {
  (void)b;
  Manager *m = data;
  if (!r || m->instances->len == 0)
    return;
  m->selected = gtk_list_box_row_get_index(r);
  if (m->selected >= (int)m->instances->len)
    return;
  Instance *i = g_ptr_array_index(m->instances, m->selected);
  playlists_refresh(m);
  guint enabled_mods = instance_enabled_mod_count(i);
  char *ready = g_strdup(i18n("details.ready"));
  char *mods_text = g_strdup_printf(i18n("details.enabled_mods"), enabled_mods);
  char *version = loader_jar_version();
  char *loader_text = g_strdup_printf(i18n("details.loader_version"),
                                      version ? version : "unknown");
  char *t = g_strdup_printf("<b>%s</b>\n\n%s\n\n%s\n\n%s\n\n%s", i->name,
                            i->path, ready, mods_text, loader_text);
  gtk_label_set_markup(GTK_LABEL(m->details), t);
  g_free(version);
  g_free(loader_text);
  g_free(mods_text);
  g_free(ready);
  g_free(t);
}

void main_window_set_active_playlist(Manager *m, const char *name) {
  if (!m->playlist_line)
    return;
  if (!name) {
    gtk_label_set_text(GTK_LABEL(m->playlist_line),
                       i18n("details.active_playlist_none"));
    return;
  }
  char *text = g_strdup_printf(i18n("details.active_playlist"), name);
  gtk_label_set_text(GTK_LABEL(m->playlist_line), text);
  g_free(text);
}
static void main_window_refresh_details(Manager *m) {
  if (!m->list)
    return;
  selected(GTK_LIST_BOX(m->list),
           gtk_list_box_get_selected_row(GTK_LIST_BOX(m->list)), m);
}
/* The Loader2 jar is copied into the instance and its output is streamed into
   the log window. The mode parameter remains for the existing UI callback. */
static gboolean launch_instance(Manager *m, const char *mod_list_path,
                                const char *mode) {
  Instance *i = selected_instance(m);
  if (!i)
    return FALSE;
  gboolean started = FALSE;
  char *s = loader_jar_source(), *jar_name = loader_jar_name(),
       *dst = g_build_filename(i->path, jar_name, NULL);
  GError *e = NULL;
  if (!s || !g_file_test(s, G_FILE_TEST_IS_REGULAR)) {
    status(m, i18n("status.loader_missing"));
    goto done;
  }
  GFile *sf = g_file_new_for_path(s), *df = g_file_new_for_path(dst);
  if (!g_file_copy(sf, df, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, &e)) {
    status(m, e->message);
    g_error_free(e);
    g_object_unref(sf);
    g_object_unref(df);
    goto done;
  }
  g_object_unref(sf);
  g_object_unref(df);
  char *j = NULL;
  if (i->mp)
    j = g_build_filename(i->path, "jdk", "bin", JAVA_BUNDLED, NULL);
  if (!j || !g_file_test(j, G_FILE_TEST_IS_EXECUTABLE)) {
    g_free(j);
    j = g_build_filename(i->path, "jre", "bin", JAVA_BUNDLED, NULL);
  }
  if (!g_file_test(j, G_FILE_TEST_IS_EXECUTABLE)) {
    g_free(j);
    j = g_strdup(JAVA_COMMAND);
  }
  GSubprocessLauncher *l = g_subprocess_launcher_new(
      G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
  g_subprocess_launcher_set_cwd(l, i->path);
  g_subprocess_launcher_setenv(l, "LANG", "C.UTF-8", TRUE);
  g_subprocess_launcher_setenv(l, "LC_ALL", "C.UTF-8", TRUE);
  char *debug_arg = g_strdup_printf("-Dfinality.debug=%s", m->print_debug_log ? "true" : "false");
  g_print("[INFO] Manager launching embedded Loader %s -> %s with Java %s; debug logging: %s\n", s, dst, j, m->print_debug_log ? "enabled" : "disabled");

  const char *av[25];
  int n = 0;
  av[n++] = j;
  av[n++] = debug_arg;
  av[n++] = "-Dfile.encoding=UTF-8";
  av[n++] = "-Dsun.stdout.encoding=UTF-8";
  av[n++] = "-Dsun.stderr.encoding=UTF-8";
  av[n++] = "-jar";
   av[n++] = jar_name;
   av[n++] = "--core";
   av[n++] = i->mp ? "./multi.jar" : "./game.jar";
   if (mod_list_path) {
     av[n++] = "--mods";
     av[n++] = mod_list_path;
   }
   (void)mode;
  av[n] = NULL;
  GSubprocess *x = g_subprocess_launcher_spawnv(l, av, &e);
  g_free(debug_arg);
  g_object_unref(l);
  if (x) {
    status(m, i18n(mode ? "status.generating" : "status.started"));
    if (m->process)
      g_object_unref(m->process);
    m->process = g_object_ref(x);
    logging_window_start(m, i->name, g_subprocess_get_stdout_pipe(x));
    g_object_unref(x);
    started = TRUE;
  } else {
    status(m, e->message);
    g_error_free(e);
  }
  g_free(j);
done:
  g_free(s);
  g_free(jar_name);
  g_free(dst);
  return started;
}
gboolean main_window_launch(Manager *m, const char *mod_list_path) {
  return launch_instance(m, mod_list_path, NULL);
}
/* The launch button applies the active playlist first and hands the JSON list
   to Loader2 with --mods. */
static void launch(GtkButton *b, gpointer data) {
  (void)b;
  Manager *m = data;
  Instance *instance = selected_instance(m);
  Playlist *playlist = playlists_find(m->playlists, m->active_playlist_id);
  if (!instance)
    return;
  if (!playlist) {
    status(m, i18n("status.no_playlist"));
    return;
  }
  char *mod_list = NULL;
  GError *error = NULL;
  if (!playlist_apply(instance, playlist, &mod_list, &error)) {
    status(m, error ? error->message : i18n("status.loader_missing"));
    g_clear_error(&error);
    return;
  }
  if (main_window_launch(m, mod_list)) {
    char *text = g_strdup_printf(i18n("status.playlist_launched"), playlist->name);
    status(m, text);
    g_free(text);
    main_window_refresh_details(m);
  }
  g_free(mod_list);
}
static void browse(GtkButton *button, gpointer data) {
  Manager *m = data;
  (void)button;
  if (m->selected < 0 || m->selected >= (int)m->instances->len)
    return;
  Instance *i = g_ptr_array_index(m->instances, m->selected);
  char *mods_path = g_build_filename(i->path, "mods", NULL);
  g_mkdir_with_parents(mods_path, 0755);

#ifdef G_OS_WIN32
  GError *e = NULL;
  GSubprocessLauncher *l = g_subprocess_launcher_new(
      G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
  g_subprocess_launcher_set_cwd(l, mods_path);
  const char *av[] = {"explorer.exe", mods_path};
  GSubprocess *x = g_subprocess_launcher_spawnv(l, av, &e);
  g_object_unref(l);
  g_object_unref(x);
  if (e) {
    g_error_free(e);
  }
#else
  char *uri = g_filename_to_uri(mods_path, NULL, NULL);
  if (uri) {
    g_app_info_launch_default_for_uri(uri, NULL, NULL);
    g_free(uri);
  }
#endif
  g_free(mods_path);
}
static void generate_mod_list(GtkButton *button, gpointer data) {
  (void)button;
  Manager *m = data;
  Instance *instance = selected_instance(m);
  Playlist *playlist = playlists_find(m->playlists, m->active_playlist_id);
  if (!instance)
    return;
  if (!playlist) {
    status(m, i18n("status.no_playlist"));
    return;
  }
  char *path = NULL;
  GError *error = NULL;
  if (!playlist_write_mod_list(instance, playlist, &path, &error)) {
    status(m, error ? error->message : i18n("status.generate_failed"));
    g_clear_error(&error);
    g_free(path);
    return;
  }
  char *text = g_strdup_printf(i18n("status.mod_list_generated"), path);
  status(m, text);
  g_free(text);
  g_free(path);
}
static void generate_jar(GtkButton *button, gpointer data) {
  (void)button;
  Manager *m = data;
  Instance *instance = selected_instance(m);
  Playlist *playlist = playlists_find(m->playlists, m->active_playlist_id);
  if (!instance)
    return;
  if (!playlist) {
    status(m, i18n("status.no_playlist"));
    return;
  }
  char *mod_list = NULL;
  GError *error = NULL;
  if (!playlist_apply(instance, playlist, &mod_list, &error)) {
    status(m, error ? error->message : i18n("status.generate_failed"));
    g_clear_error(&error);
    g_free(mod_list);
    return;
  }
  launch_instance(m, mod_list, NULL);
  g_free(mod_list);
}
static gboolean install_loader_jar(Manager *m, Instance *instance,
                                  GError **error) {
  char *source = loader_jar_source();
  char *name = loader_jar_name();
  char *dst = g_build_filename(instance->path, name, NULL);
  if (!source || !g_file_test(source, G_FILE_TEST_IS_REGULAR)) {
    status(m, i18n("status.loader_missing"));
    g_free(source);
    g_free(name);
    g_free(dst);
    return FALSE;
  }
  GFile *sf = g_file_new_for_path(source), *df = g_file_new_for_path(dst);
  gboolean ok =
      g_file_copy(sf, df, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, error);
  g_object_unref(sf);
  g_object_unref(df);
  g_free(source);
  g_free(name);
  g_free(dst);
  return ok;
}
static void generate_launch_scripts(GtkButton *button, gpointer data) {
  (void)button;
  Manager *m = data;
  Instance *instance = selected_instance(m);
  Playlist *playlist = playlists_find(m->playlists, m->active_playlist_id);
  if (!instance)
    return;
  if (!playlist) {
    status(m, i18n("status.no_playlist"));
    return;
  }
  char *dir = g_build_filename(instance->path, ".finality", NULL);
  char *jar_name = loader_jar_name();
  GError *error = NULL;
  if (g_mkdir_with_parents(dir, 0755) != 0) {
    status(m, i18n("status.generate_failed"));
    g_free(jar_name);
    g_free(dir);
    return;
  }
  char *mod_file = g_build_filename(dir, "modlist.json", NULL);
  if (!playlist_write_mod_list_to(instance, playlist, mod_file, &error)) {
    status(m, error ? error->message : i18n("status.generate_failed"));
    g_clear_error(&error);
    goto done;
  }
  if (!install_loader_jar(m, instance, &error)) {
    if (error) {
      status(m, error->message);
      g_clear_error(&error);
    }
    goto done;
  }

  const char *core = instance->mp ? "multi.jar" : "game.jar";
  const char *debug = m->print_debug_log ? "true" : "false";

  GString *sh = g_string_new(
      "#!/bin/sh\n"
      "# Generated by Finality Manager.\n"
      "SCRIPT_DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n"
      "GAME_DIR=$(CDPATH= cd -- \"$SCRIPT_DIR/..\" && pwd)\n"
      "JAVA=\"java\"\n"
      "if [ -x \"$GAME_DIR/jdk/bin/java\" ]; then\n"
      "  JAVA=\"$GAME_DIR/jdk/bin/java\"\n"
      "elif [ -x \"$GAME_DIR/jre/bin/java\" ]; then\n"
      "  JAVA=\"$GAME_DIR/jre/bin/java\"\n"
      "fi\n"
      "cd \"$GAME_DIR\" || exit 1\n");
  char *sh_cmd = g_strdup_printf(
      "exec \"$JAVA\" -Dfinality.debug=%s -Dfile.encoding=UTF-8 "
      "-Dsun.stdout.encoding=UTF-8 -Dsun.stderr.encoding=UTF-8 "
      "-jar \"%s\" --core \"%s\" "
      "--mods \"$SCRIPT_DIR/modlist.json\" \"$@\"\n",
      debug, jar_name, core);
  g_string_append(sh, sh_cmd);
  g_free(sh_cmd);

  GString *bat = g_string_new(
      "@echo off\r\n"
      "setlocal\r\n"
      "set \"SCRIPT_DIR=%~dp0\"\r\n"
      "set \"GAME_DIR=%~dp0..\"\r\n"
      "set \"JAVA=java\"\r\n"
      "if exist \"%GAME_DIR%\\jdk\\bin\\java.exe\" set "
      "\"JAVA=%GAME_DIR%\\jdk\\bin\\java.exe\"\r\n"
      "if exist \"%GAME_DIR%\\jre\\bin\\java.exe\" if /I \"%JAVA%\"==\"java\" "
      "set \"JAVA=%GAME_DIR%\\jre\\bin\\java.exe\"\r\n"
      "cd /d \"%GAME_DIR%\"\r\n");
  char *bat_cmd = g_strdup_printf(
      "\"%%JAVA%%\" -Dfinality.debug=%s -Dfile.encoding=UTF-8 "
      "-Dsun.stdout.encoding=UTF-8 -Dsun.stderr.encoding=UTF-8 "
      "-jar \"%s\" --core \"%s\" "
      "--mods \"%%SCRIPT_DIR%%modlist.json\" %%*\r\n"
      "exit /b %%ERRORLEVEL%%\r\n",
      debug, jar_name, core);
  g_string_append(bat, bat_cmd);
  g_free(bat_cmd);

  GString *ps1 = g_string_new(
      "# Generated by Finality Manager.\r\n"
      "$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path\r\n"
      "$GameDir = Split-Path -Parent $ScriptDir\r\n"
      "$Java = \"java\"\r\n"
      "$BundledJdk = Join-Path $GameDir \"jdk\\bin\\java.exe\"\r\n"
      "$BundledJre = Join-Path $GameDir \"jre\\bin\\java.exe\"\r\n"
      "if (Test-Path $BundledJdk) { $Java = $BundledJdk }\r\n"
      "elseif (Test-Path $BundledJre) { $Java = $BundledJre }\r\n"
      "Set-Location $GameDir\r\n");
  char *ps1_cmd = g_strdup_printf(
      "& $Java -Dfinality.debug=%s -Dfile.encoding=UTF-8 "
      "-Dsun.stdout.encoding=UTF-8 -Dsun.stderr.encoding=UTF-8 "
      "-jar \"%s\" --core \"%s\" "
      "--mods (Join-Path $ScriptDir \"modlist.json\") @args\r\n"
      "exit $LASTEXITCODE\r\n",
      debug, jar_name, core);
  g_string_append(ps1, ps1_cmd);
  g_free(ps1_cmd);

  char *sh_path = g_build_filename(dir, "launch.sh", NULL);
  char *bat_path = g_build_filename(dir, "launch.bat", NULL);
  char *ps1_path = g_build_filename(dir, "launch.ps1", NULL);
  gboolean ok = g_file_set_contents(sh_path, sh->str, sh->len, &error) &&
                g_file_set_contents(bat_path, bat->str, bat->len, &error) &&
                g_file_set_contents(ps1_path, ps1->str, ps1->len, &error);
  if (ok)
    g_chmod(sh_path, 0755);
  if (ok) {
    char *text = g_strdup_printf(i18n("status.launch_scripts_generated"), dir);
    status(m, text);
    g_free(text);
  } else {
    status(m, error ? error->message : i18n("status.generate_failed"));
    g_clear_error(&error);
  }
  g_free(sh_path);
  g_free(bat_path);
  g_free(ps1_path);
  g_string_free(sh, TRUE);
  g_string_free(bat, TRUE);
  g_string_free(ps1, TRUE);
done:
  g_free(jar_name);
  g_free(mod_file);
  g_free(dir);
}
static GtkWidget *tools_popover(Manager *m) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_top(box, 3);
  gtk_widget_set_margin_bottom(box, 3);
  gtk_widget_set_margin_start(box, 6);
  gtk_widget_set_margin_end(box, 6);
  GtkWidget *mod_list = gtk_button_new_with_label(i18n("tools.generate_mod_list"));
  GtkWidget *scripts =
      gtk_button_new_with_label(i18n("tools.generate_launch_scripts"));
  GtkWidget *jar = gtk_button_new_with_label(i18n("tools.generate_jar"));
  gtk_widget_set_halign(mod_list, GTK_ALIGN_FILL);
  gtk_widget_set_halign(scripts, GTK_ALIGN_FILL);
  gtk_widget_set_halign(jar, GTK_ALIGN_FILL);
  g_signal_connect(mod_list, "clicked", G_CALLBACK(generate_mod_list), m);
  g_signal_connect(scripts, "clicked", G_CALLBACK(generate_launch_scripts), m);
  g_signal_connect(jar, "clicked", G_CALLBACK(generate_jar), m);
  gtk_box_append(GTK_BOX(box), mod_list);
  gtk_box_append(GTK_BOX(box), scripts);
  gtk_box_append(GTK_BOX(box), jar);
  GtkWidget *popover = gtk_popover_new();
  gtk_popover_set_child(GTK_POPOVER(popover), box);
  return popover;
}
static GtkWidget *more_popover(Manager *m) {
  GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
  gtk_widget_set_margin_top(box, 3);
  gtk_widget_set_margin_bottom(box, 3);
  gtk_widget_set_margin_start(box, 6);
  gtk_widget_set_margin_end(box, 6);
  GtkWidget *prefs = gtk_button_new_with_label(i18n("preferences.title"));
  GtkWidget *about = gtk_button_new_with_label(i18n("about.title"));
  gtk_widget_set_halign(prefs, GTK_ALIGN_FILL);
  gtk_widget_set_halign(about, GTK_ALIGN_FILL);
  g_signal_connect(prefs, "clicked", G_CALLBACK(preferences_clicked), m);
  g_signal_connect(about, "clicked", G_CALLBACK(about_clicked), m);
  gtk_box_append(GTK_BOX(box), prefs);
  gtk_box_append(GTK_BOX(box), about);
  GtkWidget *popover = gtk_popover_new();
  gtk_popover_set_child(GTK_POPOVER(popover), box);
  return popover;
}
void app_activate(GtkApplication *app, gpointer data) {
  Manager *m = data;
  m->app = app;
  m->selected = -1;
  win32_prepare_child_stdio();
  gboolean first_run = !config_exists();
  manager_load(m);
  i18n_set_language(m->language);
  register_embedded_logo();
  char *loader_path = loader_jar_path();
  if (loader_path) {
    char *resource_dir = g_path_get_dirname(loader_path);
    GtkIconTheme *theme =
        gtk_icon_theme_get_for_display(gdk_display_get_default());
    gtk_icon_theme_add_search_path(theme, resource_dir);
    g_free(resource_dir);
  }
  g_free(loader_path);
  m->window = gtk_application_window_new(app);
  set_window_icon(m->window);
  gtk_window_set_default_icon_name("logo");
  gtk_window_set_title(GTK_WINDOW(m->window), "Finality Manager");
  gtk_window_set_default_size(GTK_WINDOW(m->window), 900, 560);
  GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
            *h = gtk_header_bar_new(),
            *a = gtk_button_new_from_icon_name("folder-open-symbolic"),
            *del = gtk_button_new_from_icon_name("user-trash-symbolic"),
            *more = gtk_menu_button_new();
  gtk_widget_set_tooltip_text(a, i18n("button.add"));
  gtk_widget_set_tooltip_text(del, i18n("button.remove"));
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(more), "open-menu-symbolic");
  gtk_widget_set_tooltip_text(more, i18n("button.more"));
  g_signal_connect(a, "clicked", G_CALLBACK(choose), m);
  g_signal_connect(del, "clicked", G_CALLBACK(delete_instance), m);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(h), a);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(h), del);
  GtkWidget *detect = gtk_button_new_from_icon_name("system-search-symbolic");
  gtk_widget_set_tooltip_text(detect, i18n("button.detect_steam"));
  g_signal_connect(detect, "clicked", G_CALLBACK(detect_steam_clicked), m);
  gtk_header_bar_pack_start(GTK_HEADER_BAR(h), detect);
  gtk_menu_button_set_popover(GTK_MENU_BUTTON(more), more_popover(m));
  gtk_header_bar_pack_end(GTK_HEADER_BAR(h), more);
  gtk_window_set_titlebar(GTK_WINDOW(m->window), h);
  GtkWidget *pan = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  m->list = gtk_list_box_new();
  gtk_list_box_set_selection_mode(GTK_LIST_BOX(m->list), GTK_SELECTION_SINGLE);
  gtk_widget_set_size_request(m->list, 330, -1);
  g_signal_connect(m->list, "row-selected", G_CALLBACK(selected), m);
  GtkWidget *sc = gtk_scrolled_window_new();
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sc), m->list);
  gtk_paned_set_start_child(GTK_PANED(pan), sc);
  gtk_widget_set_vexpand(pan, TRUE);
  gtk_widget_set_vexpand(sc, TRUE);
  gtk_paned_set_position(GTK_PANED(pan), 330);
  GtkWidget *right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
  gtk_widget_set_margin_top(right, 28);
  gtk_widget_set_margin_start(right, 32);
  gtk_widget_set_margin_end(right, 32);

  GtkWidget *t = gtk_label_new(i18n("details.title"));
  gtk_widget_add_css_class(t, "title-2");
  gtk_widget_set_halign(t, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(right), t);
  m->details = gtk_label_new(i18n("details.empty"));
  gtk_label_set_xalign(GTK_LABEL(m->details), 0);
  gtk_label_set_wrap(GTK_LABEL(m->details), TRUE);
  gtk_widget_set_vexpand(m->details, TRUE);
  gtk_widget_set_valign(m->details, GTK_ALIGN_START);
  gtk_widget_set_margin_top(m->details, 0);
  gtk_widget_set_margin_bottom(m->details, 24);
  gtk_box_append(GTK_BOX(right), m->details);
  m->playlist_line = gtk_label_new(i18n("details.active_playlist_none"));
  gtk_label_set_xalign(GTK_LABEL(m->playlist_line), 0);
  gtk_widget_set_margin_bottom(m->playlist_line, 12);
  gtk_box_append(GTK_BOX(right), m->playlist_line);
  GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_halign(actions, GTK_ALIGN_END);
  GtkWidget *tools = gtk_menu_button_new();
  gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(tools),
                                "applications-engineering-symbolic");
  gtk_widget_set_tooltip_text(tools, i18n("button.tools"));
  gtk_menu_button_set_popover(GTK_MENU_BUTTON(tools), tools_popover(m));
  gtk_box_append(GTK_BOX(actions), tools);
  GtkWidget *open = gtk_button_new_from_icon_name("folder-open-symbolic");
  gtk_widget_set_tooltip_text(open, i18n("button.browse_mods"));
  g_signal_connect(open, "clicked", G_CALLBACK(browse), m);
  gtk_box_append(GTK_BOX(actions), open);
  GtkWidget *manage = gtk_button_new_with_label(i18n("button.manage_mods"));
  g_signal_connect(manage, "clicked", G_CALLBACK(manage_mods_clicked), m);
  gtk_box_append(GTK_BOX(actions), manage);
  GtkWidget *playlists = gtk_button_new_with_label(i18n("button.playlists"));
  g_signal_connect(playlists, "clicked", G_CALLBACK(playlists_clicked), m);
  gtk_box_append(GTK_BOX(actions), playlists);
  GtkWidget *run = gtk_button_new_with_label(i18n("button.launch"));
  gtk_widget_add_css_class(run, "suggested-action");
  g_signal_connect(run, "clicked", G_CALLBACK(launch), m);
  gtk_box_append(GTK_BOX(actions), run);
  gtk_box_append(GTK_BOX(right), actions);
  gtk_paned_set_end_child(GTK_PANED(pan), right);
  gtk_widget_set_vexpand(pan, TRUE);
  gtk_box_append(GTK_BOX(root), pan);
  m->status = gtk_label_new(i18n("status.choose"));
  gtk_widget_set_halign(m->status, GTK_ALIGN_START);
  gtk_widget_set_margin_start(m->status, 14);
  gtk_widget_set_margin_top(m->status, 8);
  gtk_widget_set_margin_bottom(m->status, 8);
  gtk_box_append(GTK_BOX(root), m->status);
  if (first_run)
    detect_steam(m);
  populate(m);
  gtk_window_set_child(GTK_WINDOW(m->window), root);
  gtk_window_present(GTK_WINDOW(m->window));
}
