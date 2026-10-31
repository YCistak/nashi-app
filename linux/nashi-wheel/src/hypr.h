/* nashi-wheel: Hyprland drag trigger helpers.
 *
 * The trigger installs a small Lua hook at runtime (no config file is
 * touched): while the left button is held, pressing Shift / Ctrl / Alt emits
 * custom>>nashi:<mode> on the event socket (socket2), and releasing the button
 * afterwards emits custom>>nashi:up. The trigger reads those events and starts
 * the wheel at the cursor.
 *
 * The parsing and the Lua text are pure functions so they can be unit tested
 * without a compositor. */
#ifndef NASHI_HYPR_H
#define NASHI_HYPR_H

#include <glib.h>

typedef enum {
  HYPR_EV_NONE,     /* not ours */
  HYPR_EV_RELOAD,   /* config reloaded: Lua state and runtime binds are gone */
  HYPR_EV_TRIGGER,  /* modifier pressed while the button is held */
  HYPR_EV_UP,       /* button released after a trigger */
} HyprEventKind;

typedef struct {
  HyprEventKind kind;
  const char *mode; /* static string for HYPR_EV_TRIGGER, else NULL */
} HyprEvent;

/* One socket2 line, without the trailing newline. */
HyprEvent hypr_parse_event(const char *line);

/* The Lua API used by the hook (and the crash workaround below) is only
 * verified on Hyprland 0.56.x. Accepts "v0.56.2" or "0.56.2". */
gboolean hypr_version_supported(const char *tag);

/* Lua that installs / removes the hook. Both are idempotent.
 *
 * CRASH TRAP (Hyprland 0.56.2): methods of the HL.Keybind object returned by
 * hl.bind() (":remove()", ":set_enabled()", field reads) dereference a null
 * pointer and SEGV the compositor once that bind has been removed by any other
 * path (another bind with the same keys removed, hl.unbind, a reload). So the
 * hook never keeps or returns a HL.Keybind: binds are created inside a do-block
 * and removed only with hl.unbind("<string>"), using modifier combinations no
 * user binds (MOD3 + MOD5), so a user's own mouse:272 binds are never matched. */
const char *hypr_lua_install(void);
const char *hypr_lua_remove(void);

/* "tag" field of `j/version`. NULL if missing. */
char *hypr_version_tag_from_json(const char *json);

/* `j/cursorpos` -> global logical coordinates. */
gboolean hypr_cursor_from_json(const char *json, double *x, double *y);

/* Converts a global logical point to coordinates relative to the monitor that
 * contains it, using `j/monitors`. Scale and 90/270 degree transforms are
 * honoured. FALSE if no monitor contains the point. */
gboolean hypr_local_point(const char *monitors_json, double gx, double gy,
                          double *lx, double *ly);

/* $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE, or NULL when this is not
 * a Hyprland session. */
char *hypr_instance_dir(void);

/* Sends one request to .socket.sock ("j/version", "/eval ...") and returns the
 * whole reply. NULL on a connection error (stale instance directory). */
char *hypr_request(const char *instance_dir, const char *request, GError **error);

/* Runs Lua through "/eval". TRUE only when Hyprland answers "ok". */
gboolean hypr_eval(const char *instance_dir, const char *lua, GError **error);

/* Main loop of `nashi-wheel --hypr-trigger`. self_exe is used to start the
 * wheel. Returns the process exit code. */
int hypr_trigger_run(const char *self_exe);

#endif
