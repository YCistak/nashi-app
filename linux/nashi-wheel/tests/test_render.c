/* Display-free pixel checks on the rendered wheel, for both themes: the
 * highlighted (gold) petal must stay inside the outer ring, and the ring must
 * contrast with the petals so it reads as a ring. */
#include "wheel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

/* Luminance of the pixel composited over a mid-grey desktop. */
static double lum_over_grey(const unsigned char *row, int x) {
  const unsigned char *p = row + 4 * x;  /* ARGB32, premultiplied, native endian */
  double a = p[3] / 255.0;
  double r = p[2] / 255.0 + 0.5 * (1 - a);
  double g = p[1] / 255.0 + 0.5 * (1 - a);
  double b = p[0] / 255.0 + 0.5 * (1 - a);
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

static void check_theme(const char *theme, const char *mode, double size) {
  g_setenv("NASHI_WHEEL_THEME", theme, TRUE);
  WheelModel m;
  CHECK(wheel_demo_model(mode, size, &m));
  int side = (int)(size * 1.25);
  cairo_surface_t *surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, side, side);
  cairo_t *cr = cairo_create(surf);
  double c = side / 2.0;
  for (int hot = 0; hot < m.n_slices; hot++) {
    WheelView v = { .cx = c, .cy = c, .hot = hot };
    wheel_draw(cr, &m, &v);
    cairo_surface_flush(surf);
    unsigned char *data = cairo_image_surface_get_data(surf);
    int stride = cairo_image_surface_get_stride(surf);
    double gold_max = 0;
    for (int y = 0; y < side; y++)
      for (int x = 0; x < side; x++) {
        const unsigned char *p = data + y * stride + 4 * x;
        if (p[3] > 250 && p[2] > 200 && p[2] - p[0] > 60)  /* gold: R high, B low */
          gold_max = fmax(gold_max, hypot(x + 0.5 - c, y + 0.5 - c));
      }
    double r_out = wheel_outer_radius(size), r_back = size * 0.465;
    if (!(gold_max > r_out * 0.8 && gold_max <= r_out + 1.0 &&
          gold_max <= r_back - 4.0 * size / 320.0))
      fprintf(stderr, "%s %s hot=%d: gold reaches %.1f (outer %.1f, backdrop %.1f)\n",
              theme, mode, hot, gold_max, r_out, r_back);
    CHECK(gold_max > r_out * 0.8);
    CHECK(gold_max <= r_out + 1.0);
    CHECK(gold_max <= r_back - 4.0 * size / 320.0);
  }
  /* Ring contrast: a point in the middle of the ring against a petal point
   * (slice 0 is centred at the top; sample just right of its centre line, away
   * from the label). */
  WheelView v = { .cx = c, .cy = c, .hot = WHEEL_HIT_CENTER };
  wheel_draw(cr, &m, &v);
  cairo_surface_flush(surf);
  unsigned char *data = cairo_image_surface_get_data(surf);
  int stride = cairo_image_surface_get_stride(surf);
  double r_ring = (wheel_outer_radius(size) + size * 0.465) / 2;
  double r_pet = wheel_outer_radius(size) * 0.93;
  double ang = 20.0 * M_PI / 180.0;  /* from the top, inside slice 0 (6 slices: +-30) */
  double ring = lum_over_grey(data + (int)(c - r_ring * cos(ang)) * stride,
                              (int)(c + r_ring * sin(ang)));
  double petal = lum_over_grey(data + (int)(c - r_pet * cos(ang)) * stride,
                               (int)(c + r_pet * sin(ang)));
  if (fabs(ring - petal) < 0.10)
    fprintf(stderr, "%s: ring %.3f vs petal %.3f too similar\n", theme, ring, petal);
  CHECK(fabs(ring - petal) >= 0.10);
  cairo_destroy(cr);
  cairo_surface_destroy(surf);
}

int main(void) {
  const char *themes[] = { "light", "dark" };
  const char *modes[] = { "convert", "tools", "actions" };
  for (int t = 0; t < 2; t++)
    for (int k = 0; k < 3; k++)
      check_theme(themes[t], modes[k], 540);
  check_theme("dark", "convert", 320);
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("render checks OK\n");
  return 0;
}
