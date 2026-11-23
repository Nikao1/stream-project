#pragma once

#include "imgui.h"

// ============================================================
// INTERFACE GRAFICA - tema e componentes
// ============================================================
//
// Tudo aqui e Dear ImGui puro (nada de Windows nem Direct3D):
// o visual do programa e os componentes prontos que as telas
// usam. A parte que conversa com o Windows e o Direct3D fica em
// ui_common.
// ============================================================

// Aplica o tema (cores, cantos arredondados, espacamentos) ja
// na escala do monitor (1.0 = 100%).
void UiApplyTheme(
    float scale
);

// Fonte usada nos titulos (a padrao e a primeira carregada).
void UiSetTitleFont(
    ImFont* font
);

// Fator de escala do monitor (1.0 = 100%).
float UiScale();

// Caracteres que as fontes da interface carregam: latim com
// acentos (portugues, espanhol, frances...) e a pontuacao comum
// em titulos de janela (travessao, aspas curvas, reticencias,
// marcadores). Sem isso eles apareciam como "?".
const ImWchar* UiGlyphRanges();


// ------------------------------------------------------------
// Cores do tema
// ------------------------------------------------------------

// Tema claro/escuro (so cores; reaplica o estilo).
void UiSetLightTheme(
    bool light
);

bool UiIsLightTheme();

ImVec4 UiColorMuted();    // texto secundario
ImVec4 UiColorError();
ImVec4 UiColorSuccess();
ImVec4 UiColorAccent();


// ------------------------------------------------------------
// Componentes
// ------------------------------------------------------------

// Painel sem borda de janela, centralizado na area util, com a
// largura dada (em pixels a 100% - a escala e aplicada aqui).
// Sempre fechar com UiEndPanel().
void UiBeginCenteredPanel(
    const char* id,
    float width
);

void UiEndPanel();

// Titulo grande.
void UiTitle(
    const char* text
);

// Texto secundario (cinza), com quebra de linha automatica.
void UiMutedText(
    const char* text
);

// Texto colorido com quebra de linha automatica.
void UiWrappedColored(
    const ImVec4& color,
    const char* text
);

// Botao principal, ocupando a largura toda do painel.
bool UiPrimaryButton(
    const char* label,
    bool enabled = true
);

// Botao de destaque com tamanho escolhido (ex.: "Assistir" na
// lista da sala).
bool UiPrimaryButtonSized(
    const char* label,
    const ImVec2& size
);

// "..." animado, pra indicar que algo esta em andamento.
const char* UiBusyDots();

// Circulo girando (algo esta acontecendo). Ocupa um quadrado
// de 2 x radius; radius ja na escala da tela.
void UiSpinner(
    float radius
);

// Bolinha com a inicial do nome, numa cor que depende do nome
// (a mesma pessoa tem sempre a mesma cor). "live" = anel
// vermelho (esta transmitindo). "speaking" = anel verde por
// fora (falando no canal de voz).
void UiAvatar(
    const char* name,
    float diameter,
    bool live = false,
    float speaking = 0.0f,
    void* image = nullptr   // foto (ImTextureID); nullptr = inicial
);

// Chave liga/desliga (no lugar da caixinha de marcar): trilho
// arredondado + bolinha que desliza, com o texto a direita.
// Devolve true quando a pessoa mudou (v ja alterado).
bool UiToggle(
    const char* label,
    bool* v
);

// Icone de microfone desenhado (sem fonte de icones), num
// quadrado de "size" na posicao do cursor. slashed = riscado
// (mudo).
void UiMicIcon(
    float size,
    ImU32 color,
    bool slashed
);

// Icones desenhados (sem fonte de icones: nada a mais no
// programa). Contorno com a cor dada, centrados em center.
enum class UiIcon
{
    Mic,
    MicOff,
    Volume,
    VolumeOff,
    People,
    Chat,
    Switch,
    Sliders,
    Fullscreen,
    ExitFullscreen,
    Exit,
    Pin,
    PinOff,
    Close,
    Screen,
    Stop,
    Edit
};

void UiIconAt(
    UiIcon icon,
    ImVec2 center,
    float size,
    ImU32 color
);

// Botao quadrado so com icone (dica ao passar o mouse). active =
// destacado (ex.: painel aberto); danger = vermelho; badge > 0
// = bolinha com numero (ex.: mensagens nao lidas).
bool UiIconButton(
    const char* id,
    UiIcon icon,
    const char* tooltip,
    bool active = false,
    bool danger = false,
    int badge = 0
);

// Engrenagem (configuracoes), centrada em center. hole = cor do
// furo do meio (a do fundo do botao).
void UiGearIconAt(
    ImVec2 center,
    float size,
    ImU32 color,
    ImU32 hole
);

// Mesmo icone numa posicao da tela, sem ocupar espaco (ex.: por
// cima de um botao).
void UiMicIconAt(
    ImVec2 screenPos,
    float size,
    ImU32 color,
    bool slashed
);

// Logo do programa (monitor com o simbolo de transmitir), num
// quadrado de "size".
void UiLogo(
    float size
);


// Mesmo painel, mas encostado no canto superior esquerdo - pra
// quando ha algo atras que precisa continuar visivel (a previa
// da tela compartilhada, por exemplo).
void UiBeginCornerPanel(
    const char* id,
    float width
);

// Rotulo pequeno e cinza acima de um campo.
void UiFieldLabel(
    const char* text
);

// Imagem de fundo ocupando a janela (mantendo a proporcao),
// escurecida por "dim" (0 = original, 1 = preto). Desenhada
// atras de todos os paineis.
void UiDrawBackgroundImage(
    ImTextureID texture,
    float imageWidth,
    float imageHeight,
    float dim
);


// ------------------------------------------------------------
// Tela "falta o FFmpeg" (igual no receiver e no client)
// ------------------------------------------------------------
//
// purpose: frase curta dizendo pra que este programa usa o
// FFmpeg. Os dois bool voltam true quando o usuario clica no
// botao correspondente.
void UiFfmpegProblemPanel(
    const char* problem,
    const char* purpose,
    bool& outOpenDownloadPage,
    bool& outRecheck
);
