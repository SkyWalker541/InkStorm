#include "WeatherIcons.h"

#include <cmath>

// ---------------------------------------------------------------------------
// Primitive helpers (1-bit rendering)
// ---------------------------------------------------------------------------

static void fillCircle(GfxRenderer& r, int cx, int cy, int rad, bool black) {
  if (rad <= 0) return;
  static const int N = 24;
  int xs[N], ys[N];
  for (int i = 0; i < N; i++) {
    float a = 6.2831853f * i / N;
    xs[i] = cx + (int)(rad * cosf(a));
    ys[i] = cy + (int)(rad * sinf(a));
  }
  r.fillPolygon(xs, ys, N, black);
}

static void drawCircleOutline(GfxRenderer& r, int cx, int cy, int rad, bool black) {
  if (rad <= 1) return;
  static const int N = 48;
  int px = cx + rad, py = cy;
  for (int i = 1; i <= N; i++) {
    float a = 6.2831853f * i / N;
    int x = cx + (int)(rad * cosf(a));
    int y = cy + (int)(rad * sinf(a));
    r.drawLine(px, py, x, y, 1, black);
    px = x;
    py = y;
  }
}

static void drawSun(GfxRenderer& r, int cx, int cy, int s, bool black) {
  int rad = s * 3 / 10;
  int rayLen = s / 5;
  int gap = s / 10;
  // 12 rays with rounded tips; the disc overlaps their inner ends.
  for (int i = 0; i < 12; i++) {
    float a = 6.2831853f * i / 12;
    int dx = (int)(sinf(a) * (rad + gap));
    int dy = -(int)(cosf(a) * (rad + gap));
    int x1 = cx + dx, y1 = cy + dy;
    int x2 = cx + (int)(sinf(a) * (rad + gap + rayLen));
    int y2 = cy - (int)(cosf(a) * (rad + gap + rayLen));
    r.drawLine(x1, y1, x2, y2, 2, black);
    fillCircle(r, x2, y2, rayLen / 3 + 1, black);
  }
  fillCircle(r, cx, cy, rad, black);
  // Inner ring for a bit of polish on larger icons.
  if (rad >= 12) drawCircleOutline(r, cx, cy, rad * 3 / 5, !black);
}

static void drawMoon(GfxRenderer& r, int cx, int cy, int s, bool black) {
  int rad = s * 3 / 10;
  fillCircle(r, cx, cy, rad, black);
  // Carve crescent by overpainting an offset circle in the background.
  int ox = cx + rad / 2;
  int oy = cy - rad / 3;
  fillCircle(r, ox, oy, rad * 9 / 10, !black);
  // Small "nose" star dot on the crescent tip
  fillCircle(r, cx + rad / 3, cy - rad / 4, 1 + s / 16, black);
  // Companion stars on larger icons
  if (s >= 44) {
    fillCircle(r, cx - rad - 2, cy - rad, 1 + s / 22, black);
    fillCircle(r, cx + rad / 2, cy + rad * 7 / 10, 1 + s / 28, black);
  }
}

static void drawCloudBody(GfxRenderer& r, int cx, int cy, int s, bool black) {
  // Flat-bottomed cloud made from four overlapping circles + rect.
  int rMain = s * 20 / 100;
  int rSide = s * 13 / 100;
  int rTop = s * 9 / 100;
  int topY = cy - rMain * 3 / 4;
  int botY = cy + rMain * 5 / 8;

  fillCircle(r, cx, topY, rMain, black);
  fillCircle(r, cx - rMain, topY + rMain / 3, rSide, black);
  fillCircle(r, cx + rMain, topY + rMain / 3, rSide, black);
  fillCircle(r, cx - rMain * 4 / 10, topY - rMain / 4, rTop, black);
  fillCircle(r, cx + rMain * 5 / 10, topY - rMain / 3, rTop + 2, black);
  int rectTop = cy - rMain / 4;
  int rectH = botY - rectTop;
  r.fillRect(cx - rMain, rectTop, rMain * 2, rectH, black);
  // Smooth the two lower corners
  fillCircle(r, cx - rMain, botY - 2, rMain / 3, black);
  fillCircle(r, cx + rMain, botY - 2, rMain / 3, black);
}

static void drawRainDrops(GfxRenderer& r, int cx, int cy, int s, bool black) {
  int y0 = cy + s * 22 / 100;
  int len = s / 5;
  int gap = s / 6;
  for (int i = 0; i < 3; i++) {
    int x = cx - s / 4 + i * gap;
    r.drawLine(x, y0, x, y0 + len, 2, black);
    fillCircle(r, x, y0 + len, len / 3 + 1, black);
  }
}

static void drawSnowDots(GfxRenderer& r, int cx, int cy, int s, bool black) {
  int y0 = cy + s * 22 / 100;
  int gap = s / 5;
  for (int i = 0; i < 3; i++) {
    int x = cx - s / 4 + i * gap;
    // 6-arm flake with a small center dot
    int rad = s / 10;
    r.drawLine(x, y0 - rad, x, y0 + rad, 1, black);
    r.drawLine(x - rad, y0, x + rad, y0, 1, black);
    r.drawLine(x - rad, y0 - rad, x + rad, y0 + rad, 1, black);
    r.drawLine(x - rad, y0 + rad, x + rad, y0 - rad, 1, black);
    fillCircle(r, x, y0, 1 + s / 24, black);
  }
}

static void drawBolt(GfxRenderer& r, int cx, int cy, int s, bool black) {
  int x1 = cx + s / 6,  y1 = cy - s / 5;
  int x2 = cx - s / 6,  y2 = cy + s / 10;
  int x3 = cx - s / 12, y3 = cy + s / 10;
  int x4 = cx - s / 10, y4 = cy + s / 4;
  int x5 = cx + s / 4,  y5 = cy - s / 10;
  int x6 = cx + s / 12, y6 = cy - s / 10;
  int xs[6] = {x1, x2, x3, x4, x5, x6};
  int ys[6] = {y1, y2, y3, y4, y5, y6};
  r.fillPolygon(xs, ys, 6, black);
}

static void drawFogLines(GfxRenderer& r, int cx, int cy, int s, bool black) {
  int y0 = cy + s * 10 / 100;
  int w = s / 2;
  for (int i = 0; i < 3; i++) {
    int y = y0 + i * (s / 7);
    int ww = w - (i % 2) * (s / 10);
    r.drawLine(cx - ww, y, cx + ww, y, 2, black);
  }
}

// ---------------------------------------------------------------------------
// Public icon dispatchers
// ---------------------------------------------------------------------------

// Some glyphs draw well outside their nominal `size` box (the sun's 12 rays,
// the offset sun in partly-cloudy/shower compositions, raindrops below a
// cloud). Scale those draws down so every icon's pixels stay within
// cx +/- size/2, cy +/- size/2. Sizes are chosen from each glyph's max radial
// extent in units of its draw size (see the draw helpers above).
static int iconFitSize(int size, int wmoCode, bool isDay, bool small) {
  const int room = size / 2 - 2;
  if (room < 4) return size;
  int fit;
  switch (wmoCode) {
    case 0:
    case 1:
      fit = isDay ? (int)(room / 0.72f) : size;
      break;
    case 2:
      fit = (int)(room / 0.78f);
      break;
    case 45:
    case 48:
    case 3:
    case 71:
    case 73:
    case 75:
    case 77:
    case 85:
    case 86:
    case 95:
    case 96:
    case 99:
      fit = size;
      break;
    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
      fit = (int)(room / 0.52f);
      break;
    case 80:
    case 81:
    case 82:
      fit = (int)(room / 0.70f);
      break;
    default:
      fit = small ? size : (int)(room / 0.72f);
      break;
  }
  if (fit > size) fit = size;
  if (fit < 8) fit = 8;
  return fit;
}

static void drawGlyph(GfxRenderer& r, int cx, int cy, int size, int wmoCode, bool isDay, bool black,
                      bool small) {
  const int s = iconFitSize(size, wmoCode, isDay, small);
  switch (wmoCode) {
    case 0:
    case 1:
      if (isDay) drawSun(r, cx, cy, s, black);
      else drawMoon(r, cx, cy, s, black);
      break;

    case 2:
      // Partly cloudy: small sun/moon upper-left + cloud
      if (isDay) drawSun(r, cx - s / 4, cy - s / 4, s * 7 / 10, black);
      else drawMoon(r, cx - s / 4, cy - s / 4, s * 7 / 10, black);
      drawCloudBody(r, cx + s / 8, cy + s / 8, s * 7 / 10, black);
      break;

    case 3:
      drawCloudBody(r, cx, cy, s, black);
      break;

    case 45:
    case 48:
      drawCloudBody(r, cx, cy - s / 10, s * 8 / 10, black);
      drawFogLines(r, cx, cy + s / 6, s, black);
      break;

    case 51:
    case 53:
    case 55:
    case 56:
    case 57:
      drawCloudBody(r, cx, cy - s / 8, s * 8 / 10, black);
      drawRainDrops(r, cx, cy, s, black);
      break;

    case 61:
    case 63:
    case 65:
    case 66:
    case 67:
      drawCloudBody(r, cx, cy - s / 8, s * 8 / 10, black);
      drawRainDrops(r, cx, cy, s, black);
      break;

    case 71:
    case 73:
    case 75:
    case 77:
      drawCloudBody(r, cx, cy - s / 8, s * 8 / 10, black);
      drawSnowDots(r, cx, cy, s, black);
      break;

    case 80:
    case 81:
    case 82:
      if (isDay) drawSun(r, cx - s / 4, cy - s / 4, s * 6 / 10, black);
      else drawMoon(r, cx - s / 4, cy - s / 4, s * 6 / 10, black);
      drawCloudBody(r, cx + s / 8, cy - s / 6, s * 7 / 10, black);
      drawRainDrops(r, cx + s / 8, cy, s * 8 / 10, black);
      break;

    case 85:
    case 86:
      drawCloudBody(r, cx, cy - s / 8, s * 8 / 10, black);
      drawSnowDots(r, cx, cy, s, black);
      break;

    case 95:
    case 96:
    case 99:
      drawCloudBody(r, cx, cy - s / 6, s * 8 / 10, black);
      drawBolt(r, cx, cy + s / 6, s, black);
      break;

    default:
      if (small) drawCloudBody(r, cx, cy, s, black);
      else drawSun(r, cx, cy, s, black);
      break;
  }
}

void wxIcon(GfxRenderer& r, int cx, int cy, int size, int wmoCode, bool isDay, bool black) {
  drawGlyph(r, cx, cy, size, wmoCode, isDay, black, false);
}

void wxIconSmall(GfxRenderer& r, int cx, int cy, int size, int wmoCode, bool black) {
  drawGlyph(r, cx, cy, size, wmoCode, true, black, true);
}

const char* wxIconLabel(int wmoCode) {
  switch (wmoCode) {
    case 0:  return "Clear";
    case 1:  return "Clear";
    case 2:  return "Partly";
    case 3:  return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51: case 53: case 55: case 56: case 57: return "Drizzle";
    case 61: case 63: case 65: case 66: case 67: return "Rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: case 82: return "Showers";
    case 85: case 86: return "Snow";
    default: return "Storm";
  }
}
