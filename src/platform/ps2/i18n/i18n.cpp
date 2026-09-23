/*
 * SNESticle Revive runtime localization.
 *
 * Simplified-Chinese rendering/translation infrastructure is based on the
 * localization work from github.com/1247847495/SNESticleRevive, credited
 * there to anyi.  This branch keeps the current SNESticle Revive UI and
 * provides a runtime language layer instead of replacing it with the fork.
 */
#include "types.h"
#include "i18n.h"

static Int32 s_I18nLanguage = I18N_ENGLISH;

static const Char *s_I18nLanguageNames[I18N_LANGUAGE_COUNT] =
{
    "English",
    "Portugues (Brasil)",
    "Espanol",
    "简体中文"
};

static const Char *s_I18nText[I18N_LANGUAGE_COUNT][I18N_TEXT_COUNT] =
{
    {
        "CONFIGURATIONS", "Screen", "Video Mode", "Widescreen",
        "SNES Colors", "Filter", "Scanlines", "Overscan", "Offset X",
        "Offset Y", "Interface", "Cover Art", "Language", "Audio",
        "Game Volume", "Menu Music", "Frequency", "Performance",
        "Frameskip", "Storage / Devices", "Mass / USB", "HDD Support",
        "MMCE Cards", "MX4SIO (SD)", "HostFS (Emu)", "SMB (Network)",
        "On", "Off", "Original", "Composite", "Smooth", "Sharp",
        "Select", "Change", "Reset", "Save", "Restart required",
        "Restart", "Driver Error", "Slot 1", "Slot 2", "Slots 1+2",
        "Not Found", "Enabled", "Searching", "No Track"
    },
    {
        "CONFIGURACOES", "Tela", "Modo de Video", "Tela Larga",
        "Cores SNES", "Filtro", "Scanlines", "Overscan", "Ajuste X",
        "Ajuste Y", "Interface", "Capas", "Idioma", "Audio",
        "Volume do Jogo", "Musica do Menu", "Frequencia", "Desempenho",
        "Pular Quadros", "Armazenamento / Dispositivos", "Mass / USB",
        "Suporte HDD", "Cartoes MMCE", "MX4SIO (SD)", "HostFS (Emu)",
        "SMB (Rede)", "Ligado", "Desligado", "Original", "Composto",
        "Suave", "Nitido", "Selecionar", "Alterar", "Padrao", "Salvar",
        "Reinicio necessario", "Reiniciar", "Erro do Driver", "Slot 1",
        "Slot 2", "Slots 1+2", "Nao encontrado", "Ativado", "Procurando",
        "Sem musica"
    },
    {
        "CONFIGURACION", "Pantalla", "Modo de Video", "Pantalla Ancha",
        "Colores SNES", "Filtro", "Scanlines", "Overscan", "Ajuste X",
        "Ajuste Y", "Interfaz", "Caratulas", "Idioma", "Audio",
        "Volumen del Juego", "Musica del Menu", "Frecuencia", "Rendimiento",
        "Salto de Cuadros", "Almacenamiento / Dispositivos", "Mass / USB",
        "Soporte HDD", "Tarjetas MMCE", "MX4SIO (SD)", "HostFS (Emu)",
        "SMB (Red)", "Activado", "Desactivado", "Original", "Compuesto",
        "Suave", "Nitido", "Seleccionar", "Cambiar", "Restablecer", "Guardar",
        "Reinicio necesario", "Reiniciar", "Error de Driver", "Slot 1",
        "Slot 2", "Slots 1+2", "No encontrado", "Habilitado", "Buscando",
        "Sin musica"
    },
    {
        "配置", "屏幕", "视频模式", "宽屏", "SNES 色彩", "滤镜",
        "扫描线", "过扫描", "X 偏移", "Y 偏移", "界面", "封面",
        "语言", "音频", "游戏音量", "菜单音乐", "频率", "性能",
        "跳帧", "存储 / 设备", "USB 存储", "HDD 支持", "MMCE 卡",
        "MX4SIO (SD)", "HostFS (模拟器)", "SMB (网络)", "开", "关",
        "原始", "复合", "平滑", "锐利", "选择", "更改", "重置",
        "保存", "需要重启", "重启", "驱动错误", "插槽 1", "插槽 2",
        "插槽 1+2", "未找到", "已启用", "搜索中", "无音乐"
    }
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

const Char *I18nGetText(I18nTextE text)
{
    if (text < 0 || text >= I18N_TEXT_COUNT)
        return "";
    return s_I18nText[s_I18nLanguage][text];
}
