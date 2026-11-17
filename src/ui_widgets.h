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


// ------------------------------------------------------------
// Cores do tema
// ------------------------------------------------------------

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

// "..." animado, pra indicar que algo esta em andamento.
const char* UiBusyDots();
