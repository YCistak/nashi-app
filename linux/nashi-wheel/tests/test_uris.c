/* Display-free checks for text/uri-list parsing and the JSON output. */
#include "uris.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main(void) {
  /* CRLF lines, a comment, a blank line and a non-file URI. */
  const char *raw = "# comment\r\nfile:///tmp/a.png\r\n\r\nhttps://example.com/x\r\n"
                    "file:///tmp/b%20c.jpg\r\n";
  char **u = uris_parse_list(raw, strlen(raw));
  CHECK(g_strv_length(u) == 2);
  CHECK(g_str_equal(u[0], "file:///tmp/a.png"));
  CHECK(g_str_equal(u[1], "file:///tmp/b%20c.jpg"));

  /* Payload with no trailing newline and bare LF separators; length is honoured
   * (bytes after len must be ignored). */
  const char *raw2 = "file:///x\nfile:///yIGNORED";
  char **u2 = uris_parse_list(raw2, strlen("file:///x\nfile:///y"));
  CHECK(g_strv_length(u2) == 2 && g_str_equal(u2[1], "file:///y"));

  char **none = uris_parse_list("", 0);
  CHECK(g_strv_length(none) == 0);
  char **nul = uris_parse_list(NULL, 0);
  CHECK(g_strv_length(nul) == 0);

  char *j = uris_drop_json("convert", "to-png", u);
  CHECK(g_str_equal(j,
      "{\"t\":\"drop\",\"mode\":\"convert\",\"slice\":\"to-png\","
      "\"uris\":[\"file:///tmp/a.png\",\"file:///tmp/b%20c.jpg\"]}"));
  g_free(j);

  /* Escaping: quotes, backslashes, control characters, UTF-8 unchanged. */
  char *odd[] = { "file:///a\"b\\c\td\xc3\xa7", NULL };
  j = uris_drop_json("tools", "crop", odd);
  CHECK(strstr(j, "\"file:///a\\\"b\\\\c\\td\xc3\xa7\"") != NULL);
  g_free(j);

  j = uris_drop_json("actions", "copy", none);
  CHECK(g_str_equal(j, "{\"t\":\"drop\",\"mode\":\"actions\",\"slice\":\"copy\",\"uris\":[]}"));
  g_free(j);

  j = uris_cancel_json();
  CHECK(g_str_equal(j, "{\"t\":\"cancel\"}"));
  g_free(j);

  g_strfreev(u); g_strfreev(u2); g_strfreev(none); g_strfreev(nul);
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("uri checks OK\n");
  return 0;
}
