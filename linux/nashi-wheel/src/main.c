/* nashi-wheel: Linux drag wheel overlay (A2 skeleton + A3 drop target). */
#include "surface.h"
#include "uris.h"
#include "wheel.h"

#include <glib-unix.h>
#include <stdio.h>

#define DEFAULT_SIZE 540.0

typedef struct {
  WheelModel model;
  WheelView view;
  GtkWidget *area;
  GtkWindow *win;
  gboolean placed;  /* wheel centre already moved to the pointer */
  gboolean at_given; /* centre came from --at, not from a pointer event */
  double at_x, at_y; /* requested --at point, kept to re-clamp on every resize */
  int exit_code;
  GdkAtom uri_atom;  /* text/uri-list */
  int drop_hit;      /* hit-test result at the drop position */
  guint leave_timer; /* pending "drag ended without a drop" check */
} App;

static void app_set_hot(App *app, int hot) {
  if (app->view.hot == hot)
    return;
  app->view.hot = hot;
  gtk_widget_queue_draw(app->area);  /* redraw only on change: idle CPU stays 0 */
}

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
  (void)w;
  App *app = data;
  wheel_draw(cr, &app->model, &app->view);
  return FALSE;
}

/* Keeps the whole wheel on screen near an edge (like the macOS panel); on a
 * surface too small for the wheel the centre of the surface is used. */
static double clamp_centre(double v, int extent, double size) {
  double margin = wheel_outer_radius(size) + 12.0;
  if (extent < 2 * margin)
    return extent / 2.0;
  return CLAMP(v, margin, extent - margin);
}

static void on_size_allocate(GtkWidget *w, GdkRectangle *r, gpointer data) {
  (void)w;
  App *app = data;
  if (app->placed && app->at_given) {
    /* --at is clamped to the surface the compositor actually gave us. The
     * first allocation can be a provisional size before the layer surface is
     * configured, so always clamp the original request, not the last result.
     * Selection is by angle, so a shifted centre still works for the drag. */
    app->view.cx = clamp_centre(app->at_x, r->width, app->model.size);
    app->view.cy = clamp_centre(app->at_y, r->height, app->model.size);
  }
  if (!app->placed) {  /* until the pointer is known, centre on the surface */
    app->view.cx = r->width / 2.0;
    app->view.cy = r->height / 2.0;
  }
}

static void on_pointer(App *app, double x, double y) {
  if (!app->placed) {
    /* The wheel opens where the cursor is. */
    app->placed = TRUE;
    app->view.cx = x;
    app->view.cy = y;
    app->view.hot = WHEEL_HIT_CENTER;
    gtk_widget_queue_draw(app->area);
    return;
  }
  app_set_hot(app, wheel_hit_test(&app->model, app->view.cx, app->view.cy, x, y));
}

static gboolean on_motion(GtkWidget *w, GdkEventMotion *e, gpointer data) {
  (void)w;
  on_pointer(data, e->x, e->y);
  return TRUE;
}

static gboolean on_enter(GtkWidget *w, GdkEventCrossing *e, gpointer data) {
  (void)w;
  on_pointer(data, e->x, e->y);
  return TRUE;
}

static gboolean on_button(GtkWidget *w, GdkEventButton *e, gpointer data) {
  (void)w;
  App *app = data;
  int hit = wheel_hit_test(&app->model, app->view.cx, app->view.cy, e->x, e->y);
  if (hit >= 0 && hit < app->model.n_slices) {
    /* Same shape the real helper will report on a drop (A3). */
    printf("{\"t\":\"pick\",\"mode\":\"%s\",\"slice\":\"%s\"}\n",
           app->model.mode, app->model.slices[hit].id);
    fflush(stdout);
  } else {
    printf("{\"t\":\"cancel\"}\n");
    fflush(stdout);
  }
  gtk_main_quit();
  return TRUE;
}

/* ---- drag and drop (A3) ---------------------------------------------------
 * The surface is a text/uri-list drop target. The first drag-motion places the
 * wheel under the pointer (a compositor may not send a motion after the enter
 * for a still pointer; until then the wheel stays centred on the surface).
 * Later motions only update the highlight. A drop reports the slice under the
 * pointer, or "cancel" over the centre / outside the wheel, and exits. */
static void print_line(char *json) {
  printf("%s\n", json);
  fflush(stdout);
  g_free(json);
}

static gboolean on_drag_motion(GtkWidget *w, GdkDragContext *ctx, gint x, gint y,
                               guint time, gpointer data) {
  App *app = data;
  if (gtk_drag_dest_find_target(w, ctx, NULL) == GDK_NONE) {
    gdk_drag_status(ctx, 0, time);  /* not a file list: refuse */
    return FALSE;
  }
  g_debug("drag-motion %d,%d placed=%d", x, y, app->placed);
  if (app->leave_timer) {
    g_source_remove(app->leave_timer);
    app->leave_timer = 0;
  }
  on_pointer(app, x, y);
  /* Accept everywhere on the surface so that a drop over the centre still
   * arrives and is reported as "cancel" (a refused drop would make the source
   * play its "snap back" animation instead). */
  GdkDragAction act = gdk_drag_context_get_suggested_action(ctx);
  gdk_drag_status(ctx, act ? act : GDK_ACTION_COPY, time);
  return TRUE;
}

static gboolean on_leave_timeout(gpointer data) {
  App *app = data;
  app->leave_timer = 0;
  /* No drop followed the leave: the source cancelled (Esc) or died, or the
   * pointer went to another output. Nothing to wait for any more. */
  print_line(uris_cancel_json());
  gtk_main_quit();
  return G_SOURCE_REMOVE;
}

static void on_drag_leave(GtkWidget *w, GdkDragContext *ctx, guint time, gpointer data) {
  (void)w; (void)ctx; (void)time;
  App *app = data;
  app_set_hot(app, WHEEL_HIT_CENTER);
  /* GTK also emits drag-leave right before drag-drop, so a leave alone does
   * not end the session: give a drop 200 ms to follow. */
  if (!app->leave_timer)
    app->leave_timer = g_timeout_add(200, on_leave_timeout, app);
}

static gboolean on_drag_drop(GtkWidget *w, GdkDragContext *ctx, gint x, gint y,
                             guint time, gpointer data) {
  App *app = data;
  g_debug("drag-drop %d,%d", x, y);
  if (app->leave_timer) {
    g_source_remove(app->leave_timer);
    app->leave_timer = 0;
  }
  if (gtk_drag_dest_find_target(w, ctx, NULL) == GDK_NONE)
    return FALSE;
  /* The drop position decides the slice; the URIs arrive asynchronously. */
  app->drop_hit = app->placed
      ? wheel_hit_test(&app->model, app->view.cx, app->view.cy, x, y)
      : WHEEL_HIT_CENTER;
  gtk_drag_get_data(w, ctx, app->uri_atom, time);
  return TRUE;
}

static gboolean quit_idle(gpointer data) {
  (void)data;
  gtk_main_quit();
  return G_SOURCE_REMOVE;
}

static void on_drag_data(GtkWidget *w, GdkDragContext *ctx, gint x, gint y,
                         GtkSelectionData *sel, guint info, guint time, gpointer data) {
  (void)w; (void)x; (void)y; (void)info;
  App *app = data;
  gint len = gtk_selection_data_get_length(sel);
  if (len < 0) {  /* the source could not deliver the data */
    gtk_drag_finish(ctx, FALSE, FALSE, time);
    print_line(uris_cancel_json());
    g_idle_add(quit_idle, NULL);
    return;
  }
  char **uris = uris_parse_list((const char *)gtk_selection_data_get_data(sel), (gsize)len);
  int hit = app->drop_hit;
  gboolean on_slice = hit >= 0 && hit < app->model.n_slices && uris[0];
  if (on_slice)
    print_line(uris_drop_json(app->model.mode, app->model.slices[hit].id, uris));
  else
    print_line(uris_cancel_json());
  g_strfreev(uris);
  gtk_drag_finish(ctx, on_slice, FALSE, time);
  gdk_display_flush(gdk_display_get_default());
  g_idle_add(quit_idle, NULL);  /* let the finish reach the source first */
}

static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer data) {
  (void)w; (void)data;
  if (e->keyval == GDK_KEY_Escape) {
    printf("{\"t\":\"cancel\"}\n");
    fflush(stdout);
    gtk_main_quit();
    return TRUE;
  }
  return FALSE;
}

static gboolean on_signal(gpointer data) {
  (void)data;
  gtk_main_quit();
  return G_SOURCE_REMOVE;
}

static gboolean on_delete(GtkWidget *w, GdkEvent *e, gpointer data) {
  (void)w; (void)e; (void)data;
  gtk_main_quit();
  return TRUE;
}

static void usage(const char *argv0) {
  g_printerr("Usage: %s --demo [convert|tools|actions] [--size PX] [--no-keyboard|--drag] [--at X,Y]\n"
             "       %s --render-png FILE [--mode M] [--highlight N|none] [--size PX] [--theme light|dark]\n"
             "  --demo MODE     show a demo wheel with fixed slices (default: convert)\n"
             "  --size PX       wheel diameter in px (default %.0f)\n"
             "  --no-keyboard   do not take keyboard focus (as while dragging)\n"
             "  --drag          drag-session mode: same as --no-keyboard (the drag source owns\n"
             "                  the keyboard). A text/uri-list drop prints {\"t\":\"drop\",...} or\n"
             "                  {\"t\":\"cancel\"} and exits.\n"
             "  --at X,Y        open the wheel centred at X,Y (surface coordinates, i.e. relative\n"
             "                  to the output the overlay is on) instead of at the first pointer\n"
             "                  event. GTK3 on Wayland reports the pointer only on the first\n"
             "                  motion, so a launcher that knows the cursor should pass it.\n"
             "Env: NASHI_WHEEL_NO_LAYER=1 forces the plain-window fallback.\n",
             argv0, argv0, DEFAULT_SIZE);
}

/* Headless render: draws with the cairo core into an image surface and writes a
 * PNG. No GTK display, no Wayland surface, so it never touches the session. */
static int render_png(const char *path, const char *mode, const char *hl,
                      double size, const char *theme) {
  WheelModel model;
  if (!wheel_demo_model(mode, size, &model)) {
    g_printerr("nashi-wheel: unknown mode '%s'\n", mode);
    return 2;
  }
  if (theme)
    g_setenv("NASHI_WHEEL_THEME", theme, TRUE);
  int side = (int)(size * 1.25);
  cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, side, side);
  cairo_t *cr = cairo_create(surf);
  int hot = WHEEL_HIT_CENTER;
  if (hl && !g_str_equal(hl, "none"))
    hot = (int)g_ascii_strtoll(hl, NULL, 10);
  if (hot >= model.n_slices)
    hot = WHEEL_HIT_CENTER;
  WheelView view = { .cx = side / 2.0, .cy = side / 2.0, .hot = hot };
  wheel_draw(cr, &model, &view);
  cairo_destroy(cr);
  cairo_status_t st = cairo_surface_write_to_png(surf, path);
  cairo_surface_destroy(surf);
  if (st != CAIRO_STATUS_SUCCESS) {
    g_printerr("nashi-wheel: cannot write %s: %s\n", path, cairo_status_to_string(st));
    return 1;
  }
  return 0;
}

int main(int argc, char **argv) {
  const char *demo_mode = NULL;
  const char *png = NULL, *render_mode = "convert", *highlight = "none", *theme = NULL;
  double size = DEFAULT_SIZE;
  gboolean keyboard = TRUE;
  const char *at = NULL;

  /* Parse our own flags before GTK sees them. */
  for (int i = 1; i < argc; i++) {
    if (g_str_equal(argv[i], "--demo")) {
      demo_mode = (i + 1 < argc && argv[i + 1][0] != '-') ? argv[++i] : "convert";
    } else if (g_str_equal(argv[i], "--size") && i + 1 < argc) {
      size = g_ascii_strtod(argv[++i], NULL);
    } else if (g_str_equal(argv[i], "--render-png") && i + 1 < argc) {
      png = argv[++i];
    } else if (g_str_equal(argv[i], "--mode") && i + 1 < argc) {
      render_mode = argv[++i];
    } else if (g_str_equal(argv[i], "--highlight") && i + 1 < argc) {
      highlight = argv[++i];
    } else if (g_str_equal(argv[i], "--theme") && i + 1 < argc) {
      theme = argv[++i];
    } else if (g_str_equal(argv[i], "--at") && i + 1 < argc) {
      at = argv[++i];
    } else if (g_str_equal(argv[i], "--no-keyboard") || g_str_equal(argv[i], "--drag")) {
      keyboard = FALSE;
    } else {
      usage(argv[0]);
      return 2;
    }
  }
  if (png && size >= 200 && size <= 1600)
    return render_png(png, render_mode, highlight, size, theme);
  if (!demo_mode || size < 200 || size > 1600) {
    usage(argv[0]);
    return 2;
  }

  App app = { .view = { .hot = WHEEL_HIT_CENTER }, .drop_hit = WHEEL_HIT_CENTER };
  if (at) {
    if (!wheel_parse_point(at, &app.at_x, &app.at_y)) {
      g_printerr("nashi-wheel: --at expects X,Y (non-negative numbers), got '%s'\n", at);
      return 2;
    }
    app.view.cx = app.at_x;
    app.view.cy = app.at_y;
    app.placed = app.at_given = TRUE;
  }
  if (!wheel_demo_model(demo_mode, size, &app.model)) {
    g_printerr("nashi-wheel: unknown mode '%s'\n", demo_mode);
    return 2;
  }
  int gtk_argc = 1;
  char *gtk_argv[] = { argv[0], NULL };
  char **gtk_argv_p = gtk_argv;
  if (!gtk_init_check(&gtk_argc, &gtk_argv_p)) {
    g_printerr("nashi-wheel: cannot open display\n");
    return 1;
  }

  SurfaceOptions opts = { .want_keyboard = keyboard, .fallback_size = size * 1.6 };
  gboolean is_layer = FALSE;
  app.win = surface_new(&opts, &is_layer);
  app.area = gtk_drawing_area_new();
  gtk_widget_add_events(app.area, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK |
                                  GDK_ENTER_NOTIFY_MASK);
  gtk_widget_add_events(GTK_WIDGET(app.win), GDK_KEY_PRESS_MASK);
  gtk_container_add(GTK_CONTAINER(app.win), app.area);

  app.uri_atom = gdk_atom_intern_static_string("text/uri-list");
  GtkTargetEntry targets[] = { { (gchar *)"text/uri-list", 0, 0 } };
  gtk_drag_dest_set(app.area, 0, targets, G_N_ELEMENTS(targets),
                    GDK_ACTION_COPY | GDK_ACTION_MOVE | GDK_ACTION_LINK);
  g_signal_connect(app.area, "drag-motion", G_CALLBACK(on_drag_motion), &app);
  g_signal_connect(app.area, "drag-leave", G_CALLBACK(on_drag_leave), &app);
  g_signal_connect(app.area, "drag-drop", G_CALLBACK(on_drag_drop), &app);
  g_signal_connect(app.area, "drag-data-received", G_CALLBACK(on_drag_data), &app);

  g_signal_connect(app.area, "draw", G_CALLBACK(on_draw), &app);
  g_signal_connect(app.area, "size-allocate", G_CALLBACK(on_size_allocate), &app);
  g_signal_connect(app.area, "motion-notify-event", G_CALLBACK(on_motion), &app);
  g_signal_connect(app.area, "enter-notify-event", G_CALLBACK(on_enter), &app);
  g_signal_connect(app.area, "button-press-event", G_CALLBACK(on_button), &app);
  g_signal_connect(app.win, "key-press-event", G_CALLBACK(on_key), &app);
  g_signal_connect(app.win, "delete-event", G_CALLBACK(on_delete), NULL);
  g_unix_signal_add(SIGINT, on_signal, NULL);
  g_unix_signal_add(SIGTERM, on_signal, NULL);

  g_message("surface: %s", is_layer ? "layer-shell overlay" : "plain window (fallback)");
  gtk_widget_show_all(GTK_WIDGET(app.win));
  gtk_main();
  gtk_widget_destroy(GTK_WIDGET(app.win));
  return app.exit_code;
}
