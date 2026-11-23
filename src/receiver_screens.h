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

    // Engrenagem: configuracoes.
    bool openSettings = false;
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
    Speaking,   // falando agora
    Muted       // na voz, microfone desligado
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

    // Foto de perfil (sem previa, aparece no lugar da bolinha).
    void* avatar = nullptr;
};

struct RoomScreenModel
{
    std::string code;

    // Nome da sala (vazio = sem nome).
    std::string title;

    int memberCount = 0;

    // Largura ocupada a direita pelo painel Pessoas/Chat (a
    // lista usa o resto). 0 = janela estreita: o painel abre
    // por cima pelo botao do cabecalho.
    float rightInset = 0.0f;

    // Janela estreita: botao pra abrir o painel (aberto agora?)
    // e mensagens nao lidas (bolinha).
    bool narrow = false;
    bool panelOpen = false;
    int unreadChat = 0;

    // Link de convite (abre o programa direto na sala). Vazio =
    // sem servidor publico (so o codigo).
    std::string inviteLink;

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

    // Anel de "falando" (0..1, suavizado), mesma ordem.
    std::vector<float> memberSpeak;

    // Fotos de perfil (ImTextureID ou nullptr), mesma ordem.
    std::vector<void*> memberAvatar;

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

    // Sons de aviso e reducao de ruido (a tela altera direto e
    // avisa em actions.voiceSettingsChanged).
    bool* soundsEnabled = nullptr;
    float* soundsVolume = nullptr;
    bool* noiseSuppression = nullptr;

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

    // Renomear a sala (titleText; vazio = tirar o nome).
    bool setTitle = false;
    std::string titleText;

    // Janela estreita: abrir/fechar o painel Pessoas/Chat.
    bool togglePanel = false;

    // Enviar o que esta em chatInput.
    bool sendChat = false;

    // Canal de voz
    bool voiceJoin = false;
    bool voiceLeave = false;
    bool voiceToggleMute = false;

    // Abrir as configuracoes (aba Atalhos / aba Voz).
    bool openShortcuts = false;
    bool openVoiceSettings = false;

    // Microfone escolhido (mesmo esquema de
    // RoomScreenModel::microphone; -1 = sem mudanca).
    int voiceMicrophone = -1;

    // Saida escolhida (mesmo esquema; -1 = sem mudanca).
    int voiceSpeaker = -1;

    // Uma das listas de dispositivos acabou de ser aberta:
    // atualizar.
    bool voiceRefreshDevices = false;

    // Sons / reducao de ruido mudaram (salvar e aplicar).
    bool voiceSettingsChanged = false;

    // Soltou o volume dos sons: tocar um de exemplo.
    bool previewSound = false;

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

    // Anel de "falando" (0..1, ja suavizado).
    float speakAmount = 0.0f;

    // Foto de perfil (ImTextureID; nullptr = inicial).
    void* avatar = nullptr;
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

// ------------------------------------------------------------
// Sala sobre o video (ou transmitindo): painel lateral com
// abas Pessoas/Chat e barra de icones embaixo
// ------------------------------------------------------------

struct RoomSidePanelModel
{
    // Pessoas e voz (rightSide/topOffset nao sao usados aqui).
    PeoplePanelModel people;

    const std::vector<ChatLine>* chat = nullptr;

    char* chatInput = nullptr;
    size_t chatInputSize = 0;
    bool focusChat = false;

    // 0 = Pessoas, 1 = Chat.
    int tab = 0;

    // Fixado: nao fecha ao clicar no video.
    bool pinned = false;

    int unreadChat = 0;

    // 0 = fechado, 1 = aberto (anima o deslize e o fade).
    float progress = 1.0f;

    // Preso na tela (lista da sala em janela larga): sem
    // fixar/fechar.
    bool docked = false;

    // Engrenagem no rodape (configuracoes de voz).
    bool showGear = false;

    // Alfinete (fixar); a lista da sala nao usa.
    bool showPin = true;
};

struct RoomSidePanelActions
{
    // -1 = nao trocou.
    int setTab = -1;

    bool togglePin = false;
    bool close = false;
    bool sendChat = false;
    bool openVoiceSettings = false;

    // Mouse em cima do painel.
    bool hovered = false;

    PeoplePanelActions people;
};

// Largura do painel (o video encolhe isso quando aberto).
float RoomSidePanelWidth();

void DrawRoomSidePanel(
    const RoomSidePanelModel& model,
    RoomSidePanelActions& actions
);


struct RoomBarModel
{
    // 0 = invisivel, 1 = visivel (fade).
    float alpha = 1.0f;

    // Vendo uma transmissao (som, opcoes, tela cheia, voltar);
    // false = esta pessoa transmite (so voz, pessoas, chat e
    // configuracoes).
    bool watching = false;

    bool voiceJoined = false;
    bool voiceJoining = false;
    bool voiceMuted = false;

    bool peopleOpen = false;
    bool chatOpen = false;
    int unreadChat = 0;

    // Largura ocupada a direita (painel lateral): a barra
    // centraliza no espaco que sobra.
    float rightInset = 0.0f;

    // Teclas (vazio = nenhuma) pras dicas.
    std::string muteKey;
    std::string soundKey;
    std::string peopleKey;
    std::string chatKey;
    std::string fullscreenKey;

    // Opcoes do video (so com watching; a tela altera direto).
    float* volume = nullptr;
    bool* muted = nullptr;
    bool* fillWindow = nullptr;
    bool fullscreen = false;

    int fps = 0;
    unsigned width = 0;
    unsigned height = 0;
    unsigned long long lostNetwork = 0;
    unsigned long long recovered = 0;
    unsigned long long late = 0;

    std::string presenterName;
    std::string keyHint;

    // Outras pessoas transmitindo ("Nome · o que mostra").
    std::vector<std::string> otherPresenters;

    // Esta pessoa transmitindo (sem watching): painel da
    // transmissao, parar e voltar a sala.
    bool sharingLive = false;
    bool livePanelOpen = false;
    int viewers = 0;
};

struct RoomBarActions
{
    bool toggleMic = false;
    bool joinVoice = false;
    bool togglePeople = false;
    bool toggleChat = false;
    bool openSettings = false;
    bool toggleFullscreen = false;
    bool backToRoom = false;

    // Indice em otherPresenters (-1 = nenhum).
    int switchTo = -1;

    bool toggleLivePanel = false;
    bool stopSharing = false;

    // Mouse em cima (ou um popover aberto): mantem visivel.
    bool hovered = false;
    bool popupOpen = false;

    // Centro x e topo da barra (pra paineis abrirem acima).
    float centerX = 0.0f;
    float top = 0.0f;
};

void DrawRoomBar(
    RoomBarModel& model,
    RoomBarActions& actions
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

// ------------------------------------------------------------
// Configuracoes (abas Voz e Atalhos)
// ------------------------------------------------------------

constexpr int SETTINGS_TAB_VOICE = 0;
constexpr int SETTINGS_TAB_SHORTCUTS = 1;
constexpr int SETTINGS_TAB_GENERAL = 2;

struct SettingsModel
{
    // Aba a mostrar ao abrir (-1 = a que ja estava).
    int openTab = -1;

    // ---- Geral ----

    // A tela altera direto e avisa em themeChanged.
    bool* lightTheme = nullptr;

    // Foto de perfil (ImTextureID; nullptr = sem foto) e nome
    // (pra inicial quando nao ha foto).
    void* myAvatar = nullptr;
    std::string myName;

    // Erro ao abrir a imagem escolhida (vazio = nenhum).
    std::string avatarError;

    // Teste do microfone fora da voz.
    bool micTestRunning = false;
    float micTestLevel = 0.0f;
    bool* micTestLoopback = nullptr;

    // ---- Voz ----

    // Dispositivos: 0 = padrao do Windows, i + 1 = lista[i].
    std::vector<std::string> microphones;
    int microphone = 0;

    std::vector<std::string> speakers;
    int speaker = 0;

    // Na voz: mostra o nivel do microfone (teste).
    bool voiceJoined = false;
    bool voiceTransmitting = false;
    float micLevel = 0.0f;

    // false = Windows sem cancelamento de eco (pedir fone).
    bool echoCancel = true;

    // A tela altera direto e avisa em voiceSettingsChanged.
    bool* noiseSuppression = nullptr;
    bool* soundsEnabled = nullptr;
    float* soundsVolume = nullptr;

    // ---- Atalhos ----

    std::vector<ShortcutRow> rows;

    // Linha esperando a tecla (-1 = nenhuma).
    int capturing = -1;
};

struct SettingsActions
{
    // Dispositivo escolhido (mesmo esquema; -1 = sem mudanca).
    int microphone = -1;
    int speaker = -1;

    // Uma lista de dispositivos abriu: atualizar.
    bool refreshDevices = false;

    // Reducao de ruido / sons mudaram (salvar e aplicar).
    bool voiceSettingsChanged = false;

    // Soltou o volume dos sons: tocar um de exemplo.
    bool previewSound = false;

    // Geral
    bool themeChanged = false;
    bool chooseAvatar = false;
    bool removeAvatar = false;

    // Teste do microfone: ligar/desligar.
    bool micTestToggle = false;

    // Atalhos: capturar / remover a tecla desta linha.
    int capture = -1;
    int clear = -1;

    bool cancelCapture = false;
    bool restoreDefaults = false;

    bool close = false;
};

void DrawSettingsWindow(
    const SettingsModel& model,
    SettingsActions& actions
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
