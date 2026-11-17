#pragma once

#include <cstddef>
#include <string>
#include <vector>

// ============================================================
// TELAS DO CLIENT (quem transmite)
// ============================================================
//
// So desenho (Dear ImGui puro), como em receiver_screens: cada
// funcao recebe um "modelo" com o que mostrar e devolve, em
// ClientScreenActions, o que o usuario pediu. Quem executa e o
// main.cpp.
// ============================================================

struct ClientScreenActions
{
    // "Escolher tela e transmitir"
    bool start = false;

    // "Abrir o seletor de novo" (o usuario fechou o seletor de
    // tela do Windows sem escolher nada)
    bool openPicker = false;

    // "Aplicar" a resposta colada (convite manual)
    bool applyReply = false;

    bool openDownloadPage = false;

    bool recheckFfmpeg = false;
};


// ------------------------------------------------------------
// Tela inicial: qualidade, FPS e dispositivo de audio
// ------------------------------------------------------------

struct ClientSetupModel
{
    // Nomes pra lista de audio. O indice 0 e sempre "Padrao do
    // Windows".
    const std::vector<std::string>* audioDevices = nullptr;

    // Indices escolhidos (a tela altera direto).
    int* qualityIndex = nullptr;    // 0=480p 1=720p 2=1080p 3=nativa
    int* fpsIndex = nullptr;        // 0=60 1=30 2=15
    int* audioIndex = nullptr;

    // true = o usuario ja clicou em transmitir e a sessao esta
    // sendo criada.
    bool busy = false;

    std::string status;
};

void DrawClientSetupScreen(
    ClientSetupModel& model,
    ClientScreenActions& actions
);


// ------------------------------------------------------------
// Tela de transmissao
// ------------------------------------------------------------

struct ClientLiveModel
{
    // Codigo da sessao. Vazio = sem servidor de sessoes; ai
    // vale o convite manual abaixo.
    std::string sessionCode;

    std::string invite;

    // IP local, pra conexao direta na mesma rede (so mostrado
    // junto do convite).
    std::string localIp;

    int viewers = 0;
    int maxViewers = 0;

    // false = o seletor de tela do Windows ainda esta aberto
    // (ou foi fechado sem escolher).
    bool capturing = false;

    unsigned captureWidth = 0;
    unsigned captureHeight = 0;

    // Ex.: "720p · 60 FPS · 6 Mbps"
    std::string qualityLabel;

    bool symmetricNat = false;

    // Campo pra colar a RESPOSTA do convite manual.
    char* replyInput = nullptr;

    size_t replyInputSize = 0;

    // Resultado da ultima resposta aplicada (vazio = nenhuma).
    std::string replyStatus;

    bool replyOk = false;
};

void DrawClientLiveScreen(
    ClientLiveModel& model,
    ClientScreenActions& actions
);


// ffmpeg.exe ausente ou quebrado.
void DrawClientFfmpegProblemScreen(
    const std::string& problem,
    ClientScreenActions& actions
);
