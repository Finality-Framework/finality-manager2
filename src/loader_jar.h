#pragma once

#include <glib.h>

/* Version declared by the Finality Loader JAR (read from
   META-INF/finality-loader.properties). The caller owns the result. */
gchar *loader_jar_version(void);

/* File name of the loader JAR shipped in res/, or the versioned fallback
   name when only the embedded copy is available. The caller owns the result. */
gchar *loader_jar_name(void);

/* Absolute path of the loader JAR in res/, or NULL when it is not installed. */
gchar *loader_jar_path(void);

/* Path to a loader JAR that can be copied or launched. Falls back to a
   temporary copy of the JAR embedded in the executable. */
gchar *loader_jar_source(void);
