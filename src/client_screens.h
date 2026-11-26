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

    // Dispositivo de som trocado durante a transmissao
    bool audioChanged = false;

    // "Parar de transmitir" (volta pra tela de opcoes)
    bool roomPreviewChanged = false;

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

    // Aviso de janela preta: trocar pra tela onde ela esta /
    // dispensar o aviso.
    bool captureWholeScreen = false;
    bool dismissBlackCapture = false;

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

    // Numa sala: mostrar uma previa pequena da transmissao na
    // lista da sala (a tela altera direto e avisa em
    // actions.roomPreviewChanged).
    bool* roomPreview = nullptr;

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

    // Som (de onde vem o audio), trocavel durante a transmissao
    // (a tela altera direto e avisa em actions.audioChanged).
    const std::vector<std::string>* audioDevices = nullptr;
    int* audioIndex = nullptr;

    // Numa sala: o painel abre pela barra de icones (acima
    // dela, em anchorX/anchorY) e fecha pelo X ou pela barra.
    bool* roomPanelOpen = nullptr;

    float anchorX = 0.0f;
    float anchorY = 0.0f;

    // Controles escondidos (Tab): o painel some junto.
    bool hidden = false;

    // A janela compartilhada esta chegando preta (alguns jogos,
    // como a partida do LoL): sugerir a tela inteira.
    bool blackCapture = false;

    // A janela compartilhada foi fechada: a transmissao esta
    // pausada e encerra sozinha em closedSecondsLeft segundos
    // (actions.openPicker = escolher outra; stopSharing = parar).
    bool windowClosed = false;
    int closedSecondsLeft = 0;
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

    // Miniatura (ImTextureID). nullptr = ainda carregando, ou
    // sem imagem (janela minimizada).
    void* thumbnail = nullptr;

    float thumbnailWidth = 0.0f;
    float thumbnailHeight = 0.0f;

    // Miniaturas ainda sendo tiradas (mostra "carregando").
    bool thumbnailPending = false;

    bool minimized = false;

    // Janela: o programa pode mostrar por cima uma previa "ao
    // vivo" do Windows (que tambem tem a ultima imagem das
    // minimizadas). A tela so informa onde.
    bool live = false;
};

// Onde a previa ao vivo de uma janela vai (coordenadas da
// janela do programa): o quadro da miniatura e o recorte
// visivel (rolagem). root = janela do ImGui do seletor.
struct PickerLiveSlot
{
    int index = 0;

    float boxMinX = 0, boxMinY = 0, boxMaxX = 0, boxMaxY = 0;
    float clipMinX = 0, clipMinY = 0, clipMaxX = 0, clipMaxY = 0;

    void* root = nullptr;
};

struct ClientPickerModel
{
    const std::vector<PickerEntry>* monitors = nullptr;

    const std::vector<PickerEntry>* windows = nullptr;

    // Saida: onde vao as previas ao vivo das janelas.
    std::vector<PickerLiveSlot>* liveSlots = nullptr;

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
