#ifndef _UICHROME_H
#define _UICHROME_H

#include "types.h"
#include "uiIcons.h"

void UiChromeHeader(const char *title, Bool shoulders);
void UiChromeSection(Int32 y, const char *title);
void UiChromeScroll(Bool canUp, Bool canDown, Int32 x, Int32 yUp, Int32 yDown);
void UiChromeHint(UiIconE a, UiIconE b, Int32 x, Int32 y, const char *text);

#endif
