/* nashi-wheel --hypr-trigger: listens on Hyprland's event socket and starts the
 * wheel at the cursor when a modifier is pressed during a drag.
 *
 * Lifecycle:
 *   - no Hyprland session          -> log, exit 3, nothing sent anywhere
 *   - Hyprland other than 0.56.x   -> log, exit 3, no Lua sent
 *   - otherwise install the Lua hook, follow socket2:
 *       nashi:<mode>     start `nashi-wheel --demo <mode> --drag --at X,Y`
 *                        (an older wheel of another mode is replaced)
 *       nashi:up         SIGUSR1 to the wheel (it leaves if no drag arrived)
 *       configreloaded   the reload dropped the hook: install it again
 *   - SIGINT / SIGTERM / socket2 closed -> stop the wheel, remove the hook.
 *
 * Env (tests): NASHI_WHEEL_NOHOOK=1 never sends Lua (events can be injected
 * with `hyprctl dispatch event nashi:convert`); NASHI_WHEEL_HYPR_TAG=v0.57.0
 * pretends another Hyprland version to exercise the version gate. */
#include "hypr.h"

#include <glib-unix.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>

#define EXIT_DISABLED 3

typedef struct {
  char *dir;
  char *self_exe;
  gboolean hook;       /* Lua hook is (believed to be) installed */
  gboolean nohook;
  GPid child;          /* running wheel, 0 if none */
  const char *child_mode;
  GMainLoop *loop;
  GDataInputStream *events;
  GCancellable *cancel;
} Trigger;

static void install_hook(Trigger *t) {
  if (t->nohook)
    return;
  GError *err = NULL;
  if (hypr_eval(t->dir, hypr_lua_install(), &err)) {
    t->hook = TRUE;
    g_message("drag trigger hook installed");
  } else {
    t->hook = FALSE;
    g_warning("cannot install the drag trigger hook: %s", err ? err->message : "?");
    g_clear_error(&err);
  }
}

static void remove_hook(Trigger *t) {
  if (!t->hook)
    return;  /* never touch Hyprland if we did not install anything */
  GError *err = NULL;
  if (!hypr_eval(t->dir, hypr_lua_remove(), &err)) {
    g_warning("cannot remove the drag trigger hook: %s", err ? err->message : "?");
    g_clear_error(&err);
  }
  t->hook = FALSE;
}

static void on_child_exit(GPid pid, gint status, gpointer data) {
  Trigger *t = data;
  (void)status;
  if (t->child == pid) {
    t->child = 0;
    t->child_mode = NULL;
  }
  g_spawn_close_pid(pid);
}

static void stop_child(Trigger *t) {
  if (t->child) {
    kill(t->child, SIGTERM);
    t->child = 0;  /* reaped by its child watch */
    t->child_mode = NULL;
  }
}

/* Cursor in coordinates of its monitor; FALSE lets the wheel place itself on
 * the first pointer event instead. */
static gboolean cursor_local(Trigger *t, double *x, double *y) {
  char *cur = hypr_request(t->dir, "j/cursorpos", NULL);
  char *mons = hypr_request(t->dir, "j/monitors", NULL);
  double gx, gy;
  gboolean ok = cur && mons && hypr_cursor_from_json(cur, &gx, &gy) &&
                hypr_local_point(mons, gx, gy, x, y);
  g_free(cur);
  g_free(mons);
  return ok;
}

static void start_wheel(Trigger *t, const char *mode) {
  if (t->child && t->child_mode == mode)
    return;  /* same wheel already up (repeated key press) */
  stop_child(t);  /* another modifier: replace the wheel */

  char at[64] = "";
  double x, y;
  if (cursor_local(t, &x, &y))
    g_snprintf(at, sizeof at, "%.0f,%.0f", x, y);
  const char *argv[] = { t->self_exe, "--demo", mode, "--drag",
                         at[0] ? "--at" : NULL, at, NULL };
  GError *err = NULL;
  GPid pid;
  if (!g_spawn_async(NULL, (char **)argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
                     NULL, NULL, &pid, &err)) {
    g_warning("cannot start the wheel: %s", err->message);
    g_error_free(err);
    return;
  }
  t->child = pid;
  t->child_mode = mode;
  g_child_watch_add(pid, on_child_exit, t);
  g_message("wheel %s started (pid %d, at %s)", mode, (int)pid, at[0] ? at : "pointer");
}

static void read_next(Trigger *t);

static void on_line(GObject *src, GAsyncResult *res, gpointer data) {
  Trigger *t = data;
  GError *err = NULL;
  char *line = g_data_input_stream_read_line_finish(G_DATA_INPUT_STREAM(src), res, NULL, &err);
  if (!line) {
    /* EOF: the compositor went away (or we are shutting down). */
    if (err && !g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning("event socket: %s", err->message);
    g_clear_error(&err);
    g_main_loop_quit(t->loop);
    return;
  }
  HyprEvent ev = hypr_parse_event(line);
  switch (ev.kind) {
  case HYPR_EV_RELOAD:
    t->hook = FALSE;  /* the reload already dropped the Lua state and binds */
    install_hook(t);
    break;
  case HYPR_EV_TRIGGER:
    start_wheel(t, ev.mode);
    break;
  case HYPR_EV_UP:
    if (t->child)
      kill(t->child, SIGUSR1);
    break;
  case HYPR_EV_NONE:
    break;
  }
  g_free(line);
  read_next(t);
}

static void read_next(Trigger *t) {
  g_data_input_stream_read_line_async(t->events, G_PRIORITY_DEFAULT, t->cancel, on_line, t);
}

static gboolean on_quit_signal(gpointer data) {
  Trigger *t = data;
  g_main_loop_quit(t->loop);
  return G_SOURCE_CONTINUE;
}

static GSocketConnection *connect_events(const char *dir) {
  char *path = g_build_filename(dir, ".socket2.sock", NULL);
  GSocketAddress *addr = g_unix_socket_address_new(path);
  g_free(path);
  GSocketClient *client = g_socket_client_new();
  GError *err = NULL;
  GSocketConnection *conn = g_socket_client_connect(client, G_SOCKET_CONNECTABLE(addr), NULL, &err);
  if (!conn) {
    g_warning("cannot connect to the event socket: %s", err->message);
    g_error_free(err);
  }
  g_object_unref(client);
  g_object_unref(addr);
  return conn;
}

int hypr_trigger_run(const char *self_exe) {
  Trigger t = { .self_exe = g_strdup(self_exe) };
  t.dir = hypr_instance_dir();
  if (!t.dir) {
    g_message("not a Hyprland session (HYPRLAND_INSTANCE_SIGNATURE unset): drag trigger disabled");
    g_free(t.self_exe);
    return EXIT_DISABLED;
  }

  /* A crashed Hyprland leaves its instance directory behind; a socket that
   * does not answer means a stale session, not something to hook into. */
  GError *err = NULL;
  char *version = hypr_request(t.dir, "j/version", &err);
  if (!version) {
    g_message("Hyprland socket in %s does not answer (%s): drag trigger disabled",
              t.dir, err ? err->message : "?");
    g_clear_error(&err);
    g_free(t.dir);
    g_free(t.self_exe);
    return EXIT_DISABLED;
  }
  char *tag = g_strdup(g_getenv("NASHI_WHEEL_HYPR_TAG"));
  if (!tag)
    tag = hypr_version_tag_from_json(version);
  g_free(version);
  if (!hypr_version_supported(tag)) {
    g_message("Hyprland %s is not verified (0.56.x is): drag trigger disabled, no Lua sent",
              tag ? tag : "(unknown)");
    g_free(tag);
    g_free(t.dir);
    g_free(t.self_exe);
    return EXIT_DISABLED;
  }
  g_message("Hyprland %s", tag);
  g_free(tag);

  /* Subscribe before installing, so no event of a fresh hook is missed. */
  GSocketConnection *conn = connect_events(t.dir);
  if (!conn) {
    g_free(t.dir);
    g_free(t.self_exe);
    return 1;
  }
  t.nohook = g_getenv("NASHI_WHEEL_NOHOOK") != NULL;
  t.loop = g_main_loop_new(NULL, FALSE);
  t.cancel = g_cancellable_new();
  t.events = g_data_input_stream_new(g_io_stream_get_input_stream(G_IO_STREAM(conn)));
  guint s1 = g_unix_signal_add(SIGINT, on_quit_signal, &t);
  guint s2 = g_unix_signal_add(SIGTERM, on_quit_signal, &t);
  guint s3 = g_unix_signal_add(SIGHUP, on_quit_signal, &t);

  install_hook(&t);
  if (t.nohook)
    g_message("NASHI_WHEEL_NOHOOK set: listening only, no Lua sent");
  int rc = (t.hook || t.nohook) ? 0 : 1;
  if (rc == 0) {
    read_next(&t);
    g_main_loop_run(t.loop);
  }

  /* Cleanup: wheel first, then the hook (only if we installed it). */
  stop_child(&t);
  remove_hook(&t);
  g_cancellable_cancel(t.cancel);
  g_source_remove(s1);
  g_source_remove(s2);
  g_source_remove(s3);
  g_object_unref(t.events);
  g_object_unref(conn);
  g_object_unref(t.cancel);
  g_main_loop_unref(t.loop);
  g_free(t.dir);
  g_free(t.self_exe);
  g_message("drag trigger stopped");
  return rc;
}
