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


// Teclas de "segurar para falar" (indice salvo nas
// configuracoes; 0 = desligado, fala livre).
constexpr int VOICE_PTT_OPTION_COUNT = 5;

extern const char* const VOICE_PTT_OPTION_NAMES[VOICE_PTT_OPTION_COUNT];


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

    // Indice em VOICE_PTT_OPTION_NAMES.
    int voicePushToTalk = 0;

    // Microfones disponiveis (nomes) e o escolhido: 0 = padrao
    // do Windows, i + 1 = microphones[i].
    std::vector<std::string> microphones;
    int microphone = 0;

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

    // Novo indice de "segurar para falar" (-1 = sem mudanca).
    int voicePushToTalk = -1;

    // Microfone escolhido (mesmo esquema de
    // RoomScreenModel::microphone; -1 = sem mudanca).
    int voiceMicrophone = -1;

    // A lista de microfones acabou de ser aberta: atualizar.
    bool voiceRefreshMicrophones = false;

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


// Chat sobre o video (canto inferior esquerdo). outClose = a
// pessoa fechou. Devolve true quando envia (texto em input).
bool DrawVideoChat(
    const std::vector<ChatLine>& lines,
    char* input,
    size_t inputSize,
    bool focusInput,
    bool& outClose
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

    // Outras pessoas transmitindo na sala (troca rapida, sem
    // voltar pra lista). Texto pronto: "Nome · o que mostra".
    std::vector<std::string> otherPresenters;

    // Mensagens de chat nao lidas (botao "Chat (N)").
    int unreadChat = 0;

    // Canal de voz (botao de microfone).
    bool voiceJoined = false;
    bool voiceMuted = false;
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
};

// Desenha no canto superior direito. Devolve true se o mouse
// esta em cima (pra ele nao sumir enquanto a pessoa mexe).
bool DrawStreamOverlay(
    StreamOverlayModel& model,
    StreamOverlayActions& actions
);
