/* nashi-wheel: layer-shell overlay surface (falls back to a plain window). */
#ifndef NASHI_SURFACE_H
#define NASHI_SURFACE_H

#include <gtk/gtk.h>

typedef struct {
  gboolean want_keyboard;  /* take keyboard focus (demo/Esc); off while dragging */
  double fallback_size;    /* window size when layer-shell is unavailable */
} SurfaceOptions;

/* Creates (but does not show) the window. Must be called before the widget is
 * realized because layer-shell has to be initialised first. */
GtkWindow *surface_new(const SurfaceOptions *opts, gboolean *is_layer);

#endif
