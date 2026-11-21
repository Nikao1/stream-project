#pragma once

#include <cstddef>
#include <string>
#include <vector>

// ============================================================
// TELAS DO RECEIVER
// ============================================================
//
// So desenho (Dear ImGui puro): cada funcao recebe os dados que
// precisa mostrar e devolve, em ReceiverScreenActions, o que o
// usuario pediu. Quem executa as acoes (conectar, abrir o
// navegador...) e o receiver.cpp.
//
// Separado assim, da pra desenhar as telas fora do programa -
// e o que permite gerar uma previa delas sem rodar no Windows.
// ============================================================

struct ReceiverScreenActions
{
    bool connect = false;

    bool toggleFullscreen = false;

    bool openDownloadPage = false;

    bool recheckFfmpeg = false;

    // "Voltar" na tela de conexao (volta pro inicio)
    bool goHome = false;
};


// ------------------------------------------------------------
// Tela inicial do programa
// ------------------------------------------------------------
//
// Nome + "Criar uma sala" + "Entrar" com um codigo (de sala, ou
// de uma transmissao de versoes sem sala), e "Transmitir sem
// sala" pro fluxo direto.

struct HomeScreenModel
{
    char* name = nullptr;
    size_t nameSize = 0;

    char* code = nullptr;
    size_t codeSize = 0;

    // true = criando/entrando (o servidor pode levar ate ~1
    // minuto pra acordar).
    bool busy = false;

    std::string status;

    // Aviso em vermelho (vazio = nenhum).
    std::string error;
};

struct HomeScreenActions
{
    bool createRoom = false;

    bool enter = false;

    bool shareWithoutRoom = false;
};

void DrawHomeScreen(
    HomeScreenModel& model,
    HomeScreenActions& actions
);


// ------------------------------------------------------------
// Sala: quem esta, quem transmite, assistir/transmitir
// ------------------------------------------------------------

struct RoomPresenterEntry
{
    std::string name;
    std::string label;

    bool isMe = false;

    // Esta pessoa assiste (ou esta conectando a) este.
    bool watching = false;
};

struct RoomScreenModel
{
    std::string code;

    std::vector<RoomPresenterEntry> presenters;

    // Nomes de quem esta na sala (o desta pessoa marcado).
    std::vector<std::string> members;

    int maxMembers = 9;

    // Esta pessoa esta transmitindo.
    bool sharing = false;
    std::string sharingLabel;
    int sharingViewers = 0;

    // Conectando a alguem (nao da pra trocar no meio).
    bool connecting = false;
    std::string connectingTo;

    // Aviso neutro (ex.: "Fulano parou de transmitir.").
    std::string notice;

    // Erro (ex.: nao conseguiu conectar).
    std::string error;

    // Sem resposta do servidor da sala ha alguns segundos.
    bool offline = false;
};

struct RoomScreenActions
{
    // Indice em presenters (-1 = nenhum).
    int watch = -1;

    // "Transmitir minha tela" / "Opções da transmissão"
    bool openShare = false;

    bool stopShare = false;

    bool leave = false;
};

void DrawRoomScreen(
    RoomScreenModel& model,
    RoomScreenActions& actions
);


// Conectado a alguem da sala, esperando o primeiro frame.
void DrawRoomWaitingScreen(
    const std::string& presenterName,
    const std::string& versionWarning,
    bool& outBackToRoom
);


// ffmpeg.exe ausente ou quebrado: explica e ensina a resolver.
void DrawFfmpegProblemScreen(
    const std::string& problem,
    ReceiverScreenActions& actions
);


// Tela inicial: campo pro codigo da sessao (ou convite / IP).
// busy = uma tentativa de conexao esta em andamento.
void DrawConnectScreen(
    char* input,
    size_t inputSize,
    bool busy,
    const std::string& status,
    const std::string& error,
    ReceiverScreenActions& actions
);


// Conectado, esperando o primeiro frame. reply = texto da
// RESPOSTA do convite manual (vazio = nao mostrar).
// versionWarning = aviso de versoes diferentes (vazio = nada).
void DrawWaitingScreen(
    const std::string& reply,
    const std::string& versionWarning
);


// ------------------------------------------------------------
// Painel sobre o video (volume, exibicao e estatisticas)
// ------------------------------------------------------------

struct StreamOverlayModel
{
    // 0.0 a 1.0; a tela altera direto.
    float* volume = nullptr;

    bool* muted = nullptr;

    // true = preencher a janela (sem bordas, cortando o que
    // sobra); false = mostrar a imagem inteira (com bordas
    // quando a proporcao nao bate). A tela altera direto.
    bool* fillWindow = nullptr;

    bool fullscreen = false;

    // true = o usuario fechou o painel; aparece so um botao
    // pequeno pra reabrir.
    bool* collapsed = nullptr;

    int fps = 0;

    unsigned width = 0;
    unsigned height = 0;

    // Frames perdidos na rede e descartados por atraso (ver
    // g_statLostNetwork / g_statLate no receiver).
    unsigned long long lostNetwork = 0;
    unsigned long long late = 0;

    // Frames que perderam pedacos e foram salvos pelo reenvio.
    unsigned long long recovered = 0;

    // 0 = invisivel, 1 = totalmente visivel (o painel some
    // aos poucos quando o mouse fica parado).
    float alpha = 1.0f;

    // Assistindo dentro de uma sala: mostra quem transmite e o
    // botao "Voltar para a sala".
    bool inRoom = false;

    std::string presenterName;
};

struct StreamOverlayActions
{
    bool toggleFullscreen = false;

    // "Voltar para a sala" (so com StreamOverlayModel.inRoom)
    bool backToRoom = false;
};

// Desenha no canto superior direito. Devolve true se o mouse
// esta em cima (pra ele nao sumir enquanto a pessoa mexe).
bool DrawStreamOverlay(
    StreamOverlayModel& model,
    StreamOverlayActions& actions
);
