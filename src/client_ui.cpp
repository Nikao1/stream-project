#include "client_ui.h"

#include <ws2tcpip.h>
#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "audio_protocol.h"
#include "client_screens.h"
#include "client_view.h"
#include "capture_thumbnails.h"
#include "ffmpeg_check.h"
#include "invite_code.h"
#include "ui_common.h"
#include "app_settings.h"
#include "power_mode.h"
#include "sender.h"
#include "image_codec.h"

#include <atomic>


namespace sender
{

// Previa da transmissao na lista da sala: g_roomPreviewUi e o
// que a tela altera; g_roomPreview e lido pela thread da sala.
bool g_roomPreviewUi = true;

std::atomic<bool> g_roomPreview{ true };



namespace
{

// ============================================================
// Estado da interface
// ============================================================
//
//   FfmpegProblem -> ffmpeg.exe ausente ou quebrado
//   Setup         -> escolha de qualidade / FPS / som
//   Picking       -> escolha da tela ou janela (a sessao ja
//                    vai sendo criada em segundo plano)
//   Live          -> transmitindo
// ============================================================

enum class State
{
    FfmpegProblem,
    Setup,
    Picking,
    Live
};

std::atomic<State> g_state{ State::Setup };

// Protege os textos abaixo (escritos por outras threads, lidos
// pela interface).
std::mutex g_textMutex;

std::string g_status;

std::string g_ffmpegProblem;

std::string g_inviteText;

std::string g_localIp;

std::string g_replyStatus;

bool g_replyOk = false;

std::atomic<bool> g_symmetricNat{ false };

// Escolhas da tela inicial (so a thread da janela mexe).
int g_qualityIndex = 3;     // nativa
int g_fpsIndex = 0;         // 60
int g_audioIndex = 0;       // padrao do Windows

std::vector<AudioOutputDevice> g_audioDevices;

std::vector<std::string> g_audioDeviceNames;

char g_replyInput[64]{};


// ------------------------------------------------------------
// Sessao (criada em segundo plano, ver StartStreamingWorker)
// ------------------------------------------------------------

std::atomic<bool> g_sessionStarted{ false };

std::atomic<bool> g_sessionPending{ false };

// Transmitindo dentro de uma sala (ver sender.h SetRoomMode).
bool g_roomMode = false;


// ------------------------------------------------------------
// Seletor (so a thread da janela mexe)
// ------------------------------------------------------------

std::vector<CaptureTarget> g_monitors;
std::vector<CaptureTarget> g_windows;

std::vector<PickerEntry> g_monitorEntries;
std::vector<PickerEntry> g_windowEntries;

// Miniaturas do seletor sendo tiradas (ver capture_thumbnails).
std::shared_ptr<ThumbnailBatch> g_thumbnailBatch;

bool g_thumbnailsApplied = true;

bool g_windowAudioSupported = false;

bool g_windowAudioOnly = false;

std::string g_pickerError;

// O que esta sendo compartilhado agora (vazio = nada ainda).
std::string g_sharingLabel;


void RefreshPickerLists()
{
    g_monitors = ListCaptureMonitors();

    g_windows = ListCaptureWindows(g_hwnd);

    g_monitorEntries.clear();

    for (const CaptureTarget& target : g_monitors)
    {
        PickerEntry entry;

        entry.title = target.title;
        entry.detail = target.detail;
        entry.thumbnailPending = true;

        g_monitorEntries.push_back(entry);
    }

    g_windowEntries.clear();

    for (const CaptureTarget& target : g_windows)
    {
        PickerEntry entry;

        entry.title = target.title;
        entry.detail = target.detail;
        entry.minimized = target.minimized;
        entry.thumbnailPending = !target.minimized;

        g_windowEntries.push_back(entry);
    }

    // Miniaturas novas (numa thread; aparecem quando ficarem
    // prontas). As antigas saem.
    ClientViewClearThumbnails();

    g_thumbnailBatch =
        ThumbnailBatch::Start(g_monitors, g_windows);

    g_thumbnailsApplied = false;
}


// Miniaturas prontas -> texturas nas entradas do seletor.
void ApplyThumbnailsIfReady()
{
    if (g_thumbnailsApplied ||
        !g_thumbnailBatch ||
        !g_thumbnailBatch->IsDone())
    {
        return;
    }

    g_thumbnailsApplied = true;

    auto apply =
        [](std::vector<PickerEntry>& entries,
           const std::vector<Thumbnail>& thumbs)
        {
            for (size_t i = 0;
                 i < entries.size() && i < thumbs.size();
                 ++i)
            {
                entries[i].thumbnailPending = false;

                const Thumbnail& thumb = thumbs[i];

                if (thumb.pixels.empty())
                    continue;

                entries[i].thumbnail =
                    ClientViewAddThumbnail(
                        thumb.pixels.data(),
                        thumb.width,
                        thumb.height);

                entries[i].thumbnailWidth =
                    static_cast<float>(thumb.width);

                entries[i].thumbnailHeight =
                    static_cast<float>(thumb.height);
            }
        };

    apply(g_monitorEntries, g_thumbnailBatch->Monitors());

    apply(g_windowEntries, g_thumbnailBatch->Windows());

    g_thumbnailBatch.reset();
}


void OpenPicker()
{
    g_pickerError.clear();

    RefreshPickerLists();

    g_state = State::Picking;
}


// Abre outra copia do programa pedindo administrador (a janela
// do UAC) e fecha esta. Cancelou o UAC: continua aqui.
void RelaunchAsAdmin()
{
    wchar_t path[MAX_PATH]{};

    DWORD length =
        GetModuleFileNameW(
            nullptr,
            path,
            MAX_PATH
        );

    if (length == 0 ||
        length >= MAX_PATH)
    {
        g_pickerError =
            "Não consegui achar o próprio programa. Feche e abra "
            "com botão direito > Executar como administrador.";

        return;
    }

    SHELLEXECUTEINFOW info{};

    info.cbSize = sizeof(info);
    info.lpVerb = L"runas";
    info.lpFile = path;
    info.nShow = SW_SHOWNORMAL;

    // A copia nova espera esta fechar (ver wWinMain).
    // --transmitir: a copia nova ja abre no modo Transmitir.
    std::wstring parameters =
        L"--transmitir --esperar " +
        std::to_wstring(GetCurrentProcessId());

    info.lpParameters = parameters.c_str();

    if (!ShellExecuteExW(&info))
    {
        DWORD error = GetLastError();

        std::cout
            << "Abrir como administrador falhou (erro "
            << error
            << ").\n";

        if (error != ERROR_CANCELLED)
        {
            g_pickerError =
                "Não consegui abrir como administrador. Feche e "
                "abra com botão direito > Executar como "
                "administrador.";
        }

        return;
    }

    std::cout
        << "Reabrindo como administrador.\n";

    // Fecha pelo caminho normal (encerra a sessao e as threads).
    PostMessageW(
        g_hwnd,
        WM_CLOSE,
        0,
        0
    );
}


void PickTarget(
    const CaptureTarget& target)
{
    std::string error;

    bool windowAudio =
        g_windowAudioSupported &&
        g_windowAudioOnly;

    if (!StartCaptureForTarget(
            target,
            windowAudio,
            error))
    {
        g_pickerError = error;

        RefreshPickerLists();

        return;
    }

    g_sharingLabel =
        target.kind == CaptureTargetKind::Monitor
        ? target.title + " (" + target.detail + ")"
        : target.title;

    g_state = State::Live;
}


void SetStatus(
    const std::string& text)
{
    std::lock_guard<std::mutex> lock(g_textMutex);

    g_status = text;
}


// ============================================================
// StartStreamingWorker
// ============================================================
//
// Roda em thread propria depois que o usuario clica em
// transmitir: registra a sessao no servidor de sinalizacao (ou
// prepara o convite manual, se nao houver servidor), enquanto a
// pessoa escolhe o que compartilhar.
//
// Fica fora da thread da janela porque pode demorar: o servidor
// gratuito leva ate ~1 minuto pra "acordar".
// ============================================================

void StartStreamingWorker()
{
    g_sessionPending = true;

    SetStatus("Preparando a rede");

    // Os sockets de video/audio sobem em threads proprias e
    // fazem a consulta STUN ao iniciar - espera (ate 8s) os
    // dois terminarem, pra registrar a sessao ja com o
    // endereco publico.
    for (int i = 0;
         i < 160 &&
         !(g_udpServerStarted && g_audioServerStarted);
         ++i)
    {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(50)
        );
    }

    const StunResult& videoStun =
        g_udpServer.GetStunResult();

    const StunResult& audioStun =
        g_udpAudioServer.GetStunResult();

    if (videoStun.symmetric || audioStun.symmetric)
    {
        g_symmetricNat = true;

        std::cout
            << "AVISO: esta rede usa NAT simetrico. Conexoes "
            << "de fora da sua rede local podem nao "
            << "funcionar.\n\n";
    }

    // Hole punching: quando um receiver de outra rede entra na
    // sessao, o servidor de sinalizacao informa o endereco
    // publico dele. Mandamos pacotes pra la pro HELLO dele
    // conseguir passar pelo nosso roteador.
    g_signalingHost.SetViewerCallback(
        [](bool isAudio,
           const std::string& ip,
           uint16_t port)
        {
            if (isAudio)
            {
                g_udpAudioServer.PunchTo(ip, port);
            }
            else
            {
                g_udpServer.PunchTo(ip, port);
            }
        }
    );

    SetStatus("Criando a sessão");

    std::string localIp =
        GetPrimaryLocalIp();

    if (g_signalingHost.Start(
            5001,
            AUDIO_SERVER_UDP_PORT,
            videoStun.ok ? videoStun.publicIp : std::string(),
            videoStun.ok ? videoStun.publicPort : 0,
            audioStun.ok ? audioStun.publicPort : 0,
            CryptoToHex(
                g_hostKeys.publicKey,
                CRYPTO_KEY_SIZE)))
    {
        std::cout
            << "CODIGO DA SESSAO: "
            << g_signalingHost.GetCode()
            << "\n";
    }
    else
    {
        // ----------------------------------------------------
        // Sem servidor de sessoes: convite manual. A mesma
        // informacao que o servidor trocaria vai num texto que
        // o usuario envia por qualquer mensageiro.
        // ----------------------------------------------------

        HostInvite invite;

        if (videoStun.ok &&
            ParseIpv4(
                videoStun.publicIp,
                invite.publicIp))
        {
            invite.publicVideoPort =
                videoStun.publicPort;

            invite.publicAudioPort =
                audioStun.ok
                ? audioStun.publicPort
                : AUDIO_SERVER_UDP_PORT;
        }

        ParseIpv4(
            localIp,
            invite.localIp
        );

        invite.localVideoPort = 5001;

        invite.localAudioPort = AUDIO_SERVER_UDP_PORT;

        std::memcpy(
            invite.hostKey,
            g_hostKeys.publicKey,
            CRYPTO_KEY_SIZE
        );

        std::string inviteText =
            EncodeHostInvite(invite);

        std::cout
            << "Sem codigo de sessao (servidor de sinalizacao "
            << (SignalingIsDisabled()
                ? "desativado"
                : "indisponivel")
            << "). Convite manual: "
            << inviteText
            << "\n";

        {
            std::lock_guard<std::mutex> lock(g_textMutex);

            g_inviteText = inviteText;

            g_localIp = localIp;
        }

        g_inviteMode = true;

        // Sem sessao por codigo nao ha como o receiver provar
        // que foi convidado pelo IP - libera a conexao direta.
        // O stream continua cifrado.
        g_udpServer.SetAllowAnonymous(true);

        g_udpAudioServer.SetAllowAnonymous(true);
    }

    g_sessionPending = false;
}


// ============================================================
// ApplyInviteReply
// ============================================================
//
// Convite manual: a RESPOSTA de quem assiste traz o endereco
// publico dele. Mandamos pacotes pra la por alguns segundos, o
// que abre o nosso roteador pro HELLO dele entrar.
// ============================================================

void ApplyInviteReply(
    const std::string& text)
{
    ViewerReply reply;

    std::string status;

    bool ok = false;

    if (!LooksLikeViewerReply(text))
    {
        status =
            "Isso não parece uma resposta. Ela começa com "
            "SPV1.";
    }
    else if (!DecodeViewerReply(text, reply))
    {
        status =
            "Resposta incompleta ou alterada. Peça para a "
            "pessoa copiar de novo.";
    }
    else
    {
        std::string ip =
            FormatIpv4(reply.publicIp);

        status =
            "Resposta aceita: abrindo caminho para " +
            ip +
            ".";

        ok = true;

        std::cout
            << "Resposta de convite aceita: "
            << ip
            << "\n";

        // Repete por alguns segundos, em thread propria, pra
        // interface nao travar.
        std::thread(
            [ip, reply]
            {
                for (int i = 0; i < 8; ++i)
                {
                    g_udpServer.PunchTo(
                        ip,
                        reply.publicVideoPort
                    );

                    g_udpAudioServer.PunchTo(
                        ip,
                        reply.publicAudioPort
                    );

                    std::this_thread::sleep_for(
                        std::chrono::seconds(2)
                    );
                }
            }
        ).detach();
    }

    std::lock_guard<std::mutex> lock(g_textMutex);

    g_replyStatus = status;

    g_replyOk = ok;
}


// ============================================================
// Acoes da tela inicial
// ============================================================

// Passa a qualidade/FPS escolhidos na tela pro encoder.
// Durante a transmissao, pede pra reiniciar o encoder (o
// receiver percebe a resolucao nova sozinho e se ajusta).
void ApplyQualitySelection()
{
    static const uint32_t heights[] =
        { 480, 720, 1080, 0 };

    static const uint32_t frameRates[] =
        { 60, 30, 15 };

    uint32_t height =
        heights[g_qualityIndex >= 0 && g_qualityIndex < 4
            ? g_qualityIndex
            : 3];

    uint32_t fps =
        frameRates[g_fpsIndex >= 0 && g_fpsIndex < 3
            ? g_fpsIndex
            : 0];

    bool changed =
        height != g_targetOutputHeight.load() ||
        fps != g_targetFps.load();

    g_targetOutputHeight = height;

    g_targetFps = fps;

    std::cout
        << "Qualidade selecionada: "
        << (height > 0
            ? std::to_string(height) + "p"
            : std::string("nativa"))
        << " @ "
        << fps
        << " FPS\n";

    if (changed &&
        g_captureRunning)
    {
        g_encoderRestartRequested = true;
    }
}


void BeginStreaming()
{
    if (g_state != State::Setup)
        return;

    // Ao voltar do seletor pra esta tela, as escolhas podem
    // mudar (valem pra proxima captura), mas a sessao ja
    // criada continua a mesma.

    ApplyQualitySelection();

    // Indice 0 da lista = padrao do Windows (id vazio).
    if (g_audioIndex >= 1 &&
        g_audioIndex <=
            static_cast<int>(g_audioDevices.size()))
    {
        g_audioCapture.SelectDevice(
            g_audioDevices[g_audioIndex - 1].id
        );
    }
    else
    {
        g_audioCapture.SelectDevice(
            std::wstring()
        );
    }

    if (!g_sessionStarted.exchange(true))
    {
        std::thread(
            StartStreamingWorker
        ).detach();
    }

    OpenPicker();
}


std::string QualityLabel()
{
    uint32_t target =
        g_targetOutputHeight.load();

    uint32_t fps =
        g_targetFps.load();

    uint32_t height =
        target > 0
        ? target
        : g_captureHeight;

    return
        (target > 0
            ? std::to_string(target) + "p"
            : std::string("nativa")) +
        " · " +
        std::to_string(fps) +
        " FPS · " +
        std::to_string(
            PickBitrateMbps(height, fps)) +
        " Mbps";
}


// ============================================================
// Desenho + acoes de um frame
// ============================================================

void DrawUi(
    ClientScreenActions& actions)
{
    State state = g_state.load();

    if (state == State::FfmpegProblem)
    {
        std::string problem;

        {
            std::lock_guard<std::mutex> lock(g_textMutex);

            problem = g_ffmpegProblem;
        }

        DrawClientFfmpegProblemScreen(
            problem,
            actions
        );

        return;
    }

    if (state == State::Picking)
    {
        ClientPickerModel model;

        model.monitors = &g_monitorEntries;
        model.windows = &g_windowEntries;
        model.windowAudioOnly = &g_windowAudioOnly;
        model.windowAudioSupported = g_windowAudioSupported;
        model.switching = g_captureRunning;
        model.error = g_pickerError;
        model.offerAdmin =
            g_captureAccessDenied &&
            !g_pickerError.empty();

        DrawClientPickerScreen(
            model,
            actions
        );

        return;
    }

    if (state == State::Setup)
    {
        ClientSetupModel model;

        model.audioDevices = &g_audioDeviceNames;
        model.qualityIndex = &g_qualityIndex;
        model.fpsIndex = &g_fpsIndex;
        model.audioIndex = &g_audioIndex;
        model.busy = false;

        // Numa sala da pra voltar sempre que nao estiver
        // capturando (a sessao criada e descartada); fora dela,
        // so antes de criar a sessao.
        model.canGoBack =
            g_roomMode
            ? !g_captureRunning && !g_sessionPending
            : !g_sessionStarted;

        {
            std::lock_guard<std::mutex> lock(g_textMutex);

            model.status = g_status;
        }

        DrawClientSetupScreen(
            model,
            actions
        );

        return;
    }

    ClientLiveModel model;

    model.sessionPending = g_sessionPending;

    model.sessionCode =
        g_signalingHost.GetCode();

    model.sharingLabel = g_sharingLabel;

    model.windowAudioOnly =
        g_audioCapture.IsCapturingProcess();

    {
        std::lock_guard<std::mutex> lock(g_textMutex);

        model.sessionStatus = g_status;
        model.invite = g_inviteText;
        model.localIp = g_localIp;
        model.replyStatus = g_replyStatus;
        model.replyOk = g_replyOk;
    }

    model.viewers =
        static_cast<int>(g_udpServer.GetViewerCount());

    model.maxViewers =
        static_cast<int>(MAX_VIEWERS);

    model.capturing = g_captureRunning;
    model.captureWidth = g_captureWidth;
    model.captureHeight = g_captureHeight;
    model.qualityLabel = QualityLabel();
    model.qualityIndex = &g_qualityIndex;
    model.fpsIndex = &g_fpsIndex;
    model.inRoom = g_roomMode;
    model.roomPreview = g_roomMode ? &g_roomPreviewUi : nullptr;
    model.canStop = !g_sessionPending;

    // Upload estimado: uma copia do video por pessoa, mais ~8%
    // de cabecalhos, criptografia e reenvios.
    if (model.viewers > 0)
    {
        uint32_t target =
            g_targetOutputHeight.load();

        uint32_t height =
            target > 0
            ? target
            : (g_captureHeight > 0 ? g_captureHeight : 1080);

        double perViewer =
            PickBitrateMbps(height, g_targetFps.load()) * 1.08;

        int total =
            static_cast<int>(perViewer * model.viewers + 0.5);

        model.uploadLabel =
            "Upload estimado: ~" +
            std::to_string(total) +
            " Mbps";

        model.uploadWarning =
            total > 40;
    }
    model.symmetricNat = g_symmetricNat;
    model.replyInput = g_replyInput;
    model.replyInputSize = sizeof(g_replyInput);

    DrawClientLiveScreen(
        model,
        actions
    );
}

} // namespace


// ============================================================
// Configuracoes salvas (ver app_settings.h)
// ============================================================

AppSettings& ClientSettings()
{
    static AppSettings settings(L"client.ini");

    return settings;
}


int ClampIndex(
    int value,
    int count)
{
    return
        value >= 0 && value < count
        ? value
        : 0;
}


// Depois de montar a lista de dispositivos de som.
void LoadClientSettings()
{
    AppSettings& settings =
        ClientSettings();

    g_qualityIndex =
        ClampIndex(settings.GetInt(L"Qualidade", 3), 4);

    g_fpsIndex =
        ClampIndex(settings.GetInt(L"FPS", 0), 3);

    // Som: guardado pelo id do dispositivo (a posicao na lista
    // muda quando se pluga/despluga um fone).
    std::wstring deviceId =
        settings.GetString(L"DispositivoSom", L"");

    g_audioIndex = 0;

    for (size_t i = 0; i < g_audioDevices.size(); ++i)
    {
        if (!deviceId.empty() &&
            g_audioDevices[i].id == deviceId)
        {
            g_audioIndex = static_cast<int>(i) + 1;

            break;
        }
    }

    if (g_windowAudioSupported)
    {
        g_windowAudioOnly =
            settings.GetInt(L"SomSoDaJanela", 1) != 0;
    }

    g_roomPreviewUi =
        settings.GetInt(L"PreviaNaSala", 1) != 0;

    g_roomPreview = g_roomPreviewUi;
}


// Chamada a cada frame: grava so quando algo mudou e ficou
// parado por meio segundo.
void SaveClientSettingsIfChanged()
{
    struct Saved
    {
        int quality;
        int fps;
        int audio;
        bool windowAudio;

        bool operator==(const Saved& other) const
        {
            return quality == other.quality &&
                   fps == other.fps &&
                   audio == other.audio &&
                   windowAudio == other.windowAudio;
        }
    };

    static bool haveSaved = false;
    static Saved saved{};
    static Saved pending{};
    static ULONGLONG pendingSince = 0;

    Saved now{
        g_qualityIndex,
        g_fpsIndex,
        g_audioIndex,
        g_windowAudioOnly
    };

    if (!haveSaved)
    {
        saved = now;
        pending = now;
        haveSaved = true;

        return;
    }

    ULONGLONG tick =
        GetTickCount64();

    if (!(now == pending))
    {
        pending = now;
        pendingSince = tick;

        return;
    }

    if (pending == saved ||
        tick - pendingSince < 500)
    {
        return;
    }

    AppSettings& settings =
        ClientSettings();

    settings.SetInt(L"Qualidade", pending.quality);
    settings.SetInt(L"FPS", pending.fps);

    settings.SetString(
        L"DispositivoSom",
        pending.audio >= 1 &&
        pending.audio <= static_cast<int>(g_audioDevices.size())
            ? g_audioDevices[pending.audio - 1].id
            : std::wstring());

    if (g_windowAudioSupported)
    {
        settings.SetInt(
            L"SomSoDaJanela",
            pending.windowAudio ? 1 : 0);
    }

    saved = pending;
}


// ============================================================
// ClientUiInit / Shutdown
// ============================================================

void ClientUiInit(
    HWND hwnd,
    ID3D11Device* device,
    ID3D11DeviceContext* context)
{
    g_hwnd = hwnd;

    ClientViewInit(
        device,
        context);

    // Som so da janela: so existe no Windows 11 em diante.
    // Quando existe, ja vem marcado (e o que normalmente se
    // quer ao compartilhar uma janela).
    g_windowAudioSupported =
        SupportsPerWindowAudio();

    g_windowAudioOnly =
        g_windowAudioSupported;


    // Dispositivos de som pra lista da tela inicial.
    AudioCapture::ListDevices(g_audioDevices);

    g_audioDeviceNames.clear();

    g_audioDeviceNames.push_back("Padrão do Windows");

    for (const AudioOutputDevice& device : g_audioDevices)
    {
        g_audioDeviceNames.push_back(device.name);
    }

    // Escolhas da ultima vez (qualidade, FPS, som).
    LoadClientSettings();

    // O encoder de video e um ffmpeg.exe: sem ele nao ha
    // transmissao, entao a interface abre direto na tela que
    // explica como resolver.
    std::string problem;

    if (!CheckFfmpegAvailable(problem))
    {
        std::cout
            << "Problema com o ffmpeg: "
            << problem
            << "\n";

        g_ffmpegProblem = problem;

        g_state = State::FfmpegProblem;
    }
}




// ============================================================
// StopSharingInternal
// ============================================================
//
// "Parar de transmitir": encerra a captura e a sessao, esquece
// quem estava assistindo e volta pra tela de opcoes. O motor
// (portas, threads, encoder) continua ligado.
// ============================================================

void StopSharingInternal()
{
    // A sessao ainda esta sendo criada numa thread: parar agora
    // deixaria uma sessao orfa no servidor.
    if (g_sessionPending)
        return;

    std::cout
        << "\nTransmissao encerrada (Parar de transmitir).\n";

    StopCapture();

    g_signalingHost.Stop();

    g_udpServer.ClearViewers();
    g_udpAudioServer.ClearViewers();

    g_udpServer.SetAllowAnonymous(false);
    g_udpAudioServer.SetAllowAnonymous(false);

    g_inviteMode = false;

    {
        std::lock_guard<std::mutex> lock(g_textMutex);

        g_inviteText.clear();
        g_replyStatus.clear();
        g_status.clear();
    }

    {
        std::lock_guard<std::mutex> lock(g_displayMutex);

        g_displayPixels.clear();
        g_displayWidth = 0;
        g_displayHeight = 0;
    }

    ClientViewClearPreview();

    g_sharingLabel.clear();

    g_pickerError.clear();

    g_sessionStarted = false;

    g_state = State::Setup;
}


// ============================================================
// ClientUiDrawFrame
// ============================================================
//
// Um frame do modo Transmitir, dentro do frame da interface do
// programa: previa ao fundo, telas por cima, e o que foi
// clicado.
// ============================================================

void ClientUiDrawFrame(
    bool& outGoHome)
{
    ClientViewNewFrame();

    ApplyThumbnailsIfReady();

    // Saiu do seletor: as miniaturas nao servem mais (libera a
    // memoria delas).
    static bool hadThumbnails = false;

    if (g_state == State::Picking)
    {
        hadThumbnails = true;
    }
    else if (hadThumbnails)
    {
        hadThumbnails = false;

        ClientViewClearThumbnails();

        g_thumbnailBatch.reset();

        g_thumbnailsApplied = true;

        for (PickerEntry& entry : g_monitorEntries)
            entry.thumbnail = nullptr;

        for (PickerEntry& entry : g_windowEntries)
            entry.thumbnail = nullptr;
    }

    // --------------------------------------------------------
    // Previa da tela compartilhada, no maximo ~10x por segundo:
    // e so um espelho pra conferencia, nao precisa (nem deve)
    // gastar CPU/GPU copiando 60 telas inteiras por segundo.
    // --------------------------------------------------------

    static ULONGLONG lastPreviewTime = 0;

    ULONGLONG now = GetTickCount64();

    if (now - lastPreviewTime >= 100)
    {
        lastPreviewTime = now;

        std::lock_guard<std::mutex> lock(g_displayMutex);

        size_t needed =
            static_cast<size_t>(g_displayWidth) *
            g_displayHeight *
            4;

        if (needed > 0 &&
            g_displayPixels.size() >= needed)
        {
            ClientViewSetPreview(
                g_displayPixels.data(),
                g_displayWidth,
                g_displayHeight
            );
        }
        else
        {
            ClientViewClearPreview();
        }
    }


    SaveClientSettingsIfChanged();

    ClientViewDrawBackground();

    ClientScreenActions actions;

    DrawUi(actions);

    outGoHome =
        actions.goHome;

    // Saindo das telas do modo numa sala antes de escolher o que
    // compartilhar: a sessao criada nao serve pra nada.
    if (actions.goHome &&
        g_roomMode &&
        g_sessionStarted &&
        !g_captureRunning)
    {
        StopSharingInternal();
    }

    if (actions.stopSharing)
    {
        StopSharingInternal();

        // Na sala, volta pra lista; fora dela, fica na tela de
        // opcoes (com "Voltar" pro inicio).
        if (g_roomMode)
        {
            outGoHome = true;
        }
    }


    // --------------------------------------------------------
    // O que o usuario pediu neste frame
    // --------------------------------------------------------

    if (actions.start)
    {
        BeginStreaming();
    }

    if (actions.roomPreviewChanged)
    {
        g_roomPreview = g_roomPreviewUi;

        ClientSettings().SetInt(
            L"PreviaNaSala", g_roomPreviewUi ? 1 : 0);
    }

    if (actions.qualityChanged)
    {
        ApplyQualitySelection();
    }

    if (actions.openPicker)
    {
        OpenPicker();
    }

    if (actions.refreshPicker)
    {
        g_pickerError.clear();

        RefreshPickerLists();
    }

    if (actions.cancelPicker)
    {
        // Ja transmitindo: volta pra transmissao. Antes da
        // primeira escolha: volta pras opcoes.
        g_state =
            g_captureRunning
            ? State::Live
            : State::Setup;
    }

    if (actions.pickMonitor >= 0 &&
        actions.pickMonitor <
            static_cast<int>(g_monitors.size()))
    {
        // Copia: PickTarget pode atualizar a lista.
        CaptureTarget target =
            g_monitors[actions.pickMonitor];

        PickTarget(target);
    }

    if (actions.pickWindow >= 0 &&
        actions.pickWindow <
            static_cast<int>(g_windows.size()))
    {
        CaptureTarget target =
            g_windows[actions.pickWindow];

        PickTarget(target);
    }

    if (actions.applyReply)
    {
        ApplyInviteReply(
            std::string(g_replyInput)
        );
    }

    if (actions.relaunchAsAdmin)
    {
        RelaunchAsAdmin();
    }

    if (actions.openDownloadPage)
    {
        ShellExecuteW(
            nullptr,
            L"open",
            L"https://www.gyan.dev/ffmpeg/builds/",
            nullptr,
            nullptr,
            SW_SHOWNORMAL
        );
    }

    if (actions.recheckFfmpeg)
    {
        std::string again;

        if (CheckFfmpegAvailable(again))
        {
            g_state = State::Setup;
        }
        else
        {
            std::lock_guard<std::mutex> lock(g_textMutex);

            g_ffmpegProblem = again;
        }
    }
}


// ============================================================
// sender.h - o que o programa usa
// ============================================================

namespace
{

bool g_open = false;

bool g_engineFailed = false;

} // namespace


bool Open(
    HWND hwnd,
    ID3D11Device* device,
    ID3D11DeviceContext* context,
    std::string& outError)
{
    if (g_open)
        return true;

    if (g_engineFailed)
    {
        outError =
            "O modo Transmitir não conseguiu iniciar. Veja o "
            "stream-project.log.";

        return false;
    }

    if (!EngineStart())
    {
        g_engineFailed = true;

        EngineStop();

        outError =
            "Não foi possível iniciar a transmissão (Direct3D "
            "ou criptografia). Atualize o driver da placa de "
            "vídeo e tente de novo.";

        return false;
    }

    ClientUiInit(
        hwnd,
        device,
        context);

    g_open = true;

    return true;
}


void Draw(
    bool& outGoHome)
{
    outGoHome = false;

    if (!g_open)
        return;

    ClientUiDrawFrame(
        outGoHome);
}


bool IsOpen()
{
    return g_open;
}


bool RoomPreviewEnabled()
{
    return g_roomPreview;
}


bool CaptureRoomThumbnail(
    std::vector<uint8_t>& outJpeg)
{
    outJpeg.clear();

    std::vector<uint8_t> small;

    int width = 0;
    int height = 0;

    {
        std::lock_guard<std::mutex> lock(g_displayMutex);

        size_t needed =
            static_cast<size_t>(g_displayWidth) * g_displayHeight * 4;

        if (needed == 0 || g_displayPixels.size() < needed)
            return false;

        ImageDownscaleBgra(
            g_displayPixels.data(),
            static_cast<int>(g_displayWidth),
            static_cast<int>(g_displayHeight),
            static_cast<int>(g_displayWidth * 4),
            320,
            180,
            small,
            width,
            height);
    }

    return ImageEncodeJpeg(small.data(), width, height, 0.7f, outJpeg);
}


bool IsLive()
{
    return
        g_open &&
        g_state == State::Live;
}


void SetRoomMode(
    bool inRoom)
{
    g_roomMode = inRoom;
}


bool GetPublishInfo(
    std::string& outSessionCode,
    std::string& outSessionToken,
    std::string& outLabel)
{
    if (!g_open ||
        g_state != State::Live ||
        !g_captureRunning)
    {
        return false;
    }

    outSessionCode = g_signalingHost.GetCode();

    outSessionToken = g_signalingHost.GetToken();

    outLabel = g_sharingLabel;

    return
        !outSessionCode.empty() &&
        !outSessionToken.empty();
}


void StopSharing()
{
    if (g_open)
    {
        StopSharingInternal();
    }
}


int ViewerCount()
{
    return
        g_open
        ? static_cast<int>(g_udpServer.GetViewerCount())
        : 0;
}


void Shutdown()
{
    if (!g_open)
        return;

    EngineStop();

    ClientViewShutdown();

    g_open = false;
}

} // namespace sender
