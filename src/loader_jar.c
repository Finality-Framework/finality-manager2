#include "loader_jar.h"

#include <gio/gio.h>
#include <glib/gstdio.h>
#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif

/* The loader JAR is embedded as a binary object by the Makefile. The file is
   staged as loader.jar so these symbol names stay independent of its version. */
extern const unsigned char _binary_loader_jar_start[],
    _binary_loader_jar_end[];

#define LOADER_VERSION_ENTRY "META-INF/finality-loader.properties"
#define LOADER_PREFIX "finality-loader"
#define LOADER_SUFFIX ".jar"

static guint16 read_u16(const guint8 *p) {
  return (guint16)(p[0] | (p[1] << 8));
}

static guint32 read_u32(const guint8 *p) {
  return (guint32)p[0] | ((guint32)p[1] << 8) | ((guint32)p[2] << 16) |
         ((guint32)p[3] << 24);
}

static gchar *inflate_raw(const guint8 *data, gsize length, gsize *out_length) {
  GZlibDecompressor *decompressor =
      g_zlib_decompressor_new(G_ZLIB_COMPRESSOR_FORMAT_RAW);
  GInputStream *memory = g_memory_input_stream_new_from_data(data, length, NULL);
  GInputStream *stream =
      g_converter_input_stream_new(memory, G_CONVERTER(decompressor));
  GByteArray *output = g_byte_array_new();
  guint8 buffer[4096];
  gssize count;
  while ((count = g_input_stream_read(stream, buffer, sizeof buffer, NULL,
                                      NULL)) > 0) {
    g_byte_array_append(output, buffer, (guint)count);
  }
  g_object_unref(stream);
  g_object_unref(memory);
  g_object_unref(decompressor);
  *out_length = output->len;
  return (gchar *)g_byte_array_free(output, FALSE);
}

static gchar *version_from_text(const gchar *text) {
  if (!text)
    return NULL;
  gchar **lines = g_strsplit(text, "\n", -1);
  gchar *version = NULL;
  for (guint i = 0; lines[i]; i++) {
    gchar *line = g_strstrip(lines[i]);
    if (g_str_has_prefix(line, "version=")) {
      version = g_strdup(g_strstrip(line + strlen("version=")));
      break;
    }
  }
  g_strfreev(lines);
  if (version && !*version) {
    g_free(version);
    return NULL;
  }
  return version;
}

/* Minimal ZIP reader for a single stored or deflated entry. */
static gchar *version_from_jar(const guint8 *data, gsize size) {
  if (!data || size < 22)
    return NULL;

  gsize lowest = size > 65557 ? size - 65557 : 0;
  gsize eocd = G_MAXSIZE;
  for (gsize i = size - 22 + 1; i-- > lowest;) {
    if (read_u32(data + i) == 0x06054b50) {
      eocd = i;
      break;
    }
    if (i == 0)
      break;
  }
  if (eocd == G_MAXSIZE || eocd + 22 > size)
    return NULL;

  guint16 entries = read_u16(data + eocd + 10);
  guint32 cursor = read_u32(data + eocd + 16);
  for (guint16 i = 0; i < entries; i++) {
    if ((gsize)cursor + 46 > size || read_u32(data + cursor) != 0x02014b50)
      return NULL;

    guint16 method = read_u16(data + cursor + 10);
    guint32 compressed_size = read_u32(data + cursor + 20);
    guint16 name_length = read_u16(data + cursor + 28);
    guint16 extra_length = read_u16(data + cursor + 30);
    guint16 comment_length = read_u16(data + cursor + 32);
    guint32 local_offset = read_u32(data + cursor + 42);

    if ((gsize)cursor + 46 + name_length > size)
      return NULL;
    const gchar *name = (const gchar *)data + cursor + 46;
    if (name_length == strlen(LOADER_VERSION_ENTRY) &&
        memcmp(name, LOADER_VERSION_ENTRY, name_length) == 0) {
      if ((gsize)local_offset + 30 > size ||
          read_u32(data + local_offset) != 0x04034b50)
        return NULL;
      guint16 local_name = read_u16(data + local_offset + 26);
      guint16 local_extra = read_u16(data + local_offset + 28);
      gsize payload_offset =
          (gsize)local_offset + 30 + local_name + local_extra;
      if (payload_offset + compressed_size > size)
        return NULL;

      const guint8 *payload = data + payload_offset;
      gchar *text = NULL;
      if (method == 0) {
        text = g_strndup((const gchar *)payload, compressed_size);
      } else if (method == 8) {
        gsize inflated_size = 0;
        text = inflate_raw(payload, compressed_size, &inflated_size);
      }
      gchar *version = version_from_text(text);
      g_free(text);
      return version;
    }

    cursor += 46u + name_length + extra_length + comment_length;
  }
  return NULL;
}

static const guint8 *embedded_jar(gsize *size) {
  *size = (gsize)(_binary_loader_jar_end - _binary_loader_jar_start);
  return _binary_loader_jar_start;
}

static gchar *executable_dir(void) {
#ifdef G_OS_WIN32
  wchar_t buffer[32768];
  DWORD length = GetModuleFileNameW(NULL, buffer, G_N_ELEMENTS(buffer));
  gchar *path =
      length ? g_utf16_to_utf8((const gunichar2 *)buffer, length, NULL, NULL,
                               NULL)
             : g_strdup(".");
#else
  gchar *path = g_file_read_link("/proc/self/exe", NULL);
  if (!path)
    path = g_strdup(".");
#endif
  gchar *dir = g_path_get_dirname(path);
  g_free(path);
  return dir;
}

/* Finds the newest finality-loader-*.jar in res/ next to the executable. */
gchar *loader_jar_path(void) {
  gchar *base = executable_dir();
  gchar *res = g_build_filename(base, "res", NULL);
  g_free(base);

  GDir *dir = g_dir_open(res, 0, NULL);
  if (!dir) {
    g_free(res);
    return NULL;
  }

  gchar *best_name = NULL;
  const gchar *name;
  while ((name = g_dir_read_name(dir))) {
    if (!g_str_has_prefix(name, LOADER_PREFIX) ||
        !g_str_has_suffix(name, LOADER_SUFFIX))
      continue;
    if (!best_name || g_strcmp0(name, best_name) > 0) {
      g_free(best_name);
      best_name = g_strdup(name);
    }
  }
  g_dir_close(dir);

  gchar *path = best_name ? g_build_filename(res, best_name, NULL) : NULL;
  g_free(best_name);
  g_free(res);
  return path;
}

gchar *loader_jar_version(void) {
  gchar *path = loader_jar_path();
  if (path) {
    gchar *contents = NULL;
    gsize length = 0;
    if (g_file_get_contents(path, &contents, &length, NULL)) {
      gchar *version = version_from_jar((const guint8 *)contents, length);
      g_free(contents);
      g_free(path);
      if (version)
        return version;
    } else {
      g_free(path);
    }
  }

  gsize length = 0;
  const guint8 *data = embedded_jar(&length);
  return version_from_jar(data, length);
}

gchar *loader_jar_name(void) {
  gchar *path = loader_jar_path();
  if (path) {
    gchar *name = g_path_get_basename(path);
    g_free(path);
    return name;
  }
  gchar *version = loader_jar_version();
  gchar *name = version ? g_strdup_printf("%s-%s%s", LOADER_PREFIX, version,
                                           LOADER_SUFFIX)
                        : g_strdup(LOADER_PREFIX LOADER_SUFFIX);
  g_free(version);
  return name;
}

gchar *loader_jar_source(void) {
  gchar *path = loader_jar_path();
  if (path)
    return path;

  gchar *name = loader_jar_name();
  gchar *temp = g_build_filename(g_get_tmp_dir(),
                                 "finality-manager-embedded-loader.jar", NULL);
  gsize length = 0;
  const guint8 *data = embedded_jar(&length);
  if (!g_file_set_contents(temp, (const gchar *)data, (gssize)length, NULL)) {
    g_free(temp);
    temp = NULL;
  }
  g_free(name);
  return temp;
}
