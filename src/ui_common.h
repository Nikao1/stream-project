#pragma once

#include <windows.h>
#include <d3d11.h>

#include "imgui.h"
#include "ui_widgets.h"

// ============================================================
// INTERFACE GRAFICA - base compartilhada
// ============================================================
//
// A interface e desenhada com Dear ImGui por cima do Direct3D
// 11 que o programa ja usa pra mostrar o video: nao ha janelas
// nem controles extras do Windows, nem dependencia de runtime -
// continua sendo um executavel so.
//
// Este arquivo cuida da parte que conversa com o Windows e o
// Direct3D (inicializacao, fonte, escala de DPI, mensagens da
// janela). O tema e os componentes prontos estao em ui_widgets;
// as telas em si ficam em cada programa.
//
// Uso, uma vez por frame, com o render target ja ativo:
//
//     UiBeginFrame();
//     ... ImGui::Begin / botoes / textos ...
//     UiEndFrame();
// ============================================================

bool UiInit(
    HWND hwnd,
    ID3D11Device* device,
    ID3D11DeviceContext* context
);

void UiShutdown();


// Repassar TODAS as mensagens da janela, no inicio do
// WindowProc. true = a interface tratou a mensagem.
bool UiHandleWindowMessage(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
);


void UiBeginFrame();

// Desenha a interface no render target atual.
void UiEndFrame();


// true = um campo de texto da interface esta com o foco;
// atalhos de teclado do programa devem ser ignorados.
bool UiWantsKeyboard();
