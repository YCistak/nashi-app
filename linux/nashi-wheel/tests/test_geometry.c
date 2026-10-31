/* Display-free checks for the wheel hit-test. */
#include "wheel.h"

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { \
  fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

int main(void) {
  WheelModel m;
  CHECK(wheel_demo_model("convert", 540, &m));
  CHECK(!wheel_demo_model("nope", 540, &m));
  CHECK(wheel_demo_model("convert", 540, &m));  /* 6 slices */
  double cx = 500, cy = 400;
  double rm = (wheel_inner_radius(540) + wheel_outer_radius(540)) / 2;

  /* Centre cancels, far away selects nothing. */
  CHECK(wheel_hit_test(&m, cx, cy, cx + 5, cy) == WHEEL_HIT_CENTER);
  CHECK(wheel_hit_test(&m, cx, cy, cx + 5000, cy) == WHEEL_HIT_NONE);

  /* Slice i is centred at i * 60 degrees from the top, clockwise. */
  for (int i = 0; i < 6; i++) {
    double a = i * 60.0 * M_PI / 180.0;
    double px = cx + rm * sin(a), py = cy - rm * cos(a);
    CHECK(wheel_hit_test(&m, cx, cy, px, py) == i);
    /* Overshooting the outer edge keeps the slice (reach 1.6x). */
    double ro = wheel_outer_radius(540) * 1.5;
    CHECK(wheel_hit_test(&m, cx, cy, cx + ro * sin(a), cy - ro * cos(a)) == i);
  }
  /* Just either side of the 30 degree boundary between slice 0 and 1. */
  double r = rm;
  double a1 = 29.0 * M_PI / 180.0, a2 = 31.0 * M_PI / 180.0;
  CHECK(wheel_hit_test(&m, cx, cy, cx + r * sin(a1), cy - r * cos(a1)) == 0);
  CHECK(wheel_hit_test(&m, cx, cy, cx + r * sin(a2), cy - r * cos(a2)) == 1);

  /* Four-slice mode: slice 1 is on the right. */
  CHECK(wheel_demo_model("actions", 540, &m));
  CHECK(wheel_hit_test(&m, cx, cy, cx + rm, cy) == 1);
  CHECK(wheel_hit_test(&m, cx, cy, cx - rm, cy) == 3);

  /* Three slices start at the left, like WheelGeometry.startAngle. */
  CHECK(wheel_demo_model("actions", 540, &m));
  m.n_slices = 3;
  CHECK(wheel_hit_test(&m, cx, cy, cx - rm, cy) == 0);
  CHECK(wheel_hit_test(&m, cx, cy, cx + rm * 0.5, cy - rm * 0.8660254) == 1);
  CHECK(wheel_hit_test(&m, cx, cy, cx + rm * 0.5, cy + rm * 0.8660254) == 2);

  /* The ring ratios match the macOS wheel. */
  CHECK(fabs(wheel_outer_radius(320) - 140.8) < 1e-9);
  CHECK(fabs(wheel_inner_radius(320) / wheel_outer_radius(320) - 0.407) < 1e-9);

  /* --at X,Y parsing. */
  double px = -1, py = -1;
  CHECK(wheel_parse_point("400,300", &px, &py) && px == 400 && py == 300);
  CHECK(wheel_parse_point("12.5,0", &px, &py) && px == 12.5 && py == 0);
  px = py = -7;
  CHECK(!wheel_parse_point("400", &px, &py));
  CHECK(!wheel_parse_point("400,", &px, &py));
  CHECK(!wheel_parse_point(",300", &px, &py));
  CHECK(!wheel_parse_point("400,300x", &px, &py));
  CHECK(!wheel_parse_point("-1,5", &px, &py));
  CHECK(!wheel_parse_point("nan,5", &px, &py));
  CHECK(!wheel_parse_point("", &px, &py));
  CHECK(!wheel_parse_point(NULL, &px, &py));
  CHECK(px == -7 && py == -7);

  if (failures) return 1;
  puts("geometry: all checks passed");
  return 0;
}
