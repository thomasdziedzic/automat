#include "color.h"

#include <algorithm>
#include <limits>

#include "math.h"

namespace automaton::color {

namespace {

static constexpr float kKappa = 903.29629629629629629630;
static constexpr float kEpsilon = 0.00885645167903563082;
static constexpr float kRefU = 0.19783000664283680764;
static constexpr float kRefV = 0.46831999493879100370;
static constexpr vec3 kRGB_M[3] = {
    {3.24096994190452134377, -1.53738317757009345794, -0.49861076029300328366},
    {-0.96924363628087982613, 1.87596750150772066772, 0.04155505740717561247},
    {0.05563007969699360846, -0.20397695888897656435, 1.05697151424287856072}};

static constexpr float ToLinear(float c) {
  if (c > 0.04045)
    return pow((c + 0.055) / 1.055, 2.4);
  else
    return c / 12.92;
}

static constexpr float FromLinear(float c) {
  if (c <= 0.0031308)
    return 12.92 * c;
  else
    return 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

static constexpr vec3 RGBToXYZ(float r, float g, float b) {
  vec3 rgbl = Vec3(ToLinear(r), ToLinear(g), ToLinear(b));
  float x = Dot(
      {0.41239079926595948129, 0.35758433938387796373, 0.18048078840183428751},
      rgbl);
  float y = Dot(
      {0.21263900587151035754, 0.71516867876775592746, 0.07219231536073371500},
      rgbl);
  float z = Dot(
      {0.01933081871559185069, 0.11919477979462598791, 0.95053215224966058086},
      rgbl);
  return Vec3(x, y, z);
}

static constexpr float YToL(float y) {
  if (y <= kEpsilon)
    return y * kKappa;
  else
    return 116.0 * cbrt(y) - 16.0;
}

static constexpr float LToY(float l) {
  if (l <= 8.0) {
    return l / kKappa;
  } else {
    float x = (l + 16.0) / 116.0;
    return (x * x * x);
  }
}

static constexpr vec3 XYZToLUV(float a, float b, float c) {
  float var_u = (4.0 * a) / (a + (15.0 * b) + (3.0 * c));
  float var_v = (9.0 * b) / (a + (15.0 * b) + (3.0 * c));
  float l = YToL(b);
  if (l < 0.00000001) {
    return Vec3(l, 0, 0);
  }
  float u = 13.0 * l * (var_u - kRefU);
  float v = 13.0 * l * (var_v - kRefV);
  return Vec3(l, u, v);
}

static constexpr vec3 LUVToLCH(float l, float u, float v) {
  float h;
  float c = sqrtf(u * u + v * v);
  /* Grays: disambiguate hue */
  if (c < 0.00000001) {
    h = 0;
  } else {
    h = atan2(v, u) * 57.29577951308232087680; /* (180 / pi) */
    if (h < 0.0)
      h += 360.0;
  }
  return Vec3(l, c, h);
}

struct Bounds {
  float a;
  float b;
};

static constexpr void GetBounds(float l, Bounds bounds[6]) {
  float tl = l + 16.0;
  float sub1 = (tl * tl * tl) / 1560896.0;
  float sub2 = (sub1 > kEpsilon ? sub1 : (l / kKappa));
  int channel;
  int t;

  for (channel = 0; channel < 3; channel++) {
    float m1 = kRGB_M[channel].Elements[0];
    float m2 = kRGB_M[channel].Elements[1];
    float m3 = kRGB_M[channel].Elements[2];

    for (t = 0; t < 2; t++) {
      float top1 = (284517.0 * m1 - 94839.0 * m3) * sub2;
      float top2 = (838422.0 * m3 + 769860.0 * m2 + 731718.0 * m1) * l * sub2 -
                   769860.0 * t * l;
      float bottom = (632260.0 * m3 - 126452.0 * m2) * sub2 + 126452.0 * t;

      bounds[channel * 2 + t].a = top1 / bottom;
      bounds[channel * 2 + t].b = top2 / bottom;
    }
  }
}

static constexpr float RayLengthUntilIntersect(float theta,
                                               const Bounds *line) {
  return line->b / (sin(theta) - line->a * cos(theta));
}

static constexpr float MaxChromaForLH(float l, float h) {
  float min_len = std::numeric_limits<float>::max();
  float hrad = h * 0.01745329251994329577; /* (2 * pi / 360) */
  Bounds bounds[6];
  int i;

  GetBounds(l, bounds);
  for (i = 0; i < 6; i++) {
    float len = RayLengthUntilIntersect(hrad, &bounds[i]);

    if (len >= 0 && len < min_len)
      min_len = len;
  }
  return min_len;
}

static constexpr vec3 LCHToHSLuv(float l, float c, float h) {
  float s;

  /* White and black: disambiguate saturation */
  if (l > 99.9999999 || l < 0.00000001)
    s = 0.0;
  else
    s = c / MaxChromaForLH(l, h) * 100.0;

  /* Grays: disambiguate hue */
  if (c < 0.00000001)
    h = 0.0;

  return Vec3(h, s, l);
}

static constexpr vec3 HSLuvToLCH(float h, float s, float l) {
  float c;

  /* White and black: disambiguate chroma */
  if (l > 99.9999999 || l < 0.00000001)
    c = 0.0;
  else
    c = MaxChromaForLH(l, h) / 100.0 * s;

  /* Grays: disambiguate hue */
  if (s < 0.00000001)
    h = 0.0;

  return Vec3(l, c, h);
}

static constexpr vec3 LCHToLUV(float l, float c, float h) {
  float hrad = h * 0.01745329251994329577; /* (pi / 180.0) */
  float u = cos(hrad) * c;
  float v = sin(hrad) * c;
  return Vec3(l, u, v);
}

static constexpr vec3 LUVToXYZ(float l, float u, float v) {
  if (l <= 0.00000001) {
    /* Black will create a divide-by-zero error. */
    return Vec3(0, 0, 0);
  }

  float var_u = u / (13.0 * l) + kRefU;
  float var_v = v / (13.0 * l) + kRefV;
  float y = LToY(l);
  float x = -(9.0 * y * var_u) / ((var_u - 4.0) * var_v - var_u * var_v);
  float z = (9.0 * y - (15.0 * var_v * y) - (var_v * x)) / (3.0 * var_v);
  return Vec3(x, y, z);
}

static constexpr vec3 XYZToRGB(float x, float y, float z) {
  vec3 in_out = Vec3(x, y, z);
  float r = FromLinear(Dot(kRGB_M[0], in_out));
  float g = FromLinear(Dot(kRGB_M[1], in_out));
  float b = FromLinear(Dot(kRGB_M[2], in_out));
  return Vec3(r, g, b);
}

static constexpr vec3 HSLuvToRGB(float h, float s, float l) {
  vec3 lch = HSLuvToLCH(h, s, l);
  vec3 luv = LCHToLUV(lch.X, lch.Y, lch.Z);
  vec3 xyz = LUVToXYZ(luv.X, luv.Y, luv.Z);
  return XYZToRGB(xyz.X, xyz.Y, xyz.Z);
}

static constexpr vec3 RGBToHSLuv(float r, float g, float b) {
  vec3 xyz = RGBToXYZ(r, g, b);
  vec3 luv = XYZToLUV(xyz.X, xyz.Y, xyz.Z);
  vec3 lch = LUVToLCH(luv.X, luv.Y, luv.Z);
  return LCHToHSLuv(lch.X, lch.Y, lch.Z);
}

} // namespace

SkColor SetAlpha(SkColor color, uint8_t alpha) {
  return SkColorSetARGB(alpha, SkColorGetR(color), SkColorGetG(color),
                        SkColorGetB(color));
}

SkColor SetAlpha(SkColor color, float alpha_01) {
  return SetAlpha(color, (uint8_t)(alpha_01 * 255));
}

SkColor AdjustLightness(SkColor color, float adjust_percent) {
  vec3 hsluv =
      RGBToHSLuv(SkColorGetR(color) / 255.0, SkColorGetG(color) / 255.0,
                 SkColorGetB(color) / 255.0);
  float l = std::clamp(hsluv.Elements[2] + adjust_percent, 0.0f, 100.0f);
  vec3 rgb = HSLuvToRGB(hsluv.Elements[0], hsluv.Elements[1], l);
  return SkColorSetARGB(SkColorGetA(color), rgb.R * 255, rgb.G * 255,
                        rgb.B * 255);
}

} // namespace automaton::color