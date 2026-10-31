/* Compositor-free checks for the Hyprland trigger helpers. */
#include "hypr.h"

#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* Bind key strings created with hl.bind("...") / removed with hl.unbind("..."). */
static GPtrArray *quoted_after(const char *lua, const char *call) {
  GPtrArray *out = g_ptr_array_new_with_free_func(g_free);
  for (const char *p = strstr(lua, call); p; p = strstr(p + 1, call)) {
    const char *q = p + strlen(call);
    const char *end = strchr(q, '"');
    if (end)
      g_ptr_array_add(out, g_strndup(q, end - q));
  }
  return out;
}

static gboolean contains(GPtrArray *a, const char *s) {
  for (guint i = 0; i < a->len; i++)
    if (g_str_equal(g_ptr_array_index(a, i), s))
      return TRUE;
  return FALSE;
}

/* Regression test for the Hyprland 0.56.2 crash: a HL.Keybind object must
 * never be stored, returned or have a method called on it. */
static void check_crash_trap(const char *lua) {
  CHECK(!strstr(lua, "= hl.bind") && !strstr(lua, "=hl.bind"));
  CHECK(!strstr(lua, "return hl.bind") && !strstr(lua, "(hl.bind"));
  CHECK(!strstr(lua, ":set_enabled") && !strstr(lua, ":unbind"));
  /* The only method call allowed is on the event subscription. */
  for (const char *p = strchr(lua, ':'); p; p = strchr(p + 1, ':')) {
    if (g_str_has_prefix(p, ":remove"))
      CHECK(p - lua >= 16 && strncmp(p - 16, "_nashi_wheel_sub", 16) == 0);
  }
  CHECK(strstr(lua, "pcall(function() _nashi_wheel_sub:remove() end)") != NULL);
  /* Every bind is preceded by an unbind with the very same string, so a
   * second install never leaves two binds with the same keys. */
  GPtrArray *binds = quoted_after(lua, "hl.bind(\"");
  GPtrArray *unbinds = quoted_after(lua, "hl.unbind(\"");
  for (guint i = 0; i < binds->len; i++) {
    const char *b = g_ptr_array_index(binds, i);
    CHECK(contains(unbinds, b));
    CHECK(strstr(b, "MOD3 + MOD5 + ") != NULL);  /* never a user-like bind */
    char *ub = g_strdup_printf("hl.unbind(\"%s\")", b);
    char *bd = g_strdup_printf("hl.bind(\"%s\"", b);
    CHECK(strstr(lua, ub) < strstr(lua, bd));
    g_free(ub);
    g_free(bd);
  }
  g_ptr_array_unref(binds);
  g_ptr_array_unref(unbinds);
}

/* Minimal fake .socket.sock: answers one request with a fixed reply. */
typedef struct {
  GSocket *listener;
  const char *reply;
  char request[256];
} Fake;

static gpointer fake_serve(gpointer data) {
  Fake *f = data;
  GSocket *c = g_socket_accept(f->listener, NULL, NULL);
  if (!c)
    return NULL;
  gssize n = g_socket_receive(c, f->request, sizeof f->request - 1, NULL, NULL);
  f->request[n > 0 ? n : 0] = '\0';
  g_socket_send(c, f->reply, strlen(f->reply), NULL, NULL);
  g_socket_close(c, NULL);
  g_object_unref(c);
  return NULL;
}

static char *fake_request(const char *dir, const char *reply, const char *req, Fake *f,
                          gboolean eval, gboolean *eval_ok) {
  char *path = g_build_filename(dir, ".socket.sock", NULL);
  g_unlink(path);
  GSocketAddress *addr = g_unix_socket_address_new(path);
  f->listener = g_socket_new(G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, 0, NULL);
  f->reply = reply;
  g_socket_bind(f->listener, addr, TRUE, NULL);
  g_socket_listen(f->listener, NULL);
  GThread *th = g_thread_new("fake", fake_serve, f);
  char *out = NULL;
  if (eval)
    *eval_ok = hypr_eval(dir, req, NULL);
  else
    out = hypr_request(dir, req, NULL);
  g_thread_join(th);
  g_socket_close(f->listener, NULL);
  g_object_unref(f->listener);
  g_object_unref(addr);
  g_unlink(path);
  g_free(path);
  return out;
}

int main(void) {
  /* socket2 parsing: only our own custom events and reloads count. */
  HyprEvent e = hypr_parse_event("custom>>nashi:convert");
  CHECK(e.kind == HYPR_EV_TRIGGER && g_str_equal(e.mode, "convert"));
  e = hypr_parse_event("custom>>nashi:tools");
  CHECK(e.kind == HYPR_EV_TRIGGER && g_str_equal(e.mode, "tools"));
  e = hypr_parse_event("custom>>nashi:actions");
  CHECK(e.kind == HYPR_EV_TRIGGER && g_str_equal(e.mode, "actions"));
  CHECK(hypr_parse_event("custom>>nashi:up").kind == HYPR_EV_UP);
  CHECK(hypr_parse_event("configreloaded>>").kind == HYPR_EV_RELOAD);
  CHECK(hypr_parse_event("custom>>nashi:rm -rf").kind == HYPR_EV_NONE);
  CHECK(hypr_parse_event("custom>>nashi:convert2").kind == HYPR_EV_NONE);
  CHECK(hypr_parse_event("custom>>satsuma:convert").kind == HYPR_EV_NONE);
  CHECK(hypr_parse_event("activewindow>>a,b").kind == HYPR_EV_NONE);
  CHECK(hypr_parse_event("").kind == HYPR_EV_NONE);
  CHECK(hypr_parse_event(NULL).kind == HYPR_EV_NONE);

  /* Version gate. */
  CHECK(hypr_version_supported("v0.56.2"));
  CHECK(hypr_version_supported("0.56.0"));
  CHECK(!hypr_version_supported("v0.57.0"));
  CHECK(!hypr_version_supported("v0.55.9"));
  CHECK(!hypr_version_supported("v0.560.0"));
  CHECK(!hypr_version_supported(""));
  CHECK(!hypr_version_supported(NULL));

  char *tag = hypr_version_tag_from_json("{\"branch\":\"v0.56.2\",\"tag\":\"v0.56.2\"}");
  CHECK(tag && g_str_equal(tag, "v0.56.2"));
  g_free(tag);
  CHECK(hypr_version_tag_from_json("{}") == NULL);
  CHECK(hypr_version_tag_from_json("not json") == NULL);

  /* Lua hook safety (crash trap) for both texts. */
  check_crash_trap(hypr_lua_install());
  check_crash_trap(hypr_lua_remove());
  CHECK(strstr(hypr_lua_install(), "hl.dsp.event(\"nashi:up\")") != NULL);
  CHECK(strstr(hypr_lua_remove(), "hl.bind(") == NULL);

  /* Cursor -> monitor-local point. */
  double x, y;
  CHECK(hypr_cursor_from_json("{\"x\": 1500, \"y\": 40}", &x, &y) && x == 1500 && y == 40);
  CHECK(!hypr_cursor_from_json("{\"x\": \"a\"}", &x, &y));
  const char *mons =
    "[{\"name\":\"A\",\"x\":0,\"y\":0,\"width\":1280,\"height\":800,\"scale\":1,\"transform\":0},"
    " {\"name\":\"B\",\"x\":1280,\"y\":0,\"width\":3840,\"height\":2160,\"scale\":2,\"transform\":0},"
    " {\"name\":\"C\",\"x\":3200,\"y\":0,\"width\":1920,\"height\":1080,\"scale\":1,\"transform\":1}]";
  CHECK(hypr_local_point(mons, 100, 50, &x, &y) && x == 100 && y == 50);
  CHECK(hypr_local_point(mons, 1500, 40, &x, &y) && x == 220 && y == 40);
  CHECK(!hypr_local_point(mons, 2000, 1100, &x, &y));  /* below B (1920x1080 logical) */
  CHECK(hypr_local_point(mons, 3300, 1500, &x, &y) && x == 100 && y == 1500);  /* rotated */
  CHECK(!hypr_local_point("[]", 1, 1, &x, &y));
  CHECK(!hypr_local_point("garbage", 1, 1, &x, &y));

  /* Instance directory from the environment; a '/' in the signature is refused. */
  g_setenv("XDG_RUNTIME_DIR", "/run/user/1", TRUE);
  g_setenv("HYPRLAND_INSTANCE_SIGNATURE", "abc", TRUE);
  char *d = hypr_instance_dir();
  CHECK(d && g_str_equal(d, "/run/user/1/hypr/abc"));
  g_free(d);
  g_setenv("HYPRLAND_INSTANCE_SIGNATURE", "../x", TRUE);
  CHECK(hypr_instance_dir() == NULL);
  g_unsetenv("HYPRLAND_INSTANCE_SIGNATURE");
  CHECK(hypr_instance_dir() == NULL);

  /* IPC against a fake socket: request framing, "ok" vs "error", stale dir. */
  char *tmp = g_dir_make_tmp("nashi-hypr-XXXXXX", NULL);
  Fake f;
  char *r = fake_request(tmp, "{\"tag\":\"v0.56.2\"}", "j/version", &f, FALSE, NULL);
  CHECK(r && strstr(r, "v0.56.2") && g_str_equal(f.request, "j/version"));
  g_free(r);
  gboolean ok = FALSE;
  fake_request(tmp, "ok", "return 1", &f, TRUE, &ok);
  CHECK(ok && g_str_equal(f.request, "/eval return 1"));
  fake_request(tmp, "error: [string]:1: boom", "boom()", &f, TRUE, &ok);
  CHECK(!ok);
  CHECK(hypr_request(tmp, "j/version", NULL) == NULL);  /* nobody listening */
  g_rmdir(tmp);
  g_free(tmp);

  if (failures) return 1;
  puts("hypr: all checks passed");
  return 0;
}
