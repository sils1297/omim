#include "../../base/SRC_FIRST.hpp"

#include "equality.hpp"

#include "../screenbase.hpp"

#include "../../testing/testing.hpp"

using namespace test;

namespace
{
  void check_set_from_rect(ScreenBase & screen, int width, int height)
  {
    screen.OnSize(0, 0, width, height);

    m2::PointD b1(0.0, 0.0);
    m2::PointD b2(300.0, 300.0);
    screen.SetFromRect(m2::AARectD(m2::RectD(b1, b2)));

    b1 = screen.GtoP(b1);
    b2 = screen.GtoP(b2);

    /// check that we are in boundaries.
    TEST(my::between_s(0, width, my::rounds(b1.x)), ());
    TEST(my::between_s(0, width, my::rounds(b2.x)), ());
    TEST(my::between_s(0, height, my::rounds(b1.y)), ());
    TEST(my::between_s(0, height, my::rounds(b2.y)), ());
  }
}

UNIT_TEST(ScreenBase_P2G2P)
{
  ScreenBase screen;

  check_set_from_rect(screen, 1000, 500);
  check_set_from_rect(screen, 500, 1000);

  screen.OnSize(0, 0, 640, 480);
  screen.SetFromRect(m2::AARectD(m2::RectD(-100, -200, 500, 680)));

  /// checking that PtoG(GtoP(p)) == p

  m2::PointD pp(10.0, 20.0);
  m2::PointD pg = screen.PtoG(pp);
  TEST(pp.EqualDxDy(screen.GtoP(pg), 1.0E-10), ());

  pg = m2::PointD(550, 440);
  pp = screen.GtoP(pg);
  TEST(pg.EqualDxDy(screen.PtoG(pp), 1.0E-10), ());
}

UNIT_TEST(ScreenBase_AxisOrientation)
{
  ScreenBase screen;

  screen.OnSize(0, 0, 300, 200);
  screen.SetFromRect(m2::AARectD(m2::RectD(0, 0, 300, 200)));

  TEST(is_equal(m2::PointD(150, 100), screen.GtoP(m2::PointD(150, 100))), ());
  TEST(is_equal(m2::PointD(0, 0), screen.GtoP(m2::PointD(0, 200))), ());
  TEST(is_equal(m2::PointD(300, 0), screen.GtoP(m2::PointD(300, 200))), ());
  TEST(is_equal(m2::PointD(300, 200), screen.GtoP(m2::PointD(300, 0))), ());
  TEST(is_equal(m2::PointD(0, 200), screen.GtoP(m2::PointD(0, 0))), ());
}

UNIT_TEST(ScreenBase_X0Y0)
{
  ScreenBase screen;
  screen.OnSize(10, 10, 300, 200);
  screen.SetFromRect(m2::AARectD(m2::RectD(0, 0, 300, 200)));

  m2::PointD pxPt = screen.PtoG(m2::PointD(0, 0));

  TEST(is_equal(m2::PointD(10, 210), screen.GtoP(m2::PointD(0, 0))), ());
}

UNIT_TEST(ScreenBase_ChoosingMaxScale)
{
  ScreenBase screen;
  screen.OnSize(10, 10, 300, 200);
  screen.SetFromRect(m2::AARectD(m2::RectD(0, 0, 200, 400)));

  TEST(is_equal(screen.GtoP(m2::PointD(100, 200)), m2::PointD(160, 110)), ());
  TEST(is_equal(screen.GtoP(m2::PointD(0, 0)), m2::PointD(110, 210)), ());
  TEST(is_equal(screen.GtoP(m2::PointD(200, 0)), m2::PointD(210, 210)), ());
  TEST(is_equal(screen.GtoP(m2::PointD(0, 400)), m2::PointD(110, 10)), ());
  TEST(is_equal(screen.GtoP(m2::PointD(200, 400)), m2::PointD(210, 10)), ());

  TEST(is_equal(screen.GtoP(m2::PointD(-200, 0)), m2::PointD(10, 210)), ());
}

UNIT_TEST(ScreenBase_ExtractGtoPParams)
{

}

UNIT_TEST(ScreenBase_SetGtoPMatrix)
{

}

UNIT_TEST(ScreenBase_CalcTransform)
{
  double s = 2;
  double a = sqrt(3.) / 2.0;
  double dx = 1;
  double dy = 2;
  double s1, a1, dx1, dy1;
  math::Matrix<double, 3, 3> m = ScreenBase::CalcTransform(
                                          m2::PointD(0, 1), m2::PointD(1, 1),
                                          m2::PointD(             s * sin(a) + dx,               s * cos(a) + dy),
                                          m2::PointD(s * cos(a) + s * sin(a) + dx, -s * sin(a) + s * cos(a) + dy));

  ScreenBase::ExtractGtoPParams(m, a1, s1, dx1, dy1);

  TEST(fabs(s - s1) < 0.00001, (s, s1));
  TEST(fabs(a - a1) < 0.00001, (a, a1));
  TEST(fabs(dx - dx1) < 0.00001, (dx, dx1));
  TEST(fabs(dy - dy1) < 0.00001, (dy, dy1));
}

UNIT_TEST(ScreenBase_Rotate)
{
  ScreenBase s;
  s.OnSize(0, 0, 100, 200);
  s.SetFromRect(m2::AARectD(m2::RectD(0, 0, 100, 200)));
  s.Rotate(math::pi / 4);

  m2::RectD pxRect = s.PixelRect();
  m2::AARectD glbRect = s.GlobalRect();

  TEST(pxRect == m2::RectD(0, 0, 100, 200), ());
}
