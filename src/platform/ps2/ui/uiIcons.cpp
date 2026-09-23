/*
 * Embedded IIF1 controller-icon atlas.
 *
 * ui_icons.iif is bin2c'd by the Makefile. Runtime parsing is intentionally
 * tiny: IIF1 is just a 16-byte header followed by GS-native pixel data.
 */
#include <stdio.h>

#include "types.h"
#include "texture.h"
#include "poly.h"
#include "uiIcons.h"

extern "C" {
#include "gs.h"
}

#include "ui_icons_iif.h"

#define UI_ICONS_W       128
#define UI_ICONS_H       16
#define UI_ICONS_CELL    16
#define UI_ICONS_PSM     GS_PSMCT32
#define UI_ICONS_BYTES   (UI_ICONS_W * UI_ICONS_H * 4)

static TextureT s_UiIconsTex;
static Bool s_UiIconsReady = FALSE;

static Uint32 _UiIconsRead32(const unsigned char *p)
{
    return (Uint32)p[0]
         | ((Uint32)p[1] << 8)
         | ((Uint32)p[2] << 16)
         | ((Uint32)p[3] << 24);
}

Uint32 UiIconsGetVramSize()
{
    return UI_ICONS_BYTES;
}

Bool UiIconsInit(Uint32 uVramTBP)
{
    Uint32 width;
    Uint32 height;
    Uint32 psm;

    s_UiIconsReady = FALSE;

    if (size_ui_icons_iif < 16 ||
        ui_icons_iif[0] != 'I' || ui_icons_iif[1] != 'I' ||
        ui_icons_iif[2] != 'F' || ui_icons_iif[3] != '1')
    {
        printf("[ui-icons] invalid IIF1 header\n");
        return FALSE;
    }

    width = _UiIconsRead32(ui_icons_iif + 4);
    height = _UiIconsRead32(ui_icons_iif + 8);
    psm = _UiIconsRead32(ui_icons_iif + 12);

    if (width != UI_ICONS_W || height != UI_ICONS_H ||
        psm != UI_ICONS_PSM ||
        size_ui_icons_iif != 16U + UI_ICONS_BYTES)
    {
        printf("[ui-icons] unexpected IIF1 %ux%u psm=%u bytes=%u\n",
               (unsigned)width, (unsigned)height, (unsigned)psm,
               (unsigned)size_ui_icons_iif);
        return FALSE;
    }

    TextureNew(&s_UiIconsTex, width, height, UI_ICONS_PSM);
    TextureSetAddr(&s_UiIconsTex, uVramTBP);
    TextureSetFilter(&s_UiIconsTex, 0);
    TextureUpload(&s_UiIconsTex, (Uint8 *)(ui_icons_iif + 16));

    s_UiIconsReady = TRUE;
    return TRUE;
}

void UiIconsDraw(UiIconE eIcon, Int32 x, Int32 y, Int32 size)
{
    Int32 u;

    if (!s_UiIconsReady || eIcon < 0 || eIcon >= UI_ICON_COUNT || size <= 0)
        return;

    u = ((Int32)eIcon) * UI_ICONS_CELL;

    PolyTexture(&s_UiIconsTex);
    PolyUV(u, 0, UI_ICONS_CELL, UI_ICONS_CELL);
    PolyColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    PolyBlend(TRUE);
    PolyRect((Float32)x, (Float32)y, (Float32)size, (Float32)size);
    PolyTexture(NULL);
}
