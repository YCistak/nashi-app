/* nashi-wheel: shared types and pure geometry/drawing helpers. */
#ifndef NASHI_WHEEL_H
#define NASHI_WHEEL_H

#include <gtk/gtk.h>

#define WHEEL_MAX_SLICES 6
#define WHEEL_MIN_SLICES 2

/* Hit-test results other than a slice index. */
#define WHEEL_HIT_NONE   (-1)  /* too far away: nothing selected */
#define WHEEL_HIT_CENTER (-2)  /* inside the inner ring: cancel */

typedef struct {
  const char *id;     /* stable identifier reported to the app */
  const char *label;  /* short text drawn on the slice */
  const char *icon;   /* freedesktop icon name, or NULL for a text-only slice */
} WheelSlice;

typedef struct {
  const char *mode;                 /* "convert", "tools" or "actions" */
  WheelSlice slices[WHEEL_MAX_SLICES];
  int n_slices;
  double size;                      /* wheel diameter in px */
} WheelModel;

typedef struct {
  double cx, cy;  /* wheel centre, surface coordinates */
  int hot;        /* slice index, or a WHEEL_HIT_* value */
} WheelView;

/* Ring radii derived from the wheel size (same ratios as the macOS app:
 * outer = 0.44 * size, inner = 0.407 * outer). */
double wheel_outer_radius(double size);
double wheel_inner_radius(double size);

/* Selection is by angle only: a pointer that overshoots the outer edge keeps
 * the slice in that direction selected, up to 1.6x the outer radius. */
int wheel_hit_test(const WheelModel *model, double cx, double cy,
                   double px, double py);

/* Centre direction of slice i, in degrees clockwise from the top. The macOS
 * wheel starts at the top, except a three-slice wheel which starts at the left. */
double wheel_slice_center_deg(int n_slices, int index);

/* Parses "X,Y" (surface coordinates, non-negative, finite). Returns FALSE for
 * anything else and leaves x/y untouched. */
gboolean wheel_parse_point(const char *text, double *x, double *y);

/* Fills a demo model; returns FALSE for an unknown mode. */
gboolean wheel_demo_model(const char *mode, double size, WheelModel *out);

void wheel_draw(cairo_t *cr, const WheelModel *model, const WheelView *view);

#endif
