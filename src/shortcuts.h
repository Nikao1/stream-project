#pragma once

#include <string>

// ============================================================
// ATALHOS
// ============================================================
//
// Cada atalho e uma tecla virtual do Windows (VK_*), incluindo
// botoes do mouse (VK_XBUTTON1 = "Mouse 4"...). 0 = nenhum.
//
// Os "globais" funcionam mesmo com o jogo em primeiro plano
// (lidos na thread do microfone); os outros, so com a janela do
// programa em foco e fora de campos de texto.
// ============================================================

enum ShortcutId
{
    SHORTCUT_PUSH_TO_TALK = 0, // segurar para falar (global)
    SHORTCUT_TOGGLE_MIC,       // ligar/desligar microfone (global)
    SHORTCUT_TOGGLE_SOUND,     // som da transmissao
    SHORTCUT_FULLSCREEN,
    SHORTCUT_CHAT,
    SHORTCUT_PEOPLE,           // painel de pessoas
    SHORTCUT_TOGGLE_UI,        // mostrar/esconder controles do video
    SHORTCUT_COUNT
};

struct ShortcutDef
{
    const wchar_t* settingsKey;

    const char* label;

    const char* help;

    int defaultKey;

    bool global;
};

const ShortcutDef& GetShortcutDef(
    int id
);

// Nome da tecla pra tela ("Mouse 4", "F9", "Alt direito"...).
// 0 = "Nenhum".
std::string ShortcutKeyName(
    int virtualKey
);


// ------------------------------------------------------------
// Captura de tecla (tela de atalhos): "aperte a tecla".
// ------------------------------------------------------------

class KeyCapture
{
public:
    // Comeca a esperar. Teclas ja apertadas agora (ex.: o
    // clique que abriu a captura) sao ignoradas ate soltar.
    void Start();

    void Cancel()
    {
        m_active = false;
    }

    bool IsActive() const
    {
        return m_active;
    }

    // Chamar a cada frame enquanto ativo. Devolve:
    //   -1 = ainda esperando
    //   -2 = cancelou (Esc)
    //    0 = remover o atalho (Backspace / Delete)
    //   >0 = tecla escolhida
    int Poll();

private:
    bool m_active = false;

    bool m_wasDown[256]{};
};


// Tecla apertada AGORA (borda de descida) - pra atalhos locais.
// Chamar uma vez por frame por tecla; so conta com a janela em
// foco (window).
bool ShortcutPressed(
    int virtualKey,
    void* window
);
