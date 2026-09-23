#include "types.h"
#include "font.h"
#include "poly.h"
#include "uiIcons.h"
#include "uiChrome.h"

static void _UiChromeCenter(Int32 x, Int32 y, const char *text)
{
    FontPuts(x - FontGetStrWidth(text) / 2, y, text);
}

void UiChromeHeader(const char *title, Bool shoulders)
{
    FontSelect(0);
    PolyTexture(NULL);
    PolyBlend(TRUE);
    PolyColor4f(0.0f, 0.20f, 0.20f, 0.72f);
    PolyRect(28, 9, 200, FontGetHeight() + 4);

    FontColor4f(0.0f, 0.88f, 0.88f, 1.0f);
    _UiChromeCenter(128, 11, title ? title : "");

    if (shoulders)
    {
        UiIconsDraw(UI_ICON_L1, 35, 9, 14);
        UiIconsDraw(UI_ICON_R1, 207, 9, 14);
    }
}

void UiChromeSection(Int32 y, const char *title)
{
    FontSelect(0);
    FontColor4f(1.0f, 0.62f, 0.16f, 1.0f);
    _UiChromeCenter(128, y, title ? title : "");
}

void UiChromeScroll(Bool canUp, Bool canDown, Int32 x, Int32 yUp, Int32 yDown)
{
    if (canUp) UiIconsDraw(UI_ICON_UP, x, yUp, 8);
    if (canDown) UiIconsDraw(UI_ICON_DOWN, x, yDown, 8);
}

void UiChromeHint(UiIconE a, UiIconE b, Int32 x, Int32 y, const char *text)
{
    Int32 cursor = x;
    if (a < UI_ICON_COUNT)
    {
        UiIconsDraw(a, cursor, y - 1, 10);
        cursor += 11;
    }
    if (b < UI_ICON_COUNT)
    {
        UiIconsDraw(b, cursor, y - 1, 10);
        cursor += 11;
    }

    FontSelect(0);
    FontColor4f(0.66f, 0.66f, 0.66f, 1.0f);
    FontPuts(cursor + 2, y, text ? text : "");
}
