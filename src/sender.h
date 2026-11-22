#pragma once

#include <windows.h>
#include <d3d11.h>

#include <cstdint>
#include <string>
#include <vector>

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

// Modo sala: o painel troca o codigo da sessao por "Voltar para
// a sala" e "Parar de transmitir" (o codigo vai pra sala).
void SetRoomMode(
    bool inRoom
);

// O que publicar na sala: codigo e prova (token) da sessao e o
// que esta sendo compartilhado. false = nao esta transmitindo
// (ou a sessao ainda esta sendo criada).
bool GetPublishInfo(
    std::string& outSessionCode,
    std::string& outSessionToken,
    std::string& outLabel
);

// Para de transmitir sem fechar o programa: encerra a captura e
// a sessao, e volta pra tela de opcoes. O motor (portas,
// threads) continua ligado pra proxima vez.
void StopSharing();

// Previa na lista da sala (marcada no painel da transmissao).
// Podem ser chamadas de outra thread.
bool RoomPreviewEnabled();

// Imagem pequena (JPEG ~320x180) do que esta sendo transmitido.
bool CaptureRoomThumbnail(
    std::vector<uint8_t>& outJpeg
);

// Quantas pessoas estao assistindo esta transmissao.
int ViewerCount();

void Shutdown();

} // namespace sender
