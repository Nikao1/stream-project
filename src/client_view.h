#pragma once

#include <d3d11.h>

#include <cstdint>

// ============================================================
// PREVIA DO MODO TRANSMITIR
// ============================================================
//
// A tela/janela sendo compartilhada aparece escurecida atras do
// painel, como espelho do que esta saindo. Usa o Direct3D da
// janela do programa (o mesmo da interface): a textura e
// atualizada e desenhada pela thread da janela, dentro do frame
// da interface.
// ============================================================

namespace sender
{

void ClientViewInit(
    ID3D11Device* device,
    ID3D11DeviceContext* context
);

void ClientViewShutdown();

// Copia os pixels (BGRA) pra textura da previa.
void ClientViewSetPreview(
    const unsigned char* bgraPixels,
    uint32_t width,
    uint32_t height
);

void ClientViewClearPreview();

// Desenha a previa no fundo. Chamar dentro do frame da
// interface (entre UiBeginFrame e UiEndFrame).
void ClientViewDrawBackground();

} // namespace sender
