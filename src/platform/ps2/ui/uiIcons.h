/*
 * Compact controller/UI icon atlas.
 *
 * The source asset is assets/ui_icons.iif: a tiny IIF1 GS texture, matching
 * the native frontend-media style used by the original SNES Station.
 */
#ifndef _UIICONS_H
#define _UIICONS_H

#include "types.h"

typedef enum
{
    UI_ICON_UP = 0,
    UI_ICON_DOWN,
    UI_ICON_LEFT,
    UI_ICON_RIGHT,
    UI_ICON_CROSS,
    UI_ICON_SQUARE,
    UI_ICON_CIRCLE,
    UI_ICON_TRIANGLE,
    UI_ICON_L1,
    UI_ICON_R1,
    UI_ICON_COUNT
} UiIconE;

Uint32 UiIconsGetVramSize();
Bool UiIconsInit(Uint32 uVramTBP);
void UiIconsDraw(UiIconE eIcon, Int32 x, Int32 y, Int32 size);

#endif
