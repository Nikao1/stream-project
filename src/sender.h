#pragma once

#include <windows.h>
#include <d3d11.h>

#include <string>

// ============================================================
// MODO TRANSMITIR - o que o programa usa
// ============================================================
//
// O programa unico (receiver.cpp) e dono da janela, do
// Direct3D da tela e da interface. O modo Transmitir (main.cpp
// + client_ui.cpp) entra por aqui:
//
//   Open()     ao clicar em "Transmitir" (na primeira vez liga
//              o motor: portas, threads)
//   Draw()     a cada frame enquanto o modo estiver na tela,
//              dentro do frame da interface
//   IsLive()   ja esta compartilhando algo
//   Shutdown() ao fechar o programa
// ============================================================

namespace sender
{

// false = nao deu pra ligar (explicacao em outError).
bool Open(
    HWND hwnd,
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    std::string& outError
);

// outGoHome = a pessoa clicou em "Voltar" (so aparece antes de
// a transmissao comecar).
void Draw(
    bool& outGoHome
);

bool IsOpen();

bool IsLive();

void Shutdown();

} // namespace sender
