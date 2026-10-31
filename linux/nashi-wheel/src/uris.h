/* nashi-wheel: text/uri-list parsing and the JSON lines the helper prints.
 * Pure GLib, no display needed, so it can be unit tested. */
#ifndef NASHI_URIS_H
#define NASHI_URIS_H

#include <glib.h>

/* Parses a text/uri-list payload (RFC 2483: CRLF separated, '#' comments,
 * blank lines ignored). Only file:// URIs are kept, anything else is dropped.
 * Returns a NULL-terminated, newly allocated vector (g_strfreev), possibly empty. */
char **uris_parse_list(const char *data, gsize len);

/* Appends s to out as a JSON string literal (quotes included). */
void uris_json_string(GString *out, const char *s);

/* {"t":"drop","mode":..,"slice":..,"uris":[..]} without a trailing newline. */
char *uris_drop_json(const char *mode, const char *slice, char **uris);

/* {"t":"cancel"} */
char *uris_cancel_json(void);

#endif
