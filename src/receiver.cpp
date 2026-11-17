#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <fstream>
#include <iterator>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <iostream>
#include <string>
#include <cstdint>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>

#include "h264_decoder.h"
#include "audio_protocol.h"
#include "audio_decoder.h"
#include "audio_playback.h"
#include "video_protocol.h"
#include "signaling_client.h"
#include "nat_traversal.h"
#include "ffmpeg_check.h"

#pragma comment(lib, "ws2_32.lib")


// ============================================================
// Configuração
// ============================================================

constexpr uint16_t UDP_PORT =
    5002;

constexpr uint16_t SERVER_UDP_PORT =
    5001;


// ============================================================
// Protocolo UDP (orientado a frame) - ver video_protocol.h
// ============================================================


// ============================================================
// Buffer UDP
// ============================================================

constexpr int UDP_RECEIVE_BUFFER_SIZE =
    64 * 1024 * 1024;


// ============================================================
// Variáveis globais
// ============================================================

HWND g_hwnd =
    nullptr;


// IP do computador que roda o stream-client - preenchido pelo
// prompt no início de main(). Padrão 127.0.0.1 (mesma máquina).
std::string g_serverIp =
    "127.0.0.1";


// Portas do client (video/audio). Os valores padrao valem pra
// conexao por IP; entrando por codigo de sessao, vem do
// servidor de sinalizacao.
uint16_t g_serverVideoPort =
    SERVER_UDP_PORT;

uint16_t g_serverAudioPort =
    AUDIO_SERVER_UDP_PORT;


// Porta local realmente usada pelo socket de video (pode nao
// ser a padrao, se ela ja estiver ocupada).
uint16_t g_localVideoPort =
    UDP_PORT;


// ============================================================
// BindUdpWithFallback
// ============================================================
//
// Tenta a porta padrao; se ja estiver em uso (ex.: outro
// receiver aberto nesta mesma maquina), deixa o Windows
// escolher uma porta livre. Funciona porque o client responde
// pro endereco de onde o HELLO veio, seja qual for a porta.
//
// Devolve a porta usada, ou 0 se nao deu.
// ============================================================

uint16_t BindUdpWithFallback(
    SOCKET socket,
    uint16_t preferredPort)
{
    sockaddr_in address{};

    address.sin_family =
        AF_INET;

    address.sin_addr.s_addr =
        htonl(INADDR_ANY);

    address.sin_port =
        htons(preferredPort);

    if (bind(
            socket,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) == 0)
    {
        return preferredPort;
    }

    address.sin_port =
        0;

    if (bind(
            socket,
            reinterpret_cast<sockaddr*>(&address),
            sizeof(address)) != 0)
    {
        return 0;
    }

    sockaddr_in bound{};

    int boundSize =
        sizeof(bound);

    if (getsockname(
            socket,
            reinterpret_cast<sockaddr*>(&bound),
            &boundSize) != 0)
    {
        return 0;
    }

    return ntohs(bound.sin_port);
}


// Codigo da sessao em que entramos (vazio = conexao direta por
// IP, sem servidor de sinalizacao).
std::string g_sessionCode;


// ============================================================
// AnnounceToSession
// ============================================================
//
// Hole punching, lado do receiver: descobre o endereco publico
// deste socket (STUN) e avisa o host pelo servidor de
// sinalizacao. Com isso o host manda pacotes pra ca e o
// roteador dele passa a aceitar os nossos HELLOs.
//
// Chamar logo depois do bind, antes de comecar a ler do
// socket. Nao faz nada em conexao direta por IP.
// ============================================================

void AnnounceToSession(
    SOCKET socket,
    bool isAudio)
{
    if (g_sessionCode.empty())
        return;


    const char* channel =
        isAudio ? "audio" : "video";


    StunResult stun =
        StunDiscover(socket);


    if (!stun.ok)
    {
        std::cout
            << "STUN ("
            << channel
            << "): sem resposta - conexao so pela rede "
            << "local.\n";

        return;
    }


    std::cout
        << "Endereco publico ("
        << channel
        << "): "
        << stun.publicIp
        << ":"
        << stun.publicPort
        << "\n";


    if (stun.symmetric)
    {
        std::cout
            << "AVISO: esta rede usa NAT simetrico. A conexao "
            << "com outra rede pode nao funcionar.\n";
    }


    std::string error;


    if (!SignalingAnnounceViewer(
            g_sessionCode,
            isAudio,
            stun.publicIp,
            stun.publicPort,
            error))
    {
        std::cout
            << "Sinalizacao ("
            << channel
            << "): "
            << error
            << "\n";
    }
}


// ============================================================
// Renderizador Direct3D 11
//
// Substitui o desenho via GDI (StretchDIBits/BitBlt) por uma
// textura + shader simples. Motivo: GDI tem um histórico de
// comportamentos dependentes de driver difíceis de prever (um
// deles já causou distorção real neste projeto); D3D11 dá
// controle total sobre exatamente como o frame é escalado,
// sem depender de como cada driver interpreta chamadas GDI
// legadas.
//
// Funciona em qualquer GPU (Intel/AMD/NVIDIA) - D3D11 é uma
// API padrão do Windows, não específica de fabricante.
// ============================================================

using Microsoft::WRL::ComPtr;

ComPtr<ID3D11Device> g_rendererDevice;
ComPtr<ID3D11DeviceContext> g_rendererContext;
ComPtr<IDXGISwapChain> g_swapChain;
ComPtr<ID3D11RenderTargetView> g_renderTargetView;

ComPtr<ID3D11VertexShader> g_vertexShader;
ComPtr<ID3D11PixelShader> g_pixelShader;
ComPtr<ID3D11SamplerState> g_samplerState;
ComPtr<ID3D11Buffer> g_transformBuffer;

ComPtr<ID3D11Texture2D> g_videoTexture;
ComPtr<ID3D11ShaderResourceView> g_videoSRV;

uint32_t g_videoTextureWidth =
    0;

uint32_t g_videoTextureHeight =
    0;

std::mutex g_rendererMutex;


SOCKET g_udpSocket =
    INVALID_SOCKET;


// ============================================================
// Endereço do servidor
//
// É preenchido no main() depois de configurar
// 127.0.0.1:5001.
// ============================================================

sockaddr_in g_serverAddress{};


// ============================================================
// Caminho alternativo ate o client
// ============================================================
//
// Entrando por codigo de sessao recebemos DOIS enderecos do
// client: o local (mesma rede) e o publico (internet). O
// servidor de sinalizacao chuta qual usar, mas pode errar -
// ex.: duas maquinas na mesma casa, uma saindo por IPv4 e a
// outra por IPv6, parecem estar em redes diferentes.
//
// Entao mandamos HELLO pros dois e seguimos com o caminho por
// onde o stream de fato chegar.
// ============================================================

std::string g_serverAltIp;

uint16_t g_serverAltVideoPort =
    0;

uint16_t g_serverAltAudioPort =
    0;

sockaddr_in g_serverAltAddress{};

bool g_haveAltAddress =
    false;


bool SameEndpoint(
    const sockaddr_in& a,
    const sockaddr_in& b)
{
    return
        a.sin_addr.s_addr == b.sin_addr.s_addr &&
        a.sin_port == b.sin_port;
}


// Monta um sockaddr_in a partir de IP + porta. false se o IP
// nao for um IPv4 valido.
bool MakeEndpoint(
    const std::string& ip,
    uint16_t port,
    sockaddr_in& outAddress)
{
    outAddress = {};

    outAddress.sin_family =
        AF_INET;

    outAddress.sin_port =
        htons(port);

    return
        port != 0 &&
        inet_pton(
            AF_INET,
            ip.c_str(),
            &outAddress.sin_addr) == 1;
}


// ============================================================
// Controle do stream
//
// false = ainda não recebemos vídeo válido
// true  = já recebemos pelo menos um pacote de vídeo válido
// ============================================================

std::atomic<bool>
g_streamReceived{ false };


// ============================================================
// Decoder
// ============================================================

H264Decoder g_decoder;

AudioDecoder g_audioDecoder;

AudioPlayback g_audioPlayback;


// ============================================================
// Frame decodificado
// ============================================================

std::vector<unsigned char>
g_framePixels;


uint32_t g_frameWidth =
    0;


uint32_t g_frameHeight =
    0;


uint64_t g_frameNumber =
    0;


// ============================================================
// Mutex do frame
// ============================================================

std::mutex g_frameMutex;


// ============================================================
// Controle
// ============================================================

std::atomic<bool>
g_running{ true };


// ============================================================
// FPS
// ============================================================

std::atomic<int>
g_fps{ 0 };


int g_fpsFrameCount =
    0;


ULONGLONG g_fpsStartTime =
    GetTickCount64();


// ============================================================
// Reassembly de frame
//
// Mantemos apenas o frame "atual" sendo montado. Se chegar um
// fragmento de um frameId mais novo antes do atual completar,
// o atual é descartado (favorece latência baixa: preferimos
// pular um frame a acumular atraso).
// ============================================================

struct PendingFrame
{
    uint32_t frameId = 0;
    uint32_t frameSize = 0;
    uint32_t fragCount = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t timestampMs = 0;
    bool isKeyframe = false;
    bool active = false;

    std::vector<unsigned char> data;
    std::vector<bool> fragReceived;
    uint32_t fragReceivedCount = 0;
};

PendingFrame g_pendingFrame;


// ============================================================
// Sincronização A/V
// ============================================================
//
// video e audio chegam com timestamps relativos à MESMA
// origem de tempo no client (ver g_streamClockStart em
// main.cpp). Aqui calibramos, no primeiro pacote (de
// qualquer um dos dois streams) que chegar, a tradução desse
// tempo do sender pro relógio local do receiver:
//
//   local_target_time = g_syncBaseline + sender_timestamp_ms
//
// O vídeo usa isso pra segurar a exibição de um frame até a
// hora certa (em vez de mostrar assim que decodifica). O
// áudio não precisa desse tratamento - o WASAPI já toca no
// ritmo real (hardware clock), então só participamos da
// calibração do relógio comum por ele também.
// ============================================================

std::mutex g_syncMutex;

bool g_haveSyncBaseline =
    false;

ULONGLONG g_syncBaseline =
    0;


ULONGLONG EstablishSyncBaseline(
    uint32_t senderTimestampMs)
{
    std::lock_guard<std::mutex> lock(
        g_syncMutex
    );


    if (!g_haveSyncBaseline)
    {
        g_syncBaseline =
            GetTickCount64() -
            senderTimestampMs;

        g_haveSyncBaseline =
            true;
    }


    return g_syncBaseline;
}


// Timestamps dos frames de vídeo, na mesma ordem em que foram
// entregues ao decoder (fila FIFO - o decoder não reordena).
std::deque<uint32_t> g_videoTimestampQueue;


struct PendingVideoFrame
{
    std::vector<unsigned char> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t timestampMs = 0;
};

// Frames já decodificados, aguardando a hora certa (segundo o
// relógio sincronizado) de aparecer na tela.
std::deque<PendingVideoFrame> g_pendingVideoFrames;


// ============================================================
// Estatísticas
// ============================================================

uint32_t g_lastCompletedFrameId =
    0;

bool g_haveCompletedFrame =
    false;

uint64_t g_framesCompleted =
    0;

// true = o decoder nao pode receber frames ate chegar um
// keyframe. Um frame comum (P) depende do anterior; se algum
// se perdeu no caminho, os seguintes sairiam corrompidos (ou
// derrubariam o ffmpeg). Entao, depois de qualquer perda,
// esperamos o proximo keyframe (o client manda um por segundo).
bool g_waitingKeyframe =
    true;

ULONGLONG g_lastDecoderRestart =
    0;

// Quantas vezes o decoder morreu nesta execucao. Na segunda,
// troca a decodificacao por GPU pela de CPU (mais compativel).
int g_decoderDeaths =
    0;

uint64_t g_framesDropped =
    0;


// ============================================================
// Processar frame decodificado
// ============================================================

void TryPresentPendingFrames();

void ProcessDecodedFrame()
{
    std::vector<unsigned char>
        decodedFrame;


    // ========================================================
    // Pode haver mais de um frame esperando no decoder.
    // Pegamos todos, ficando no final com o mais recente.
    // ========================================================

    while (
        g_decoder.GetFrame(
            decodedFrame
        ))
    {
        if (decodedFrame.empty())
        {
            return;
        }


        uint32_t width =
            g_frameWidth;


        uint32_t height =
            g_frameHeight;


        // ====================================================
        // Timestamp correspondente (FIFO - mesma ordem em que
        // os frames foram entregues ao decoder via WriteData).
        // ====================================================

        uint32_t timestampMs =
            0;


        {
            std::lock_guard<std::mutex> lock(
                g_frameMutex
            );


            if (!g_videoTimestampQueue.empty())
            {
                timestampMs =
                    g_videoTimestampQueue.front();

                g_videoTimestampQueue.pop_front();
            }


            // ------------------------------------------------
            // Enfileirar pra apresentação sincronizada, em vez
            // de exibir imediatamente.
            // ------------------------------------------------

            PendingVideoFrame pending;

            pending.pixels =
                std::move(decodedFrame);

            pending.width =
                width;

            pending.height =
                height;

            pending.timestampMs =
                timestampMs;


            g_pendingVideoFrames.push_back(
                std::move(pending)
            );


            // ------------------------------------------------
            // Limite de seguranca: se acumular demais (ex:
            // calibracao de relogio ruim, ou decoder muito a
            // frente do relogio de sincronizacao), descarta os
            // mais antigos em vez de deixar o atraso crescer
            // sem controle.
            // ------------------------------------------------

            constexpr size_t MAX_PENDING_VIDEO_FRAMES =
                12;


            while (g_pendingVideoFrames.size() >
                   MAX_PENDING_VIDEO_FRAMES)
            {
                g_pendingVideoFrames.pop_front();

                g_framesDropped++;
            }
        }


        decodedFrame.clear();
    }


    // ========================================================
    // Libera pra exibição os frames cuja hora (segundo o
    // relógio sincronizado com o áudio) já chegou.
    // ========================================================

    TryPresentPendingFrames();
}


// ============================================================
// TryPresentPendingFrames
// ============================================================
//
// Percorre g_pendingVideoFrames em ordem e promove pra exibição
// (g_framePixels) todo frame cuja hora alvo já chegou, segundo
// o relógio sincronizado (g_syncBaseline + timestampMs do
// frame). Isso é o que efetivamente sincroniza o vídeo com o
// áudio: em vez de mostrar assim que decodifica, ele espera a
// vez certa.
// ============================================================

void TryPresentPendingFrames()
{
    bool presented =
        false;


    {
        std::lock_guard<std::mutex> lock(
            g_frameMutex
        );


        std::lock_guard<std::mutex> syncLock(
            g_syncMutex
        );


        ULONGLONG now =
            GetTickCount64();


        while (!g_pendingVideoFrames.empty())
        {
            PendingVideoFrame& front =
                g_pendingVideoFrames.front();


            ULONGLONG targetTime =
                g_syncBaseline +
                front.timestampMs;


            if (g_haveSyncBaseline &&
                now < targetTime)
            {
                // Ainda não é a vez deste frame.
                break;
            }


            g_framePixels =
                std::move(
                    front.pixels
                );


            g_pendingVideoFrames.pop_front();


            g_frameNumber++;


            presented =
                true;
        }
    }


    if (!presented)
    {
        return;
    }


    // ========================================================
    // FPS
    // ========================================================

    g_fpsFrameCount++;


    ULONGLONG now =
        GetTickCount64();


    if (
        now -
        g_fpsStartTime >=
        1000)
    {
        g_fps =
            g_fpsFrameCount;


        g_fpsFrameCount =
            0;


        g_fpsStartTime =
            now;
    }


    // ========================================================
    // Atualizar título + repintar
    // ========================================================

    if (g_hwnd)
    {
        wchar_t title[256]{};


        swprintf_s(
            title,
            L"Stream Receiver UDP - H264 - FPS: %d - Frame: %llu - %ux%u - Dropped: %llu",
            g_fps.load(),
            static_cast<unsigned long long>(
                g_frameNumber
            ),
            g_frameWidth,
            g_frameHeight,
            static_cast<unsigned long long>(
                g_framesDropped
            )
        );


        SetWindowTextW(
            g_hwnd,
            title
        );


        InvalidateRect(
            g_hwnd,
            nullptr,
            FALSE
        );
    }
}


// ============================================================
// PrintDecoderLogTail
// ============================================================
//
// Mostra no console as ultimas mensagens de erro do ffmpeg
// (arquivo ffmpeg-decoder.log) - e o que diz POR QUE o decoder
// parou.
// ============================================================

void PrintDecoderLogTail()
{
    std::ifstream file(
        "ffmpeg-decoder.log",
        std::ios::binary
    );

    if (!file)
    {
        std::cout
            << "(sem ffmpeg-decoder.log)\n";

        return;
    }

    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>()
    );

    const size_t maxTail = 900;

    if (content.size() > maxTail)
    {
        content =
            content.substr(content.size() - maxTail);
    }

    std::cout
        << "---- ffmpeg disse: ----\n"
        << (content.empty()
            ? std::string("(nada)\n")
            : content)
        << "\n-----------------------\n";
}


// ============================================================
// StartNewPendingFrame
// ============================================================
//
// Descarta o que estiver em montagem (se houver) e inicia a
// montagem de um novo frame a partir do header recebido.
// ============================================================

void StartNewPendingFrame(
    const VideoFrameFragmentHeader& header)
{
    if (g_pendingFrame.active &&
        g_pendingFrame.fragReceivedCount <
            g_pendingFrame.fragCount)
    {
        // Havia um frame incompleto em montagem: foi
        // "atropelado" por um frame mais novo. Conta como
        // descartado.

        g_framesDropped++;

        g_waitingKeyframe =
            true;
    }


    g_pendingFrame.frameId =
        header.frameId;

    g_pendingFrame.frameSize =
        header.frameSize;

    g_pendingFrame.fragCount =
        header.fragCount;

    g_pendingFrame.width =
        header.width;

    g_pendingFrame.height =
        header.height;

    g_pendingFrame.timestampMs =
        header.timestampMs;

    g_pendingFrame.isKeyframe =
        (header.flags &
         VIDEO_FLAG_KEYFRAME) != 0;

    g_pendingFrame.active =
        true;

    g_pendingFrame.fragReceivedCount =
        0;


    g_pendingFrame.data.assign(
        header.frameSize,
        0
    );


    g_pendingFrame.fragReceived.assign(
        header.fragCount,
        false
    );
}


// ============================================================
// Processar pacote UDP
// ============================================================

void ProcessUdpPacket(
    const char* buffer,
    int packetSize)
{
    // ========================================================
    // Tamanho mínimo
    // ========================================================

    if (
        packetSize <
        static_cast<int>(
            VIDEO_HEADER_SIZE
        ))
    {
        return;
    }


    // ========================================================
    // Ler header
    // ========================================================

    VideoFrameFragmentHeader header{};


    std::memcpy(
        &header,
        buffer,
        sizeof(header)
    );


    // ========================================================
    // Converter ordem de bytes
    // ========================================================

    header.magic =
        ntohl(header.magic);

    header.frameId =
        ntohl(header.frameId);

    header.fragIndex =
        ntohl(header.fragIndex);

    header.fragCount =
        ntohl(header.fragCount);

    header.frameSize =
        ntohl(header.frameSize);

    header.fragSize =
        ntohl(header.fragSize);

    header.width =
        ntohl(header.width);

    header.height =
        ntohl(header.height);

    header.flags =
        ntohl(header.flags);

    header.timestampMs =
        ntohl(header.timestampMs);


    // ========================================================
    // Validações básicas
    // ========================================================

    if (header.magic !=
        VIDEO_PROTOCOL_MAGIC)
    {
        return;
    }


    if (header.fragCount == 0 ||
        header.fragIndex >=
            header.fragCount)
    {
        return;
    }


    if (header.frameSize == 0 ||
        header.fragSize == 0 ||
        header.fragSize >
            VIDEO_UDP_PAYLOAD_SIZE)
    {
        return;
    }


    if (header.width == 0 ||
        header.height == 0)
    {
        return;
    }


    constexpr uint32_t MAX_WIDTH =
        7680;

    constexpr uint32_t MAX_HEIGHT =
        4320;


    if (header.width > MAX_WIDTH ||
        header.height > MAX_HEIGHT)
    {
        return;
    }


    const int expectedSize =
        static_cast<int>(
            VIDEO_HEADER_SIZE +
            header.fragSize
        );


    if (packetSize !=
        expectedSize)
    {
        return;
    }


    // ========================================================
    // Detectar mudança de resolução
    // ========================================================

    if (
        header.width != g_frameWidth ||
        header.height != g_frameHeight)
    {
        std::cout
            << "\n========================================\n"
            << "Nova resolucao recebida: "
            << header.width
            << "x"
            << header.height
            << "\n"
            << "Reiniciando decoder H264...\n"
            << "========================================\n";


        g_decoder.Stop();


        if (!g_decoder.Start(
                header.width,
                header.height))
        {
            std::cout
                << "Falha ao iniciar decoder H264.\n";


            g_frameWidth = 0;
            g_frameHeight = 0;

            return;
        }


        g_frameWidth =
            header.width;

        g_frameHeight =
            header.height;


        // Qualquer reassembly em andamento não serve mais.

        g_pendingFrame =
            PendingFrame{};

        g_haveCompletedFrame =
            false;

        g_waitingKeyframe =
            true;


        {
            std::lock_guard<std::mutex> lock(
                g_frameMutex
            );


            g_videoTimestampQueue.clear();

            g_pendingVideoFrames.clear();
        }


        std::cout
            << "Decoder H264 iniciado com sucesso.\n";
    }


    // ========================================================
    // Descartar fragmentos de frames antigos (já concluídos
    // ou já abandonados).
    // ========================================================

    if (g_haveCompletedFrame &&
        header.frameId <=
            g_lastCompletedFrameId)
    {
        return;
    }


    // ========================================================
    // Novo frame começando?
    // ========================================================

    if (g_pendingFrame.active &&
        header.frameId <
            g_pendingFrame.frameId)
    {
        // Fragmento atrasado de um frame mais antigo que já
        // foi substituído pela montagem atual - ignora, não
        // reinicia a montagem em andamento.

        return;
    }


    if (!g_pendingFrame.active ||
        header.frameId !=
            g_pendingFrame.frameId)
    {
        StartNewPendingFrame(
            header
        );
    }


    // ========================================================
    // Fragmento duplicado?
    // ========================================================

    if (g_pendingFrame.fragReceived[
            header.fragIndex])
    {
        return;
    }


    // ========================================================
    // Copiar o payload deste fragmento na posição certa
    // ========================================================

    const uint32_t fragOffset =
        header.fragIndex *
        VIDEO_UDP_PAYLOAD_SIZE;


    if (fragOffset +
            header.fragSize >
        g_pendingFrame.frameSize)
    {
        // Header inconsistente - ignora o pacote.
        return;
    }


    std::memcpy(
        g_pendingFrame.data.data() +
            fragOffset,

        buffer +
            VIDEO_HEADER_SIZE,

        header.fragSize
    );


    g_pendingFrame.fragReceived[
        header.fragIndex] = true;

    g_pendingFrame.fragReceivedCount++;


    // ========================================================
    // Frame ainda incompleto: aguarda mais fragmentos.
    // ========================================================

    if (g_pendingFrame.fragReceivedCount <
        g_pendingFrame.fragCount)
    {
        return;
    }


    // ========================================================
    // FRAME COMPLETO - entregar ao decoder.
    //
    // Só chegamos aqui quando 100% dos fragmentos foram
    // recebidos, então o decoder nunca vê um frame parcial
    // ou corrompido por perda de pacote.
    // ========================================================

    // Pulou algum frameId? Entao um frame se perdeu inteiro.
    if (g_haveCompletedFrame &&
        g_pendingFrame.frameId !=
            g_lastCompletedFrameId + 1)
    {
        g_framesDropped++;

        g_waitingKeyframe =
            true;
    }


    g_lastCompletedFrameId =
        g_pendingFrame.frameId;

    g_haveCompletedFrame =
        true;

    g_framesCompleted++;

    g_pendingFrame.active =
        false;


    // ========================================================
    // Depois de uma perda, so um keyframe "reinicia" a imagem.
    // Frames comuns que chegarem antes dele sao ignorados.
    // ========================================================

    if (g_pendingFrame.isKeyframe)
    {
        g_waitingKeyframe =
            false;
    }

    if (g_waitingKeyframe)
    {
        return;
    }


    EstablishSyncBaseline(
        g_pendingFrame.timestampMs
    );


    {
        std::lock_guard<std::mutex> lock(
            g_frameMutex
        );


        g_videoTimestampQueue.push_back(
            g_pendingFrame.timestampMs
        );
    }


    if (!g_decoder.WriteData(
            reinterpret_cast<const char*>(
                g_pendingFrame.data.data()
            ),
            static_cast<uint32_t>(
                g_pendingFrame.data.size()
            )))
    {
        // O ffmpeg do decoder morreu. Reinicia (no maximo uma
        // vez por segundo) e espera o proximo keyframe.
        ULONGLONG nowMs =
            GetTickCount64();

        if (nowMs - g_lastDecoderRestart >= 1000)
        {
            g_lastDecoderRestart =
                nowMs;

            std::cout
                << "\nDecoder parou - reiniciando...\n";

            PrintDecoderLogTail();

            g_decoderDeaths++;

            if (g_decoderDeaths == 2)
            {
                std::cout
                    << "Trocando para decodificacao por "
                    << "software (CPU).\n";

                g_decoder.SetHardwareDecode(false);
            }

            g_decoder.Stop();

            if (!g_decoder.Start(
                    g_frameWidth,
                    g_frameHeight))
            {
                std::cout
                    << "Falha ao reiniciar decoder H264.\n";
            }
        }

        {
            std::lock_guard<std::mutex> lock(
                g_frameMutex
            );

            g_videoTimestampQueue.clear();
            g_pendingVideoFrames.clear();
        }

        g_waitingKeyframe =
            true;

        return;
    }


    // ========================================================
    // Verificar se o decoder já produziu frame
    // ========================================================

    ProcessDecodedFrame();
}


// ============================================================
// Enviar HELLO para o servidor
// ============================================================

bool SendHello()
{
    if (g_haveAltAddress)
    {
        sendto(
            g_udpSocket,
            "HELLO",
            5,
            0,
            reinterpret_cast<sockaddr*>(
                &g_serverAltAddress
            ),
            sizeof(g_serverAltAddress)
        );
    }


    const char hello[] =
        "HELLO";


    int sent =
        sendto(
            g_udpSocket,

            hello,

            static_cast<int>(
                std::strlen(hello)
            ),

            0,

            reinterpret_cast<sockaddr*>(
                &g_serverAddress
            ),

            sizeof(g_serverAddress)
        );


    if (
        sent ==
        SOCKET_ERROR)
    {
        std::cout
            << "Falha ao enviar HELLO. Erro: "
            << WSAGetLastError()
            << "\n";


        return false;
    }


    return true;
}


// ============================================================
// Thread UDP
// ============================================================

void ReceiverThread()
{
    std::cout
        << "Aguardando stream H264 UDP...\n";


    std::cout
        << "Porta local: "
        << g_localVideoPort
        << "\n";


    std::cout
        << "========================================\n\n";


    std::vector<char> buffer(
        VIDEO_UDP_PACKET_SIZE
    );


    // ========================================================
    // Controle do intervalo entre HELLOs
    // ========================================================

    ULONGLONG lastHelloTime =
        0;


    while (g_running)
    {
        // ====================================================
        // Reenviar HELLO periodicamente
        //
        // Enquanto ainda não recebemos vídeo, enviamos
        // um HELLO a cada 1 segundo.
        // ====================================================

        ULONGLONG now =
            GetTickCount64();


        if (
            !g_streamReceived &&
            (
                lastHelloTime == 0 ||
                now - lastHelloTime >= 1000
            ))
        {
            if (SendHello())
            {
                std::cout
                    << "HELLO UDP enviado para "
                    << g_serverIp
                    << ":"
                    << g_serverVideoPort
                    << "\n";
            }


            lastHelloTime =
                now;
        }


        // ====================================================
        // Keepalive durante o stream
        //
        // Roteadores so mantem a porta UDP aberta enquanto
        // sair trafego daqui - e, com o stream rodando, o
        // receiver so recebe. Um pacote minusculo a cada 5s
        // mantem o caminho aberto (o client descarta).
        // ====================================================

        if (
            g_streamReceived &&
            now - lastHelloTime >= 5000)
        {
            sendto(
                g_udpSocket,
                "KEEP",
                4,
                0,
                reinterpret_cast<sockaddr*>(
                    &g_serverAddress
                ),
                sizeof(g_serverAddress)
            );


            lastHelloTime =
                now;
        }


        // ====================================================
        // Esperar dados UDP por até 200 ms
        //
        // Usamos select() para não ficar bloqueado
        // indefinidamente no recvfrom().
        // ====================================================

        fd_set readSet{};


        FD_ZERO(
            &readSet
        );


        FD_SET(
            g_udpSocket,
            &readSet
        );


        timeval timeout{};


        timeout.tv_sec =
            0;


        timeout.tv_usec =
            200000;


        int result =
            select(
                0,
                &readSet,
                nullptr,
                nullptr,
                &timeout
            );


        if (!g_running)
            break;


        // ====================================================
        // Timeout
        // ====================================================

        if (result == 0)
            continue;


        // ====================================================
        // Erro
        // ====================================================

        if (result == SOCKET_ERROR)
        {
            int error =
                WSAGetLastError();


            if (g_running)
            {
                std::cout
                    << "\nselect() erro: "
                    << error
                    << "\n";
            }


            break;
        }


        // ====================================================
        // Há dados no socket
        // ====================================================

        if (!FD_ISSET(
                g_udpSocket,
                &readSet))
        {
            continue;
        }


        sockaddr_in senderAddress{};


        int senderAddressSize =
            sizeof(senderAddress);


        int received =
            recvfrom(
                g_udpSocket,

                buffer.data(),

                static_cast<int>(
                    buffer.size()
                ),

                0,

                reinterpret_cast<sockaddr*>(
                    &senderAddress
                ),

                &senderAddressSize
            );


        if (
            received ==
            SOCKET_ERROR)
        {
            if (!g_running)
                break;


            int error =
                WSAGetLastError();


            std::cout
                << "\nrecvfrom() erro: "
                << error
                << "\n";


            continue;
        }


        if (received <= 0)
            continue;


        // ====================================================
        // Recebemos alguma coisa.
        //
        // ProcessUdpPacket() verifica se é um pacote
        // de vídeo válido.
        // ====================================================

        ProcessUdpPacket(
            buffer.data(),
            received
        );


        // ====================================================
        // Se já temos resolução configurada, significa que
        // recebemos pelo menos um pacote de vídeo válido.
        // ====================================================

        if (
            g_frameWidth > 0 &&
            g_frameHeight > 0)
        {
            if (!g_streamReceived)
            {
                g_streamReceived =
                    true;

                // O stream veio pelo caminho alternativo?
                // Entao e por ele que seguem os keepalives.
                if (g_haveAltAddress &&
                    SameEndpoint(
                        senderAddress,
                        g_serverAltAddress))
                {
                    g_serverAddress =
                        g_serverAltAddress;
                }


                std::cout
                    << "\n========================================\n"
                    << "STREAM UDP RECEBIDO!\n"
                    << "Resolucao: "
                    << g_frameWidth
                    << "x"
                    << g_frameHeight
                    << "\n"
                    << "HELLO nao sera mais reenviado.\n"
                    << "========================================\n\n";
            }
        }
    }


    std::cout
        << "\nThread UDP encerrada.\n";
}


// ============================================================
// AudioReceiverThread
// ============================================================
//
// Auto-contida: cria seu próprio socket UDP (porta
// AUDIO_UDP_PORT), envia HELLO pro client (AUDIO_SERVER_UDP_PORT)
// e recebe/decodifica/toca o áudio - tudo em thread e socket
// independentes do vídeo, de propósito (evita que áudio fique
// enfileirado atrás do processamento de frames de vídeo).
// ============================================================

void AudioReceiverThread()
{
    // ========================================================
    // Socket
    // ========================================================

    SOCKET audioSocket =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );


    if (audioSocket ==
        INVALID_SOCKET)
    {
        std::cout
            << "AudioReceiver: falha ao criar socket. "
            << "Erro: "
            << WSAGetLastError()
            << "\n";

        return;
    }


    sockaddr_in localAddress{};

    localAddress.sin_family =
        AF_INET;

    localAddress.sin_addr.s_addr =
        htonl(INADDR_ANY);

    localAddress.sin_port =
        htons(AUDIO_UDP_PORT);


    uint16_t localAudioPort =
        BindUdpWithFallback(
            audioSocket,
            AUDIO_UDP_PORT
        );

    if (localAudioPort == 0)
    {
        std::cout
            << "AudioReceiver: falha no bind. Erro: "
            << WSAGetLastError()
            << "\n";

        closesocket(audioSocket);

        return;
    }


    sockaddr_in serverAddress{};

    serverAddress.sin_family =
        AF_INET;

    serverAddress.sin_port =
        htons(g_serverAudioPort);


    inet_pton(
        AF_INET,
        g_serverIp.c_str(),
        &serverAddress.sin_addr
    );


    AnnounceToSession(
        audioSocket,
        true
    );


    // Caminho alternativo (ver g_serverAltIp).
    sockaddr_in altAddress{};

    bool haveAlt =
        !g_serverAltIp.empty() &&
        MakeEndpoint(
            g_serverAltIp,
            g_serverAltAudioPort,
            altAddress) &&
        !SameEndpoint(
            altAddress,
            serverAddress);


    std::cout
        << "AudioReceiver: escutando na porta "
        << localAudioPort
        << ", servidor em "
        << g_serverIp
        << ":"
        << g_serverAudioPort
        << "\n";


    // ========================================================
    // Decoder + playback
    // ========================================================

    if (!g_audioDecoder.Start())
    {
        std::cout
            << "AudioReceiver: falha ao iniciar "
            << "decoder Opus.\n";

        closesocket(audioSocket);

        return;
    }


    if (!g_audioPlayback.Start())
    {
        std::cout
            << "AudioReceiver: falha ao iniciar "
            << "reprodução.\n";

        g_audioDecoder.Stop();

        closesocket(audioSocket);

        return;
    }


    // ========================================================
    // Estado de sequência (deteccao de perda p/ PLC)
    // ========================================================

    bool haveSequence =
        false;

    uint32_t lastSequence =
        0;

    bool streamReceived =
        false;


    std::vector<char> buffer(
        AUDIO_UDP_PACKET_SIZE
    );


    ULONGLONG lastHelloTime =
        0;


    while (g_running)
    {
        // ====================================================
        // Reenviar HELLO periodicamente ate receber audio
        // ====================================================

        ULONGLONG now =
            GetTickCount64();


        if (!streamReceived &&
            (lastHelloTime == 0 ||
             now - lastHelloTime >= 1000))
        {
            sendto(
                audioSocket,
                "HELLO",
                5,
                0,
                reinterpret_cast<sockaddr*>(
                    &serverAddress
                ),
                sizeof(serverAddress)
            );

            if (haveAlt)
            {
                sendto(
                    audioSocket,
                    "HELLO",
                    5,
                    0,
                    reinterpret_cast<sockaddr*>(
                        &altAddress
                    ),
                    sizeof(altAddress)
                );
            }


            lastHelloTime =
                now;
        }


        // Keepalive durante o stream (mesmo motivo do video:
        // manter a porta aberta no roteador).
        if (streamReceived &&
            now - lastHelloTime >= 5000)
        {
            sendto(
                audioSocket,
                "KEEP",
                4,
                0,
                reinterpret_cast<sockaddr*>(
                    &serverAddress
                ),
                sizeof(serverAddress)
            );

            lastHelloTime =
                now;
        }


        // ====================================================
        // Esperar dados por ate 200ms
        // ====================================================

        fd_set readSet{};

        FD_ZERO(&readSet);

        FD_SET(
            audioSocket,
            &readSet
        );


        timeval timeout{};

        timeout.tv_sec = 0;

        timeout.tv_usec = 200000;


        int result =
            select(
                0,
                &readSet,
                nullptr,
                nullptr,
                &timeout
            );


        if (!g_running)
            break;


        if (result <= 0)
            continue;


        sockaddr_in fromAddress{};

        int fromSize =
            sizeof(fromAddress);


        int received =
            recvfrom(
                audioSocket,
                buffer.data(),
                static_cast<int>(
                    buffer.size()
                ),
                0,
                reinterpret_cast<sockaddr*>(
                    &fromAddress
                ),
                &fromSize
            );


        if (received <
            static_cast<int>(
                AUDIO_HEADER_SIZE
            ))
        {
            continue;
        }


        // ====================================================
        // Parse header
        // ====================================================

        AudioPacketHeader header{};

        std::memcpy(
            &header,
            buffer.data(),
            AUDIO_HEADER_SIZE
        );


        header.magic =
            ntohl(header.magic);

        header.sequence =
            ntohl(header.sequence);

        header.timestampMs =
            ntohl(header.timestampMs);

        header.payloadSize =
            ntohl(header.payloadSize);


        if (header.magic !=
            AUDIO_PROTOCOL_MAGIC)
        {
            continue;
        }


        if (header.payloadSize == 0 ||
            header.payloadSize >
                AUDIO_UDP_MAX_PAYLOAD)
        {
            continue;
        }


        if (received !=
            static_cast<int>(
                AUDIO_HEADER_SIZE +
                header.payloadSize))
        {
            continue;
        }


        if (!streamReceived)
        {
            streamReceived =
                true;

            if (haveAlt &&
                SameEndpoint(
                    fromAddress,
                    altAddress))
            {
                serverAddress =
                    altAddress;
            }


            std::cout
                << "\n========================================\n"
                << "STREAM DE AUDIO RECEBIDO!\n"
                << "========================================\n\n";
        }


        EstablishSyncBaseline(
            header.timestampMs
        );


        // ====================================================
        // Deteccao de perda -> PLC (packet loss concealment)
        // ====================================================

        if (haveSequence)
        {
            uint32_t expected =
                lastSequence + 1;


            if (header.sequence >
                expected)
            {
                uint32_t missing =
                    header.sequence -
                    expected;


                // Limite de seguranca - nao gera concealment
                // absurdo em reconexoes/saltos grandes.

                if (missing > 10)
                {
                    missing = 10;
                }


                for (uint32_t i = 0;
                     i < missing;
                     ++i)
                {
                    std::vector<float> concealed;


                    if (g_audioDecoder.DecodeLost(
                            concealed))
                    {
                        g_audioPlayback.PushSamples(
                            concealed
                        );
                    }
                }
            }
        }


        haveSequence =
            true;

        lastSequence =
            header.sequence;


        // ====================================================
        // Decode + tocar
        // ====================================================

        std::vector<float> pcm;


        if (g_audioDecoder.Decode(
                reinterpret_cast<const unsigned char*>(
                    buffer.data() +
                        AUDIO_HEADER_SIZE
                ),
                header.payloadSize,
                pcm))
        {
            g_audioPlayback.PushSamples(
                pcm
            );
        }
    }


    g_audioPlayback.Stop();

    g_audioDecoder.Stop();

    closesocket(
        audioSocket
    );


    std::cout
        << "\nThread de audio encerrada.\n";
}


// ============================================================
// SaveCurrentFrameAsBmp
// ============================================================
//
// Salva o frame que está sendo exibido no momento num arquivo
// .bmp, direto dos bytes que o receiver realmente tem em mãos
// - sem passar por print de tela, foto, nem nada que possa
// alterar/falhar na captura. Ativado pela tecla S.
// ============================================================

void SaveCurrentFrameAsBmp()
{
    std::vector<unsigned char> pixelsCopy;

    uint32_t width =
        0;

    uint32_t height =
        0;


    {
        std::lock_guard<std::mutex> lock(
            g_frameMutex
        );


        if (g_framePixels.empty() ||
            g_frameWidth == 0 ||
            g_frameHeight == 0)
        {
            std::cout
                << "\nSaveCurrentFrameAsBmp: nenhum frame "
                << "disponivel ainda.\n";

            return;
        }


        pixelsCopy =
            g_framePixels;

        width =
            g_frameWidth;

        height =
            g_frameHeight;
    }


    const size_t expectedSize =
        static_cast<size_t>(width) *
        static_cast<size_t>(height) *
        4;


    if (pixelsCopy.size() <
        expectedSize)
    {
        std::cout
            << "\nSaveCurrentFrameAsBmp: buffer de pixels "
            << "menor que o esperado ("
            << pixelsCopy.size()
            << " < "
            << expectedSize
            << ").\n";

        return;
    }


    BITMAPFILEHEADER fileHeader{};

    BITMAPINFOHEADER infoHeader{};


    infoHeader.biSize =
        sizeof(BITMAPINFOHEADER);

    infoHeader.biWidth =
        static_cast<LONG>(width);

    infoHeader.biHeight =
        static_cast<LONG>(height); // positivo = bottom-up

    infoHeader.biPlanes =
        1;

    infoHeader.biBitCount =
        32;

    infoHeader.biCompression =
        BI_RGB;

    infoHeader.biSizeImage =
        static_cast<DWORD>(
            expectedSize
        );


    fileHeader.bfType =
        0x4D42; // 'BM'

    fileHeader.bfOffBits =
        sizeof(BITMAPFILEHEADER) +
        sizeof(BITMAPINFOHEADER);

    fileHeader.bfSize =
        fileHeader.bfOffBits +
        infoHeader.biSizeImage;


    const wchar_t* outPath =
        L"receiver_debug_frame.bmp";


    std::ofstream file(
        outPath,
        std::ios::binary
    );


    if (!file)
    {
        std::cout
            << "\nSaveCurrentFrameAsBmp: falha ao criar "
            << "o arquivo.\n";

        return;
    }


    file.write(
        reinterpret_cast<const char*>(&fileHeader),
        sizeof(fileHeader)
    );

    file.write(
        reinterpret_cast<const char*>(&infoHeader),
        sizeof(infoHeader)
    );


    // --------------------------------------------------------
    // pixelsCopy está top-down (linha 0 = topo). BMP com altura
    // positiva é bottom-up, então escrevemos as linhas na ordem
    // inversa.
    // --------------------------------------------------------

    const size_t rowBytes =
        static_cast<size_t>(width) *
        4;


    for (int y = static_cast<int>(height) - 1;
         y >= 0;
         --y)
    {
        const unsigned char* row =
            pixelsCopy.data() +
            static_cast<size_t>(y) *
                rowBytes;


        file.write(
            reinterpret_cast<const char*>(row),
            rowBytes
        );
    }


    file.close();


    wchar_t fullPath[MAX_PATH]{};


    GetFullPathNameW(
        outPath,
        MAX_PATH,
        fullPath,
        nullptr
    );


    // --------------------------------------------------------
    // Converter pra string estreita - evita misturar
    // std::wcout com std::cout no mesmo stdin/stdout usado no
    // resto do arquivo.
    // --------------------------------------------------------

    char narrowPath[MAX_PATH * 2]{};


    WideCharToMultiByte(
        CP_UTF8,
        0,
        fullPath,
        -1,
        narrowPath,
        sizeof(narrowPath),
        nullptr,
        nullptr
    );


    std::cout
        << "\nFrame salvo em: "
        << narrowPath
        << " ("
        << width
        << "x"
        << height
        << ")\n";
}


// ============================================================
// RENDERIZADOR DIRECT3D 11
// ============================================================
//
// Shaders embutidos como texto HLSL, compilados em tempo de
// execução. Desenha um quad de tela cheia (sem vertex buffer -
// os 4 cantos vêm direto do SV_VertexID) escalado pelo fator
// calculado no cbuffer, preservando a proporção (letterbox) -
// exatamente a mesma lógica de antes, só que feita pela GPU
// via shader em vez de aritmética manual + StretchDIBits.
// ============================================================

static const char* kShaderSource =
    "cbuffer TransformBuffer : register(b0)\n"
    "{\n"
    "    float2 scale;\n"
    "    float2 padding;\n"
    "};\n"
    "\n"
    "struct VSOutput\n"
    "{\n"
    "    float4 pos : SV_POSITION;\n"
    "    float2 uv : TEXCOORD0;\n"
    "};\n"
    "\n"
    "VSOutput VSMain(uint id : SV_VertexID)\n"
    "{\n"
    "    float2 positions[4] =\n"
    "    {\n"
    "        float2(-1.0, -1.0),\n"
    "        float2(-1.0,  1.0),\n"
    "        float2( 1.0, -1.0),\n"
    "        float2( 1.0,  1.0)\n"
    "    };\n"
    "\n"
    "    float2 uvs[4] =\n"
    "    {\n"
    "        float2(0.0, 1.0),\n"
    "        float2(0.0, 0.0),\n"
    "        float2(1.0, 1.0),\n"
    "        float2(1.0, 0.0)\n"
    "    };\n"
    "\n"
    "    VSOutput output;\n"
    "    output.pos = float4(positions[id] * scale, 0.0, 1.0);\n"
    "    output.uv = uvs[id];\n"
    "    return output;\n"
    "}\n"
    "\n"
    "Texture2D videoTexture : register(t0);\n"
    "SamplerState videoSampler : register(s0);\n"
    "\n"
    "float4 PSMain(VSOutput input) : SV_TARGET\n"
    "{\n"
    "    return videoTexture.Sample(videoSampler, input.uv);\n"
    "}\n";


struct TransformData
{
    float scaleX;
    float scaleY;
    float padding0;
    float padding1;
};


// ============================================================
// CreateRenderTargetView
// ============================================================
//
// (Re)cria a view do back buffer atual do swap chain. Chamado
// na inicialização e sempre que o swap chain é redimensionado.
// ============================================================

bool CreateRenderTargetView()
{
    ComPtr<ID3D11Texture2D> backBuffer;


    HRESULT hr =
        g_swapChain->GetBuffer(
            0,
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(
                backBuffer.GetAddressOf()
            )
        );


    if (FAILED(hr))
    {
        std::cout
            << "Renderer: falha ao obter back buffer. "
            << "HRESULT: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";

        return false;
    }


    hr =
        g_rendererDevice->CreateRenderTargetView(
            backBuffer.Get(),
            nullptr,
            g_renderTargetView.ReleaseAndGetAddressOf()
        );


    return SUCCEEDED(hr);
}


// ============================================================
// InitializeD3D11Renderer
// ============================================================

bool InitializeD3D11Renderer(
    HWND hwnd)
{
    RECT clientRect{};

    GetClientRect(
        hwnd,
        &clientRect
    );


    UINT initialWidth =
        static_cast<UINT>(
            clientRect.right - clientRect.left
        );

    UINT initialHeight =
        static_cast<UINT>(
            clientRect.bottom - clientRect.top
        );


    if (initialWidth == 0)
        initialWidth = 1;

    if (initialHeight == 0)
        initialHeight = 1;


    // --------------------------------------------------------
    // Device + swap chain
    // --------------------------------------------------------

    DXGI_SWAP_CHAIN_DESC scd{};

    scd.BufferCount = 1;
    scd.BufferDesc.Width = initialWidth;
    scd.BufferDesc.Height = initialHeight;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.BufferDesc.RefreshRate.Numerator = 60;
    scd.BufferDesc.RefreshRate.Denominator = 1;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.SampleDesc.Quality = 0;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;


    D3D_FEATURE_LEVEL featureLevel{};


    HRESULT hr =
        D3D11CreateDeviceAndSwapChain(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            0,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            &scd,
            g_swapChain.GetAddressOf(),
            g_rendererDevice.GetAddressOf(),
            &featureLevel,
            g_rendererContext.GetAddressOf()
        );


    if (FAILED(hr))
    {
        std::cout
            << "Renderer: D3D11CreateDeviceAndSwapChain "
            << "falhou. HRESULT: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";

        return false;
    }


    if (!CreateRenderTargetView())
    {
        return false;
    }


    // --------------------------------------------------------
    // Shaders
    // --------------------------------------------------------

    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> errorBlob;


    hr =
        D3DCompile(
            kShaderSource,
            strlen(kShaderSource),
            nullptr,
            nullptr,
            nullptr,
            "VSMain",
            "vs_4_0",
            0,
            0,
            vsBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );


    if (FAILED(hr))
    {
        std::cout
            << "Renderer: falha ao compilar vertex shader. "
            << (errorBlob
                ? static_cast<const char*>(
                    errorBlob->GetBufferPointer())
                : "erro desconhecido")
            << "\n";

        return false;
    }


    errorBlob.Reset();


    hr =
        D3DCompile(
            kShaderSource,
            strlen(kShaderSource),
            nullptr,
            nullptr,
            nullptr,
            "PSMain",
            "ps_4_0",
            0,
            0,
            psBlob.GetAddressOf(),
            errorBlob.GetAddressOf()
        );


    if (FAILED(hr))
    {
        std::cout
            << "Renderer: falha ao compilar pixel shader. "
            << (errorBlob
                ? static_cast<const char*>(
                    errorBlob->GetBufferPointer())
                : "erro desconhecido")
            << "\n";

        return false;
    }


    hr =
        g_rendererDevice->CreateVertexShader(
            vsBlob->GetBufferPointer(),
            vsBlob->GetBufferSize(),
            nullptr,
            g_vertexShader.GetAddressOf()
        );

    if (FAILED(hr))
        return false;


    hr =
        g_rendererDevice->CreatePixelShader(
            psBlob->GetBufferPointer(),
            psBlob->GetBufferSize(),
            nullptr,
            g_pixelShader.GetAddressOf()
        );

    if (FAILED(hr))
        return false;


    // --------------------------------------------------------
    // Sampler (filtragem bilinear - suaviza o escalonamento)
    // --------------------------------------------------------

    D3D11_SAMPLER_DESC sampDesc{};

    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;


    hr =
        g_rendererDevice->CreateSamplerState(
            &sampDesc,
            g_samplerState.GetAddressOf()
        );

    if (FAILED(hr))
        return false;


    // --------------------------------------------------------
    // Constant buffer (fator de escala do letterbox)
    // --------------------------------------------------------

    D3D11_BUFFER_DESC cbDesc{};

    cbDesc.ByteWidth = sizeof(TransformData);
    cbDesc.Usage = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;


    hr =
        g_rendererDevice->CreateBuffer(
            &cbDesc,
            nullptr,
            g_transformBuffer.GetAddressOf()
        );

    if (FAILED(hr))
        return false;


    std::cout
        << "Renderer: Direct3D 11 inicializado com sucesso.\n";


    return true;
}


// ============================================================
// EnsureVideoTexture
// ============================================================
//
// Cria (ou recria, se o tamanho mudou) a textura dinâmica que
// recebe os pixels BGRA decodificados a cada frame.
// ============================================================

bool EnsureVideoTexture(
    uint32_t width,
    uint32_t height)
{
    if (g_videoTexture &&
        width == g_videoTextureWidth &&
        height == g_videoTextureHeight)
    {
        return true;
    }


    D3D11_TEXTURE2D_DESC desc{};

    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;


    ComPtr<ID3D11Texture2D> texture;


    HRESULT hr =
        g_rendererDevice->CreateTexture2D(
            &desc,
            nullptr,
            texture.GetAddressOf()
        );


    if (FAILED(hr))
    {
        std::cout
            << "Renderer: falha ao criar textura de video "
            << "("
            << width
            << "x"
            << height
            << "). HRESULT: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";

        return false;
    }


    ComPtr<ID3D11ShaderResourceView> srv;


    hr =
        g_rendererDevice->CreateShaderResourceView(
            texture.Get(),
            nullptr,
            srv.GetAddressOf()
        );


    if (FAILED(hr))
    {
        return false;
    }


    g_videoTexture =
        texture;

    g_videoSRV =
        srv;

    g_videoTextureWidth =
        width;

    g_videoTextureHeight =
        height;


    return true;
}


// ============================================================
// ResizeRenderer
// ============================================================
//
// Chamado no WM_SIZE. Libera a render target view antiga,
// redimensiona os buffers do swap chain, e recria a view.
// ============================================================

void ResizeRenderer(
    uint32_t width,
    uint32_t height)
{
    if (!g_swapChain ||
        width == 0 ||
        height == 0)
    {
        return;
    }


    std::lock_guard<std::mutex> lock(
        g_rendererMutex
    );


    g_renderTargetView.Reset();


    HRESULT hr =
        g_swapChain->ResizeBuffers(
            0,
            width,
            height,
            DXGI_FORMAT_UNKNOWN,
            0
        );


    if (FAILED(hr))
    {
        std::cout
            << "Renderer: ResizeBuffers falhou. HRESULT: 0x"
            << std::hex
            << hr
            << std::dec
            << "\n";

        return;
    }


    CreateRenderTargetView();
}


// ============================================================
// RenderFrame
// ============================================================
//
// Substitui o antigo WM_PAINT baseado em GDI. Sobe o frame
// atual (g_framePixels) como textura, calcula o fator de
// escala que preserva a proporção (mesmo cálculo de antes,
// agora indo pro shader em vez de pro StretchDIBits), e
// apresenta na tela.
// ============================================================

void RenderFrame()
{
    if (!g_rendererDevice)
    {
        return;
    }


    std::lock_guard<std::mutex> lock(
        g_rendererMutex
    );


    if (!g_renderTargetView)
    {
        return;
    }


    RECT clientRect{};

    GetClientRect(
        g_hwnd,
        &clientRect
    );


    int windowWidth =
        clientRect.right - clientRect.left;

    int windowHeight =
        clientRect.bottom - clientRect.top;


    if (windowWidth <= 0 ||
        windowHeight <= 0)
    {
        return;
    }


    // --------------------------------------------------------
    // Copiar o frame atual
    // --------------------------------------------------------

    uint32_t width =
        0;

    uint32_t height =
        0;

    bool haveFrame =
        false;


    {
        std::lock_guard<std::mutex> frameLock(
            g_frameMutex
        );


        if (!g_framePixels.empty() &&
            g_frameWidth > 0 &&
            g_frameHeight > 0)
        {
            width =
                g_frameWidth;

            height =
                g_frameHeight;


            if (EnsureVideoTexture(
                    width,
                    height))
            {
                D3D11_MAPPED_SUBRESOURCE mapped{};


                HRESULT hr =
                    g_rendererContext->Map(
                        g_videoTexture.Get(),
                        0,
                        D3D11_MAP_WRITE_DISCARD,
                        0,
                        &mapped
                    );


                if (SUCCEEDED(hr))
                {
                    const size_t rowBytes =
                        static_cast<size_t>(width) *
                        4;


                    for (uint32_t y = 0;
                         y < height;
                         ++y)
                    {
                        std::memcpy(
                            static_cast<BYTE*>(
                                mapped.pData) +
                                static_cast<size_t>(y) *
                                    mapped.RowPitch,

                            g_framePixels.data() +
                                static_cast<size_t>(y) *
                                    rowBytes,

                            rowBytes
                        );
                    }


                    g_rendererContext->Unmap(
                        g_videoTexture.Get(),
                        0
                    );


                    haveFrame =
                        true;
                }
            }
        }
    }


    // --------------------------------------------------------
    // Limpar (fundo preto - letterbox)
    // --------------------------------------------------------

    float clearColor[4] =
        { 0.0f, 0.0f, 0.0f, 1.0f };


    g_rendererContext->OMSetRenderTargets(
        1,
        g_renderTargetView.GetAddressOf(),
        nullptr
    );


    g_rendererContext->ClearRenderTargetView(
        g_renderTargetView.Get(),
        clearColor
    );


    if (haveFrame &&
        g_videoSRV)
    {
        // ------------------------------------------------
        // Fator de escala (preserva a proporção - letterbox)
        //
        // Mesmo cálculo de sempre: se a imagem é "mais larga"
        // que a janela relativamente, encolhe a altura; senão
        // encolhe a largura. Agora isso vira o parâmetro do
        // shader em vez de coordenadas de StretchDIBits.
        // ------------------------------------------------

        double imageAspect =
            static_cast<double>(width) /
            static_cast<double>(height);

        double windowAspect =
            static_cast<double>(windowWidth) /
            static_cast<double>(windowHeight);


        TransformData transform{};


        if (imageAspect > windowAspect)
        {
            transform.scaleX = 1.0f;

            transform.scaleY =
                static_cast<float>(
                    windowAspect / imageAspect
                );
        }
        else
        {
            transform.scaleX =
                static_cast<float>(
                    imageAspect / windowAspect
                );

            transform.scaleY = 1.0f;
        }


        g_rendererContext->UpdateSubresource(
            g_transformBuffer.Get(),
            0,
            nullptr,
            &transform,
            0,
            0
        );


        D3D11_VIEWPORT viewport{};

        viewport.Width =
            static_cast<float>(windowWidth);

        viewport.Height =
            static_cast<float>(windowHeight);

        viewport.MaxDepth =
            1.0f;


        g_rendererContext->RSSetViewports(
            1,
            &viewport
        );


        g_rendererContext->IASetPrimitiveTopology(
            D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP
        );


        g_rendererContext->VSSetShader(
            g_vertexShader.Get(),
            nullptr,
            0
        );


        g_rendererContext->VSSetConstantBuffers(
            0,
            1,
            g_transformBuffer.GetAddressOf()
        );


        g_rendererContext->PSSetShader(
            g_pixelShader.Get(),
            nullptr,
            0
        );


        g_rendererContext->PSSetShaderResources(
            0,
            1,
            g_videoSRV.GetAddressOf()
        );


        g_rendererContext->PSSetSamplers(
            0,
            1,
            g_samplerState.GetAddressOf()
        );


        g_rendererContext->Draw(
            4,
            0
        );
    }


    // Present com vsync (1) - sincroniza com o monitor, evita
    // tearing e limita naturalmente a taxa de apresentação.
    g_swapChain->Present(
        1,
        0
    );
}


// ============================================================
// Window Procedure
// ============================================================

void SaveCurrentFrameAsBmp();

LRESULT CALLBACK WindowProc(
    HWND hwnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_ERASEBKGND:
    {
        // ========================================================
        // Evita o "flash" de limpeza de fundo do Windows antes do
        // WM_PAINT rodar - o WM_PAINT já cuida de preencher tudo
        // (com double buffer), então esse erase default é
        // redundante e é uma das causas do flicker.
        // ========================================================

        return 1;
    }


    case WM_PAINT:
    {
        PAINTSTRUCT ps{};

        BeginPaint(
            hwnd,
            &ps
        );

        RenderFrame();

        EndPaint(
            hwnd,
            &ps
        );

        return 0;
    }


    case WM_SIZE:
    {
        uint32_t newWidth =
            LOWORD(lParam);

        uint32_t newHeight =
            HIWORD(lParam);

        ResizeRenderer(
            newWidth,
            newHeight
        );

        return 0;
    }


    case WM_KEYDOWN:
    {
        // ====================================================
        // Tecla S: salva o frame exibido no momento em BMP,
        // sem passar por nenhuma ferramenta de print/foto -
        // pra diagnosticar problemas visuais direto do pixel
        // real que o receiver está desenhando.
        // ====================================================

        if (wParam == 'S')
        {
            SaveCurrentFrameAsBmp();
        }


        return 0;
    }


    case WM_DESTROY:
    {
        // ====================================================
        // Parar aplicação
        // ====================================================

        g_running =
            false;


        // ====================================================
        // Desbloquear recvfrom()/select()
        // ====================================================

        if (
            g_udpSocket !=
            INVALID_SOCKET)
        {
            shutdown(
                g_udpSocket,
                SD_BOTH
            );
        }


        // ====================================================
        // O decoder NÃO é parado aqui.
        //
        // O ReceiverThread pode ainda estar usando-o.
        // ====================================================

        PostQuitMessage(
            0
        );


        return 0;
    }
    }


    return DefWindowProcW(
        hwnd,
        uMsg,
        wParam,
        lParam
    );
}


// ============================================================
// MAIN
// ============================================================

int main()
{
    // ========================================================
    // DPI AWARENESS
    //
    // Mantém a janela e os cálculos de letterbox corretos em
    // monitores com escala. Precisa ser chamado antes de
    // qualquer janela ser criada.
    // ========================================================

    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
    );


    std::cout
        << "========== RECEIVER H264 UDP ==========\n";


    // Comecar com o log do decoder vazio a cada execucao.
    std::ofstream(
        "ffmpeg-decoder.log",
        std::ios::trunc
    );


    // Sem ffmpeg funcionando nao ha video - melhor avisar ja
    // do que deixar a pessoa olhando pra uma tela preta.
    {
        std::string ffmpegProblem;

        if (!CheckFfmpegAvailable(ffmpegProblem))
        {
            PrintFfmpegHelp(ffmpegProblem);

            std::cout
                << "Pressione Enter para sair.\n";

            std::cin.get();

            return 1;
        }
    }


    std::cout
        << "Stream Project - H264/NVENC\n\n";


    // ========================================================
    // CODIGO DA SESSAO ou IP
    //
    // - Codigo de sessao (6 letras/numeros, mostrado no
    //   stream-client): o endereco de quem transmite vem do
    //   servidor de sinalizacao.
    // - IP digitado direto: conexao manual, como antes.
    // - Em branco: 127.0.0.1 (tudo na mesma maquina).
    // ========================================================

    for (;;)
    {
        std::cout
            << "Digite o CODIGO DA SESSAO mostrado no "
            << "stream-client\n"
            << "(ou o IP de quem transmite; em branco = este "
            << "mesmo computador)\n"
            << "Codigo ou IP: ";


        std::string input;

        if (!std::getline(
                std::cin,
                input))
        {
            return 1;
        }


        // tirar espacos nas pontas
        size_t first =
            input.find_first_not_of(" \t\r");

        size_t last =
            input.find_last_not_of(" \t\r");

        input =
            first == std::string::npos
            ? ""
            : input.substr(first, last - first + 1);


        if (input.empty())
        {
            g_serverIp =
                "127.0.0.1";

            break;
        }


        if (!LooksLikeSessionCode(input))
        {
            g_serverIp =
                input;

            break;
        }


        std::cout
            << "Procurando a sessao "
            << input
            << "...\n";


        SignalingHostInfo host;

        std::string error;


        if (!SignalingJoinSession(
                input,
                host,
                error))
        {
            std::cout
                << "Nao foi possivel entrar na sessao: "
                << error
                << "\n\n";

            continue;
        }


        g_serverIp =
            host.sameNetwork
            ? host.localIp
            : host.publicIp;

        // Mesma rede: IP e portas locais do host. Redes
        // diferentes: IP e portas publicas (as que o roteador
        // dele expoe, descobertas por STUN).
        g_serverVideoPort =
            host.sameNetwork
            ? host.videoPort
            : host.publicVideoPort;

        g_serverAudioPort =
            host.sameNetwork
            ? host.audioPort
            : host.publicAudioPort;

        g_sessionCode =
            input;

        // O outro caminho (o que o servidor NAO escolheu).
        g_serverAltIp =
            host.sameNetwork
            ? host.publicIp
            : host.localIp;

        g_serverAltVideoPort =
            host.sameNetwork
            ? host.publicVideoPort
            : host.videoPort;

        g_serverAltAudioPort =
            host.sameNetwork
            ? host.publicAudioPort
            : host.audioPort;


        std::cout
            << "Sessao encontrada ("
            << (host.sameNetwork
                ? "mesma rede"
                : "redes diferentes")
            << ").\n";


        if (!host.sameNetwork)
        {
            std::cout
                << "Conectando direto pela internet (hole "
                << "punching) - pode levar alguns segundos.\n";
        }

        break;
    }


    std::cout
        << "\nServidor: "
        << g_serverIp
        << "\n\n";


    // ========================================================
    // Winsock
    // ========================================================

    WSADATA wsaData{};


    if (
        WSAStartup(
            MAKEWORD(2, 2),
            &wsaData
        ) != 0)
    {
        std::cout
            << "Falha ao iniciar Winsock.\n";


        return 1;
    }


    // ========================================================
    // Socket UDP
    // ========================================================

    g_udpSocket =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );


    if (
        g_udpSocket ==
        INVALID_SOCKET)
    {
        std::cout
            << "Falha ao criar socket UDP.\n";


        WSACleanup();


        return 1;
    }


    // ========================================================
    // Buffer UDP
    // ========================================================

    int receiveBufferSize =
        UDP_RECEIVE_BUFFER_SIZE;


    if (
        setsockopt(
            g_udpSocket,
            SOL_SOCKET,
            SO_RCVBUF,
            reinterpret_cast<
                const char*
            >(
                &receiveBufferSize
            ),
            sizeof(receiveBufferSize)
        ) == SOCKET_ERROR)
    {
        std::cout
            << "Aviso: SO_RCVBUF falhou. Erro: "
            << WSAGetLastError()
            << "\n";
    }
    else
    {
        std::cout
            << "Buffer UDP solicitado: "
            << UDP_RECEIVE_BUFFER_SIZE /
                (1024 * 1024)
            << " MB\n";
    }


    // ========================================================
    // Bind
    // ========================================================

    sockaddr_in localAddress{};


    localAddress.sin_family =
        AF_INET;


    localAddress.sin_addr.s_addr =
        htonl(
            INADDR_ANY
        );


    localAddress.sin_port =
        htons(
            UDP_PORT
        );


    g_localVideoPort =
        BindUdpWithFallback(
            g_udpSocket,
            UDP_PORT
        );

    if (g_localVideoPort == 0)
    {
        std::cout
            << "Falha no bind UDP.\n"
            << "Erro: "
            << WSAGetLastError()
            << "\n";

        closesocket(
            g_udpSocket
        );

        WSACleanup();

        return 1;
    }


    if (g_localVideoPort != UDP_PORT)
    {
        std::cout
            << "Porta "
            << UDP_PORT
            << " ocupada (outro receiver aberto?) - usando "
            << "outra.\n";
    }


    std::cout
        << "UDP receiver na porta "
        << g_localVideoPort
        << "\n";


    // ========================================================
    // Endereço do servidor
    // ========================================================

    sockaddr_in serverAddress{};


    serverAddress.sin_family =
        AF_INET;


    serverAddress.sin_port =
        htons(
            g_serverVideoPort
        );


    if (
        inet_pton(
            AF_INET,
            g_serverIp.c_str(),
            &serverAddress.sin_addr
        ) != 1)
    {
        std::cout
            << "Falha ao configurar endereco do servidor.\n";


        closesocket(
            g_udpSocket
        );


        WSACleanup();


        return 1;
    }


    // ========================================================
    // Salvar endereço do servidor globalmente
    // ========================================================

    g_serverAddress =
        serverAddress;

    g_haveAltAddress =
        !g_serverAltIp.empty() &&
        MakeEndpoint(
            g_serverAltIp,
            g_serverAltVideoPort,
            g_serverAltAddress) &&
        !SameEndpoint(
            g_serverAltAddress,
            g_serverAddress);


    // Hole punching (so quando entramos por codigo de sessao).
    AnnounceToSession(
        g_udpSocket,
        false
    );


    std::cout
        << "Servidor configurado: "
        << g_serverIp
        << ":"
        << g_serverVideoPort
        << "\n";


    std::cout
        << "HELLO sera enviado automaticamente "
        << "a cada 1 segundo ate o stream iniciar.\n\n";


    // ========================================================
    // Janela
    // ========================================================

    const wchar_t CLASS_NAME[] =
        L"StreamReceiverWindow";


    HINSTANCE hInstance =
        GetModuleHandleW(
            nullptr
        );


    WNDCLASSW wc{};


    wc.lpfnWndProc =
        WindowProc;


    wc.hInstance =
        hInstance;


    wc.lpszClassName =
        CLASS_NAME;


    wc.hCursor =
        LoadCursor(
            nullptr,
            IDC_ARROW
        );


    wc.hbrBackground =
        static_cast<HBRUSH>(
            GetStockObject(
                BLACK_BRUSH
            )
        );


    if (
        !RegisterClassW(
            &wc
        ))
    {
        std::cout
            << "Falha ao registrar janela.\n";


        closesocket(
            g_udpSocket
        );


        WSACleanup();


        return 1;
    }


    // ========================================================
    // Criar janela
    // ========================================================

    g_hwnd =
        CreateWindowExW(
            0,

            CLASS_NAME,

            L"Stream Receiver H264",

            WS_OVERLAPPEDWINDOW,

            CW_USEDEFAULT,
            CW_USEDEFAULT,

            1000,
            700,

            nullptr,
            nullptr,

            hInstance,
            nullptr
        );


    if (!g_hwnd)
    {
        std::cout
            << "Falha ao criar janela.\n";


        closesocket(
            g_udpSocket
        );


        WSACleanup();


        return 1;
    }


    if (!InitializeD3D11Renderer(
            g_hwnd))
    {
        std::cout
            << "Falha ao inicializar o renderizador "
            << "Direct3D 11.\n";


        closesocket(
            g_udpSocket
        );


        WSACleanup();


        return 1;
    }


    ShowWindow(
        g_hwnd,
        SW_SHOW
    );


    UpdateWindow(
        g_hwnd
    );


    // ========================================================
    // Thread UDP
    // ========================================================

    std::thread receiverThread(
        ReceiverThread
    );


    std::thread audioReceiverThread(
        AudioReceiverThread
    );


    // ========================================================
    // Message loop
    // ========================================================

    MSG msg{};


    while (
        GetMessageW(
            &msg,
            nullptr,
            0,
            0
        ) > 0)
    {
        TranslateMessage(
            &msg
        );


        DispatchMessageW(
            &msg
        );
    }


    // ========================================================
    // Encerrar
    // ========================================================

    g_running =
        false;


    // ========================================================
    // Desbloquear recvfrom()/select()
    // ========================================================

    if (
        g_udpSocket !=
        INVALID_SOCKET)
    {
        shutdown(
            g_udpSocket,
            SD_BOTH
        );
    }


    // ========================================================
    // Esperar thread UDP
    // ========================================================

    if (
        receiverThread.joinable())
    {
        receiverThread.join();
    }


    if (
        audioReceiverThread.joinable())
    {
        audioReceiverThread.join();
    }


    // ========================================================
    // Agora o decoder pode ser encerrado com segurança
    // ========================================================

    g_decoder.Stop();


    // ========================================================
    // Fechar socket
    // ========================================================

    if (
        g_udpSocket !=
        INVALID_SOCKET)
    {
        closesocket(
            g_udpSocket
        );


        g_udpSocket =
            INVALID_SOCKET;
    }


    // ========================================================
    // Winsock
    // ========================================================

    WSACleanup();


    std::cout
        << "\nReceiver encerrado.\n";


    return 0;
}