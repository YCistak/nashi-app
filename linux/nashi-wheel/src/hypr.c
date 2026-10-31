/* nashi-wheel: Hyprland IPC, socket2 parsing and the Lua hook text. */
#include "hypr.h"

#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <json-glib/json-glib.h>
#include <string.h>

/* Unique bind strings (see the crash trap note in hypr.h). With ignore_mods
 * the modifiers in the string do not affect matching, they only make the bind
 * identity unique so hl.unbind() can never hit a user's bind. */
#define BIND_DOWN "MOD3 + MOD5 + mouse:272"
#define BIND_UP   "CAPS + MOD3 + MOD5 + mouse:272"

/* Key codes are xkb (evdev + 8): Shift 50/62, Ctrl 37/105, Alt 64/108,
 * Super 133/134. With Super held the gesture is a window move/resize, not a
 * file drag, so nothing is emitted. The event subscription object (unlike a
 * HL.Keybind) is safe to remove; it is still wrapped in pcall. */
static const char LUA_INSTALL[] =
  "do\n"
  "  if _nashi_wheel_sub then pcall(function() _nashi_wheel_sub:remove() end); _nashi_wheel_sub = nil end\n"
  "  hl.unbind(\"" BIND_DOWN "\"); hl.unbind(\"" BIND_UP "\")\n"
  "  local st = { held = false, open = false }\n"
  "  hl.bind(\"" BIND_DOWN "\", function() st.held = true end,"
  " { non_consuming = true, ignore_mods = true, transparent = true })\n"
  "  hl.bind(\"" BIND_UP "\", function()\n"
  "    st.held = false\n"
  "    if st.open then st.open = false; hl.dispatch(hl.dsp.event(\"nashi:up\")) end\n"
  "  end, { release = true, non_consuming = true, ignore_mods = true, transparent = true })\n"
  "  local MODE = { [50] = \"convert\", [62] = \"convert\", [37] = \"tools\", [105] = \"tools\","
  " [64] = \"actions\", [108] = \"actions\" }\n"
  "  _nashi_wheel_sub = hl.on(\"input.keyboard.key\", function(code, _, state)\n"
  "    if state ~= 1 or not st.held then return end\n"
  "    local m = MODE[code]\n"
  "    if not m or hl.is_key_down(133) or hl.is_key_down(134) then return end\n"
  "    st.open = true\n"
  "    hl.dispatch(hl.dsp.event(\"nashi:\" .. m))\n"
  "  end)\n"
  "end";

static const char LUA_REMOVE[] =
  "do\n"
  "  if _nashi_wheel_sub then pcall(function() _nashi_wheel_sub:remove() end); _nashi_wheel_sub = nil end\n"
  "  hl.unbind(\"" BIND_DOWN "\"); hl.unbind(\"" BIND_UP "\")\n"
  "end";

const char *hypr_lua_install(void) { return LUA_INSTALL; }
const char *hypr_lua_remove(void) { return LUA_REMOVE; }

HyprEvent hypr_parse_event(const char *line) {
  HyprEvent ev = { HYPR_EV_NONE, NULL };
  if (!line)
    return ev;
  if (g_str_has_prefix(line, "configreloaded>>")) {
    ev.kind = HYPR_EV_RELOAD;
    return ev;
  }
  static const char prefix[] = "custom>>nashi:";
  if (!g_str_has_prefix(line, prefix))
    return ev;
  const char *name = line + sizeof prefix - 1;
  static const char *const modes[] = { "convert", "tools", "actions" };
  for (guint i = 0; i < G_N_ELEMENTS(modes); i++) {
    if (g_str_equal(name, modes[i])) {
      ev.kind = HYPR_EV_TRIGGER;
      ev.mode = modes[i];
      return ev;
    }
  }
  if (g_str_equal(name, "up"))
    ev.kind = HYPR_EV_UP;
  return ev;
}

gboolean hypr_version_supported(const char *tag) {
  if (!tag)
    return FALSE;
  if (tag[0] == 'v')
    tag++;
  return g_str_has_prefix(tag, "0.56.");
}

static JsonNode *parse_json(const char *json) {
  if (!json)
    return NULL;
  JsonParser *p = json_parser_new();
  JsonNode *root = NULL;
  if (json_parser_load_from_data(p, json, -1, NULL) && json_parser_get_root(p))
    root = json_node_copy(json_parser_get_root(p));
  g_object_unref(p);
  return root;
}

static gboolean get_num(JsonObject *o, const char *k, double *out) {
  JsonNode *n = json_object_get_member(o, k);
  if (!n || JSON_NODE_TYPE(n) != JSON_NODE_VALUE)
    return FALSE;
  GType t = json_node_get_value_type(n);
  if (t != G_TYPE_INT64 && t != G_TYPE_DOUBLE)
    return FALSE;
  *out = json_node_get_double(n);
  return TRUE;
}

char *hypr_version_tag_from_json(const char *json) {
  JsonNode *root = parse_json(json);
  char *tag = NULL;
  if (root && JSON_NODE_HOLDS_OBJECT(root)) {
    JsonObject *o = json_node_get_object(root);
    if (json_object_has_member(o, "tag"))
      tag = g_strdup(json_object_get_string_member_with_default(o, "tag", NULL));
  }
  if (root)
    json_node_unref(root);
  return tag;
}

gboolean hypr_cursor_from_json(const char *json, double *x, double *y) {
  JsonNode *root = parse_json(json);
  gboolean ok = FALSE;
  if (root && JSON_NODE_HOLDS_OBJECT(root)) {
    JsonObject *o = json_node_get_object(root);
    double px, py;
    if (get_num(o, "x", &px) && get_num(o, "y", &py)) {
      *x = px;
      *y = py;
      ok = TRUE;
    }
  }
  if (root)
    json_node_unref(root);
  return ok;
}

gboolean hypr_local_point(const char *monitors_json, double gx, double gy,
                          double *lx, double *ly) {
  JsonNode *root = parse_json(monitors_json);
  gboolean ok = FALSE;
  if (root && JSON_NODE_HOLDS_ARRAY(root)) {
    JsonArray *a = json_node_get_array(root);
    for (guint i = 0; i < json_array_get_length(a) && !ok; i++) {
      JsonNode *n = json_array_get_element(a, i);
      if (!JSON_NODE_HOLDS_OBJECT(n))
        continue;
      JsonObject *m = json_node_get_object(n);
      double x, y, w, h, scale = 1, transform = 0;
      if (!get_num(m, "x", &x) || !get_num(m, "y", &y) ||
          !get_num(m, "width", &w) || !get_num(m, "height", &h))
        continue;
      get_num(m, "scale", &scale);
      get_num(m, "transform", &transform);
      if (scale < 0.1)
        scale = 1;
      w /= scale;  /* width/height are in pixels, positions are logical */
      h /= scale;
      int t = (int)transform;
      if (t == 1 || t == 3 || t == 5 || t == 7) {
        double s = w;
        w = h;
        h = s;
      }
      if (gx >= x && gx < x + w && gy >= y && gy < y + h) {
        *lx = gx - x;
        *ly = gy - y;
        ok = TRUE;
      }
    }
  }
  if (root)
    json_node_unref(root);
  return ok;
}

char *hypr_instance_dir(void) {
  const char *sig = g_getenv("HYPRLAND_INSTANCE_SIGNATURE");
  const char *rt = g_getenv("XDG_RUNTIME_DIR");
  if (!sig || !*sig || !rt || !*rt || strchr(sig, '/'))
    return NULL;
  return g_build_filename(rt, "hypr", sig, NULL);
}

static gboolean send_all(GSocket *sock, const char *data, GError **error) {
  gsize len = strlen(data), done = 0;
  while (done < len) {
    gssize n = g_socket_send(sock, data + done, len - done, NULL, error);
    if (n <= 0)
      return FALSE;
    done += (gsize)n;
  }
  return TRUE;
}

char *hypr_request(const char *instance_dir, const char *request, GError **error) {
  char *path = g_build_filename(instance_dir, ".socket.sock", NULL);
  GSocketAddress *addr = g_unix_socket_address_new(path);
  g_free(path);
  GSocket *sock = g_socket_new(G_SOCKET_FAMILY_UNIX, G_SOCKET_TYPE_STREAM, 0, error);
  char *reply = NULL;
  if (sock) {
    g_socket_set_timeout(sock, 5);  /* never hang on a wedged compositor */
    if (g_socket_connect(sock, addr, NULL, error) && send_all(sock, request, error)) {
      GString *buf = g_string_new(NULL);
      char chunk[4096];
      gssize n;
      while ((n = g_socket_receive(sock, chunk, sizeof chunk, NULL, error)) > 0)
        g_string_append_len(buf, chunk, n);
      if (n == 0)
        reply = g_string_free(buf, FALSE);
      else
        g_string_free(buf, TRUE);
    }
    g_object_unref(sock);
  }
  g_object_unref(addr);
  return reply;
}

gboolean hypr_eval(const char *instance_dir, const char *lua, GError **error) {
  char *req = g_strconcat("/eval ", lua, NULL);
  char *reply = hypr_request(instance_dir, req, error);
  g_free(req);
  gboolean ok = reply && g_str_equal(g_strstrip(reply), "ok");
  if (reply && !ok)
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", reply);
  g_free(reply);
  return ok;
}
