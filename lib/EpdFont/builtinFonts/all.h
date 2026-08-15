#pragma once

// Built-in reading fonts are fixed at 10, 12, 14, and 16 pt. The default
// variant includes emoji/symbol and PHM CJK fallbacks; noemoji includes only
// the primary fonts.
#ifdef OMIT_EMOJI_FONTS
#define BUILTIN_READING_FONT_HEADER(name) <builtinFonts/noemoji/name.h>
#else
#define BUILTIN_READING_FONT_HEADER(name) <builtinFonts/name.h>
#endif

#include BUILTIN_READING_FONT_HEADER(bitter_10_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_10_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_10_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_10_regular)
#include BUILTIN_READING_FONT_HEADER(bitter_12_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_12_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_12_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_12_regular)
#include BUILTIN_READING_FONT_HEADER(bitter_14_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_14_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_14_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_14_regular)
#include BUILTIN_READING_FONT_HEADER(bitter_16_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_16_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_16_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_16_regular)

#ifndef OMIT_XLARGE_FONT
#include BUILTIN_READING_FONT_HEADER(bitter_18_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_18_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_18_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_18_regular)
#endif
#ifndef OMIT_HUGE_FONT
#include BUILTIN_READING_FONT_HEADER(bitter_20_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_20_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_20_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_20_regular)
#endif

#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_regular)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_regular)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_regular)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_regular)

#ifndef OMIT_TEENSY_FONT
#include BUILTIN_READING_FONT_HEADER(charein_8_bold)
#include BUILTIN_READING_FONT_HEADER(charein_8_bolditalic)
#include BUILTIN_READING_FONT_HEADER(charein_8_italic)
#include BUILTIN_READING_FONT_HEADER(charein_8_regular)
#endif
#ifndef OMIT_ITTY_BITTY_FONT
#include BUILTIN_READING_FONT_HEADER(charein_9_bold)
#include BUILTIN_READING_FONT_HEADER(charein_9_bolditalic)
#include BUILTIN_READING_FONT_HEADER(charein_9_italic)
#include BUILTIN_READING_FONT_HEADER(charein_9_regular)
#endif
#ifndef OMIT_TINY_FONT
#include BUILTIN_READING_FONT_HEADER(charein_10_bold)
#include BUILTIN_READING_FONT_HEADER(charein_10_bolditalic)
#include BUILTIN_READING_FONT_HEADER(charein_10_italic)
#include BUILTIN_READING_FONT_HEADER(charein_10_regular)
#endif
#ifndef OMIT_SMALL_FONT
#include BUILTIN_READING_FONT_HEADER(charein_12_bold)
#include BUILTIN_READING_FONT_HEADER(charein_12_bolditalic)
#include BUILTIN_READING_FONT_HEADER(charein_12_italic)
#include BUILTIN_READING_FONT_HEADER(charein_12_regular)
#endif
#ifndef OMIT_MEDIUM_FONT
#include BUILTIN_READING_FONT_HEADER(charein_14_bold)
#include BUILTIN_READING_FONT_HEADER(charein_14_bolditalic)
#include BUILTIN_READING_FONT_HEADER(charein_14_italic)
#include BUILTIN_READING_FONT_HEADER(charein_14_regular)
#endif
#ifndef OMIT_LARGE_FONT
#include BUILTIN_READING_FONT_HEADER(charein_16_bold)
#include BUILTIN_READING_FONT_HEADER(charein_16_bolditalic)
#include BUILTIN_READING_FONT_HEADER(charein_16_italic)
#include BUILTIN_READING_FONT_HEADER(charein_16_regular)
#endif
#ifndef OMIT_XLARGE_FONT
#include BUILTIN_READING_FONT_HEADER(charein_18_bold)
#include BUILTIN_READING_FONT_HEADER(charein_18_bolditalic)
#include BUILTIN_READING_FONT_HEADER(charein_18_italic)
#include BUILTIN_READING_FONT_HEADER(charein_18_regular)
#endif
#ifndef OMIT_HUGE_FONT
#include BUILTIN_READING_FONT_HEADER(charein_20_bold)
#include BUILTIN_READING_FONT_HEADER(charein_20_bolditalic)
#include BUILTIN_READING_FONT_HEADER(charein_20_italic)
#include BUILTIN_READING_FONT_HEADER(charein_20_regular)
#endif

#undef BUILTIN_READING_FONT_HEADER

// UI fonts - no emoji or PHM variants.
#include <builtinFonts/inter_10_bold.h>
#include <builtinFonts/inter_10_regular.h>
#include <builtinFonts/inter_12_bold.h>
#include <builtinFonts/inter_12_regular.h>
#include <builtinFonts/inter_8_regular.h>
