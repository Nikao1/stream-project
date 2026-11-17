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
#include "ffmpeg_check.h"
#include "invite_code.h"
#include "ui_common.h"


namespace
{

// ============================================================
// Estado da interface
// ============================================================
//
//   FfmpegProblem -> ffmpeg.exe ausente ou quebrado
//   Setup         -> escolha de qualidade / fluidez / som
//   Starting      -> criando a sessao (thread propria)
//   Live          -> seletor de tela aberto, ou transmitindo
// ============================================================

enum class State
{
    FfmpegProblem,
    Setup,
    Starting,
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
// prepara o convite manual, se nao houver servidor) e so entao
// pede pra janela abrir o seletor de tela do Windows.
//
// Fica fora da thread da janela porque pode demorar: o servidor
// gratuito leva ate ~1 minuto pra "acordar".
// ============================================================

void StartStreamingWorker()
{
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

    // Abre o seletor de tela. PostMessageW e seguro entre
    // threads; a janela trata o WM_APP + 1.
    PostMessageW(
        g_hwnd,
        WM_APP + 1,
        0,
        0
    );

    g_state = State::Live;
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

void BeginStreaming()
{
    if (g_state != State::Setup)
        return;

    static const uint32_t heights[] =
        { 480, 720, 1080, 0 };

    static const uint32_t frameRates[] =
        { 60, 30, 15 };

    g_targetOutputHeight =
        heights[g_qualityIndex >= 0 && g_qualityIndex < 4
            ? g_qualityIndex
            : 3];

    g_targetFps =
        frameRates[g_fpsIndex >= 0 && g_fpsIndex < 3
            ? g_fpsIndex
            : 0];

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

    std::cout
        << "Qualidade selecionada: "
        << (g_targetOutputHeight > 0
            ? std::to_string(g_targetOutputHeight) + "p"
            : std::string("nativa"))
        << " @ "
        << g_targetFps
        << " FPS\n";

    g_state = State::Starting;

    std::thread(
        StartStreamingWorker
    ).detach();
}


std::string QualityLabel()
{
    uint32_t height =
        g_targetOutputHeight > 0
        ? g_targetOutputHeight
        : g_captureHeight;

    return
        (g_targetOutputHeight > 0
            ? std::to_string(g_targetOutputHeight) + "p"
            : std::string("nativa")) +
        " · " +
        std::to_string(g_targetFps) +
        " FPS · " +
        std::to_string(
            PickBitrateMbps(height, g_targetFps)) +
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

    if (state == State::Setup ||
        state == State::Starting)
    {
        ClientSetupModel model;

        model.audioDevices = &g_audioDeviceNames;
        model.qualityIndex = &g_qualityIndex;
        model.fpsIndex = &g_fpsIndex;
        model.audioIndex = &g_audioIndex;
        model.busy = state == State::Starting;

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

    model.sessionCode =
        g_signalingHost.GetCode();

    {
        std::lock_guard<std::mutex> lock(g_textMutex);

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
// ClientUiSetupLog
// ============================================================

void ClientUiSetupLog()
{
    FILE* logFile = nullptr;

    if (freopen_s(
            &logFile,
            "stream-client.log",
            "w",
            stdout) != 0)
    {
        // Nao deu pra criar o arquivo (pasta sem permissao de
        // escrita, ou outro stream-client ja aberto): descarta
        // o log, mas o programa segue funcionando.
        freopen_s(
            &logFile,
            "NUL",
            "w",
            stdout
        );
    }

    // Sem buffer: se o programa fechar de repente, o log ja
    // esta no disco.
    setvbuf(
        stdout,
        nullptr,
        _IONBF,
        0
    );

    std::cerr.rdbuf(
        std::cout.rdbuf()
    );
}


// ============================================================
// ClientUiInit / Shutdown
// ============================================================

bool ClientUiInit(
    HWND hwnd)
{
    if (!ClientViewInit(hwnd))
        return false;

    // Dispositivos de som pra lista da tela inicial.
    AudioCapture::ListDevices(g_audioDevices);

    g_audioDeviceNames.clear();

    g_audioDeviceNames.push_back("Padrão do Windows");

    for (const AudioOutputDevice& device : g_audioDevices)
    {
        g_audioDeviceNames.push_back(device.name);
    }

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

    return true;
}


void ClientUiShutdown()
{
    ClientViewShutdown();
}


// ============================================================
// Mensagens da janela
// ============================================================

bool ClientUiHandleMessage(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    return
        UiHandleWindowMessage(
            hwnd,
            message,
            wParam,
            lParam
        );
}


void ClientUiResize(
    uint32_t width,
    uint32_t height)
{
    ClientViewResize(
        width,
        height
    );
}


// ============================================================
// ClientUiRenderFrame
// ============================================================

void ClientUiRenderFrame()
{
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


    if (!ClientViewBeginFrame())
        return;

    ClientScreenActions actions;

    DrawUi(actions);

    ClientViewEndFrame();


    // --------------------------------------------------------
    // O que o usuario pediu neste frame
    // --------------------------------------------------------

    if (actions.start)
    {
        BeginStreaming();
    }

    if (actions.openPicker)
    {
        PostMessageW(
            g_hwnd,
            WM_APP + 1,
            0,
            0
        );
    }

    if (actions.applyReply)
    {
        ApplyInviteReply(
            std::string(g_replyInput)
        );
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
