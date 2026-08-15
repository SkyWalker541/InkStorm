#pragma once

#include <GfxRenderer.h>
#include <MappedInputManager.h>

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "components/UITheme.h"
#include "fontIds.h"

// A single-dialog popup with several independent option groups (sections).
// Used by the weather settings for settings that have more than one aspect
// (e.g. Time: show/hide + format + refresh interval) so one row opens one
// dialog instead of several rows.
//
// Navigation: Up/Down move a cursor across the option rows (wrapping);
// Confirm applies the row under the cursor to its group and the dialog STAYS
// OPEN so several settings can be changed in one visit. The current value of
// each group is marked with a dot. Back (or tapping outside the dialog)
// closes it.
class WeatherMultiPopup {
 public:
  struct Group {
    const char* label = nullptr;  // section header, may be null
    std::vector<std::string> options;
    int current = 0;  // index of the group's current value
  };

  void show(const char* title, std::vector<Group> groups, std::function<void(int group, int option)> onSelect) {
    title_ = title ? title : "";
    groups_ = std::move(groups);
    onSelect_ = std::move(onSelect);
    active_ = true;
    layoutValid_ = false;
    activatedAtMs_ = millis();
  }

  bool isActive() const { return active_; }

  void close() { active_ = false; }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active_) return false;

    int tx = 0;
    int ty = 0;
    if (input.wasScreenTapped(tx, ty)) {
      const Layout& layout = getLayout(input.getRenderer());
      if (rows_.empty()) {
        active_ = false;
        return true;
      }
      if (!contains(layout.dialog, tx, ty)) {
        active_ = false;
        requestUpdate();
        return true;
      }
      for (int i = layout.firstRow; i < layout.firstRow + layout.visibleRows; i++) {
        const Row& row = rows_[i];
        if (row.isSection) continue;
        const int rowY = layout.rowTop[i] - layout.rowTop[layout.firstRow] + layout.listTop;
        if (ty >= rowY && ty < rowY + layout.rowHeight) {
          applyRow(i);
          requestUpdate();
          return true;
        }
      }
      return true;  // tap on the dialog chrome keeps it open
    }

    if (input.wasPressed(MappedInputManager::Button::Up) || input.wasPressed(MappedInputManager::Button::Left)) {
      if ((millis() - activatedAtMs_) < kIgnoreImmediatePressMs) return true;
      moveCursor(-1);
      requestUpdate();
      return true;
    }
    if (input.wasPressed(MappedInputManager::Button::Down) || input.wasPressed(MappedInputManager::Button::Right)) {
      if ((millis() - activatedAtMs_) < kIgnoreImmediatePressMs) return true;
      moveCursor(1);
      requestUpdate();
      return true;
    }
    if (input.wasPressed(MappedInputManager::Button::Confirm)) {
      if ((millis() - activatedAtMs_) < kIgnoreImmediatePressMs) return true;
      if (cursor_ >= 0 && cursor_ < static_cast<int>(rows_.size()) && !rows_[cursor_].isSection) {
        applyRow(cursor_);
        requestUpdate();
      }
      return true;
    }
    if (input.wasPressed(MappedInputManager::Button::Back)) {
      if ((millis() - activatedAtMs_) < kIgnoreImmediatePressMs) return true;
      active_ = false;
      input.suppressNextBackRelease();
      requestUpdate();
      return true;
    }
    return true;
  }

  bool processRenderSymbols(GfxRenderer& renderer, const MappedInputManager& input) const {
    if (!active_) return false;
    const auto symbols = input.mapSymbols(ButtonHintSymbol::Close, ButtonHintSymbol::Select, ButtonHintSymbol::Up,
                                          ButtonHintSymbol::Down);
    GUI.drawButtonHintsSymbols(renderer, symbols.btn1, symbols.btn2, symbols.btn3, symbols.btn4);
    render(renderer);
    renderer.displayBuffer();
    return true;
  }

 private:
  struct Row {
    bool isSection = false;
    int group = 0;
    int option = 0;
  };

  static bool contains(const Rect& r, int x, int y) {
    return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height;
  }

  void moveCursor(int delta) {
    const int n = static_cast<int>(rows_.size());
    if (n <= 0) return;
    int i = cursor_;
    do {
      i = (i + delta + n) % n;
    } while (rows_[i].isSection && i != cursor_);
    if (!rows_[i].isSection) cursor_ = i;
    layoutValid_ = false;  // recompute visible window for the new cursor position
  }

  void applyRow(int rowIndex) {
    const Row& row = rows_[rowIndex];
    if (row.group >= 0 && row.group < static_cast<int>(groups_.size())) {
      groups_[row.group].current = row.option;
    }
    if (onSelect_) onSelect_(row.group, row.option);
  }

  void buildRows() {
    rows_.clear();
    for (int g = 0; g < static_cast<int>(groups_.size()); g++) {
      const Group& group = groups_[g];
      if (group.label != nullptr && group.label[0] != '\0') {
        Row r;
        r.isSection = true;
        r.group = g;
        rows_.push_back(r);
      }
      for (int o = 0; o < static_cast<int>(group.options.size()); o++) {
        Row r;
        r.group = g;
        r.option = o;
        rows_.push_back(r);
      }
    }
    if (rows_.empty()) {
      Row r;
      rows_.push_back(r);
    }
    cursor_ = std::clamp(cursor_, 0, static_cast<int>(rows_.size()) - 1);
    if (rows_[cursor_].isSection) moveCursor(1);
  }

  struct Layout {
    Rect dialog{0, 0, 0, 0};
    int dialogX = 0;
    int dialogY = 0;
    int dialogW = 0;
    int innerPadding = 0;
    int listTop = 0;
    int listHeight = 0;
    int rowHeight = 0;
    int sectionRowHeight = 0;
    int firstRow = 0;
    int visibleRows = 0;
    int scrollBarX = 0;
    bool hasHiddenRows = false;
    std::vector<std::string> titleLines;
    std::vector<Row> rows;
    std::vector<int> rowTop;
  };

  const Layout& getLayout(const GfxRenderer& renderer) const {
    if (layoutValid_) return layout_;
    const_cast<WeatherMultiPopup*>(this)->buildLayout(renderer);
    layoutValid_ = true;
    return layout_;
  }

  void buildLayout(const GfxRenderer& renderer) {
    buildRows();
    layout_ = Layout();

    const auto& metrics = UITheme::getInstance().getMetrics();
    const int pageWidth = renderer.getScreenWidth();
    const int pageHeight = renderer.getScreenHeight();
    const int optionFontId = metrics.optionPopupUseSmallFont ? UI_10_FONT_ID : UI_12_FONT_ID;
    const EpdFontFamily::Style optionStyle =
        metrics.optionPopupOptionFontBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;

    const int itemSpacing = metrics.optionPopupItemSpacing;
    const int innerPadding = metrics.optionPopupInnerPadding;
    const int selectionHPadding = metrics.optionPopupSelectionHPadding;
    const int selectionVPadding = metrics.optionPopupSelectionVPadding;
    const int titleGap = metrics.optionPopupTitleGap;

    const int optionLineHeight = renderer.getLineHeight(optionFontId);
    const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int sectionLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    layout_.rowHeight = optionLineHeight + selectionVPadding * 2;
    layout_.sectionRowHeight = sectionLineHeight + 4;

    int maxTextWidth = renderer.getTextWidth(UI_12_FONT_ID, title_.c_str(), EpdFontFamily::BOLD);
    for (const auto& group : groups_) {
      for (const auto& opt : group.options) {
        const int w = renderer.getTextWidth(optionFontId, opt.c_str(), optionStyle);
        if (w > maxTextWidth) maxTextWidth = w;
      }
    }

    const int rowCount = static_cast<int>(rows_.size());
    layout_.rowTop.resize(rowCount);
    int totalH = 0;
    for (int i = 0; i < rowCount; i++) {
      layout_.rowTop[i] = totalH;
      const int h = rows_[i].isSection ? layout_.sectionRowHeight : layout_.rowHeight;
      totalH += h;
      if (i + 1 < rowCount) totalH += itemSpacing;
    }

    const int maxDialogH =
        std::max(layout_.rowHeight + titleLineHeight + titleGap + innerPadding * 2,
                 pageHeight - metrics.buttonHintsHeight - metrics.optionPopupDialogSideMargin * 2);
    const int dialogW = std::min((maxTextWidth + innerPadding * 2 + selectionHPadding * 2 + metrics.scrollBarWidth +
                                  metrics.scrollBarRightOffset + selectionHPadding) *
                                     12 / 10,
                                 pageWidth - metrics.optionPopupDialogSideMargin * 2);
    const int titleContentWidth = std::max(1, dialogW - innerPadding * 2);
    const int maxTitleLines =
        std::max(1, (maxDialogH - innerPadding * 2 - titleGap - layout_.rowHeight) / titleLineHeight);
    const auto titleLines =
        renderer.wrappedText(UI_12_FONT_ID, title_.c_str(), titleContentWidth, maxTitleLines, EpdFontFamily::BOLD);
    const int titleHeight = static_cast<int>(titleLines.size()) * titleLineHeight;
    layout_.titleLines = titleLines;

    const int maxListHeight = std::max(layout_.rowHeight, maxDialogH - innerPadding * 2 - titleHeight - titleGap);

    // Visible window around the cursor.
    int first = 0;
    int last = 0;
    while (last < rowCount) {
      const int h = layout_.rowTop[last] + (rows_[last].isSection ? layout_.sectionRowHeight : layout_.rowHeight) -
                    layout_.rowTop[first];
      if (h > maxListHeight) break;
      last++;
    }
    layout_.hasHiddenRows = last < rowCount || totalH > maxListHeight;
    if (layout_.hasHiddenRows) {
      while (cursor_ >= last && last < rowCount) {
        first++;
        last++;
      }
      while (cursor_ < first && first > 0) {
        first--;
        last--;
      }
      if (last - first == 0) {
        first = cursor_;
        last = cursor_ + 1;
      }
    }
    layout_.firstRow = first;
    layout_.visibleRows = last - first;

    const int listHeight = layout_.rowTop[last - 1] + (rows_[last - 1].isSection ? layout_.sectionRowHeight
                                                                                  : layout_.rowHeight) -
                           layout_.rowTop[first];
    const int scrollBarGutter =
        layout_.hasHiddenRows ? metrics.scrollBarWidth + metrics.scrollBarRightOffset + selectionHPadding : 0;
    const int contentHeight = titleHeight + titleGap + listHeight;
    const int dialogH = contentHeight + innerPadding * 2;
    const int dialogX = (pageWidth - dialogW) / 2;
    const int dialogY = (pageHeight - dialogH) / 2;

    layout_.dialog = Rect{dialogX, dialogY, dialogW, dialogH};
    layout_.listTop = dialogY + innerPadding + titleHeight + titleGap;
    layout_.dialogX = dialogX;
    layout_.dialogY = dialogY;
    layout_.dialogW = dialogW;
    layout_.innerPadding = innerPadding;
    layout_.listHeight = listHeight;
    layout_.scrollBarX = dialogX + dialogW - innerPadding - metrics.scrollBarRightOffset;
  }

  void render(const GfxRenderer& renderer) const {
    const Layout& layout = getLayout(renderer);
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int optionFontId = metrics.optionPopupUseSmallFont ? UI_10_FONT_ID : UI_12_FONT_ID;
    const EpdFontFamily::Style optionStyle =
        metrics.optionPopupOptionFontBold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int frameThickness = metrics.popupFrameThickness;
    const int frameRadius = metrics.popupCornerRadius;
    const int itemSpacing = metrics.optionPopupItemSpacing;

    const int dx = layout.dialogX;
    const int dy = layout.dialogY;
    const int dw = layout.dialogW;
    const int dh = layout.dialog.height;

    if (frameRadius > 0) {
      renderer.fillRoundedRect(dx - frameThickness, dy - frameThickness, dw + frameThickness * 2,
                               dh + frameThickness * 2, frameRadius + frameThickness, Color::White);
      renderer.fillRoundedRect(dx, dy, dw, dh, frameRadius, Color::Black);
      renderer.fillRoundedRect(dx + frameThickness, dy + frameThickness, dw - frameThickness * 2,
                               dh - frameThickness * 2, frameRadius - frameThickness > 0 ? frameRadius - frameThickness : 0,
                               Color::White);
    } else {
      renderer.fillRect(dx - frameThickness, dy - frameThickness, dw + frameThickness * 2, dh + frameThickness * 2, true);
      renderer.fillRect(dx, dy, dw, dh, false);
    }

    int y = dy + layout.innerPadding;
    for (const auto& line : layout.titleLines) {
      const int lineWidth = renderer.getTextWidth(UI_12_FONT_ID, line.c_str(), EpdFontFamily::BOLD);
      renderer.drawText(UI_12_FONT_ID, dx + (dw - lineWidth) / 2, y, line.c_str(), true, EpdFontFamily::BOLD);
      y += renderer.getLineHeight(UI_12_FONT_ID);
    }

    if (metrics.optionPopupTitleSeparator) {
      const int sepY = y + metrics.optionPopupTitleGap / 2;
      renderer.drawLine(dx + layout.innerPadding, sepY, dx + dw - layout.innerPadding, sepY, true);
    }
    y = layout.listTop;

    const int itemRectX = dx + layout.innerPadding;
    const int itemRectW = std::max(1, dw - layout.innerPadding * 2 -
                                         (layout.hasHiddenRows ? metrics.scrollBarWidth + metrics.scrollBarRightOffset +
                                                                     metrics.optionPopupSelectionHPadding
                                                               : 0));
    const int selectionRadius = metrics.optionPopupSelectionRadius;

    if (layout.hasHiddenRows) {
      const int scrollBarX = layout.scrollBarX;
      const int scrollBarHeight =
          std::max(metrics.scrollBarWidth, (layout.listHeight * layout.visibleRows) / static_cast<int>(rows_.size()));
      const int scrollRange = std::max(0, layout.listHeight - scrollBarHeight);
      const int scrollSteps = std::max(1, static_cast<int>(rows_.size()) - layout.visibleRows);
      const int scrollBarY = y + (scrollRange * layout.firstRow) / scrollSteps;
      renderer.drawLine(scrollBarX, y, scrollBarX, y + layout.listHeight, true);
      renderer.fillRect(scrollBarX - metrics.scrollBarWidth, scrollBarY, metrics.scrollBarWidth, scrollBarHeight, true);
    }

    for (int i = layout.firstRow; i < layout.firstRow + layout.visibleRows; i++) {
      const Row& row = rows_[i];
      const int rowY = layout.rowTop[i] - layout.rowTop[layout.firstRow] + y;

      if (row.isSection) {
        const char* label = groups_[row.group].label;
        if (label != nullptr && label[0] != '\0') {
          renderer.drawText(UI_10_FONT_ID, itemRectX + metrics.optionPopupSelectionHPadding, rowY, label, true,
                            EpdFontFamily::BOLD);
        }
        continue;
      }

      const bool isCursor = (i == cursor_);
      const bool isCurrent = (row.option == groups_[row.group].current);
      if (isCursor) {
        if (selectionRadius > 0) {
          renderer.fillRoundedRect(itemRectX, rowY, itemRectW, layout.rowHeight, selectionRadius, Color::Black);
        } else {
          renderer.fillRect(itemRectX, rowY, itemRectW, layout.rowHeight, true);
        }
      }

      const char* labelText = groups_[row.group].options[row.option].c_str();
      const int textW = renderer.getTextWidth(optionFontId, labelText, optionStyle);
      const int textY = rowY + (layout.rowHeight - renderer.getLineHeight(optionFontId)) / 2;
      const int markerW = isCurrent ? 6 : 0;
      const int textX = itemRectX + (itemRectW - textW) / 2 + markerW / 2;
      if (isCurrent) {
        const int dotY = rowY + (layout.rowHeight - 4) / 2;
        renderer.fillRect(itemRectX + 4, dotY, 4, 4, !isCursor);
      }
      renderer.drawText(optionFontId, textX, textY, labelText, !isCursor, optionStyle);
    }
  }

  std::string title_;
  std::vector<Group> groups_;
  std::function<void(int, int)> onSelect_;
  std::vector<Row> rows_;
  int cursor_ = 0;
  bool active_ = false;
  mutable bool layoutValid_ = false;
  mutable Layout layout_;
  uint32_t activatedAtMs_ = 0;

  static constexpr uint32_t kIgnoreImmediatePressMs = 250;
};
