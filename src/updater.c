#include "updater.h"
#include "i18n.h"
#include "version.h"

#include <gio/gio.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <string.h>

#define UPDATE_OWNER "Finality-Framework"
#define UPDATE_REPO "finality-manager2"
#define UPDATE_API_HOST "api.github.com"
#define UPDATE_API_PATH "/repos/" UPDATE_OWNER "/" UPDATE_REPO "/releases/latest"
#define UPDATE_RELEASE_URL \
  "https://github.com/" UPDATE_OWNER "/" UPDATE_REPO "/releases/latest"
#define UPDATE_TIMEOUT 10

typedef struct {
  Manager *manager;
  char *tag;
  char *url;
} UpdateCheck;

static gsize header_end(const guint8 *data, gsize length) {
  for (gsize i = 0; i + 3 < length; i++)
    if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' &&
        data[i + 3] == '\n')
      return i;
  return (gsize)-1;
}

/* Decodes a chunked HTTP body. Returns a newly allocated, NUL-terminated
   string, or NULL when the body is malformed. */
static char *decode_chunked(const guint8 *body, gsize length) {
  GByteArray *output = g_byte_array_new();
  gsize i = 0;
  for (;;) {
    gsize size = 0;
    gboolean digits = FALSE;
    while (i < length) {
      int c = body[i], value;
      if (c >= '0' && c <= '9')
        value = c - '0';
      else if (c >= 'a' && c <= 'f')
        value = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F')
        value = c - 'A' + 10;
      else
        break;
      size = size * 16 + (gsize)value;
      digits = TRUE;
      i++;
    }
    if (!digits)
      break;
    while (i < length && body[i] != '\n')
      i++;
    if (i < length)
      i++;
    if (size == 0)
      break;
    if (i + size > length)
      size = length - i;
    g_byte_array_append(output, body + i, (guint)size);
    i += size;
    if (i < length && body[i] == '\r')
      i++;
    if (i < length && body[i] == '\n')
      i++;
  }
  g_byte_array_append(output, (const guint8 *)"", 1);
  return (char *)g_byte_array_free(output, FALSE);
}

/* Performs a minimal blocking HTTPS GET. Returns the decoded body, or NULL on
   any failure (including systems without a usable TLS backend). */
static char *https_get(const char *host, const char *path) {
  GSocketClient *client = g_socket_client_new();
  g_socket_client_set_tls(client, TRUE);
  g_socket_client_set_timeout(client, UPDATE_TIMEOUT);
  GProxyResolver *proxy = g_proxy_resolver_get_default();
  if (proxy)
    g_socket_client_set_proxy_resolver(client, proxy);

  GError *error = NULL;
  GSocketConnection *connection =
      g_socket_client_connect_to_host(client, host, 443, NULL, &error);
  if (!connection) {
    if (error)
      g_error_free(error);
    g_object_unref(client);
    return NULL;
  }

  char *request = g_strdup_printf(
      "GET %s HTTP/1.1\r\n"
      "Host: %s\r\n"
      "User-Agent: Finality-Manager/%s\r\n"
      "Accept: application/vnd.github+json\r\n"
      "Connection: close\r\n\r\n",
      path, host, APP_VERSION);
  GOutputStream *output =
      g_io_stream_get_output_stream(G_IO_STREAM(connection));
  gsize written = 0;
  gboolean ok = g_output_stream_write_all(output, request, strlen(request),
                                          &written, NULL, &error);
  g_free(request);
  if (ok)
    ok = g_output_stream_flush(output, NULL, &error);

  GByteArray *response = g_byte_array_new();
  if (ok) {
    GInputStream *input =
        g_io_stream_get_input_stream(G_IO_STREAM(connection));
    guint8 buffer[4096];
    gssize count;
    while ((count = g_input_stream_read(input, buffer, sizeof buffer, NULL,
                                        &error)) > 0)
      g_byte_array_append(response, buffer, (guint)count);
    if (count < 0)
      ok = FALSE;
  }

  char *body = NULL;
  if (ok && response->len > 0) {
    gsize end = header_end(response->data, response->len);
    if (end != (gsize)-1 && end + 4 <= response->len) {
      char *status = g_strndup((char *)response->data, end);
      if (!g_strstr_len(status, -1, " 200 "))
        ok = FALSE;
      g_free(status);
      const guint8 *payload = response->data + end + 4;
      gsize payload_length = response->len - end - 4;
      char *headers = g_strndup((char *)response->data, end);
      char *lower = g_ascii_strdown(headers, -1);
      if (g_strstr_len(lower, -1, "transfer-encoding: chunked"))
        body = decode_chunked(payload, payload_length);
      else
        body = g_strndup((char *)payload, payload_length);
      g_free(lower);
      g_free(headers);
    }
  }

  if (error)
    g_error_free(error);
  g_byte_array_unref(response);
  g_io_stream_close(G_IO_STREAM(connection), NULL, NULL);
  g_object_unref(connection);
  g_object_unref(client);
  if (!ok) {
    g_free(body);
    return NULL;
  }
  return body;
}

/* Reads a string field from the small subset of JSON returned by the releases
   API. The first occurrence of the key is used. */
static char *json_string(const char *json, const char *key) {
  char *needle = g_strdup_printf("\"%s\"", key);
  const char *position = strstr(json, needle);
  g_free(needle);
  if (!position)
    return NULL;
  position = strchr(position + strlen(key) + 2, ':');
  if (!position)
    return NULL;
  position++;
  while (*position == ' ' || *position == '\t' || *position == '\r' ||
         *position == '\n')
    position++;
  if (*position != '"')
    return NULL;
  position++;
  GString *value = g_string_new(NULL);
  while (*position && *position != '"') {
    if (*position == '\\' && position[1]) {
      position++;
      switch (*position) {
      case 'n':
        g_string_append_c(value, '\n');
        break;
      case 'r':
        g_string_append_c(value, '\r');
        break;
      case 't':
        g_string_append_c(value, '\t');
        break;
      default:
        g_string_append_c(value, *position);
        break;
      }
      position++;
    } else {
      g_string_append_c(value, *position);
      position++;
    }
  }
  return g_string_free(value, FALSE);
}

static void parse_version(const char *version, int parts[3]) {
  parts[0] = parts[1] = parts[2] = 0;
  if (!version)
    return;
  while (*version == 'v' || *version == 'V' || *version == ' ')
    version++;
  for (int i = 0; i < 3; i++) {
    long value = 0;
    while (g_ascii_isdigit(*version)) {
      value = value * 10 + (*version - '0');
      version++;
    }
    parts[i] = (int)value;
    if (*version != '.')
      break;
    version++;
  }
}

static int compare_versions(const char *left, const char *right) {
  int a[3], b[3];
  parse_version(left, a);
  parse_version(right, b);
  for (int i = 0; i < 3; i++)
    if (a[i] != b[i])
      return a[i] < b[i] ? -1 : 1;
  return 0;
}

static void update_choice(GObject *source, GAsyncResult *result,
                          gpointer data) {
  char *url = data;
  GError *error = NULL;
  int choice =
      gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);
  if (error)
    g_error_free(error);
  if (choice == 1 && url)
    g_app_info_launch_default_for_uri(url, NULL, NULL);
  g_free(url);
}

static gboolean show_update(gpointer data) {
  UpdateCheck *check = data;
  if (check->tag && check->manager && GTK_IS_WINDOW(check->manager->window)) {
    GtkAlertDialog *dialog = gtk_alert_dialog_new(i18n("update.title"));
    char *message =
        g_strdup_printf(i18n("update.message"), check->tag, APP_VERSION);
    gtk_alert_dialog_set_detail(dialog, message);
    char *later = g_strdup(i18n("update.later"));
    char *open = g_strdup(i18n("update.open"));
    const char *buttons[] = {later, open, NULL};
    gtk_alert_dialog_set_buttons(dialog, buttons);
    gtk_alert_dialog_set_cancel_button(dialog, 0);
    gtk_alert_dialog_set_default_button(dialog, 1);
    gtk_alert_dialog_choose(dialog, GTK_WINDOW(check->manager->window), NULL,
                            update_choice,
                            g_strdup(check->url ? check->url
                                                : UPDATE_RELEASE_URL));
    g_object_unref(dialog);
    g_free(open);
    g_free(later);
    g_free(message);
  }
  g_free(check->tag);
  g_free(check->url);
  g_free(check);
  return G_SOURCE_REMOVE;
}

static gpointer update_thread(gpointer data) {
  UpdateCheck *check = data;
  char *json = https_get(UPDATE_API_HOST, UPDATE_API_PATH);
  if (json) {
    char *tag = json_string(json, "tag_name");
    char *url = json_string(json, "html_url");
    g_free(json);
    if (tag && compare_versions(tag, APP_VERSION) > 0) {
      check->tag = tag;
      check->url = url;
    } else {
      g_free(tag);
      g_free(url);
    }
  }
  g_idle_add(show_update, check);
  return NULL;
}

void updater_check(Manager *m) {
  static gboolean checked;
  if (checked)
    return;
  checked = TRUE;
  UpdateCheck *check = g_new0(UpdateCheck, 1);
  check->manager = m;
  g_thread_unref(g_thread_new("update-check", update_thread, check));
}
