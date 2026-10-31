/* Wheel geometry and cairo drawing. No GDK windows or Wayland calls here, so
 * the hit-test can be unit tested without a display. */
#include "wheel.h"

#include <math.h>
#include <string.h>

#define REACH 1.6  /* selection stays valid up to REACH * outer radius */

double wheel_outer_radius(double size) { return size * 0.37; }
double wheel_inner_radius(double size) { return wheel_outer_radius(size) * 0.36; }

int wheel_hit_test(const WheelModel *model, double cx, double cy,
                   double px, double py) {
  double dx = px - cx, dy = py - cy;
  double r = hypot(dx, dy);
  if (r < wheel_inner_radius(model->size))
    return WHEEL_HIT_CENTER;
  if (r > wheel_outer_radius(model->size) * REACH)
    return WHEEL_HIT_NONE;
  int n = model->n_slices;
  if (n < 1)
    return WHEEL_HIT_NONE;
  /* 0 degrees = top, clockwise. Slice i is centred on i * 360 / n. */
  double ang = atan2(dy, dx) * 180.0 / M_PI + 90.0;
  ang = fmod(fmod(ang, 360.0) + 360.0, 360.0);
  double step = 360.0 / n;
  return (int)floor(fmod(ang + step / 2.0, 360.0) / step);
}

static const struct {
  const char *mode;
  int n;
  WheelSlice slices[WHEEL_MAX_SLICES];
} DEMO[] = {
  { "convert", 6, { {"to-jpg", "JPG"}, {"to-png", "PNG"}, {"to-webp", "WebP"},
                    {"to-avif", "AVIF"}, {"to-gif", "GIF"}, {"to-pdf", "PDF"} } },
  { "tools",   6, { {"compress", "Compress"}, {"resize", "Resize"}, {"crop", "Crop"},
                    {"strip-meta", "Strip"}, {"ocr", "OCR"}, {"qr", "QR"} } },
  { "actions", 4, { {"copy", "Copy"}, {"move", "Move"}, {"rename", "Rename"},
                    {"share", "Share"} } },
};

gboolean wheel_demo_model(const char *mode, double size, WheelModel *out) {
  for (guint i = 0; i < G_N_ELEMENTS(DEMO); i++) {
    if (strcmp(DEMO[i].mode, mode) != 0)
      continue;
    memset(out, 0, sizeof *out);
    out->mode = DEMO[i].mode;
    out->n_slices = DEMO[i].n;
    out->size = size;
    for (int k = 0; k < DEMO[i].n; k++)
      out->slices[k] = DEMO[i].slices[k];
    return TRUE;
  }
  return FALSE;
}

/* Angle of slice edge/centre in cairo's coordinate system (0 = +x, clockwise
 * positive, y down), given an offset in slice steps from the top. */
static double angle_at(int n, double steps) {
  return -M_PI / 2.0 + steps * (2.0 * M_PI / n);
}

static void slice_path(cairo_t *cr, double cx, double cy, double r_in,
                       double r_out, double a0, double a1) {
  cairo_new_sub_path(cr);
  cairo_arc(cr, cx, cy, r_out, a0, a1);
  cairo_arc_negative(cr, cx, cy, r_in, a1, a0);
  cairo_close_path(cr);
}

void wheel_draw(cairo_t *cr, const WheelModel *model, const WheelView *view) {
  const int n = model->n_slices;
  const double r_out = wheel_outer_radius(model->size);
  const double r_in = wheel_inner_radius(model->size);
  const double gap = 0.012;  /* radians trimmed from each slice edge */

  /* Clear the whole surface every frame; a partial repaint of a layer surface
   * leaves stale pixels. */
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);

  PangoLayout *layout = pango_cairo_create_layout(cr);
  PangoFontDescription *fd = pango_font_description_from_string("Sans Bold 15");
  pango_layout_set_font_description(layout, fd);
  pango_font_description_free(fd);

  for (int i = 0; i < n; i++) {
    double a0 = angle_at(n, i - 0.5) + gap, a1 = angle_at(n, i + 0.5) - gap;
    gboolean hot = (view->hot == i);
    slice_path(cr, view->cx, view->cy, r_in, r_out, a0, a1);
    if (hot)
      cairo_set_source_rgba(cr, 0.96, 0.55, 0.20, 0.96);
    else
      cairo_set_source_rgba(cr, 0.10, 0.11, 0.14, 0.86);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1, 1, 1, hot ? 0.9 : 0.18);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    double am = angle_at(n, i);
    double rm = (r_in + r_out) / 2.0;
    int tw, th;
    pango_layout_set_text(layout, model->slices[i].label, -1);
    pango_layout_get_pixel_size(layout, &tw, &th);
    cairo_set_source_rgba(cr, 1, 1, 1, 1);
    cairo_move_to(cr, view->cx + rm * cos(am) - tw / 2.0,
                  view->cy + rm * sin(am) - th / 2.0);
    pango_cairo_show_layout(cr, layout);
  }

  /* Centre disc: cancel zone. */
  cairo_new_sub_path(cr);  /* avoid a stray line from the last slice path */
  cairo_arc(cr, view->cx, view->cy, r_in - 6, 0, 2 * M_PI);
  if (view->hot == WHEEL_HIT_CENTER)
    cairo_set_source_rgba(cr, 0.80, 0.25, 0.25, 0.92);
  else
    cairo_set_source_rgba(cr, 0.10, 0.11, 0.14, 0.86);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
  cairo_stroke(cr);

  const char *center = (view->hot >= 0 && view->hot < n)
                         ? model->slices[view->hot].label : model->mode;
  fd = pango_font_description_from_string("Sans 11");
  pango_layout_set_font_description(layout, fd);
  pango_font_description_free(fd);
  pango_layout_set_text(layout, center, -1);
  int tw, th;
  pango_layout_get_pixel_size(layout, &tw, &th);
  cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
  cairo_move_to(cr, view->cx - tw / 2.0, view->cy - th / 2.0);
  pango_cairo_show_layout(cr, layout);

  g_object_unref(layout);
}
