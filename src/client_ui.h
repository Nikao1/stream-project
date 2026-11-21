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

#include <d3d11.h>

// ============================================================
// MODO TRANSMITIR - ligacao entre o motor e a interface
// ============================================================
//
// O main.cpp e o motor (captura, encoder, rede). A interface do
// modo Transmitir (telas, botoes, fluxo de "comecar a
// transmitir") fica em client_ui.cpp e conversa com o motor
// lendo/escrevendo algumas variaveis do main.cpp, declaradas
// abaixo com "extern".
//
// O programa (receiver.cpp) so usa o que esta em sender.h.
// ============================================================

namespace sender
{

// ------------------------------------------------------------
// Definidas no main.cpp
// ------------------------------------------------------------

// Janela do programa (pra deixar ela fora da lista de janelas
// e pra fechar o programa ao reabrir como administrador).
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
extern std::atomic<uint32_t> g_targetFps;

extern std::atomic<uint32_t> g_targetOutputHeight;

extern std::atomic<bool> g_encoderRestartRequested;

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


// Liga/desliga o motor (portas UDP, threads). Ver main.cpp.
bool EngineStart();

void EngineStop();

} // namespace sender
