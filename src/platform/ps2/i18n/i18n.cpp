/*
 * SNESticle Revive UI language selection.
 * Chinese font/rendering support is adapted from the localization work by
 * GitHub user 1247847495 (Chinese localization credited as anyi in that fork).
 */
#include "types.h"
#include "i18n.h"

static Int32 s_I18nLanguage = I18N_ENGLISH;

static const Char *s_I18nLanguageNames[I18N_LANGUAGE_COUNT] =
{
    "English",
    "Portugues (Brasil)",
    "Espanol",
    "Simplified Chinese"
};

void I18nSetLanguage(Int32 language)
{
    if (language >= 0 && language < I18N_LANGUAGE_COUNT)
        s_I18nLanguage = language;
}

Int32 I18nGetLanguage()
{
    return s_I18nLanguage;
}

void I18nCycleLanguage(Int32 direction)
{
    s_I18nLanguage += direction < 0 ? -1 : 1;
    if (s_I18nLanguage < 0)
        s_I18nLanguage = I18N_LANGUAGE_COUNT - 1;
    if (s_I18nLanguage >= I18N_LANGUAGE_COUNT)
        s_I18nLanguage = 0;
}

const Char *I18nGetLanguageName()
{
    return s_I18nLanguageNames[s_I18nLanguage];
}
