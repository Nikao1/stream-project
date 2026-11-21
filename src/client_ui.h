#pragma once

#include <winsock2.h>
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "udp_server.h"
#include "udp_audio_server.h"
#include "signaling_client.h"
#include "audio_capture.h"
#include "stream_crypto.h"
#include "capture_targets.h"

// ============================================================
// INTERFACE DO CLIENT - ligacao com o resto do programa
// ============================================================
//
// O main.cpp continua dono da captura, do encoder e da rede. A
// interface (telas, botoes, fluxo de "comecar a transmitir")
// fica em client_ui.cpp, e conversa com o main.cpp de duas
// formas:
//
//   - le/escreve algumas variaveis globais do main.cpp,
//     declaradas abaixo com "extern";
//   - o main.cpp chama as funcoes ClientUi... nos pontos certos
//     (abertura, mensagens da janela, cada frame, encerramento).
// ============================================================

// ------------------------------------------------------------
// Definidas no main.cpp
// ------------------------------------------------------------

extern HWND g_hwnd;

extern bool g_captureAccessDenied;

extern UdpServer g_udpServer;

extern UdpAudioServer g_udpAudioServer;

extern SignalingHost g_signalingHost;

extern CryptoKeyPair g_hostKeys;

extern AudioCapture g_audioCapture;

extern std::atomic<bool> g_inviteMode;

extern std::atomic<bool> g_udpServerStarted;

extern std::atomic<bool> g_audioServerStarted;

// Escolhas de qualidade (a interface preenche antes de comecar).
extern uint32_t g_targetFps;

extern uint32_t g_targetOutputHeight;

extern bool g_captureRunning;

extern uint32_t g_captureWidth;

extern uint32_t g_captureHeight;

// Ultimo frame capturado, pra previa na janela.
extern std::vector<unsigned char> g_displayPixels;

extern std::mutex g_displayMutex;

extern uint32_t g_displayWidth;

extern uint32_t g_displayHeight;

uint32_t PickBitrateMbps(
    uint32_t outputHeight,
    uint32_t fps
);

// Comeca (ou troca) a captura pra tela/janela escolhida.
// windowAudioOnly = capturar so o som da janela. Em caso de
// erro devolve false e a explicacao em outError. Chamar pela
// thread da janela.
bool StartCaptureForTarget(
    const CaptureTarget& target,
    bool windowAudioOnly,
    std::string& outError
);


// ------------------------------------------------------------
// Definidas em client_ui.cpp
// ------------------------------------------------------------

// Manda tudo que vai pro std::cout/std::cerr pra
// stream-client.log (o programa nao tem mais console). Chamar
// logo no inicio do wWinMain.
void ClientUiSetupLog();

// Cria o Direct3D da janela e a interface. Chamar depois de
// criar a janela (e de iniciar o Winsock).
bool ClientUiInit(
    HWND hwnd
);

void ClientUiShutdown();

// Posicao/tamanho da janela da ultima vez. Chamar depois de
// criar a janela e antes do ShowWindow; devolve o comando pro
// ShowWindow (defaultShowCommand se nao havia nada salvo).
int ClientUiRestoreWindowPlacement(
    HWND hwnd,
    int defaultShowCommand
);

// Chamar no WM_DESTROY (guarda a janela e devolve a energia ao
// normal).
void ClientUiSaveWindowPlacement(
    HWND hwnd
);

// Chamar no inicio do WindowProc. true = a interface tratou.
bool ClientUiHandleMessage(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam
);

// Chamar no WM_SIZE.
void ClientUiResize(
    uint32_t width,
    uint32_t height
);

// Desenha a janela (previa + interface) e apresenta, esperando
// o vsync. Chamar em laco, pela thread da janela.
void ClientUiRenderFrame();
