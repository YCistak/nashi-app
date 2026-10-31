#include "surface.h"

#include <gtk-layer-shell/gtk-layer-shell.h>

GtkWindow *surface_new(const SurfaceOptions *opts, gboolean *is_layer) {
  GtkWindow *win = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
  gtk_window_set_title(win, "Nashi wheel");
  gtk_window_set_decorated(win, FALSE);
  gtk_widget_set_app_paintable(GTK_WIDGET(win), TRUE);

  /* An RGBA visual is required for a transparent surface. */
  GdkScreen *screen = gtk_widget_get_screen(GTK_WIDGET(win));
  GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
  if (visual)
    gtk_widget_set_visual(GTK_WIDGET(win), visual);

  *is_layer = !g_getenv("NASHI_WHEEL_NO_LAYER") && gtk_layer_is_supported();
  if (*is_layer) {
    gtk_layer_init_for_window(win);
    gtk_layer_set_layer(win, GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_namespace(win, "nashi-wheel");
    /* Ignore areas reserved by bars and panels. */
    gtk_layer_set_exclusive_zone(win, -1);
    /* No fixed size: anchored to all four edges, the compositor sizes the
     * surface to the output it picks (the focused one). */
    static const GtkLayerShellEdge edges[] = {
      GTK_LAYER_SHELL_EDGE_TOP, GTK_LAYER_SHELL_EDGE_BOTTOM,
      GTK_LAYER_SHELL_EDGE_LEFT, GTK_LAYER_SHELL_EDGE_RIGHT };
    for (guint i = 0; i < G_N_ELEMENTS(edges); i++) {
      gtk_layer_set_anchor(win, edges[i], TRUE);
      gtk_layer_set_margin(win, edges[i], 0);
    }
    /* While a drag is running the drag source owns the keyboard, so the
     * overlay must not take focus then. */
    gtk_layer_set_keyboard_mode(win, opts->want_keyboard
        ? GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE
        : GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
  } else {
    gtk_window_set_default_size(win, (int)opts->fallback_size, (int)opts->fallback_size);
    gtk_window_set_position(win, GTK_WIN_POS_CENTER);
    gtk_window_set_keep_above(win, TRUE);
    gtk_window_set_skip_taskbar_hint(win, TRUE);
  }
  return win;
}
