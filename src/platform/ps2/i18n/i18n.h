#ifndef SNESTICLE_I18N_H
#define SNESTICLE_I18N_H

#include "types.h"

typedef enum
{
    I18N_ENGLISH = 0,
    I18N_PORTUGUESE_BR,
    I18N_SPANISH,
    I18N_CHINESE_SIMPLIFIED,
    I18N_LANGUAGE_COUNT
} I18nLanguageE;

void I18nSetLanguage(Int32 language);
Int32 I18nGetLanguage();
void I18nCycleLanguage(Int32 direction);
const Char *I18nGetLanguageName();

#endif
