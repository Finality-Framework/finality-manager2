/* Regression test for Windows Java log capture.

   When this program is built as a GUI-subsystem executable (which is how the
   application itself is built, see PLATFORM_LDFLAGS) it has no console.  GLib
   then spawns children through its console-less helper, and java.exe loses the
   stdout pipe: the manager's log window stays empty.  win32_prepare_child_stdio()
   fixes that by giving the process a hidden console before spawning.

   The test spawns a Java process that prints a few lines one by one and checks
   that every line arrives through the same GDataInputStream pipe the log window
   uses.  Without the fix this captures nothing and fails.

   Java is looked up from JAVA_HOME or PATH, or passed as the first argument.
   When no Java executable is found the test reports SKIP and succeeds, so it is
   harmless on machines without a JVM.

   Build and run (Windows/Wine):
     make log-capture-test-windows
     make test-windows
*/
#include <gio/gio.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>

#include "win32_compat.h"

static GMainLoop *loop;
static int lines_read = 0;
static int expected = 3;
static GSubprocess *proc = NULL;
static gboolean timed_out = FALSE;

static void read_log_line(GObject *source, GAsyncResult *result,
                          gpointer data) {
  (void)source;
  GDataInputStream *stream = data;
  GError *error = NULL;
  gsize length = 0;
  char *line = g_data_input_stream_read_line_finish(stream, result, &length,
                                                    &error);
  if (line) {
    printf("captured: %s\n", line);
    lines_read++;
    g_free(line);
    if (lines_read >= expected) {
      g_main_loop_quit(loop);
      return;
    }
    g_data_input_stream_read_line_async(stream, G_PRIORITY_DEFAULT, NULL,
                                        read_log_line, stream);
    return;
  }
  if (error) {
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CLOSED))
      fprintf(stderr, "read error: %s\n", error->message);
    g_error_free(error);
  }
  g_main_loop_quit(loop);
}

static gboolean on_timeout(gpointer data) {
  (void)data;
  timed_out = TRUE;
  if (proc)
    g_subprocess_force_exit(proc);
  g_main_loop_quit(loop);
  return G_SOURCE_REMOVE;
}

static char *find_java(const char *explicit_path) {
  if (explicit_path && *explicit_path)
    return g_strdup(explicit_path);
  const char *env = g_getenv("FINALITY_TEST_JAVA");
  if (env && *env)
    return g_strdup(env);
  const char *home = g_getenv("JAVA_HOME");
  if (home && *home) {
    char *candidate = g_build_filename(home, "bin", "java.exe", NULL);
    if (g_file_test(candidate, G_FILE_TEST_IS_EXECUTABLE))
      return candidate;
    g_free(candidate);
  }
  char *found = g_find_program_in_path("java.exe");
  if (!found)
    found = g_find_program_in_path("java");
  return found;
}

int main(int argc, char **argv) {
  char *java = find_java(argc > 1 ? argv[1] : NULL);
  if (!java || !*java) {
    printf("SKIP: no java executable found (set FINALITY_TEST_JAVA)\n");
    return 0;
  }
  printf("using java: %s\n", java);

  /* This is the fix under test: give the GUI process a console so GLib keeps
     the child's stdout pipe intact. */
  win32_prepare_child_stdio();

  /* The child prints its lines with a delay so buffering cannot hide a loss of
     the pipe: if the pipe is gone nothing at all is captured. */
  char *source =
      "public class CapTest { public static void main(String[] a) "
      "throws Exception { "
      "for (int i = 1; i <= 3; i++) { System.out.println(\"line \" + i); "
      "System.out.flush(); Thread.sleep(200); } } }";
  char *dir = g_dir_make_tmp("finality-captest-XXXXXX", NULL);
  if (!dir) {
    fprintf(stderr, "failed to create temp dir\n");
    g_free(java);
    g_free(source);
    return 1;
  }
  char *src_path = g_build_filename(dir, "CapTest.java", NULL);
  g_file_set_contents(src_path, source, -1, NULL);

  /* Compile the tiny probe with javac if present.  If that is not possible the
     test still works by running the simpler "-version" style output below. */
  char *javac = NULL;
  char *java_bin = g_path_get_dirname(java);
  char *javac_guess = g_build_filename(java_bin, "javac.exe", NULL);
  if (g_file_test(javac_guess, G_FILE_TEST_IS_EXECUTABLE))
    javac = g_strdup(javac_guess);
  g_free(javac_guess);
  g_free(java_bin);

  gboolean compiled = FALSE;
  if (javac) {
    const char *cav[] = {javac, src_path, NULL};
    GError *ce = NULL;
    GSubprocess *c = g_subprocess_launcher_spawnv(
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                  G_SUBPROCESS_FLAGS_STDERR_SILENCE),
        cav, &ce);
    if (c) {
      g_subprocess_wait(c, NULL, NULL);
      compiled = g_subprocess_get_successful(c);
      g_object_unref(c);
    } else if (ce) {
      g_error_free(ce);
    }
  }

  const char *av[8];
  int n = 0;
  av[n++] = java;
  if (compiled) {
    av[n++] = "-cp";
    av[n++] = dir;
    av[n++] = "CapTest";
  } else {
    /* Fallback: java -version writes three lines to stdout-ish streams. */
    expected = 1;
    av[n++] = "-version";
  }
  av[n] = NULL;

  GError *error = NULL;
  GSubprocessLauncher *launcher = g_subprocess_launcher_new(
      G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_MERGE);
  g_subprocess_launcher_set_cwd(launcher, dir);
  proc = g_subprocess_launcher_spawnv(launcher, av, &error);
  g_object_unref(launcher);
  if (!proc) {
    fprintf(stderr, "spawn failed: %s\n", error ? error->message : "unknown");
    if (error)
      g_error_free(error);
    g_free(javac);
    g_free(src_path);
    g_free(source);
    g_rmdir(dir);
    g_free(dir);
    g_free(java);
    return 1;
  }

  GDataInputStream *stream =
      g_data_input_stream_new(g_subprocess_get_stdout_pipe(proc));
  g_data_input_stream_set_newline_type(stream, G_DATA_STREAM_NEWLINE_TYPE_ANY);
  loop = g_main_loop_new(NULL, FALSE);
  g_data_input_stream_read_line_async(stream, G_PRIORITY_DEFAULT, NULL,
                                      read_log_line, stream);
  g_timeout_add_seconds(20, on_timeout, NULL);
  g_main_loop_run(loop);

  int result;
  if (lines_read >= expected) {
    printf("PASS: captured %d line(s)\n", lines_read);
    result = 0;
  } else {
    printf("FAIL: captured %d of %d line(s)%s\n", lines_read, expected,
           timed_out ? " (timed out)" : "");
    result = 1;
  }

  if (proc) {
    g_subprocess_force_exit(proc);
    g_object_unref(proc);
  }
  g_object_unref(stream);
  if (loop)
    g_main_loop_unref(loop);
  g_free(javac);
  char *class_file = g_build_filename(dir, "CapTest.class", NULL);
  g_remove(class_file);
  g_free(class_file);
  g_remove(src_path);
  g_rmdir(dir);
  g_free(src_path);
  g_free(source);
  g_free(dir);
  g_free(java);
  return result;
}
