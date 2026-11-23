#include "shortcuts.h"

#include <windows.h>

namespace
{

const ShortcutDef DEFS[SHORTCUT_COUNT] =
{
    {
        L"AtalhoFalar",
        "Segurar para falar",
        "Sua voz só sai enquanto a tecla estiver apertada. "
        "Nenhum = microfone aberto.",
        0,
        true
    },
    {
        L"AtalhoMicrofone",
        "Ligar/desligar microfone",
        "Silencia ou volta o seu microfone na voz.",
        0,
        true
    },
    {
        L"AtalhoSom",
        "Som da transmissão",
        "Liga e desliga o som de quem você está assistindo.",
        'M',
        false
    },
    {
        L"AtalhoTelaCheia",
        "Tela cheia",
        "Entra e sai da tela cheia (Esc também sai).",
        VK_F11,
        false
    },
    {
        L"AtalhoChat",
        "Abrir o chat",
        "Abre o chat sobre o vídeo ou a transmissão.",
        VK_RETURN,
        false
    },
    {
        L"AtalhoPessoas",
        "Pessoas na sala",
        "Mostra quem está na sala e na voz, sobre o vídeo ou a "
        "transmissão.",
        'P',
        false
    },
    {
        L"AtalhoInterface",
        "Mostrar/esconder controles",
        "Esconde (ou mostra) a barra de botões e o painel lateral "
        "sobre o vídeo, pra ver só a transmissão.",
        VK_TAB,
        false
    },
};


std::string ToUtf8(
    const wchar_t* text)
{
    int size =
        WideCharToMultiByte(
            CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);

    if (size <= 1)
        return std::string();

    std::string out(static_cast<size_t>(size - 1), '\0');

    WideCharToMultiByte(
        CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);

    return out;
}


// Teclas "genericas" (Shift/Ctrl/Alt sem lado) aparecem junto
// com as especificas: a captura fica com a especifica.
bool IsGenericModifier(
    int vk)
{
    return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU;
}

} // namespace


const ShortcutDef& GetShortcutDef(
    int id)
{
    if (id < 0 || id >= SHORTCUT_COUNT)
    {
        id = 0;
    }

    return DEFS[id];
}


std::string ShortcutKeyName(
    int vk)
{
    switch (vk)
    {
    case 0:            return "Nenhum";
    case VK_RBUTTON:   return "Botão direito do mouse";
    case VK_MBUTTON:   return "Botão do meio do mouse";
    case VK_XBUTTON1:  return "Mouse 4";
    case VK_XBUTTON2:  return "Mouse 5";
    case VK_LSHIFT:    return "Shift esquerdo";
    case VK_RSHIFT:    return "Shift direito";
    case VK_LCONTROL:  return "Ctrl esquerdo";
    case VK_RCONTROL:  return "Ctrl direito";
    case VK_LMENU:     return "Alt esquerdo";
    case VK_RMENU:     return "Alt direito";
    case VK_RETURN:    return "Enter";
    case VK_SPACE:     return "Espaço";
    case VK_TAB:       return "Tab";
    case VK_CAPITAL:   return "Caps Lock";
    case VK_UP:        return "Seta para cima";
    case VK_DOWN:      return "Seta para baixo";
    case VK_LEFT:      return "Seta para esquerda";
    case VK_RIGHT:     return "Seta para direita";
    default:           break;
    }

    if (vk >= VK_F1 && vk <= VK_F24)
    {
        return "F" + std::to_string(vk - VK_F1 + 1);
    }

    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z'))
    {
        return std::string(1, static_cast<char>(vk));
    }

    // Demais: nome que o Windows da pra tecla (no idioma do
    // teclado).
    UINT scan = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);

    LONG param = static_cast<LONG>(scan << 16);

    switch (vk)
    {
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT: case VK_DIVIDE: case VK_NUMLOCK:
        param |= 1 << 24; // tecla estendida
        break;
    default:
        break;
    }

    wchar_t name[64]{};

    if (scan != 0 && GetKeyNameTextW(param, name, 64) > 0)
    {
        return ToUtf8(name);
    }

    return "Tecla " + std::to_string(vk);
}


void KeyCapture::Start()
{
    m_active = true;

    for (int vk = 1; vk < 256; ++vk)
    {
        m_wasDown[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
}


int KeyCapture::Poll()
{
    if (!m_active)
        return -2;

    for (int vk = 1; vk < 256; ++vk)
    {
        bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;

        bool pressed = down && !m_wasDown[vk];

        m_wasDown[vk] = down;

        // Botao esquerdo: e o de clicar na tela, nao vira
        // atalho.
        if (!pressed ||
            vk == VK_LBUTTON ||
            IsGenericModifier(vk))
        {
            continue;
        }

        m_active = false;

        if (vk == VK_ESCAPE)
            return -2;

        if (vk == VK_BACK || vk == VK_DELETE)
            return 0;

        return vk;
    }

    return -1;
}


bool ShortcutPressed(
    int vk,
    void* window)
{
    static bool wasDown[256]{};

    if (vk <= 0 || vk >= 256)
        return false;

    bool down = (GetAsyncKeyState(vk) & 0x8000) != 0;

    bool pressed = down && !wasDown[vk];

    wasDown[vk] = down;

    return
        pressed &&
        GetForegroundWindow() == static_cast<HWND>(window);
}
