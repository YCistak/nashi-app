/* nashi-wheel: Linux drag wheel overlay (step A2: skeleton with --demo mode). */
#include "surface.h"
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
  int exit_code;
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

static void on_size_allocate(GtkWidget *w, GdkRectangle *r, gpointer data) {
  (void)w;
  App *app = data;
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
  g_printerr("Usage: %s --demo [convert|tools|actions] [--size PX] [--no-keyboard]\n"
             "       %s --render-png FILE [--mode M] [--highlight N|none] [--size PX] [--theme light|dark]\n"
             "  --demo MODE     show a demo wheel with fixed slices (default: convert)\n"
             "  --size PX       wheel diameter in px (default %.0f)\n"
             "  --no-keyboard   do not take keyboard focus (as while dragging)\n"
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
    } else if (g_str_equal(argv[i], "--no-keyboard")) {
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

  App app = { .view = { .hot = WHEEL_HIT_CENTER } };
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
