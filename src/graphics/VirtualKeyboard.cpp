#include "configuration.h"
#if HAS_SCREEN
#include "VirtualKeyboard.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include "main.h"
#include <Arduino.h>
#include <cstring>
#include <vector>

namespace graphics
{

namespace
{
// Compact per-key definition used to build the runtime `keyboard` grid.
// label/alt are UTF-8 C strings so non-ASCII (Cyrillic) keys render through
// the normal UTF-8 -> font-table pipeline (OLEDDisplay::drawString) instead
// of needing raw single-byte codepage tricks.
struct KeyDef {
    const char *label; // inserted/shown text; nullptr for action keys (BACK/ENTER/SPACE/ESC)
    VirtualKeyType type;
    const char *alt; // long-press alternate (nullptr = none)
};

// OLEDDisplay::getStringWidth(const char*) implicitly converts to the String
// overload, which measures with utf8=false -- each byte of a multi-byte UTF-8
// character (e.g. Cyrillic) is then treated as its own glyph, inflating the
// result. drawString() itself decodes UTF-8 correctly; measuring must match it.
uint16_t utf8StringWidth(OLEDDisplay *display, const char *text)
{
    return display->getStringWidth(text, (uint16_t)strlen(text), true);
}
} // namespace

VirtualKeyboard::VirtualKeyboard() : cursorRow(0), cursorCol(0), lastActivityTime(millis())
{
    initializeKeyboard();
    // Set cursor to H(2, 5)
    cursorRow = 2;
    cursorCol = 5;
}

VirtualKeyboard::~VirtualKeyboard() {}

void VirtualKeyboard::initializeKeyboard()
{
    // English (QWERTY), 4 rows x 12 columns.
    static const KeyDef LAYOUT_EN[KEYBOARD_ROWS][KEYBOARD_COLS] = {
        {{"1", VK_CHAR, nullptr},
         {"2", VK_CHAR, nullptr},
         {"3", VK_CHAR, nullptr},
         {"4", VK_CHAR, nullptr},
         {"5", VK_CHAR, nullptr},
         {"6", VK_CHAR, nullptr},
         {"7", VK_CHAR, nullptr},
         {"8", VK_CHAR, nullptr},
         {"9", VK_CHAR, nullptr},
         {"0", VK_CHAR, nullptr},
         {"'", VK_CHAR, nullptr},
         {nullptr, VK_BACKSPACE, nullptr}},
        {{"q", VK_CHAR, "Q"},
         {"w", VK_CHAR, "W"},
         {"e", VK_CHAR, "E"},
         {"r", VK_CHAR, "R"},
         {"t", VK_CHAR, "T"},
         {"y", VK_CHAR, "Y"},
         {"u", VK_CHAR, "U"},
         {"i", VK_CHAR, "I"},
         {"o", VK_CHAR, "O"},
         {"p", VK_CHAR, "P"},
         {"-", VK_CHAR, nullptr},
         {nullptr, VK_ENTER, nullptr}},
        {{"a", VK_CHAR, "A"},
         {"s", VK_CHAR, "S"},
         {"d", VK_CHAR, "D"},
         {"f", VK_CHAR, "F"},
         {"g", VK_CHAR, "G"},
         {"h", VK_CHAR, "H"},
         {"j", VK_CHAR, "J"},
         {"k", VK_CHAR, "K"},
         {"l", VK_CHAR, "L"},
         {";", VK_CHAR, nullptr},
         {"_", VK_CHAR, nullptr},
         {nullptr, VK_SPACE, nullptr}},
        {{"z", VK_CHAR, "Z"},
         {"x", VK_CHAR, "X"},
         {"c", VK_CHAR, "C"},
         {"v", VK_CHAR, "V"},
         {"b", VK_CHAR, "B"},
         {"n", VK_CHAR, "N"},
         {"m", VK_CHAR, "M"},
         {".", VK_CHAR, nullptr},
         {",", VK_CHAR, nullptr},
         {"?", VK_CHAR, "/"},
         {"!", VK_CHAR, nullptr},
         {nullptr, VK_ESC, nullptr}}};

    // Ukrainian (JCUKEN), 4 rows x 12 columns. Letters are plain UTF-8 string
    // literals so they render through the normal UTF-8 font pipeline (needs
    // OLED_UA so FONT_SMALL/etc resolve to the Cyrillic-capable font).
    // Omits the rare letter "ge with upturn" (~50 native words); approximate with the plain "ge".
    static const KeyDef LAYOUT_UA[KEYBOARD_ROWS][KEYBOARD_COLS] = {
        {{"1", VK_CHAR, nullptr},
         {"2", VK_CHAR, nullptr},
         {"3", VK_CHAR, nullptr},
         {"4", VK_CHAR, nullptr},
         {"5", VK_CHAR, nullptr},
         {"6", VK_CHAR, nullptr},
         {"7", VK_CHAR, nullptr},
         {"8", VK_CHAR, nullptr},
         {"9", VK_CHAR, nullptr},
         {"0", VK_CHAR, nullptr},
         {"'", VK_CHAR, nullptr},
         {nullptr, VK_BACKSPACE, nullptr}},
        {{"й", VK_CHAR, "Й"},
         {"ц", VK_CHAR, "Ц"},
         {"у", VK_CHAR, "У"},
         {"к", VK_CHAR, "К"},
         {"е", VK_CHAR, "Е"},
         {"н", VK_CHAR, "Н"},
         {"г", VK_CHAR, "Г"},
         {"ш", VK_CHAR, "Ш"},
         {"щ", VK_CHAR, "Щ"},
         {"з", VK_CHAR, "З"},
         {"х", VK_CHAR, "Х"},
         {nullptr, VK_ENTER, nullptr}},
        {{"ф", VK_CHAR, "Ф"},
         {"і", VK_CHAR, "І"},
         {"в", VK_CHAR, "В"},
         {"а", VK_CHAR, "А"},
         {"п", VK_CHAR, "П"},
         {"р", VK_CHAR, "Р"},
         {"о", VK_CHAR, "О"},
         {"л", VK_CHAR, "Л"},
         {"д", VK_CHAR, "Д"},
         {"ж", VK_CHAR, "Ж"},
         {"є", VK_CHAR, "Є"},
         {nullptr, VK_SPACE, nullptr}},
        {{"я", VK_CHAR, "Я"},
         {"ч", VK_CHAR, "Ч"},
         {"с", VK_CHAR, "С"},
         {"м", VK_CHAR, "М"},
         {"и", VK_CHAR, "И"},
         {"т", VK_CHAR, "Т"},
         {"ь", VK_CHAR, "Ь"},
         {"б", VK_CHAR, "Б"},
         {"ю", VK_CHAR, "Ю"},
         {",", VK_CHAR, "."},
         {"ї", VK_CHAR, "Ї"},
         {nullptr, VK_ESC, nullptr}}};

    const KeyDef(*LAYOUT)[KEYBOARD_COLS] = (currentLayout == KB_LAYOUT_UA) ? LAYOUT_UA : LAYOUT_EN;

    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        for (int col = 0; col < KEYBOARD_COLS; col++) {
            const KeyDef &def = LAYOUT[row][col];
            keyboard[row][col] = {def.label, def.alt, def.type};
        }
    }
}

void VirtualKeyboard::draw(OLEDDisplay *display, int16_t offsetX, int16_t offsetY)
{
    // Repeat ticking is driven by NotificationRenderer once per frame
    // Base styles
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    // Screen geometry
    const int screenW = display->getWidth();
    const int screenH = display->getHeight();

    // Decide wide-screen mode: if there is comfortable width, allow taller keys and reserve fixed width for last column labels
    // Heuristic: if screen width >= 200px (e.g., 240x135), treat as wide
    const bool isWide = screenW >= 200;

    // Determine last-column label max width
    display->setFont(FONT_SMALL);
    const int wENTER = utf8StringWidth(display, "ENTER");
    int lastColLabelW = wENTER; // ENTER is usually the widest
    // Smaller padding on very small screens to avoid excessive whitespace
    const int lastColPad = (screenW <= 128 ? 2 : 6);
    const int reservedLastColW = lastColLabelW + lastColPad; // reserved width for last column keys

    // Always reserve width for the rightmost text column to avoid overlap on small screens
    int cellW = 0;
    int leftoverW = 0;
    {
        const int leftCols = KEYBOARD_COLS - 1; // 10 input characters
        int usableW = screenW - reservedLastColW;
        if (usableW < leftCols) {
            // Guard: ensure at least 1px per left cell if labels are extremely wide (unlikely)
            usableW = leftCols;
        }
        cellW = usableW / leftCols;
        leftoverW = usableW - cellW * leftCols; // distribute extra pixels over left columns (left to right)
    }

    // Dynamic key geometry
    int cellH = KEY_HEIGHT;
    int keyboardStartY = 0;
    if (screenH <= 64) {
        const int headerHeight = headerText.empty() ? 0 : (FONT_HEIGHT_SMALL - 2);
        const int gapBelowHeader = 0;
        const int singleLineBoxHeight = FONT_HEIGHT_SMALL;
        const int gapAboveKeyboard = 0;
        keyboardStartY = offsetY + headerHeight + gapBelowHeader + singleLineBoxHeight + gapAboveKeyboard;
        if (keyboardStartY < 0)
            keyboardStartY = 0;
        if (keyboardStartY > screenH)
            keyboardStartY = screenH;
        int keyboardHeight = screenH - keyboardStartY;
        cellH = std::max(1, keyboardHeight / KEYBOARD_ROWS);
    } else if (isWide) {
        // For wide screens (e.g., T114 240x135), prefer square keys: height equals left-column key width.
        cellH = std::max((int)KEY_HEIGHT, cellW);

        // Guarantee at least 2 lines of input are visible by reducing cell height minimally if needed.
        // Replicate the spacing used in drawInputArea(): headerGap=1, box-to-header gap=1, gap above keyboard=1
        display->setFont(FONT_SMALL);
        const int headerHeight = headerText.empty() ? 0 : (FONT_HEIGHT_SMALL + 1);
        const int headerToBoxGap = 1;
        const int gapAboveKb = 1;
        const int minBoxHeightForTwoLines = 2 * FONT_HEIGHT_SMALL + 2; // inner 1px top/bottom
        int maxKeyboardHeight = screenH - (offsetY + headerHeight + headerToBoxGap + minBoxHeightForTwoLines + gapAboveKb);
        int maxCellHAllowed = maxKeyboardHeight / KEYBOARD_ROWS;
        if (maxCellHAllowed < (int)KEY_HEIGHT)
            maxCellHAllowed = KEY_HEIGHT;
        if (maxCellHAllowed > 0 && cellH > maxCellHAllowed) {
            cellH = maxCellHAllowed;
        }
        // Keyboard placement from bottom for wide screens
        int keyboardHeight = KEYBOARD_ROWS * cellH;
        keyboardStartY = screenH - keyboardHeight;
        if (keyboardStartY < 0)
            keyboardStartY = 0;
    } else {
        // Default (non-wide, non-64px) e.g. SH1107 128x128:
        // cellH = FONT_HEIGHT_SMALL - 2 so rows are tighter while still hosting the font
        cellH = std::max((int)KEY_HEIGHT, FONT_HEIGHT_SMALL - 2);
        int keyboardHeight = KEYBOARD_ROWS * cellH;
        keyboardStartY = screenH - keyboardHeight;
        if (keyboardStartY < 0)
            keyboardStartY = 0;
    }

    // Draw input area above keyboard
    drawInputArea(display, offsetX, offsetY, keyboardStartY);

    // Precompute per-column x and width with leftover distributed over left columns for even spacing
    int colX[KEYBOARD_COLS];
    int colW[KEYBOARD_COLS];
    int runningX = offsetX;
    for (int col = 0; col < KEYBOARD_COLS - 1; ++col) {
        int wcol = cellW + (col < leftoverW ? 1 : 0);
        colX[col] = runningX;
        colW[col] = wcol;
        runningX += wcol;
    }
    // Last column
    colX[KEYBOARD_COLS - 1] = runningX;
    colW[KEYBOARD_COLS - 1] = reservedLastColW;

    // Draw keyboard grid
    for (int row = 0; row < KEYBOARD_ROWS; row++) {
        for (int col = 0; col < KEYBOARD_COLS; col++) {
            const VirtualKey &k = keyboard[row][col];
            if (k.label != nullptr || k.type != VK_CHAR) {
                const bool isLastCol = (col == KEYBOARD_COLS - 1);
                int x = colX[col];
                int w = colW[col];
                int y = offsetY + keyboardStartY + row * cellH;
                int h = cellH;
                bool selected = (row == cursorRow && col == cursorCol);
                drawKey(display, k, selected, x, y, (uint8_t)w, (uint8_t)h, isLastCol);
            }
        }
    }
}

void VirtualKeyboard::drawInputArea(OLEDDisplay *display, int16_t offsetX, int16_t offsetY, int16_t keyboardStartY)
{
    display->setColor(WHITE);

    const int screenWidth = display->getWidth();
    const int screenHeight = display->getHeight();
    // Use the standard small font metrics for input box sizing (restore original size)
    const int inputLineH = FONT_HEIGHT_SMALL;

    // Header uses the standard small (which may be larger on big screens)
    display->setFont(FONT_SMALL);
    int headerHeight = 0;
    if (!headerText.empty()) {
        // Draw header and reserve exact font height (plus a tighter gap) to maximize input area
        display->drawString(offsetX + 2, offsetY, headerText.c_str());
        if (screenHeight <= 64) {
            headerHeight = FONT_HEIGHT_SMALL - 2; // 11px
        } else {
            headerHeight = FONT_HEIGHT_SMALL; // no extra padding baked in
        }
    }

    const int boxX = offsetX;
    const int boxWidth = screenWidth;
    int boxY;
    int boxHeight;
    if (screenHeight <= 64) {
        const int gapBelowHeader = 0;
        const int fixedBoxHeight = inputLineH;
        const int gapAboveKeyboard = 0;
        boxY = offsetY + headerHeight + gapBelowHeader;
        boxHeight = fixedBoxHeight;
        if (boxY + boxHeight + gapAboveKeyboard > keyboardStartY) {
            int over = boxY + boxHeight + gapAboveKeyboard - keyboardStartY;
            boxHeight = std::max(1, fixedBoxHeight - over);
        }
    } else {
        const int gapBelowHeader = 1;
        int gapAboveKeyboard = 1;
        int tmpBoxY = offsetY + headerHeight + gapBelowHeader;
        const int minBoxHeight = inputLineH + 2;
        int availableH = keyboardStartY - tmpBoxY - gapAboveKeyboard;
        if (availableH < minBoxHeight)
            availableH = minBoxHeight;
        boxY = tmpBoxY;
        boxHeight = availableH;
    }

    // Draw box border
    display->drawRect(boxX, boxY, boxWidth, boxHeight);

    display->setFont(FONT_SMALL);

    // Text rendering: multi-line if space allows (>= 2 lines), else single-line with leading ellipsis
    const int textX = boxX + 2;
    const int maxTextWidth = boxWidth - 4;
    const int maxLines = (boxHeight - 2) / inputLineH;
    if (maxLines >= 2) {
        // Inner bounds for caret clamping
        const int innerLeft = boxX + 1;
        const int innerRight = boxX + boxWidth - 2;
        const int innerTop = boxY + 1;
        const int innerBottom = boxY + boxHeight - 2;

        // Wrap text greedily into lines that fit maxTextWidth
        std::vector<std::string> lines;
        {
            std::string remaining = inputText;
            while (!remaining.empty()) {
                int bestLen = 0;
                for (int len = 1; len <= (int)remaining.size(); ++len) {
                    int w = utf8StringWidth(display, remaining.substr(0, len).c_str());
                    if (w <= maxTextWidth)
                        bestLen = len;
                    else
                        break;
                }
                if (bestLen == 0) {
                    // At least show one character to make progress
                    bestLen = 1;
                }
                lines.emplace_back(remaining.substr(0, bestLen));
                remaining.erase(0, bestLen);
            }
        }

        const bool scrolledUp = ((int)lines.size() > maxLines);
        int caretX = textX;
        int caretY = innerTop;

        // Leave a small top gap to render '...' without replacing the first line
        const int topInset = 2;
        const int lineStep = std::max(1, inputLineH - 1); // slightly tighter than font height
        int lineY = innerTop + topInset;

        if (scrolledUp) {
            // Draw three small dots centered horizontally, vertically at the midpoint of the gap
            // between the inner top and the first line's top baseline. This avoids using a tall glyph.
            const int firstLineTop = lineY;                                   // baseline top for the first visible line
            const int gapMidY = innerTop + (firstLineTop - innerTop) / 2 + 1; // shift down 1px as requested
            const int centerX = boxX + boxWidth / 2;
            const int dotSpacing = 3; // px between dots
            const int dotSize = 1;    // small square dot
            display->fillRect(centerX - dotSpacing, gapMidY, dotSize, dotSize);
            display->fillRect(centerX, gapMidY, dotSize, dotSize);
            display->fillRect(centerX + dotSpacing, gapMidY, dotSize, dotSize);
        }

        // How many lines fit with our top inset and tighter step
        const int linesCapacity = std::max(1, (innerBottom - lineY + 1) / lineStep);
        const int linesToShow = std::min((int)lines.size(), linesCapacity);
        const int startIndex = scrolledUp ? ((int)lines.size() - linesToShow) : 0;

        for (int i = 0; i < linesToShow; ++i) {
            const std::string &chunk = lines[startIndex + i];
            display->drawString(textX, lineY, chunk.c_str());
            caretX = textX + utf8StringWidth(display, chunk.c_str());
            caretY = lineY;
            lineY += lineStep;
        }

        // Draw caret at end of the last visible line
        int caretPadY = 2;
        if (boxHeight >= inputLineH + 4)
            caretPadY = 3;
        int cursorTop = caretY + caretPadY;
        // Use lineStep so caret height matches the row spacing
        int cursorH = lineStep - caretPadY * 2;
        if (cursorH < 1)
            cursorH = 1;
        // Clamp vertical bounds to stay inside the inner rect
        if (cursorTop < innerTop)
            cursorTop = innerTop;
        if (cursorTop + cursorH - 1 > innerBottom)
            cursorH = innerBottom - cursorTop + 1;
        if (cursorH < 1)
            cursorH = 1;
        // Only draw if cursor is inside inner bounds
        if (caretX >= innerLeft && caretX <= innerRight) {
            display->drawVerticalLine(caretX, cursorTop, cursorH);
        }
    } else {
        std::string displayText = inputText;
        int textW = utf8StringWidth(display, displayText.c_str());
        std::string scrolled = displayText;
        if (textW > maxTextWidth) {
            // Trim from the left until it fits
            while (textW > maxTextWidth && !scrolled.empty()) {
                scrolled.erase(0, 1);
                textW = utf8StringWidth(display, scrolled.c_str());
            }
            // Add leading ellipsis and ensure it still fits
            if (scrolled != displayText) {
                scrolled = "..." + scrolled;
                textW = utf8StringWidth(display, scrolled.c_str());
                // If adding ellipsis causes overflow, trim more after the ellipsis
                while (textW > maxTextWidth && scrolled.size() > 3) {
                    scrolled.erase(3, 1); // remove chars after the ellipsis
                    textW = utf8StringWidth(display, scrolled.c_str());
                }
            }
        } else {
            // Keep textW in sync with what we draw
            textW = utf8StringWidth(display, scrolled.c_str());
        }

        int textY;
        if (screenHeight <= 64) {
            textY = boxY + (boxHeight - inputLineH) / 2;
        } else {
            const int innerTop = boxY + 1;
            const int innerBottom = boxY + boxHeight - 2;

            // Center text vertically within inner box for single-line, then clamp so it never overlaps borders
            int innerH = innerBottom - innerTop + 1;
            textY = innerTop + std::max(0, (innerH - inputLineH) / 2);
            // Clamp fully inside the inner rect
            if (textY < innerTop)
                textY = innerTop;
            int maxTop = innerBottom - inputLineH + 1;
            if (textY > maxTop)
                textY = maxTop;
        }

        if (!scrolled.empty()) {
            display->drawString(textX, textY, scrolled.c_str());
        }

        int cursorX = textX + textW;
        if (screenHeight > 64) {
            const int innerRight = boxX + boxWidth - 2;
            if (cursorX > innerRight)
                cursorX = innerRight;
        }

        int cursorTop, cursorH;
        if (screenHeight <= 64) {
            cursorH = 10;
            cursorTop = boxY + (boxHeight - cursorH) / 2;
        } else {
            const int innerLeft = boxX + 1;
            const int innerRight = boxX + boxWidth - 2;
            const int innerTop = boxY + 1;
            const int innerBottom = boxY + boxHeight - 2;

            cursorTop = boxY + 2;
            cursorH = boxHeight - 4;
            if (cursorH < 1)
                cursorH = 1;
            if (cursorTop < innerTop)
                cursorTop = innerTop;
            if (cursorTop + cursorH - 1 > innerBottom)
                cursorH = innerBottom - cursorTop + 1;
            if (cursorH < 1)
                cursorH = 1;

            if (cursorX < innerLeft || cursorX > innerRight)
                return;
        }

        display->drawVerticalLine(cursorX, cursorTop, cursorH);
    }
}

void VirtualKeyboard::drawKey(OLEDDisplay *display, const VirtualKey &key, bool selected, int16_t x, int16_t y, uint8_t width,
                              uint8_t height, bool isLastCol)
{
    // Draw key content
    display->setFont(FONT_SMALL);
    const int fontH = FONT_HEIGHT_SMALL;
    // Build label and metrics first
    std::string keyText;
    if (key.type == VK_BACKSPACE || key.type == VK_ENTER || key.type == VK_SPACE || key.type == VK_ESC) {
        // Keep literal text labels for the action keys on the rightmost column
        keyText = (key.type == VK_BACKSPACE) ? "BACK"
                  : (key.type == VK_ENTER)   ? "ENTER"
                  : (key.type == VK_SPACE)   ? "SPACE"
                  : (key.type == VK_ESC)     ? "ESC"
                                             : "";
    } else if (key.label && strcmp(key.label, "?") == 0) {
        // Show the common "/" pairing next to "?" like on a real keyboard
        keyText = "?/";
    } else if (key.altLabel && currentLayout != KB_LAYOUT_UA) {
        // Keycaps show the long-press alternate (typically uppercase), like a real keyboard.
        // Skipped for Ukrainian: this compact font's uppercase Cyrillic glyphs are wider than
        // their advance, so they overflow the narrow key cells (e.g. "Й", "Ї").
        keyText = key.altLabel;
    } else {
        keyText = key.label ? key.label : "";
    }

    int textWidth = utf8StringWidth(display, keyText.c_str());
    // Label alignment
    // - Rightmost action column: right-align text with a small right padding (~2px) so it hugs screen edge neatly.
    // - Other keys: center horizontally; use ceil-style rounding to avoid appearing left-biased on odd widths.
    int textX;
    if (isLastCol) {
        const int rightPad = 1;
        textX = x + width - textWidth - rightPad;
        if (textX < x)
            textX = x; // guard
    } else {
        // Use ceil rounding for all screens (consistent with 128x64 behavior for numbers)
        textX = x + (width - textWidth + 1) / 2;
    }
    int contentTop = y;
    int contentH = height;
    if (selected) {
        display->setColor(WHITE);
        bool isAction = (key.type == VK_BACKSPACE || key.type == VK_ENTER || key.type == VK_SPACE || key.type == VK_ESC);

        if (display->getHeight() <= 64 && !isAction) {
            display->fillRect(x, y, width, height);
        } else if (isAction) {
            const int padX = 1;
            const int padY = 2;
            int hlW = textWidth + padX * 2;
            int hlX = textX - padX;

            if (hlX < x) {
                hlW -= (x - hlX);
                hlX = x;
            }
            int maxW = (x + width) - hlX;
            if (hlW > maxW)
                hlW = maxW;
            if (hlW < 1)
                hlW = 1;

            int hlH = std::min(fontH + padY * 2, (int)height);
            int hlY = y + (height - hlH) / 2;
            display->fillRect(hlX, hlY, hlW, hlH);
            contentTop = hlY;
            contentH = hlH;
        } else {
            display->fillRect(x, y, width, height);
        }
        display->setColor(BLACK);
    } else {
        display->setColor(WHITE);
    }

    int centeredTextY;
    if (display->getHeight() <= 64) {
        centeredTextY = y + (height - fontH) / 2;
    } else {
        centeredTextY = contentTop + (contentH - fontH) / 2;
    }
    if (display->getHeight() > 64) {
        if (centeredTextY < contentTop)
            centeredTextY = contentTop;
        if (centeredTextY + fontH > contentTop + contentH)
            centeredTextY = std::max(contentTop, contentTop + contentH - fontH);
    }

    if (display->getHeight() <= 64 && keyText.size() == 1) {
        char ch = keyText[0];
        if (ch == '.' || ch == ',' || ch == ';') {
            centeredTextY -= 1;
        }
    }
#ifdef MUZI_BASE // Correct issue with character vertical position on MUZI_BASE
    centeredTextY -= 2;
#endif
    display->drawString(textX, centeredTextY, keyText.c_str());
}

const char *VirtualKeyboard::getLabelForKey(const VirtualKey &key, bool isLongPress)
{
    if (key.type != VK_CHAR) {
        return nullptr;
    }
    if (isLongPress && key.altLabel) {
        return key.altLabel;
    }
    return key.label;
}

void VirtualKeyboard::switchLayout()
{
    currentLayout = (currentLayout == KB_LAYOUT_EN) ? KB_LAYOUT_UA : KB_LAYOUT_EN;
    initializeKeyboard();
    resetTimeout();
}

void VirtualKeyboard::moveCursorDelta(int dRow, int dCol)
{
    resetTimeout();
    // wrap around rows and cols in the 4x11 grid
    int r = (int)cursorRow + dRow;
    int c = (int)cursorCol + dCol;
    if (r < 0)
        r = KEYBOARD_ROWS - 1;
    else if (r >= KEYBOARD_ROWS)
        r = 0;
    if (c < 0)
        c = KEYBOARD_COLS - 1;
    else if (c >= KEYBOARD_COLS)
        c = 0;
    cursorRow = (uint8_t)r;
    cursorCol = (uint8_t)c;
}

void VirtualKeyboard::moveCursorUp()
{
    moveCursorDelta(-1, 0);
}
void VirtualKeyboard::moveCursorDown()
{
    moveCursorDelta(1, 0);
}
void VirtualKeyboard::moveCursorLeft()
{
    resetTimeout();

    if (cursorCol > 0) {
        cursorCol--;
    } else {
        if (cursorRow > 0) {
            cursorRow--;
            cursorCol = KEYBOARD_COLS - 1;
        } else {
            cursorRow = KEYBOARD_ROWS - 1;
            cursorCol = KEYBOARD_COLS - 1;
        }
    }
}
void VirtualKeyboard::moveCursorRight()
{
    resetTimeout();

    if (cursorCol < KEYBOARD_COLS - 1) {
        cursorCol++;
    } else {
        if (cursorRow < KEYBOARD_ROWS - 1) {
            cursorRow++;
            cursorCol = 0;
        } else {
            cursorRow = 0;
            cursorCol = 0;
        }
    }
}

void VirtualKeyboard::handlePress()
{
    resetTimeout(); // Reset timeout on any input activity

    const VirtualKey &key = keyboard[cursorRow][cursorCol];

    // Don't handle press if the key is empty (but allow special keys)
    if (key.type == VK_CHAR && !key.label) {
        return;
    }

    // For character keys, insert the base (lowercase/normal) text
    if (key.type == VK_CHAR) {
        insertText(key.label);
        return;
    }

    // Handle non-character keys immediately
    switch (key.type) {
    case VK_BACKSPACE:
        deleteCharacter();
        break;
    case VK_ENTER:
        submitText();
        break;
    case VK_SPACE:
        insertText(" ");
        break;
    case VK_ESC:
        if (onTextEntered) {
            std::function<void(const std::string &)> callback = onTextEntered;
            onTextEntered = nullptr;
            inputText = "";
            callback("");
        }
        return;
    default:
        break;
    }
}

void VirtualKeyboard::handleLongPress()
{
    resetTimeout(); // Reset timeout on any input activity

    const VirtualKey &key = keyboard[cursorRow][cursorCol];

    // Don't handle press if the key is empty (but allow special keys)
    if (key.type == VK_CHAR && !key.label) {
        return;
    }

    // For character keys, insert the uppercase/alternate text (falls back to base text)
    if (key.type == VK_CHAR) {
        insertText(key.altLabel ? key.altLabel : key.label);
        return;
    }

    switch (key.type) {
    case VK_BACKSPACE:
        // One-shot: delete up to 5 characters on long press
        for (int i = 0; i < 5; ++i) {
            if (inputText.empty())
                break;
            deleteCharacter();
        }
        break;
    case VK_ENTER:
        submitText();
        break;
    case VK_SPACE:
        insertText(" ");
        break;
    case VK_ESC:
        if (onTextEntered) {
            onTextEntered("");
        }
        break;
    default:
        break;
    }
}

void VirtualKeyboard::insertText(const char *text)
{
    if (!text) {
        return;
    }
    if (inputText.length() < 160) { // Reasonable text length limit
        inputText += text;
    }
}

void VirtualKeyboard::deleteCharacter()
{
    if (!inputText.empty()) {
        inputText.pop_back();
    }
}

void VirtualKeyboard::submitText()
{
    LOG_INFO("Virtual keyboard: submitting text '%s'", inputText.c_str());

    // Only submit if text is not empty
    if (!inputText.empty() && onTextEntered) {
        // Store callback and text to submit before clearing callback
        std::function<void(const std::string &)> callback = onTextEntered;
        std::string textToSubmit = inputText;
        onTextEntered = nullptr;
        // Don't clear inputText here - let the calling module handle cleanup
        // inputText = "";  // Removed: keep text visible until module cleans up
        callback(textToSubmit);
    } else if (inputText.empty()) {
        // For empty text, just ignore the submission - don't clear callback
        // This keeps the virtual keyboard responsive for further input
        LOG_INFO("Virtual keyboard: empty text submitted, ignoring - keyboard remains active");
    } else {
        // No callback available
        if (screen) {
            screen->setFrames(graphics::Screen::FOCUS_PRESERVE);
        }
    }
}

void VirtualKeyboard::setInputText(const std::string &text)
{
    inputText = text;
}

std::string VirtualKeyboard::getInputText() const
{
    return inputText;
}

void VirtualKeyboard::setHeader(const std::string &header)
{
    headerText = header;
}

void VirtualKeyboard::setCallback(std::function<void(const std::string &)> callback)
{
    onTextEntered = callback;
}

void VirtualKeyboard::resetTimeout()
{
    lastActivityTime = millis();
}

bool VirtualKeyboard::isTimedOut() const
{
    return (millis() - lastActivityTime) > TIMEOUT_MS;
}

} // namespace graphics
#endif
