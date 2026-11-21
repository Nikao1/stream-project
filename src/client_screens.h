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
    // "Continuar" na tela inicial
    bool start = false;

    // "Voltar" na tela inicial (volta pro inicio do programa)
    bool goHome = false;

    // "Trocar o que está sendo compartilhado"
    bool openPicker = false;

    // Qualidade ou FPS trocados durante a transmissao
    bool qualityChanged = false;

    // "Parar de transmitir" (volta pra tela de opcoes)
    bool stopSharing = false;

    // Seletor: item clicado (indice na lista de telas ou de
    // janelas; -1 = nenhum).
    int pickMonitor = -1;
    int pickWindow = -1;

    bool refreshPicker = false;

    bool cancelPicker = false;

    // Seletor: reabrir o programa como administrador
    bool relaunchAsAdmin = false;

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

    // Mostra "Voltar" (so antes de a sessao ser criada).
    bool canGoBack = false;

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
    // true = a sessao ainda esta sendo criada (o servidor pode
    // levar ate ~1 minuto pra acordar).
    bool sessionPending = false;

    std::string sessionStatus;

    // Codigo da sessao. Vazio (e sem sessionPending) = sem
    // servidor de sessoes; ai vale o convite manual abaixo.
    std::string sessionCode;

    // O que esta sendo compartilhado, ex.: "Tela 1" / titulo
    // da janela.
    std::string sharingLabel;

    // true = so o som da janela compartilhada.
    bool windowAudioOnly = false;

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

    // Mesmos indices da tela inicial (a tela altera direto e
    // avisa em actions.qualityChanged).
    int* qualityIndex = nullptr;
    int* fpsIndex = nullptr;

    // Numa sala: sem codigo de sessao no painel (quem esta na
    // sala ve a transmissao na lista) e com "Voltar para a
    // sala" (actions.goHome).
    bool inRoom = false;

    // Upload estimado (uma copia do video por pessoa), ex.:
    // "~26 Mbps de upload". Vazio = ninguem assistindo.
    std::string uploadLabel;

    // true = upload alto: sugere baixar a qualidade.
    bool uploadWarning = false;

    // false = a sessao ainda esta sendo criada (nao da pra
    // parar no meio).
    bool canStop = true;

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


// ------------------------------------------------------------
// Seletor: o que compartilhar
// ------------------------------------------------------------

struct PickerEntry
{
    std::string title;

    std::string detail;
};

struct ClientPickerModel
{
    const std::vector<PickerEntry>* monitors = nullptr;

    const std::vector<PickerEntry>* windows = nullptr;

    // "So o som da janela" (a tela altera direto).
    bool* windowAudioOnly = nullptr;

    bool windowAudioSupported = false;

    // true = ja esta transmitindo (o seletor esta trocando a
    // fonte); habilita "Cancelar".
    bool switching = false;

    // Erro da ultima tentativa (ex.: a janela foi fechada).
    std::string error;

    // O Windows recusou a captura e rodar como administrador
    // resolve: mostra o botao "Abrir como administrador".
    bool offerAdmin = false;
};

void DrawClientPickerScreen(
    ClientPickerModel& model,
    ClientScreenActions& actions
);


// ffmpeg.exe ausente ou quebrado.
void DrawClientFfmpegProblemScreen(
    const std::string& problem,
    ClientScreenActions& actions
);
