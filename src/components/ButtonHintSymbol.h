#pragma once

// Symbols shown inside the four front-button hint boxes. The weather app uses
// these instead of text labels; themes render them centered, oriented
// correctly for the current screen orientation (portrait or landscape).
enum class ButtonHintSymbol {
  None,
  Back,     // left-pointing arrow
  Menu,     // three horizontal lines
  Select,   // checkmark
  Up,       // up arrow
  Down,     // down arrow
  Ok,       // checkmark (confirm on non-list screens)
  Close,    // X (dismiss / go back on the weather app)
  Refresh,  // circular arrow (manual weather refresh)
};
