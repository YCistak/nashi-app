#include "uris.h"

#include <string.h>

char **uris_parse_list(const char *data, gsize len) {
  GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
  gchar *copy = g_strndup(data ? data : "", len);  /* the payload is not NUL-terminated */
  gchar **lines = g_strsplit_set(copy, "\r\n", -1);
  for (gchar **l = lines; *l; l++) {
    gchar *line = g_strstrip(*l);
    if (line[0] == '\0' || line[0] == '#')
      continue;
    if (!g_str_has_prefix(line, "file://"))
      continue;
    g_ptr_array_add(out, g_strdup(line));
  }
  g_strfreev(lines);
  g_free(copy);
  g_ptr_array_add(out, NULL);
  return (char **)g_ptr_array_free(out, FALSE);
}

void uris_json_string(GString *out, const char *s) {
  g_string_append_c(out, '"');
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    switch (*p) {
      case '"':  g_string_append(out, "\\\""); break;
      case '\\': g_string_append(out, "\\\\"); break;
      case '\n': g_string_append(out, "\\n"); break;
      case '\r': g_string_append(out, "\\r"); break;
      case '\t': g_string_append(out, "\\t"); break;
      default:
        if (*p < 0x20)
          g_string_append_printf(out, "\\u%04x", *p);
        else
          g_string_append_c(out, (char)*p);  /* UTF-8 passes through unchanged */
    }
  }
  g_string_append_c(out, '"');
}

char *uris_drop_json(const char *mode, const char *slice, char **uris) {
  GString *j = g_string_new("{\"t\":\"drop\",\"mode\":");
  uris_json_string(j, mode);
  g_string_append(j, ",\"slice\":");
  uris_json_string(j, slice);
  g_string_append(j, ",\"uris\":[");
  for (gsize i = 0; uris && uris[i]; i++) {
    if (i)
      g_string_append_c(j, ',');
    uris_json_string(j, uris[i]);
  }
  g_string_append(j, "]}");
  return g_string_free(j, FALSE);
}

char *uris_cancel_json(void) { return g_strdup("{\"t\":\"cancel\"}"); }
