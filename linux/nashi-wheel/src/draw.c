/* Wheel geometry and cairo drawing. No GDK windows or Wayland calls here, so
 * the hit-test can be unit tested without a display.
 *
 * The look follows Sources/NashiApp/Wheel.swift and WheelGeometry.swift of the
 * macOS app: rounded annular petals, a frosted backdrop, a gold selection
 * gradient and an opaque centre disc that shows the selected title. */
#include "wheel.h"

#include <math.h>
#include <string.h>

#define REACH 1.6  /* selection stays valid up to REACH * outer radius */

/* WheelGeometry.radius / .inner */
double wheel_outer_radius(double size) { return size * 0.44; }
double wheel_inner_radius(double size) { return wheel_outer_radius(size) * 0.407; }

double wheel_slice_center_deg(int n, int index) {
  /* Mac start angle is 90 (top) for every count except 3, which uses 180
   * (left); math angles run counter-clockwise, so convert to clockwise-from-top. */
  double start_top_cw = (n == 3) ? 270.0 : 0.0;
  return fmod(start_top_cw + index * 360.0 / (n > 0 ? n : 1), 360.0);
}

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
  /* 0 degrees = top, clockwise. */
  double ang = atan2(dy, dx) * 180.0 / M_PI + 90.0;
  double step = 360.0 / n;
  /* Rotate so slice 0 is centred on offset 0, then bucket by step. */
  ang -= wheel_slice_center_deg(n, 0);
  ang = fmod(fmod(ang, 360.0) + 360.0, 360.0);
  return (int)floor(fmod(ang + step / 2.0, 360.0) / step);
}

static const struct {
  const char *mode;
  int n;
  WheelSlice slices[WHEEL_MAX_SLICES];
} DEMO[] = {
  /* Convert petals are text-only on macOS (no symbol for a format name). */
  { "convert", 6, { {"to-jpg", "JPG", NULL}, {"to-png", "PNG", NULL},
                    {"to-webp", "WebP", NULL}, {"to-avif", "AVIF", NULL},
                    {"to-gif", "GIF", NULL}, {"to-pdf", "PDF", NULL} } },
  /* SF Symbols have no Linux build: nearest Adwaita symbolic icons. */
  { "tools",   6, { {"compress", "Compress", "view-restore-symbolic"},
                    {"resize", "Resize", "zoom-fit-best-symbolic"},
                    {"crop", "Crop", "edit-cut-symbolic"},
                    {"strip-meta", "Strip", "edit-clear-all-symbolic"},
                    {"ocr", "OCR", "insert-text-symbolic"},
                    {"qr", "QR", "scanner-symbolic"} } },
  { "actions", 4, { {"copy", "Copy", "edit-copy-symbolic"},
                    {"move", "Move", "folder-symbolic"},
                    {"rename", "Rename", "document-edit-symbolic"},
                    {"share", "Share", "send-to-symbolic"} } },
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

/* ---- theme ---------------------------------------------------------------
 * Mac uses adaptive system colours; the values below are sRGB samples of
 * controlBackgroundColor / labelColor in the light and dark appearances. */
typedef struct {
  double backdrop[4], petal[4], disc[3], label[4], edge[4];
  double rim[4];  /* hairline on the backdrop's outer edge */
} Palette;

static const Palette LIGHT = {
  .backdrop = {0.89, 0.89, 0.89, 0.86}, .petal = {0.99, 0.99, 0.99, 0.88},
  .disc = {1, 1, 1}, .label = {0, 0, 0, 0.85}, .edge = {0.5, 0.5, 0.5, 0.24},
  .rim = {0.5, 0.5, 0.5, 0.17} };
/* In the dark palette the backdrop used to be almost the colour of the petals
 * and of a dark desktop, so the outer ring did not read as a ring: the gold
 * petal looked like it reached the wheel's edge. The ring is lighter and
 * more opaque and gets a light hairline; the petal geometry is unchanged. */
static const Palette DARK = {
  .backdrop = {0.28, 0.28, 0.30, 0.94}, .petal = {0.13, 0.13, 0.14, 0.88},
  .disc = {0.12, 0.12, 0.13}, .label = {1, 1, 1, 0.85}, .edge = {0.5, 0.5, 0.5, 0.24},
  .rim = {1, 1, 1, 0.16} };

/* NashiArtwork.gold / goldEnd / ink */
static const double GOLD[3] = {0.89, 0.72, 0.35};
static const double GOLD_END[3] = {0.96, 0.82, 0.49};
static const double INK[3] = {0.20, 0.25, 0.14};

static gboolean theme_is_dark(void) {
  const char *env = g_getenv("NASHI_WHEEL_THEME");
  if (env)
    return g_str_equal(env, "dark");
  GtkSettings *s = gdk_display_get_default() ? gtk_settings_get_default() : NULL;
  if (!s)
    return FALSE;
  gboolean prefer = FALSE;
  gchar *name = NULL;
  g_object_get(s, "gtk-application-prefer-dark-theme", &prefer,
               "gtk-theme-name", &name, NULL);
  if (!prefer && name) {
    gchar *low = g_ascii_strdown(name, -1);
    prefer = strstr(low, "dark") != NULL;
    g_free(low);
  }
  g_free(name);
  return prefer;
}

/* ---- petal path ---------------------------------------------------------- */
typedef struct { double cx, cy, r_in, r_out, scale; } Rings;

static void pt(const Rings *g, double r, double a, double *x, double *y) {
  *x = g->cx + r * cos(a);
  *y = g->cy + r * sin(a);
}

/* WheelGeometry.petal: 0.9 degree gap per side, corner radius
 * min(10 * scale, inner * delta / 4), corners as quadratic-like curves.
 * Every point lies on or inside r_out, so the petal never leaves the ring. */
static void petal_path(cairo_t *cr, const Rings *g, int n, int index) {
  double delta = 2.0 * M_PI / n, gap = 0.9 * M_PI / 180.0;
  double mid = -M_PI / 2.0 + wheel_slice_center_deg(n, index) * M_PI / 180.0;
  double a = mid - delta / 2 + gap, b = mid + delta / 2 - gap;
  double corner = fmin(10 * g->scale, g->r_in * delta / 4);
  double oi = corner / g->r_out, ii = corner / g->r_in;  /* radians */
  double x, y, x1, y1, x2, y2;

  cairo_new_path(cr);
  pt(g, g->r_out - corner, a, &x, &y);
  cairo_move_to(cr, x, y);
  pt(g, g->r_out, a, &x1, &y1);
  pt(g, g->r_out, a + oi, &x2, &y2);
  cairo_curve_to(cr, x1, y1, x1, y1, x2, y2);
  cairo_arc(cr, g->cx, g->cy, g->r_out, a + oi, b - oi);
  pt(g, g->r_out, b, &x1, &y1);
  pt(g, g->r_out - corner, b, &x2, &y2);
  cairo_curve_to(cr, x1, y1, x1, y1, x2, y2);
  pt(g, g->r_in + corner, b, &x, &y);
  cairo_line_to(cr, x, y);
  pt(g, g->r_in, b, &x1, &y1);
  pt(g, g->r_in, b - ii, &x2, &y2);
  cairo_curve_to(cr, x1, y1, x1, y1, x2, y2);
  cairo_arc_negative(cr, g->cx, g->cy, g->r_in, b - ii, a + ii);
  pt(g, g->r_in, a, &x1, &y1);
  pt(g, g->r_in + corner, a, &x2, &y2);
  cairo_curve_to(cr, x1, y1, x1, y1, x2, y2);
  cairo_close_path(cr);
}

/* ---- labels -------------------------------------------------------------- */
static PangoLayout *make_layout(cairo_t *cr, const char *family, PangoWeight weight,
                                double px, const char *text, double max_w) {
  PangoLayout *l = pango_cairo_create_layout(cr);
  PangoFontDescription *fd = pango_font_description_new();
  pango_font_description_set_family(fd, family);
  pango_font_description_set_weight(fd, weight);
  pango_font_description_set_absolute_size(fd, px * PANGO_SCALE);
  pango_layout_set_font_description(l, fd);
  pango_font_description_free(fd);
  pango_layout_set_alignment(l, PANGO_ALIGN_CENTER);
  pango_layout_set_text(l, text, -1);
  /* Set a wrap width only when needed: with a width set Pango reports the
   * whole box as the logical extent and the text would sit off-centre. */
  PangoRectangle natural;
  pango_layout_get_pixel_extents(l, NULL, &natural);
  if (max_w > 0 && natural.width > max_w) {  /* wrap whole words only */
    pango_layout_set_width(l, (int)(max_w * PANGO_SCALE));
    pango_layout_set_wrap(l, PANGO_WRAP_WORD);
  }
  return l;
}

/* WheelLabelLayout.fits: sample the padded box edges against the petal. */
static gboolean box_inside(cairo_t *cr, double x0, double y0, double x1, double y1,
                           double pad) {
  x0 -= pad; y0 -= pad; x1 += pad; y1 += pad;
  for (int s = 0; s <= 8; s++) {
    double t = s / 8.0;
    double px = x0 + (x1 - x0) * t, py = y0 + (y1 - y0) * t;
    if (!cairo_in_fill(cr, px, y0) || !cairo_in_fill(cr, px, y1) ||
        !cairo_in_fill(cr, x0, py) || !cairo_in_fill(cr, x1, py))
      return FALSE;
  }
  return TRUE;
}

static GdkPixbuf *load_icon(const char *name, int px, const GdkRGBA *fg) {
  /* Headless rendering (--render-png) has no display, hence no default theme. */
  static GtkIconTheme *own;
  GtkIconTheme *theme;
  if (gdk_display_get_default()) {
    theme = gtk_icon_theme_get_default();
  } else {
    if (!own) {
      own = gtk_icon_theme_new();
      gtk_icon_theme_set_custom_theme(own, "Adwaita");
    }
    theme = own;
  }
  GtkIconInfo *info = gtk_icon_theme_lookup_icon(
      theme, name, px, GTK_ICON_LOOKUP_FORCE_SIZE | GTK_ICON_LOOKUP_FORCE_SYMBOLIC);
  if (!info)
    return NULL;
  GdkPixbuf *pb = gtk_icon_info_load_symbolic(info, fg, NULL, NULL, NULL, NULL, NULL);
  g_object_unref(info);
  return pb;
}

/* Draws one petal's icon + title like Wheel.swift: font 13 (text only) or 9.5
 * (with icon), icon 20, 4 spacing, all x scale, title uppercased. The block is
 * centred on the annular-sector centroid; if it does not fit it is nudged
 * radially/tangentially, then the font shrinks by up to 3 * scale. */
static void draw_slice_label(cairo_t *cr, const Rings *g, int n, int index,
                             const WheelSlice *s, const double rgb[3], double alpha) {
  double sc = g->scale;
  gboolean has_icon = s->icon != NULL;
  char *title = g_utf8_strup(s->label, -1);
  double angle = -M_PI / 2.0 + wheel_slice_center_deg(n, index) * M_PI / 180.0;
  double sweep = (2.0 * M_PI / n) - 1.8 * M_PI / 180.0;
  double centroid = n > 1
      ? 4 * sin(sweep / 2) * (pow(g->r_out, 3) - pow(g->r_in, 3)) /
            (3 * sweep * (g->r_out * g->r_out - g->r_in * g->r_in))
      : (g->r_out + g->r_in) / 2;
  double pref = centroid / g->r_out;
  double icon_px = 20 * sc, spacing = 4 * sc, base = (has_icon ? 9.5 : 13) * sc;
  static const double offsets[5] = {0, -4, 4, -8, 8};
  GdkRGBA fg = { rgb[0], rgb[1], rgb[2], alpha };

  petal_path(cr, g, n, index);  /* current path = fit region for cairo_in_fill */
  for (double red = 0; red <= 3 * sc + 1e-9; red += 0.25 * sc) {
    double px = fmax(6 * sc, base - red);
    PangoLayout *l = make_layout(cr, "Nunito, Sans", PANGO_WEIGHT_BOLD, px, title, 84 * sc);
    PangoRectangle ink, logical;
    pango_layout_get_pixel_extents(l, &ink, &logical);
    double tw = logical.width, th = logical.height;
    double bh = th + (has_icon ? icon_px + spacing : 0);
    double bw = fmax(tw, has_icon ? icon_px : 0);
    for (int k = 0; k <= 24; k++) {
      double f = pref + ((k + 1) / 2) * 0.02 * ((k & 1) ? 1 : -1);
      if (k == 0) f = pref;
      if (f <= g->r_in / g->r_out || f >= 0.86) continue;
      for (int o = 0; o < 5; o++) {
        double t = offsets[o] * sc, rad = g->r_out * f;
        double mx = g->cx + rad * cos(angle) - t * sin(angle);
        double my = g->cy + rad * sin(angle) + t * cos(angle);
        double top = my - bh / 2, left = mx - bw / 2;
        double ty = top + (has_icon ? icon_px + spacing : 0);
        if (has_icon &&
            !box_inside(cr, mx - icon_px / 2, top, mx + icon_px / 2, top + icon_px, 3.5 * sc))
          continue;
        if (!box_inside(cr, mx - tw / 2, ty, mx + tw / 2, ty + th, 3.5 * sc))
          continue;
        (void)left;
        cairo_save(cr);
        cairo_new_path(cr);
        if (has_icon) {
          GdkPixbuf *pb = load_icon(s->icon, (int)round(icon_px), &fg);
          if (pb) {
            gdk_cairo_set_source_pixbuf(cr, pb, round(mx - icon_px / 2), round(top));
            cairo_paint(cr);
            g_object_unref(pb);
          }
        }
        cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], alpha);
        cairo_move_to(cr, mx - tw / 2.0, ty);
        pango_cairo_show_layout(cr, l);
        cairo_restore(cr);
        g_object_unref(l);
        g_free(title);
        return;
      }
    }
    g_object_unref(l);
  }
  g_free(title);
}

static void draw_center_title(cairo_t *cr, const Rings *g, const char *text,
                              double rad_max, const double rgb[3], double alpha) {
  /* Wheel.swift: 11pt semibold system font. Inter is wider than SF at the same
   * size, so it is drawn at 10.5 to match the macOS look. Max width inner*2 - 16*scale. */
  double sc = g->scale;
  PangoLayout *l = make_layout(cr, "Inter, Sans", PANGO_WEIGHT_SEMIBOLD, 10.5 * sc, text, fmin(rad_max, 2 * g->r_in - 16 * sc));
  int w, h;
  pango_layout_get_pixel_size(l, &w, &h);
  cairo_set_source_rgba(cr, rgb[0], rgb[1], rgb[2], alpha);
  cairo_move_to(cr, g->cx - w / 2.0, g->cy - h / 2.0);
  pango_cairo_show_layout(cr, l);
  g_object_unref(l);
}

/* ---- main entry ---------------------------------------------------------- */
void wheel_draw(cairo_t *cr, const WheelModel *model, const WheelView *view) {
  const int n = model->n_slices;
  const double size = model->size;
  const Palette *pal = theme_is_dark() ? &DARK : &LIGHT;
  Rings g = { view->cx, view->cy, wheel_inner_radius(size) * 1.0,
              wheel_outer_radius(size), size / 320.0 };

  /* Clear the whole surface every frame; a partial repaint of a layer surface
   * leaves stale pixels. */
  cairo_save(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_restore(cr);

  /* Backdrop disc: Mac insets bounds by 3.5% per side and uses a blurred
   * popover material; Linux has no behind-blur here, so a soft shadow and a
   * more opaque fill stand in for it. */
  double r_back = size * (0.5 - 0.035);
  cairo_pattern_t *sh = cairo_pattern_create_radial(g.cx, g.cy + 4 * g.scale, r_back * 0.9,
                                                    g.cx, g.cy + 4 * g.scale, r_back + 26 * g.scale);
  cairo_pattern_add_color_stop_rgba(sh, 0, 0, 0, 0, 0.30);
  cairo_pattern_add_color_stop_rgba(sh, 1, 0, 0, 0, 0);
  cairo_set_source(cr, sh);
  cairo_arc(cr, g.cx, g.cy + 4 * g.scale, r_back + 26 * g.scale, 0, 2 * M_PI);
  cairo_fill(cr);
  cairo_pattern_destroy(sh);

  cairo_new_path(cr);
  cairo_arc(cr, g.cx, g.cy, r_back, 0, 2 * M_PI);
  cairo_set_source_rgba(cr, pal->backdrop[0], pal->backdrop[1], pal->backdrop[2], pal->backdrop[3]);
  cairo_fill_preserve(cr);
  cairo_set_source_rgba(cr, pal->rim[0], pal->rim[1], pal->rim[2], pal->rim[3]);
  cairo_set_line_width(cr, 0.7);
  cairo_stroke(cr);

  for (int i = 0; i < n; i++) {
    gboolean hot = (view->hot == i);
    petal_path(cr, &g, n, i);
    if (hot) {
      /* NSGradient(gold -> goldEnd, angle 80): runs bottom to top, tilted
       * 10 degrees to the right, across the petal's bounding box. */
      double x1, y1, x2, y2;
      cairo_path_extents(cr, &x1, &y1, &x2, &y2);
      double dx = cos(80 * M_PI / 180), dy = -sin(80 * M_PI / 180);
      double mx = (x1 + x2) / 2, my = (y1 + y2) / 2;
      double half = (fabs(dx) * (x2 - x1) + fabs(dy) * (y2 - y1)) / 2;
      cairo_pattern_t *gr = cairo_pattern_create_linear(mx - dx * half, my - dy * half,
                                                        mx + dx * half, my + dy * half);
      cairo_pattern_add_color_stop_rgb(gr, 0, GOLD[0], GOLD[1], GOLD[2]);
      cairo_pattern_add_color_stop_rgb(gr, 1, GOLD_END[0], GOLD_END[1], GOLD_END[2]);
      cairo_set_source(cr, gr);
      cairo_fill_preserve(cr);
      cairo_pattern_destroy(gr);
    } else {
      cairo_set_source_rgba(cr, pal->petal[0], pal->petal[1], pal->petal[2], pal->petal[3]);
      cairo_fill_preserve(cr);
    }
    cairo_set_source_rgba(cr, pal->edge[0], pal->edge[1], pal->edge[2], pal->edge[3]);
    cairo_set_line_width(cr, 0.7);
    cairo_stroke(cr);

    const double *ink = hot ? INK : pal->label;
    draw_slice_label(cr, &g, n, i, &model->slices[i], ink, hot ? 1.0 : pal->label[3]);
  }

  /* Centre disc: radius inner - 4 * scale, opaque, shows the selected title
   * or the mode name. A hover over it just shows the mode (no red cancel tint). */
  cairo_new_path(cr);
  cairo_arc(cr, g.cx, g.cy, g.r_in - 4 * g.scale, 0, 2 * M_PI);
  cairo_set_source_rgb(cr, pal->disc[0], pal->disc[1], pal->disc[2]);
  cairo_fill(cr);

  char *mode_title = g_strdup(model->mode);
  if (mode_title[0]) mode_title[0] = g_ascii_toupper(mode_title[0]);
  const char *center = (view->hot >= 0 && view->hot < n) ? model->slices[view->hot].label
                                                          : mode_title;
  draw_center_title(cr, &g, center, 2 * g.r_in, pal->label, pal->label[3]);
  g_free(mode_title);
}
