#pragma once

#include <GfxRenderer.h>

// Draws a weather icon centered at (cx, cy). size is the bounding box.
void wxIcon(GfxRenderer& r, int cx, int cy, int size, int wmoCode, bool isDay, bool black);
void wxIconSmall(GfxRenderer& r, int cx, int cy, int size, int wmoCode, bool black);
const char* wxIconLabel(int wmoCode);
