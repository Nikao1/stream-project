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

// Uma linha do chat.
struct ChatLine
{
    std::string name;
    std::string text;

    // Escrita por esta pessoa.
    bool mine = false;
};


// Caixa de chat: mensagens (rolavel, sempre no fim quando chega
// algo novo) + campo pra escrever. Devolve true quando a pessoa
// envia (o texto fica em "input"; quem chama limpa).
// focusInput = colocar o cursor no campo agora.
bool DrawChatBox(
    const char* id,
    const std::vector<ChatLine>& lines,
    char* input,
    size_t inputSize,
    float height,
    bool focusInput
);


// Avisos rapidos no canto (somem sozinhos).
struct Toast
{
    std::string text;

    // ImGui::GetTime() de quando apareceu.
    double shownAt = 0.0;
};

// Desenha os avisos ainda validos (somem em ~5 s).
void DrawToasts(
    const std::vector<Toast>& toasts
);


// Estado de cada pessoa no canal de voz.
enum class MemberVoice
{
    Out,        // fora da voz
    Connecting, // na voz, ainda sem som chegando
    In,         // na voz
    Speaking    // falando agora
};


struct RoomPresenterEntry
{
    std::string name;
    std::string label;

    bool isMe = false;

    // Esta pessoa assiste (ou esta conectando a) este.
    bool watching = false;

    // Previa do que esta sendo transmitido (ImTextureID; nullptr
    // = sem previa - mostra a bolinha com a inicial).
    void* thumbnail = nullptr;
    float thumbWidth = 0.0f;
    float thumbHeight = 0.0f;
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

    // Quem esta na sala que esta transmitindo (mesma ordem de
    // members) - bolinha com anel vermelho.
    std::vector<bool> memberLive;

    // Mesma ordem de members.
    std::vector<bool> memberIsMe;
    std::vector<MemberVoice> memberVoice;

    // Volume de cada pessoa na voz (0 a 2; 1 = normal).
    std::vector<float> memberVolume;

    // Canal de voz desta pessoa
    bool voiceJoined = false;
    bool voiceJoining = false;
    bool voiceMuted = false;

    // Microfone abrindo agora (falando, ou fala livre ligada).
    bool voiceTransmitting = false;

    // false = Windows sem cancelamento de eco (pedir fone).
    bool voiceEchoCancel = true;

    float voiceMicLevel = 0.0f;

    // Nome da tecla de "segurar para falar" (vazio = fala
    // livre) e da de ligar/desligar o microfone (vazio = nenhuma).
    std::string voicePttKey;
    std::string voiceMuteKey;

    // Microfones disponiveis (nomes) e o escolhido: 0 = padrao
    // do Windows, i + 1 = microphones[i].
    std::vector<std::string> microphones;
    int microphone = 0;

    // Saidas de som (onde a voz dos outros toca), mesmo esquema.
    std::vector<std::string> speakers;
    int speaker = 0;

    std::string voiceError;

    // Chat da sala
    std::vector<ChatLine> chat;
    char* chatInput = nullptr;
    size_t chatInputSize = 0;
};

struct RoomScreenActions
{
    // Indice em presenters (-1 = nenhum).
    int watch = -1;

    // "Transmitir minha tela" / "Opções da transmissão"
    bool openShare = false;

    bool stopShare = false;

    bool leave = false;

    // Enviar o que esta em chatInput.
    bool sendChat = false;

    // Canal de voz
    bool voiceJoin = false;
    bool voiceLeave = false;
    bool voiceToggleMute = false;

    // Abrir a tela de atalhos.
    bool openShortcuts = false;

    // Microfone escolhido (mesmo esquema de
    // RoomScreenModel::microphone; -1 = sem mudanca).
    int voiceMicrophone = -1;

    // Saida escolhida (mesmo esquema; -1 = sem mudanca).
    int voiceSpeaker = -1;

    // Uma das listas de dispositivos acabou de ser aberta:
    // atualizar.
    bool voiceRefreshDevices = false;

    // Volume de uma pessoa mudou (indice em members, -1 = nada).
    int volumeMember = -1;
    float volumeValue = 1.0f;
};

void DrawRoomScreen(
    RoomScreenModel& model,
    RoomScreenActions& actions
);


// Conectado (ou conectando) a alguem da sala, esperando a
// imagem. status: a etapa atual (ex.: "Procurando a sessão").
void DrawRoomWaitingScreen(
    const std::string& presenterName,
    const std::string& status,
    const std::string& versionWarning,
    bool& outBackToRoom
);


// Chat sobre o video (canto inferior esquerdo, ou direito com
// rightSide). outClose = a pessoa fechou. Devolve true quando
// envia (texto em input). occupiedTop: altura ja ocupada no
// topo do mesmo lado (o chat encolhe pra nao ficar por cima).
bool DrawVideoChat(
    const std::vector<ChatLine>& lines,
    char* input,
    size_t inputSize,
    bool focusInput,
    bool& outClose,
    bool rightSide = false,
    float occupiedTop = 0.0f
);


// ------------------------------------------------------------
// Painel "Pessoas" (sobre o video ou a transmissao)
// ------------------------------------------------------------

struct PeopleEntry
{
    std::string name;

    bool isMe = false;

    // Transmitindo (anel vermelho).
    bool live = false;

    MemberVoice voice = MemberVoice::Out;

    // 0 a 2 (1 = normal).
    float volume = 1.0f;
};

struct PeoplePanelModel
{
    std::vector<PeopleEntry> people;

    bool voiceJoined = false;
    bool voiceJoining = false;
    bool voiceMuted = false;
    bool voiceTransmitting = false;

    float micLevel = 0.0f;

    // Teclas (vazio = nenhuma), pra lembrar a pessoa.
    std::string pttKey;
    std::string muteKey;

    std::string voiceError;

    // true = encostado na direita (transmitindo); false =
    // esquerda (assistindo - o painel do video fica na direita).
    bool rightSide = false;

    // Distancia extra do topo (ex.: abaixo da barra de botoes).
    float topOffset = 0.0f;
};

struct PeoplePanelActions
{
    bool close = false;

    bool voiceJoin = false;
    bool voiceLeave = false;
    bool toggleMute = false;

    // Indice em people (-1 = nada).
    int volumeMember = -1;
    float volumeValue = 1.0f;

    bool openShortcuts = false;
};

// Devolve a altura ocupada (pra empilhar o que vem embaixo).
float DrawPeoplePanel(
    const PeoplePanelModel& model,
    PeoplePanelActions& actions
);


// Barra de botoes da sala no canto superior direito enquanto
// esta pessoa transmite (la nao ha o painel do video).
// Devolve a altura ocupada (pra o painel de pessoas ficar
// embaixo).
struct ShareToolbarActions
{
    bool togglePeople = false;
    bool toggleChat = false;
    bool toggleMute = false;

    // Mouse em cima (mantem a barra visivel).
    bool hovered = false;
};

// alpha: some junto com o painel do video quando o mouse fica
// parado (assistindo); 1 = sempre visivel.
float DrawShareRoomToolbar(
    bool peopleOpen,
    bool chatOpen,
    int unreadChat,
    bool voiceJoined,
    bool voiceMuted,
    ShareToolbarActions& actions,
    float alpha = 1.0f
);


// ------------------------------------------------------------
// Tela de atalhos
// ------------------------------------------------------------

struct ShortcutRow
{
    std::string label;
    std::string help;
    std::string keyName;

    bool hasKey = false;
    bool global = false;
};

struct ShortcutsModel
{
    std::vector<ShortcutRow> rows;

    // Linha esperando a tecla (-1 = nenhuma).
    int capturing = -1;
};

struct ShortcutsActions
{
    // Comecar a capturar a tecla desta linha.
    int capture = -1;

    // Remover o atalho desta linha.
    int clear = -1;

    bool cancelCapture = false;

    bool restoreDefaults = false;

    bool close = false;
};

void DrawShortcutsWindow(
    const ShortcutsModel& model,
    ShortcutsActions& actions
);


// Quem esta falando no canal de voz, discreto no canto
// superior esquerdo (sobre o video ou a tela de transmitir).
// Nao desenha nada com a lista vazia.
void DrawVoiceSpeakers(
    const std::vector<std::string>& names,
    bool myMicMuted
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
// hostFull = quem transmite avisou que ja tem 8 assistindo.
void DrawWaitingScreen(
    const std::string& reply,
    const std::string& versionWarning,
    bool hostFull = false
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

    // Outras pessoas transmitindo na sala (troca rapida, sem
    // voltar pra lista). Texto pronto: "Nome · o que mostra".
    std::vector<std::string> otherPresenters;

    // Mensagens de chat nao lidas (botao "Chat (N)").
    int unreadChat = 0;

    // Canal de voz (botao de microfone).
    bool voiceJoined = false;
    bool voiceMuted = false;

    // Painel de pessoas aberto (botao destacado).
    bool peopleOpen = false;

    // Linha de atalhos no rodape (ex.: "M: som  ·  F11: ...").
    std::string keyHint;

    // Distancia extra do topo (abaixo da barra da sala).
    float topOffset = 0.0f;

    // Painel de pessoas aberto no canto direito: este painel
    // fica ao lado dele (a esquerda), nao embaixo.
    bool besidePeoplePanel = false;

    // Altura do painel de pessoas: janela estreita demais pra
    // ficar ao lado -> fica embaixo dele.
    float peoplePanelHeight = 0.0f;
};

struct StreamOverlayActions
{
    bool toggleFullscreen = false;

    // "Voltar para a sala" (so com StreamOverlayModel.inRoom)
    bool backToRoom = false;

    // Indice em otherPresenters (-1 = nenhum).
    int switchTo = -1;

    // Abrir/fechar o chat sobre o video.
    bool toggleChat = false;

    bool toggleVoiceMute = false;

    bool togglePeople = false;

    bool openShortcuts = false;
};

// Desenha no canto superior direito. Devolve true se o mouse
// esta em cima (pra ele nao sumir enquanto a pessoa mexe).
bool DrawStreamOverlay(
    StreamOverlayModel& model,
    StreamOverlayActions& actions
);
