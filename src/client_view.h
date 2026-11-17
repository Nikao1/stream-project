#pragma once

#include <windows.h>

#include <cstdint>

// ============================================================
// JANELA DO CLIENT (Direct3D 11)
// ============================================================
//
// Cuida do que aparece na janela de quem transmite: a previa da
// tela compartilhada, ao fundo, e a interface por cima.
//
// Usa um dispositivo Direct3D PROPRIO, separado do que a
// captura de tela usa. O contexto do Direct3D 11 nao pode ser
// usado por duas threads ao mesmo tempo, e a captura roda em
// outra thread - com dispositivos separados, um nunca atrapalha
// o outro.
//
// Todas as funcoes devem ser chamadas pela thread da janela.
// ============================================================

bool ClientViewInit(
    HWND hwnd
);

void ClientViewShutdown();

// Chamar no WM_SIZE.
void ClientViewResize(
    uint32_t width,
    uint32_t height
);

// Atualiza a imagem da previa (pixels BGRA, de cima pra baixo).
// E uma copia cara em telas grandes - quem chama deve limitar
// a poucas vezes por segundo.
void ClientViewSetPreview(
    const unsigned char* bgraPixels,
    uint32_t width,
    uint32_t height
);

// Remove a previa (volta ao fundo vazio).
void ClientViewClearPreview();

// Entre Begin e End, desenhar a interface com ImGui. Begin
// devolve false se nao ha onde desenhar (janela minimizada).
// End apresenta o frame esperando o vsync.
bool ClientViewBeginFrame();

void ClientViewEndFrame();
