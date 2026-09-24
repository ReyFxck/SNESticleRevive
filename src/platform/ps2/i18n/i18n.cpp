/*
 * SNESticle Revive runtime localization.
 *
 * Simplified-Chinese rendering/translation infrastructure is based on the
 * localization work from github.com/1247847495/SNESticleRevive, credited
 * there to anyi. This implementation keeps the current Revive UI and adds
 * runtime translation instead of replacing screens with the older fork UI.
 */
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "i18n.h"

static Int32 s_I18nLanguage = I18N_ENGLISH;
static Char s_I18nDynamic[384];

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

typedef struct
{
    const Char *en;
    const Char *pt;
    const Char *es;
    const Char *zh;
} I18nPhraseT;

static const I18nPhraseT s_Phrases[] =
{
    { "BROWSER", "NAVEGADOR", "NAVEGADOR", "浏览器" },
    { "STATE FILES", "ARQUIVOS DE ESTADO", "ARCHIVOS DE ESTADO", "状态文件" },
    { "MESSAGE LOG", "LOG DE MENSAGENS", "REGISTRO", "消息日志" },
    { "SMB NETWORK", "REDE SMB", "RED SMB", "SMB 网络" },
    { "Save States", "ESTADOS SALVOS", "ESTADOS GUARDADOS", "存档" },
    { "Save State Location", "LOCAL DOS ESTADOS", "UBICACION DE ESTADOS", "存档位置" },
    { "Memory Card", "CARTAO DE MEMORIA", "TARJETA DE MEMORIA", "记忆卡" },
    { "System / Tools", "SISTEMA / FERRAMENTAS", "SISTEMA / HERRAMIENTAS", "系统 / 工具" },
    { "File Menu", "MENU DE ARQUIVO", "MENU DE ARCHIVO", "文件菜单" },
    { "State Files", "Arquivos de Estado", "Archivos de Estado", "状态文件" },
    { "Quick Save", "Save Rapido", "Guardado Rapido", "快速存档" },
    { "Current Target", "Destino Atual", "Destino Actual", "当前目标" },
    { "Choose Save Location Again", "Escolher Local Novamente", "Elegir Ubicacion de Nuevo", "重新选择位置" },
    { "Browse State Files", "Explorar Estados", "Explorar Estados", "浏览存档" },
    { "Ask Save Location Again", "Escolher Local Novamente", "Elegir Ubicacion de Nuevo", "重新选择位置" },
    { "Choose quick-save location", "Escolha o local do save rapido", "Elige la ubicacion del guardado rapido", "选择快速存档位置" },
    { "Format Card", "Formatar Cartao", "Formatear Tarjeta", "格式化记忆卡" },
    { "Warning", "Aviso", "Aviso", "警告" },
    { "Formatting erases the entire card.", "Formatar apaga todo o cartao.", "Formatear borra toda la tarjeta.", "格式化会清空整张记忆卡。" },
    { "Select", "Selecionar", "Seleccionar", "选择" },
    { "Open", "Abrir", "Abrir", "打开" },
    { "Back", "Voltar", "Volver", "返回" },
    { "Cover", "Capa", "Caratula", "封面" },
    { "PgUp", "PgAc", "PagAnt", "上页" },
    { "PgDn", "PgAb", "PagSig", "下页" },
    { "Network", "Rede", "Red", "网络" },
    { "No Covers", "Sem Capas", "Sin Caratulas", "无封面" },
    { "Choose", "Escolher", "Elegir", "选择" },
    { "Cancel", "Cancelar", "Cancelar", "取消" },
    { "Change", "Alterar", "Cambiar", "更改" },
    { "Info", "Info", "Info", "信息" },
    { "Scroll", "Rolar", "Desplazar", "滚动" },
    { "Deleting a state removes both banks", "Excluir remove os dois bancos", "Eliminar quita ambos bancos", "删除会移除两个存档区" },
    { "X: open  SELECT: file menu", "X: abrir  SELECT: menu de arquivo", "X: abrir  SELECT: menu de archivo", "X: 打开  SELECT: 文件菜单" },
    { "Delete removes both state banks", "Excluir remove os dois bancos", "Eliminar quita ambos bancos", "删除会移除两个存档区" },
    { "HDD: choose partition first", "HDD: escolha a particao primeiro", "HDD: elige primero la particion", "HDD: 请先选择分区" },
    { "X: choose, save, return", "X: escolher, salvar, voltar", "X: elegir, guardar, volver", "X: 选择、保存、返回" },
    { "Circle: cancel", "Circulo: cancelar", "Circulo: cancelar", "圆圈: 取消" },
    { "Circle: cancel safely", "Circulo: cancelar com seguranca", "Circulo: cancelar con seguridad", "圆圈: 安全取消" },
    { "Change later in State Manager", "Altere depois no Gerenciador de Estados", "Cambialo luego en Estados", "之后可在存档管理器中更改" },
    { "Save storage selected.", "Armazenamento de save selecionado.", "Almacenamiento seleccionado.", "已选择存档存储。" },
    { "Quick slot selected.", "Slot rapido selecionado.", "Ranura rapida seleccionada.", "已选择快速存档槽。" },
    { "Choose Storage and press X first.", "Escolha Armazenamento e pressione X primeiro.", "Elige Almacenamiento y pulsa X primero.", "请先选择存储并按 X。" },
    { "Save location cleared. Select Storage, then X.", "Local limpo. Escolha Armazenamento e depois X.", "Ubicacion borrada. Elige Almacenamiento y luego X.", "位置已清除。选择存储后按 X。" },
    { "No - Cancel", "Nao - Cancelar", "No - Cancelar", "否 - 取消" },
    { "YES - Format mc%d:", "SIM - Formatar mc%d:", "SI - Formatear mc%d:", "是 - 格式化 mc%d:" },
    { "mc0: is not formatted", "mc0: nao esta formatado", "mc0: no esta formateada", "mc0: 未格式化" },
    { "mc1: is not formatted", "mc1: nao esta formatado", "mc1: no esta formateada", "mc1: 未格式化" },
    { "FORMATTING ERASES THE ENTIRE CARD", "FORMATAR APAGA TODO O CARTAO", "FORMATEAR BORRA TODA LA TARJETA", "格式化会清空整张记忆卡" },
    { "Select YES, then press X", "Selecione SIM e pressione X", "Selecciona SI y pulsa X", "选择“是”后按 X" },
    { "Memory card formatted.", "Cartao de memoria formatado.", "Tarjeta de memoria formateada.", "记忆卡已格式化。" },
    { "Memory card formatted, but save failed.", "Cartao formatado, mas o save falhou.", "Tarjeta formateada, pero fallo el guardado.", "记忆卡已格式化，但保存失败。" },
    { "Return to PS2 Browser", "Voltar ao Navegador do PS2", "Volver al Navegador de PS2", "返回 PS2 浏览器" },
    { "Power Off PS2", "Desligar PS2", "Apagar PS2", "关闭 PS2" },
    { "Status", "Status", "Estado", "状态" },
    { "Server IP", "IP do Servidor", "IP del Servidor", "服务器 IP" },
    { "Port", "Porta", "Puerto", "端口" },
    { "Share", "Compartilhamento", "Recurso", "共享" },
    { "Username", "Usuario", "Usuario", "用户名" },
    { "Password", "Senha", "Contrasena", "密码" },
    { "Actions", "Acoes", "Acciones", "操作" },
    { "Save & Connect", "Salvar e Conectar", "Guardar y Conectar", "保存并连接" },
    { "Disconnect", "Desconectar", "Desconectar", "断开连接" },
    { "Guest", "Convidado", "Invitado", "访客" },
    { "(empty)", "(vazio)", "(vacio)", "(空)" },
    { "Octet", "Octeto", "Octeto", "字节" },
    { "Value", "Valor", "Valor", "值" },
    { "Done", "Concluir", "Listo", "完成" },
    { "Cursor", "Cursor", "Cursor", "光标" },
    { "Char", "Caractere", "Caracter", "字符" },
    { "Delete", "Excluir", "Borrar", "删除" },
    { "Edit", "Editar", "Editar", "编辑" },
    { "Off", "Desligado", "Desactivado", "关" },
    { "Connecting", "Conectando", "Conectando", "连接中" },
    { "Connected", "Conectado", "Conectado", "已连接" },
    { "No SMB.CNF", "Sem SMB.CNF", "Sin SMB.CNF", "缺少 SMB.CNF" },
    { "Bad SMB.CNF", "SMB.CNF invalido", "SMB.CNF invalido", "SMB.CNF 无效" },
    { "Save Error", "Erro ao Salvar", "Error al Guardar", "保存错误" },
    { "Network Error", "Erro de Rede", "Error de Red", "网络错误" },
    { "DHCP Timeout", "Tempo DHCP Esgotado", "Tiempo DHCP Agotado", "DHCP 超时" },
    { "Driver Error", "Erro do Driver", "Error de Driver", "驱动错误" },
    { "Connect Error", "Erro de Conexao", "Error de Conexion", "连接错误" },
    { "SMB1 Required", "SMB1 Necessario", "SMB1 Necesario", "需要 SMB1" },
    { "Auth Error", "Erro de Autenticacao", "Error de Autenticacion", "认证错误" },
    { "Share Error", "Erro de Compartilhamento", "Error de Recurso", "共享错误" },
    { "Browse Error", "Erro de Navegacao", "Error de Navegacion", "浏览错误" },
    { "Enabled", "Ativado", "Habilitado", "已启用" },
    { "USB: Starting driver...", "USB: iniciando driver...", "USB: iniciando driver...", "USB: 正在启动驱动..." },
    { "SMB: Connecting...", "SMB: conectando...", "SMB: conectando...", "SMB: 正在连接..." },
    { "SMB: Saving config...", "SMB: salvando configuracao...", "SMB: guardando configuracion...", "SMB: 正在保存配置..." },
    { "SMB: Disconnected", "SMB: desconectado", "SMB: desconectado", "SMB: 已断开" },
    { "Saving state slot %d...", "Salvando estado no slot %d...", "Guardando estado en ranura %d...", "正在保存到槽 %d..." },
    { "Loading state slot %d...", "Carregando estado do slot %d...", "Cargando estado de ranura %d...", "正在载入槽 %d..." },
    { "Saving SRAM...", "Salvando SRAM...", "Guardando SRAM...", "正在保存 SRAM..." },
    { "Saving SRAM before exit...", "Salvando SRAM antes de sair...", "Guardando SRAM antes de salir...", "退出前正在保存 SRAM..." },
    { "SRAM save failed - exit cancelled.", "Falha ao salvar SRAM - saida cancelada.", "Fallo al guardar SRAM - salida cancelada.", "SRAM 保存失败 - 已取消退出。" },
    { "No save-state operation yet.", "Nenhuma operacao de estado ainda.", "Aun no hay operacion de estado.", "尚无存档操作。" },
    { "No game loaded.", "Nenhum jogo carregado.", "Ningun juego cargado.", "未加载游戏。" },
    { "This system cannot save states.", "Este sistema nao suporta save states.", "Este sistema no admite estados.", "此系统无法保存状态。" },
    { "No SNES ROM loaded.", "Nenhuma ROM SNES carregada.", "No hay ROM SNES cargada.", "未加载 SNES ROM。" },
    { "Save states are disabled during netplay.", "Save states ficam desativados no netplay.", "Los estados estan desactivados en netplay.", "联机时禁用存档。" },
    { "Ready: NES cartridge and mapper state.", "Pronto: cartucho NES e estado do mapper.", "Listo: cartucho NES y estado del mapper.", "就绪: NES 卡带和 Mapper 状态。" },
    { "Ready: SNES + SA-1 state.", "Pronto: estado SNES + SA-1.", "Listo: estado SNES + SA-1.", "就绪: SNES + SA-1 状态。" },
    { "Ready: base SNES hardware.", "Pronto: hardware SNES base.", "Listo: hardware SNES base.", "就绪: 基础 SNES 硬件。" },
    { "Cannot identify the loaded ROM.", "Nao foi possivel identificar a ROM.", "No se pudo identificar la ROM.", "无法识别已加载的 ROM。" },
    { "Could not snapshot the NES mapper state.", "Falha ao capturar o estado do mapper NES.", "No se pudo capturar el mapper NES.", "无法保存 NES Mapper 状态。" },
    { "No Covers", "Sem Capas", "Sin Caratulas", "无封面" },
    { "Copy File", "Copiar Arquivo", "Copiar Archivo", "复制文件" },
    { "Paste File", "Colar Arquivo", "Pegar Archivo", "粘贴文件" },
    { "Delete file", "Excluir Arquivo", "Eliminar Archivo", "删除文件" },
    { "Select=Network", "Select=Rede", "Select=Red", "Select=网络" },
    { "Reset", "Padrao", "Restablecer", "重置" },
    { "Save", "Salvar", "Guardar", "保存" },
    { "Internal HDD", "HDD Interno", "HDD Interno", "内部 HDD" },
    { "Auto", "Automatico", "Automatico", "自动" },
    { "Formatting mc%d:...", "Formatando mc%d:...", "Formateando mc%d:...", "正在格式化 mc%d:..." },
    { "Could not format mc%d:.", "Nao foi possivel formatar mc%d:.", "No se pudo formatear mc%d:.", "无法格式化 mc%d:。" },
    { "Format failed - Circle: cancel", "Falha ao formatar - Circulo: cancelar", "Fallo al formatear - Circulo: cancelar", "格式化失败 - 圆圈: 取消" },
    { "%s state is not serialized yet.", "Estado %s ainda nao foi serializado.", "El estado %s aun no esta serializado.", "%s 状态尚未序列化。" },
    { "USB driver failed (%d).", "Driver USB falhou (%d).", "Fallo el driver USB (%d).", "USB 驱动失败 (%d)。" },
    { "Loaded slot %d from %s.", "Slot %d carregado de %s.", "Ranura %d cargada desde %s.", "已从 %s 载入槽 %d。" },
    { "State load ok: %s", "Estado carregado: %s", "Estado cargado: %s", "状态载入成功: %s" },
    { "Slot %d is incomplete or corrupt.", "Slot %d incompleto ou corrompido.", "Ranura %d incompleta o corrupta.", "槽 %d 不完整或已损坏。" },
    { "Slot %d belongs to another ROM.", "Slot %d pertence a outra ROM.", "Ranura %d pertenece a otra ROM.", "槽 %d 属于另一 ROM。" },
    { "No state found in slot %d.", "Nenhum estado encontrado no slot %d.", "No hay estado en la ranura %d.", "槽 %d 中没有存档。" },
    { "State load failed: %s", "Falha ao carregar estado: %s", "Fallo al cargar estado: %s", "状态载入失败: %s" },
    { "State payload: raw=%u stored=%u encoding=%s", "Dados do estado: bruto=%u salvo=%u formato=%s", "Datos del estado: bruto=%u guardado=%u formato=%s", "存档数据: 原始=%u 保存=%u 编码=%s" },
    { "Saved slot %d to %s.", "Slot %d salvo em %s.", "Ranura %d guardada en %s.", "槽 %d 已保存到 %s。" },
    { "State save ok: %s", "Estado salvo: %s", "Estado guardado: %s", "状态保存成功: %s" },
    { "State save failed: %s", "Falha ao salvar estado: %s", "Fallo al guardar estado: %s", "状态保存失败: %s" },
    { "mc%d: is not formatted.", "mc%d: nao esta formatado.", "mc%d: no esta formateada.", "mc%d: 未格式化。" },
    { "Could not save slot %d to %s.", "Nao foi possivel salvar slot %d em %s.", "No se pudo guardar ranura %d en %s.", "无法将槽 %d 保存到 %s。" },
    { "USB: Driver failed (%d)", "USB: driver falhou (%d)", "USB: fallo del driver (%d)", "USB: 驱动失败 (%d)" },
    { "SMB: %s (error %d)", "SMB: %s (erro %d)", "SMB: %s (error %d)", "SMB: %s (错误 %d)" },
    { "SMB: Connected\n%s", "SMB: Conectado\n%s", "SMB: Conectado\n%s", "SMB: 已连接\n%s" },
    { "CD/DVD: Starting driver...", "CD/DVD: iniciando driver...", "CD/DVD: iniciando driver...", "CD/DVD: 正在启动驱动..." },
    { "CD/DVD driver failed (%d).", "Driver CD/DVD falhou (%d).", "Fallo el driver CD/DVD (%d).", "CD/DVD 驱动失败 (%d)。" },
    { "ERROR: Cannot load disksys.rom", "ERRO: Nao foi possivel carregar disksys.rom", "ERROR: No se pudo cargar disksys.rom", "错误: 无法加载 disksys.rom" },
    { "Copy host: -> mc0:", "Copiar host: -> mc0:", "Copiar host: -> mc0:", "复制 host: -> mc0:" },
    { "Copy mc0: -> mc1:", "Copiar mc0: -> mc1:", "Copiar mc0: -> mc1:", "复制 mc0: -> mc1:" },
    { "Copy mc1: -> mc0:", "Copiar mc1: -> mc0:", "Copiar mc1: -> mc0:", "复制 mc1: -> mc0:" },
    { "Copy mc0: -> host:", "Copiar mc0: -> host:", "Copiar mc0: -> host:", "复制 mc0: -> host:" },
    { "Dump memory -> host:", "Exportar memoria -> host:", "Volcar memoria -> host:", "导出内存 -> host:" },
    { "Add PSX CD to mc0:title.db", "Adicionar CD PSX ao mc0:title.db", "Agregar CD PSX a mc0:title.db", "添加 PSX CD 到 mc0:title.db" },
    { "Dump mc0:title.db -> tty0:", "Exportar mc0:title.db -> tty0:", "Volcar mc0:title.db -> tty0:", "导出 mc0:title.db -> tty0:" },
    { "Copy rom0:libsd -> host:", "Copiar rom0:libsd -> host:", "Copiar rom0:libsd -> host:", "复制 rom0:libsd -> host:" }
};

static const Char *_PhraseFor(const I18nPhraseT *p)
{
    switch (s_I18nLanguage)
    {
        case I18N_PORTUGUESE_BR: return p->pt;
        case I18N_SPANISH: return p->es;
        case I18N_CHINESE_SIMPLIFIED: return p->zh;
        default: return p->en;
    }
}

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
    if (s_I18nLanguage < 0) s_I18nLanguage = I18N_LANGUAGE_COUNT - 1;
    if (s_I18nLanguage >= I18N_LANGUAGE_COUNT) s_I18nLanguage = 0;
}

const Char *I18nGetLanguageName()
{
    return s_I18nLanguageNames[s_I18nLanguage];
}

const Char *I18nGetText(I18nTextE text)
{
    if (text < 0 || text >= I18N_TEXT_COUNT) return "";
    return s_I18nText[s_I18nLanguage][text];
}

const Char *I18nTranslate(const Char *english)
{
    Uint32 i;
    const Char *suffix;

    if (!english || !english[0] || s_I18nLanguage == I18N_ENGLISH)
        return english;

    for (i = 0; i < sizeof(s_Phrases) / sizeof(s_Phrases[0]); i++)
    {
        if (!strcmp(english, s_Phrases[i].en))
            return _PhraseFor(&s_Phrases[i]);
    }

#define PREFIX(en, pt, es, zh) \
    if (!strncmp(english, en, sizeof(en)-1)) { \
        suffix = english + sizeof(en)-1; \
        const Char *prefix = (s_I18nLanguage == I18N_PORTUGUESE_BR) ? pt : \
                             (s_I18nLanguage == I18N_SPANISH) ? es : zh; \
        snprintf(s_I18nDynamic, sizeof(s_I18nDynamic), "%s%s", prefix, suffix); \
        return s_I18nDynamic; \
    }

    PREFIX("Storage: ", "Armazenamento: ", "Almacenamiento: ", "存储: ")
    PREFIX("Quick Slot: ", "Slot Rapido: ", "Ranura Rapida: ", "快速槽: ")
    PREFIX("Quick target: ", "Destino rapido: ", "Destino rapido: ", "快速目标: ")
    PREFIX("Current Target ", "Destino Atual ", "Destino Actual ", "当前目标 ")
    PREFIX("SMB: ", "SMB: ", "SMB: ", "SMB: ")
    PREFIX("YES - Format mc", "SIM - Formatar mc", "SI - Formatear mc", "是 - 格式化 mc")
    PREFIX("Formatting mc", "Formatando mc", "Formateando mc", "正在格式化 mc")
    PREFIX("Could not format mc", "Nao foi possivel formatar mc", "No se pudo formatear mc", "无法格式化 mc")
#undef PREFIX

    return english;
}
