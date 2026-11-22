#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>
#include <fstream>
#include <iterator>

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <iostream>
#include <string>
#include <cstdint>
#include <vector>
#include <cctype>
#include <thread>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <atomic>
#include <cstring>

#include "h264_decoder.h"
#include "audio_protocol.h"
#include "audio_decoder.h"
#include "audio_playback.h"
#include "video_protocol.h"
#include "video_reassembly.h"
#include "signaling_client.h"
#include "nat_traversal.h"
#include "ffmpeg_check.h"
#include "stream_crypto.h"
#include "invite_code.h"
#include "ui_common.h"
#include "receiver_screens.h"
#include "crash_report.h"
#include "app_settings.h"
#include "power_mode.h"
#include "sender.h"
#include "room_client.h"
#include "voice_channel.h"
#include "version.h"

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


// ============================================================
// Criptografia do canal (ver stream_crypto.h)
// ============================================================

// Par de chaves desta execucao.
CryptoKeyPair g_myKeys{};

// true = entramos por codigo de sessao e ja temos a chave
// publica de quem transmite -> HELLO com prova ("HEL2").
// false = conexao direta por IP -> HELLO sem prova ("HEL1"),
// e a chave publica do outro lado chega num pacote "HKEY".
bool g_authenticatedMode =
    false;

std::mutex g_cryptoMutex;

std::atomic<bool> g_haveSessionKey{ false };

uint8_t g_sessionKey[CRYPTO_KEY_SIZE]{};


// Calcula e guarda a chave de sessao a partir da chave publica
// de quem transmite. Nao troca uma chave ja estabelecida.
bool EstablishSessionKey(
    const uint8_t hostPublicKey[CRYPTO_KEY_SIZE])
{
    std::lock_guard<std::mutex> lock(
        g_cryptoMutex
    );

    if (g_haveSessionKey)
        return true;

    if (!CryptoDeriveSessionKey(
            g_myKeys.secretKey,
            hostPublicKey,
            hostPublicKey,
            g_myKeys.publicKey,
            g_sessionKey))
    {
        return false;
    }

    g_haveSessionKey =
        true;

    return true;
}


bool GetSessionKey(
    uint8_t outKey[CRYPTO_KEY_SIZE])
{
    if (!g_haveSessionKey)
        return false;

    std::lock_guard<std::mutex> lock(
        g_cryptoMutex
    );

    std::memcpy(
        outKey,
        g_sessionKey,
        CRYPTO_KEY_SIZE
    );

    return true;
}


// Monta o HELLO deste receiver pro canal dado. Devolve o
// tamanho do pacote.
int BuildHelloPacket(
    uint8_t channel,
    uint8_t outPacket[CRYPTO_HELLO_AUTH_SIZE])
{
    uint8_t key[CRYPTO_KEY_SIZE];

    if (g_authenticatedMode &&
        GetSessionKey(key))
    {
        CryptoBuildAuthHello(
            g_myKeys.publicKey,
            key,
            channel,
            outPacket
        );

        return static_cast<int>(
            CRYPTO_HELLO_AUTH_SIZE);
    }

    CryptoBuildAnonHello(
        g_myKeys.publicKey,
        outPacket
    );

    return static_cast<int>(
        CRYPTO_HELLO_ANON_SIZE);
}


// Trata um pacote recem-chegado: se for a chave publica de
// quem transmite ("HKEY", so na conexao direta), guarda; se
// for dado cifrado, decifra pra outPlain. Devolve o tamanho do
// conteudo decifrado, ou 0 se nao ha nada pra processar.
int DecryptIncoming(
    uint8_t channel,
    const char* packet,
    int packetSize,
    char* outPlain)
{
    const uint8_t* bytes =
        reinterpret_cast<const uint8_t*>(packet);

    size_t size =
        static_cast<size_t>(packetSize);

    if (size == CRYPTO_HOST_KEY_PACKET_SIZE &&
        CryptoHasTag(bytes, size, "HKEY"))
    {
        if (!g_authenticatedMode)
        {
            EstablishSessionKey(bytes + 4);
        }

        return 0;
    }

    uint8_t key[CRYPTO_KEY_SIZE];

    if (!GetSessionKey(key))
        return 0;

    size_t plainSize = 0;

    if (!CryptoOpen(
            key,
            channel,
            bytes,
            size,
            reinterpret_cast<uint8_t*>(outPlain),
            plainSize))
    {
        return 0;
    }

    return static_cast<int>(plainSize);
}


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

// Modo de convite manual (ver invite_code.h): entramos colando
// o CONVITE de quem transmite, sem servidor de sinalizacao.
bool g_inviteMode =
    false;

std::mutex g_replyMutex;

StunResult g_myStun[2];

bool g_replyPrinted =
    false;

// Texto da RESPOSTA (convite manual), pra interface mostrar com
// um botao de copiar. Protegido por g_replyMutex.
std::string g_replyText;


void AnnounceToSession(
    SOCKET socket,
    bool isAudio)
{
    if (g_sessionCode.empty() &&
        !g_inviteMode)
    {
        return;
    }


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


    // --------------------------------------------------------
    // Sessao por codigo: o servidor de sinalizacao repassa o
    // nosso endereco pra quem transmite.
    // --------------------------------------------------------

    if (!g_sessionCode.empty())
    {
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

        return;
    }


    // --------------------------------------------------------
    // Convite manual: quem repassa e o proprio usuario. Quando
    // os dois canais (video e audio) tiverem endereco, mostra
    // a RESPOSTA pra ele enviar de volta.
    // --------------------------------------------------------

    std::lock_guard<std::mutex> lock(
        g_replyMutex
    );

    g_myStun[isAudio ? 1 : 0] =
        stun;

    ViewerReply reply;

    if (g_replyPrinted ||
        !g_myStun[0].ok ||
        !g_myStun[1].ok ||
        !ParseIpv4(
            g_myStun[0].publicIp,
            reply.publicIp))
    {
        return;
    }

    reply.publicVideoPort =
        g_myStun[0].publicPort;

    reply.publicAudioPort =
        g_myStun[1].publicPort;

    g_replyPrinted =
        true;

    g_replyText =
        EncodeViewerReply(reply);

    std::cout
        << "\n========================================\n"
        << "RESPOSTA - copie e envie pra quem transmite:\n\n"
        << EncodeViewerReply(reply)
        << "\n\n"
        << "(necessaria quando voces estao em redes\n"
        << "diferentes; na mesma rede conecta sem ela)\n"
        << "========================================\n\n";
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


// Dimensoes DO FRAME QUE ESTA EM g_framePixels - mudam junto
// com ele (sob g_frameMutex), na hora em que o frame vai pra
// tela.
//
// Nao confundir com g_frameWidth/g_frameHeight logo abaixo,
// que sao a resolucao do STREAM e mudam assim que chega um
// pacote com resolucao nova - enquanto g_framePixels ainda
// guarda o ultimo frame no tamanho antigo. Desenhar usando
// uma com os pixels da outra fazia o receiver ler alem do fim
// do buffer e fechar sozinho quando quem transmite trocava a
// janela compartilhada.
uint32_t g_shownWidth =
    0;

uint32_t g_shownHeight =
    0;


// true se "pixels" tem pelo menos width x height x 4 bytes.
bool PixelsFit(
    const std::vector<unsigned char>& pixels,
    uint32_t width,
    uint32_t height)
{
    return
        width > 0 &&
        height > 0 &&
        pixels.size() >=
            static_cast<size_t>(width) * height * 4;
}


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

// true enquanto as threads de video/audio do modo Assistir devem
// rodar. Desligar (ViewerDisconnect) encerra so a conexao - o
// programa continua (ex.: trocar de tela numa sala).
std::atomic<bool>
g_watching{ false };

inline bool WatchKeepGoing()
{
    return g_running && g_watching;
}

// Ultimo pacote valido (video ou sinal de vida) de quem
// transmite - pra perceber conexao caida (reconexao automatica).
std::atomic<ULONGLONG> g_lastVideoPacketMs{ 0 };

// Quem transmite manda sinal de vida (versao 0.6.0 em diante).
// So com ele da pra distinguir "tela parada" de "conexao caiu".
std::atomic<bool> g_senderSendsAlive{ false };


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
// Montagem dos frames (com pedido de reenvio - ver
// video_reassembly.h). So a thread de video mexe nela.
// ============================================================

VideoReassembler g_reassembler;


// Relogio em ms com fracao (GetTickCount64 so anda de ~15 em
// ~15 ms - grosso demais pra medir a ida e volta da rede).
double NowMs()
{
    static LARGE_INTEGER frequency = []
    {
        LARGE_INTEGER value;

        QueryPerformanceFrequency(&value);

        return value;
    }();

    LARGE_INTEGER counter;

    QueryPerformanceCounter(&counter);

    return
        static_cast<double>(counter.QuadPart) * 1000.0 /
        static_cast<double>(frequency.QuadPart);
}


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

ULONGLONG g_lastDecoderRestart =
    0;

// Quantas vezes o decoder morreu nesta execucao. Na segunda,
// troca a decodificacao por GPU pela de CPU (mais compativel).
int g_decoderDeaths =
    0;

// ------------------------------------------------------------
// Estatisticas por causa (vao pro log a cada 5s e pra tela, no
// painel de volume). Servem pra separar "a rede perdeu" de "o
// computador nao deu conta" quando a imagem trava.
// ------------------------------------------------------------

// Frames que nao chegaram inteiros (pacote perdido na rede e o
// reenvio nao chegou a tempo).
std::atomic<uint64_t> g_statLostNetwork{ 0 };

// Frames que perderam pedacos, mas foram salvos pelo reenvio.
std::atomic<uint64_t> g_statRecovered{ 0 };

// Ida e volta ate quem transmite (ms), medida pelos reenvios.
std::atomic<int> g_statRoundTripMs{ 0 };

// Frames que chegaram, mas foram descartados esperando o
// proximo keyframe (consequencia de uma perda anterior).
std::atomic<uint64_t> g_statSkippedForKeyframe{ 0 };

// Frames decodificados descartados por atraso (este computador
// nao conseguiu mostrar a tempo).
std::atomic<uint64_t> g_statLate{ 0 };


// frameId do primeiro pacote da resolucao atual. Pela internet
// os pacotes podem chegar fora de ordem: logo depois de uma
// troca de resolucao (ex.: quem transmite maximizou a janela),
// ainda chegam pacotes atrasados do tamanho antigo. Sem esta
// marca, cada um deles parecia "outra troca" e o decoder era
// reiniciado de novo e de novo.
uint32_t g_resolutionFrameId =
    0;

bool g_haveResolutionFrameId =
    false;


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


        // Frame que ainda saiu do decoder antigo, logo depois de
        // uma troca de resolucao: o tamanho nao bate com a
        // resolucao atual. Descarta.
        if (decodedFrame.size() !=
            static_cast<size_t>(g_frameWidth) *
                g_frameHeight * 4)
        {
            decodedFrame.clear();

            continue;
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

                g_statLate++;
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


            // Frame com tamanho que nao bate (sobra de antes de
            // uma troca de resolucao): descarta.
            if (!PixelsFit(
                    front.pixels,
                    front.width,
                    front.height))
            {
                g_pendingVideoFrames.pop_front();

                continue;
            }


            g_framePixels =
                std::move(
                    front.pixels
                );

            g_shownWidth =
                front.width;

            g_shownHeight =
                front.height;


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


    // (O titulo da janela e atualizado pela thread da janela -
    // ver UpdateWindowTitle. Daqui, da thread de video, o
    // SetWindowText faria esta thread esperar pela da janela,
    // e a janela pode estar esperando esta thread terminar.)
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
// SendVideoNack
// ============================================================
//
// Pedido de reenvio montado pelo g_reassembler: cifra com a
// chave de sessao (canal proprio) e manda pra quem transmite,
// pelo mesmo caminho do stream.
// ============================================================

uint64_t g_feedbackCounter =
    0;

void SendVideoNack(
    const uint8_t* nack,
    size_t size)
{
    uint8_t key[CRYPTO_KEY_SIZE];

    if (!GetSessionKey(key) ||
        size > VIDEO_NACK_MAX_SIZE)
    {
        return;
    }

    uint8_t sealed[VIDEO_NACK_MAX_SIZE + CRYPTO_OVERHEAD];

    size_t sealedSize =
        CryptoSeal(
            key,
            CRYPTO_CHANNEL_VIDEO_FEEDBACK,
            g_feedbackCounter++,
            nack,
            size,
            sealed
        );

    sendto(
        g_udpSocket,
        reinterpret_cast<const char*>(sealed),
        static_cast<int>(sealedSize),
        0,
        reinterpret_cast<sockaddr*>(&g_serverAddress),
        sizeof(g_serverAddress)
    );
}


// Copia os contadores do g_reassembler pras estatisticas que a
// tela e o log leem.
void SyncReassemblyStats()
{
    g_statLostNetwork =
        g_reassembler.LostFrames();

    g_statSkippedForKeyframe =
        g_reassembler.SkippedForKeyframe();

    g_statRecovered =
        g_reassembler.RecoveredFrames();

    g_statRoundTripMs =
        static_cast<int>(g_reassembler.RoundTripMs() + 0.5);
}


// ============================================================
// DeliverAssembledFrame
// ============================================================
//
// Frame completo (e na ordem) vindo do g_reassembler: vai pro
// decoder. O g_reassembler ja garante que, depois de uma perda,
// o primeiro frame entregue e um keyframe.
// ============================================================

void DeliverAssembledFrame(
    AssembledFrame& frame)
{
    EstablishSyncBaseline(
        frame.timestampMs
    );


    {
        std::lock_guard<std::mutex> lock(
            g_frameMutex
        );


        g_videoTimestampQueue.push_back(
            frame.timestampMs
        );
    }


    if (!g_decoder.WriteData(
            reinterpret_cast<const char*>(
                frame.data.data()
            ),
            static_cast<uint32_t>(
                frame.data.size()
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

        g_reassembler.RequireKeyframe();

        return;
    }


    // ========================================================
    // Verificar se o decoder já produziu frame
    // ========================================================

    ProcessDecodedFrame();
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

    // Pacote atrasado de antes da ultima troca de resolucao:
    // descarta. (Um salto MUITO pra tras significa que quem
    // transmite reiniciou a contagem - ai vale normalmente.)
    if (g_haveResolutionFrameId &&
        header.frameId < g_resolutionFrameId &&
        g_resolutionFrameId - header.frameId < 600)
    {
        return;
    }


    if (
        header.width != g_frameWidth ||
        header.height != g_frameHeight)
    {
        std::cout
            << "\n========================================\n"
            << "Resolucao: "
            << g_frameWidth
            << "x"
            << g_frameHeight
            << " -> "
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

        g_resolutionFrameId =
            header.frameId;

        g_haveResolutionFrameId =
            true;


        // Qualquer montagem em andamento não serve mais.

        g_reassembler.Reset();


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
    // Montagem (pede reenvio do que faltar; entrega os frames
    // completos, na ordem, via DeliverAssembledFrame)
    // ========================================================

    g_reassembler.AddFragment(
        header,
        reinterpret_cast<const unsigned char*>(
            buffer + VIDEO_HEADER_SIZE),
        NowMs()
    );

    SyncReassemblyStats();
}


// ============================================================
// Enviar HELLO para o servidor
// ============================================================

bool SendHello()
{
    uint8_t hello[CRYPTO_HELLO_AUTH_SIZE];

    int helloSize =
        BuildHelloPacket(
            CRYPTO_CHANNEL_VIDEO,
            hello
        );


    if (g_haveAltAddress)
    {
        sendto(
            g_udpSocket,
            reinterpret_cast<const char*>(hello),
            helloSize,
            0,
            reinterpret_cast<sockaddr*>(
                &g_serverAltAddress
            ),
            sizeof(g_serverAltAddress)
        );
    }


    int sent =
        sendto(
            g_udpSocket,
            reinterpret_cast<const char*>(hello),
            helloSize,
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


    // Pacote como chega da rede (cifrado, um pouco maior)...
    std::vector<char> buffer(
        VIDEO_UDP_PACKET_SIZE +
        CRYPTO_OVERHEAD
    );

    // ...e o conteudo depois de decifrado.
    std::vector<char> plainBuffer(
        VIDEO_UDP_PACKET_SIZE
    );


    // ========================================================
    // Controle do intervalo entre HELLOs
    // ========================================================

    ULONGLONG lastHelloTime =
        0;


    g_reassembler.SetCallbacks(
        DeliverAssembledFrame,
        SendVideoNack
    );


    while (WatchKeepGoing())
    {
        // ====================================================
        // Perdas: pede reenvio do que faltou / desiste do que
        // demorou demais (tambem quando nao chega pacote).
        // ====================================================

        g_reassembler.Service(
            NowMs());

        SyncReassemblyStats();

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


        // Com frame em montagem, acorda a cada 5 ms pra
        // perceber perdas rapido; sem nada pendente, 200 ms.
        timeout.tv_usec =
            g_reassembler.HasPending()
            ? 5000
            : 200000;


        int result =
            select(
                0,
                &readSet,
                nullptr,
                nullptr,
                &timeout
            );


        if (!WatchKeepGoing())
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
            if (!WatchKeepGoing())
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

        // Tudo que chega do client vem cifrado. Pacote que nao
        // decifra (de outra origem, adulterado, ou anterior a
        // troca de chaves) e simplesmente ignorado.
        int plainSize =
            DecryptIncoming(
                CRYPTO_CHANNEL_VIDEO,
                buffer.data(),
                received,
                plainBuffer.data()
            );

        if (plainSize <= 0)
            continue;

        g_lastVideoPacketMs = GetTickCount64();

        // Sinal de vida (tela parada do outro lado): nada a
        // montar.
        if (plainSize == static_cast<int>(sizeof(VIDEO_ALIVE_TAG)) &&
            std::memcmp(
                plainBuffer.data(),
                VIDEO_ALIVE_TAG,
                sizeof(VIDEO_ALIVE_TAG)) == 0)
        {
            g_senderSendsAlive = true;

            continue;
        }


        ProcessUdpPacket(
            plainBuffer.data(),
            plainSize
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


    // Saindo so desta conexao (o programa continua): avisa
    // quem transmite pra liberar a vaga na hora, em vez de
    // esperar os 15 s sem sinal de vida.
    if (g_running &&
        g_udpSocket != INVALID_SOCKET)
    {
        sendto(
            g_udpSocket,
            "BYE1",
            4,
            0,
            reinterpret_cast<sockaddr*>(&g_serverAddress),
            sizeof(g_serverAddress)
        );
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
        AUDIO_UDP_PACKET_SIZE +
        CRYPTO_OVERHEAD
    );

    std::vector<char> plainAudio(
        AUDIO_UDP_PACKET_SIZE
    );


    ULONGLONG lastHelloTime =
        0;


    while (WatchKeepGoing())
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
            uint8_t hello[CRYPTO_HELLO_AUTH_SIZE];

            int helloSize =
                BuildHelloPacket(
                    CRYPTO_CHANNEL_AUDIO,
                    hello
                );

            sendto(
                audioSocket,
                reinterpret_cast<const char*>(hello),
                helloSize,
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
                    reinterpret_cast<const char*>(hello),
                    helloSize,
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


        if (!WatchKeepGoing())
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


        if (received <= 0)
        {
            continue;
        }


        // Decifra e segue trabalhando no mesmo buffer, ja com
        // o conteudo original (header + Opus).
        {
            int plainSize =
                DecryptIncoming(
                    CRYPTO_CHANNEL_AUDIO,
                    buffer.data(),
                    received,
                    plainAudio.data()
                );

            if (plainSize <= 0)
                continue;

            std::memcpy(
                buffer.data(),
                plainAudio.data(),
                static_cast<size_t>(plainSize)
            );

            received =
                plainSize;
        }


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

    // (ver o fim do ReceiverThread)
    if (g_running)
    {
        sendto(
            audioSocket,
            "BYE1",
            4,
            0,
            reinterpret_cast<sockaddr*>(&serverAddress),
            sizeof(serverAddress)
        );
    }

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


        if (!PixelsFit(
                g_framePixels,
                g_shownWidth,
                g_shownHeight))
        {
            std::cout
                << "\nSaveCurrentFrameAsBmp: nenhum frame "
                << "disponivel ainda.\n";

            return;
        }


        pixelsCopy =
            g_framePixels;

        width =
            g_shownWidth;

        height =
            g_shownHeight;
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
// INTERFACE + CONEXAO
// ============================================================
//
// O programa abre direto numa janela com a tela de conexao (o
// console de antes virou arquivo de log). O fluxo:
//
//   Connect     -> usuario digita codigo / cola convite / IP
//   Connecting  -> uma thread resolve o destino e abre a rede
//                  (a interface continua respondendo)
//   Connected   -> threads de video e audio rodando; ate o
//                  primeiro frame chegar, mostra "aguardando"
//
// FfmpegProblem aparece no lugar de tudo se o ffmpeg.exe faltar
// ou estiver quebrado.
// ============================================================

enum class UiState
{
    FfmpegProblem,
    Connect,
    Connecting,
    Connected
};

std::atomic<UiState> g_uiState{ UiState::Connect };


// ============================================================
// Modo do programa
// ============================================================
//
// Um programa so (StreamProject.exe) com dois modos:
//
//   Home   tela inicial: "Assistir" ou "Transmitir"
//   Watch  assistir - o fluxo do antigo stream-receiver (este
//          arquivo)
//   Share  transmitir sem sala - o antigo stream-client, que
//          virou o modulo "sender" (sender.h)
//   Room   numa sala: lista de quem transmite, assistir
//          qualquer um, transmitir tambem (ver RoomView)
//
// A janela, o Direct3D e a interface sao sempre deste arquivo;
// no modo Share, quem desenha as telas e o sender.
// ============================================================

enum class AppMode
{
    Home,
    Watch,
    Share,
    Room
};

std::atomic<AppMode> g_appMode{ AppMode::Home };

// Aviso na tela inicial (ex.: o modo Transmitir nao conseguiu
// iniciar). So a thread da interface mexe.
std::string g_homeError;

// Protege os textos abaixo (escritos pela thread de conexao,
// lidos pela interface).
std::mutex g_uiMutex;

std::string g_uiError;

std::string g_uiStatus;

std::string g_ffmpegProblem;

// Aviso de versoes diferentes entre quem transmite e este
// receiver (vazio = sem aviso). Protegido por g_uiMutex.
std::string g_versionWarning;

// Campo de texto da tela de conexao (so a thread da interface
// mexe nele).
char g_uiInput[256]{};

// Volume (0.0 a 1.0) e mudo - so a thread da interface mexe;
// aplicados no g_audioPlayback a cada frame.
float g_volume = 1.0f;

bool g_muted = false;

// Canal de voz: tecla de "segurar para falar" (indice em
// VOICE_PTT_OPTION_NAMES, salvo) e microfone mudo (so nesta
// execucao).
int g_voicePushToTalk = 0;

bool g_voiceMuted = false;

// Microfone da voz (id do Windows, salvo; vazio = padrao) e a
// lista mostrada na tela (atualizada ao entrar na voz e ao
// abrir a lista - enumerar a cada frame seria caro).
std::wstring g_voiceMicrophone;

std::vector<VoiceCapture::Device> g_microphones;

// Saida da voz (mesmo esquema).
std::wstring g_voiceSpeaker;

std::vector<VoiceCapture::Device> g_speakers;

// Indice da opcao -> tecla virtual (0 = fala livre).
int VoicePushToTalkKey(
    int option)
{
    switch (option)
    {
    case 1: return VK_XBUTTON1;
    case 2: return VK_XBUTTON2;
    case 3: return VK_RMENU;
    case 4: return VK_RCONTROL;
    default: return 0;
    }
}

// Exibicao do video (ver StreamOverlayModel).
bool g_fillWindow = false;

bool g_overlayCollapsed = false;

// Tela cheia: guarda como a janela estava pra poder voltar.
bool g_fullscreen = false;

WINDOWPLACEMENT g_windowedPlacement{};


// ============================================================
// Configuracoes salvas (ver app_settings.h)
// ============================================================

AppSettings& ReceiverSettings()
{
    static AppSettings settings(L"receiver.ini");

    return settings;
}


void LoadReceiverSettings()
{
    AppSettings& settings =
        ReceiverSettings();

    int volume =
        settings.GetInt(L"Volume", 100);

    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;

    g_volume =
        static_cast<float>(volume) / 100.0f;

    g_muted =
        settings.GetInt(L"Mudo", 0) != 0;

    g_fillWindow =
        settings.GetInt(L"PreencherJanela", 0) != 0;

    g_overlayCollapsed =
        settings.GetInt(L"PainelFechado", 0) != 0;

    g_voicePushToTalk =
        settings.GetInt(L"VozTecla", 0);

    g_voiceMicrophone =
        settings.GetString(L"VozMicrofone", L"");

    g_voiceSpeaker =
        settings.GetString(L"VozSaida", L"");

    if (g_voicePushToTalk < 0 ||
        g_voicePushToTalk >= VOICE_PTT_OPTION_COUNT)
    {
        g_voicePushToTalk = 0;
    }
}


// Chamada a cada frame: grava so quando algo mudou e ficou
// parado por meio segundo (arrastar o volume nao vira dezenas
// de gravacoes).
void SaveReceiverSettingsIfChanged()
{
    struct Saved
    {
        int volume;
        bool muted;
        bool fill;
        bool collapsed;
        int voiceKey;

        bool operator==(const Saved& other) const
        {
            return volume == other.volume &&
                   muted == other.muted &&
                   fill == other.fill &&
                   collapsed == other.collapsed &&
                   voiceKey == other.voiceKey;
        }
    };

    static bool haveSaved = false;
    static Saved saved{};
    static Saved pending{};
    static ULONGLONG pendingSince = 0;

    Saved now{
        static_cast<int>(g_volume * 100.0f + 0.5f),
        g_muted,
        g_fillWindow,
        g_overlayCollapsed,
        g_voicePushToTalk
    };

    if (!haveSaved)
    {
        // Primeira chamada: e o que acabou de ser carregado.
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
        ReceiverSettings();

    settings.SetInt(L"Volume", pending.volume);
    settings.SetInt(L"Mudo", pending.muted ? 1 : 0);
    settings.SetInt(L"PreencherJanela", pending.fill ? 1 : 0);
    settings.SetInt(L"PainelFechado", pending.collapsed ? 1 : 0);
    settings.SetInt(L"VozTecla", pending.voiceKey);

    saved = pending;
}


void SaveReceiverWindowPlacement()
{
    if (!g_hwnd)
        return;

    if (g_fullscreen)
    {
        // Em tela cheia, o que vale e como a janela estava
        // antes.
        ReceiverSettings().SaveWindowPlacement(
            g_windowedPlacement);

        return;
    }

    WINDOWPLACEMENT placement{};

    placement.length = sizeof(placement);

    if (GetWindowPlacement(g_hwnd, &placement))
    {
        ReceiverSettings().SaveWindowPlacement(
            placement);
    }
}

// Pedido de tela cheia vindo da interface (botao do painel). Ver
// o comentario em DrawReceiverUi.
constexpr UINT WM_APP_TOGGLE_FULLSCREEN = WM_APP + 10;


// ============================================================
// ToggleFullscreen
// ============================================================
//
// Tela cheia "sem borda": a janela perde a moldura e passa a
// ocupar o monitor inteiro em que esta. E o jeito que players
// de video costumam fazer - alternar e instantaneo e nao muda
// a resolucao do monitor.
// ============================================================

void ToggleFullscreen()
{
    if (!g_hwnd)
        return;

    DWORD style =
        static_cast<DWORD>(
            GetWindowLongW(g_hwnd, GWL_STYLE));

    if (!g_fullscreen)
    {
        MONITORINFO monitor{};

        monitor.cbSize =
            sizeof(monitor);

        g_windowedPlacement.length =
            sizeof(g_windowedPlacement);

        if (!GetWindowPlacement(
                g_hwnd,
                &g_windowedPlacement) ||
            !GetMonitorInfoW(
                MonitorFromWindow(
                    g_hwnd,
                    MONITOR_DEFAULTTOPRIMARY),
                &monitor))
        {
            return;
        }

        SetWindowLongW(
            g_hwnd,
            GWL_STYLE,
            static_cast<LONG>(
                style & ~WS_OVERLAPPEDWINDOW)
        );

        SetWindowPos(
            g_hwnd,
            HWND_TOP,
            monitor.rcMonitor.left,
            monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left,
            monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED
        );

        g_fullscreen = true;
    }
    else
    {
        SetWindowLongW(
            g_hwnd,
            GWL_STYLE,
            static_cast<LONG>(
                style | WS_OVERLAPPEDWINDOW)
        );

        SetWindowPlacement(
            g_hwnd,
            &g_windowedPlacement
        );

        SetWindowPos(
            g_hwnd,
            nullptr,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED
        );

        g_fullscreen = false;
    }
}

std::atomic<bool> g_connectBusy{ false };

// "Voltar" enquanto a conexao ainda estava sendo montada: a
// thread de conexao desiste ao terminar, em vez de comecar a
// receber.
std::atomic<bool> g_connectCancelled{ false };

// O que foi usado na ultima conexao (codigo, convite ou IP) -
// pra reconexao automatica.
std::string g_lastWatchTarget;

std::thread g_connectThread;

std::thread g_receiverThread;

std::thread g_audioReceiverThread;


void SetUiStatus(
    const std::string& text)
{
    std::lock_guard<std::mutex> lock(
        g_uiMutex
    );

    g_uiStatus =
        text;
}


// Volta tudo que uma tentativa de conexao anterior possa ter
// deixado preenchido, pra uma nova tentativa comecar limpa.
void ResetConnectionState()
{
    g_sessionCode.clear();

    g_inviteMode =
        false;

    g_authenticatedMode =
        false;

    {
        std::lock_guard<std::mutex> lock(
            g_cryptoMutex
        );

        g_haveSessionKey =
            false;
    }

    g_serverIp =
        "127.0.0.1";

    g_serverVideoPort =
        SERVER_UDP_PORT;

    g_serverAudioPort =
        AUDIO_SERVER_UDP_PORT;

    g_serverAltIp.clear();

    g_serverAltVideoPort =
        0;

    g_serverAltAudioPort =
        0;

    g_haveAltAddress =
        false;

    {
        std::lock_guard<std::mutex> lock(
            g_replyMutex
        );

        g_replyPrinted =
            false;

        g_replyText.clear();

        g_myStun[0] =
            StunResult{};

        g_myStun[1] =
            StunResult{};
    }
}


// ============================================================
// ResolveTarget
// ============================================================
//
// Interpreta o que o usuario digitou e descobre pra onde
// conectar:
//
//   - em branco            -> este mesmo computador
//   - convite ("SPH1...")  -> conexao sem servidor
//   - codigo de sessao     -> pergunta ao servidor de sinalizacao
//   - endereco IP          -> conexao direta
// ============================================================

bool ResolveTarget(
    std::string input,
    std::string& outError)
{
    size_t first =
        input.find_first_not_of(" \t\r\n");

    size_t last =
        input.find_last_not_of(" \t\r\n");

    input =
        first == std::string::npos
        ? ""
        : input.substr(first, last - first + 1);


    if (input.empty())
    {
        g_serverIp =
            "127.0.0.1";

        return true;
    }


    // --------------------------------------------------------
    // Convite manual
    // --------------------------------------------------------

    if (LooksLikeHostInvite(input))
    {
        HostInvite invite;

        if (!DecodeHostInvite(input, invite) ||
            !EstablishSessionKey(invite.hostKey))
        {
            outError =
                "Convite incompleto ou alterado. Copie o texto "
                "inteiro de novo.";

            return false;
        }

        g_authenticatedMode =
            true;

        g_inviteMode =
            true;

        std::string localIp =
            FormatIpv4(invite.localIp);

        if (!IsZeroIpv4(invite.publicIp))
        {
            // Caminho principal pela internet, com a rede local
            // como alternativa (o HELLO vai pros dois; segue o
            // que responder).
            g_serverIp =
                FormatIpv4(invite.publicIp);

            g_serverVideoPort =
                invite.publicVideoPort;

            g_serverAudioPort =
                invite.publicAudioPort;

            g_serverAltIp =
                localIp;

            g_serverAltVideoPort =
                invite.localVideoPort;

            g_serverAltAudioPort =
                invite.localAudioPort;
        }
        else
        {
            g_serverIp =
                localIp;

            g_serverVideoPort =
                invite.localVideoPort;

            g_serverAudioPort =
                invite.localAudioPort;
        }

        std::cout
            << "Convite aceito.\n";

        return true;
    }


    // --------------------------------------------------------
    // Codigo de sessao
    // --------------------------------------------------------

    if (LooksLikeSessionCode(input))
    {
        SetUiStatus(
            "Procurando a sessão"
        );

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
                << "\n";

            // O detalhe tecnico fica no log; na tela vai uma
            // frase que a pessoa consiga agir em cima.
            if (error.find("sessao nao encontrada") !=
                std::string::npos)
            {
                outError =
                    "Sessão não encontrada. Confira o código - "
                    "ou a transmissão já foi encerrada.";
            }
            else
            {
                outError =
                    "Não foi possível falar com o servidor de "
                    "sessões. Verifique sua internet e tente de "
                    "novo.";
            }

            return false;
        }

        // Chave publica de quem transmite: sem ela nao ha como
        // montar o canal cifrado.
        uint8_t hostPublicKey[CRYPTO_KEY_SIZE];

        if (!CryptoFromHex(
                host.hostKey,
                hostPublicKey,
                CRYPTO_KEY_SIZE) ||
            !EstablishSessionKey(hostPublicKey))
        {
            outError =
                "Quem está transmitindo usa uma versão antiga "
                "do programa, sem criptografia. Peça para "
                "atualizar e tente de novo.";

            return false;
        }

        g_authenticatedMode =
            true;

        g_sessionCode =
            input;

        // Versoes diferentes costumam funcionar, mas sao a
        // primeira suspeita quando algo da errado.
        if (!host.hostVersion.empty() &&
            host.hostVersion != STREAM_APP_VERSION)
        {
            std::cout
                << "AVISO: quem transmite usa a versao "
                << host.hostVersion
                << " (esta e a "
                << STREAM_APP_VERSION
                << ").\n";

            std::lock_guard<std::mutex> lock(
                g_uiMutex
            );

            g_versionWarning =
                "Quem transmite usa a versão " +
                host.hostVersion +
                " e você a " STREAM_APP_VERSION
                ". Se algo der errado, usem a mesma versão.";
        }

        // Mesma rede: IP e portas locais do host. Redes
        // diferentes: IP e portas publicas. O outro caminho
        // fica como alternativa.
        g_serverIp =
            host.sameNetwork
            ? host.localIp
            : host.publicIp;

        g_serverVideoPort =
            host.sameNetwork
            ? host.videoPort
            : host.publicVideoPort;

        g_serverAudioPort =
            host.sameNetwork
            ? host.audioPort
            : host.publicAudioPort;

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

        return true;
    }


    // --------------------------------------------------------
    // Endereco IP
    // --------------------------------------------------------

    uint8_t ip[4];

    if (!ParseIpv4(input, ip))
    {
        outError =
            "Isso não parece um código de sessão (6 letras e "
            "números), um convite nem um endereço IP.";

        return false;
    }

    g_serverIp =
        input;

    return true;
}


// ============================================================
// SetupVideoSocket
// ============================================================
//
// Cria o socket UDP de video, descobre o endereco publico dele
// (STUN) e avisa quem transmite, conforme o modo de conexao.
// ============================================================

bool SetupVideoSocket(
    std::string& outError)
{
    g_udpSocket =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );

    if (g_udpSocket == INVALID_SOCKET)
    {
        outError =
            "Falha ao criar o socket de rede (erro " +
            std::to_string(WSAGetLastError()) +
            ").";

        return false;
    }


    int receiveBufferSize =
        UDP_RECEIVE_BUFFER_SIZE;

    setsockopt(
        g_udpSocket,
        SOL_SOCKET,
        SO_RCVBUF,
        reinterpret_cast<const char*>(
            &receiveBufferSize),
        sizeof(receiveBufferSize)
    );


    g_localVideoPort =
        BindUdpWithFallback(
            g_udpSocket,
            UDP_PORT
        );

    if (g_localVideoPort == 0)
    {
        outError =
            "Falha ao abrir a porta de rede (erro " +
            std::to_string(WSAGetLastError()) +
            ").";

        closesocket(g_udpSocket);

        g_udpSocket =
            INVALID_SOCKET;

        return false;
    }

    std::cout
        << "UDP receiver na porta "
        << g_localVideoPort
        << "\n";


    if (!MakeEndpoint(
            g_serverIp,
            g_serverVideoPort,
            g_serverAddress))
    {
        outError =
            "Endereço de quem transmite inválido: " +
            g_serverIp +
            ".";

        closesocket(g_udpSocket);

        g_udpSocket =
            INVALID_SOCKET;

        return false;
    }

    g_haveAltAddress =
        !g_serverAltIp.empty() &&
        MakeEndpoint(
            g_serverAltIp,
            g_serverAltVideoPort,
            g_serverAltAddress) &&
        !SameEndpoint(
            g_serverAltAddress,
            g_serverAddress);


    // Hole punching (sessao por codigo ou convite manual).
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

    return true;
}


// ============================================================
// ConnectWorker / StartConnect
// ============================================================
//
// A conexao roda numa thread propria porque pode demorar (o
// servidor de sinalizacao gratuito leva ate ~1 minuto pra
// "acordar"; o STUN, alguns segundos). A interface segue viva
// e mostra o andamento.
// ============================================================

void ConnectWorker(
    std::string input)
{
    ResetConnectionState();

    {
        std::lock_guard<std::mutex> lock(
            g_uiMutex
        );

        g_versionWarning.clear();
    }

    std::string error;

    bool ok =
        ResolveTarget(input, error);

    if (ok)
    {
        SetUiStatus(
            "Preparando a conexão"
        );

        ok =
            SetupVideoSocket(error);
    }

    if (!ok)
    {
        {
            std::lock_guard<std::mutex> lock(
                g_uiMutex
            );

            g_uiError =
                error;
        }

        g_uiState =
            UiState::Connect;

        g_connectBusy =
            false;

        return;
    }

    if (g_connectCancelled)
    {
        // Desistiram no meio (voltaram pra sala): nao comeca.
        closesocket(g_udpSocket);

        g_udpSocket =
            INVALID_SOCKET;

        g_uiState =
            UiState::Connect;

        g_connectBusy =
            false;

        return;
    }

    g_watching =
        true;

    g_receiverThread =
        std::thread(ReceiverThread);

    g_audioReceiverThread =
        std::thread(AudioReceiverThread);

    g_uiState =
        UiState::Connected;

    g_connectBusy =
        false;
}


void StartConnect()
{
    if (g_connectBusy.exchange(true))
        return;

    g_connectCancelled =
        false;

    g_lastWatchTarget =
        std::string(g_uiInput);

    // A tentativa anterior (se houve) ja terminou - so falta
    // recolher a thread.
    if (g_connectThread.joinable())
    {
        g_connectThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(
            g_uiMutex
        );

        g_uiError.clear();

        g_uiStatus =
            "Conectando";
    }

    g_uiState =
        UiState::Connecting;

    g_connectThread =
        std::thread(
            ConnectWorker,
            std::string(g_uiInput)
        );
}


// ============================================================
// ViewerDisconnect
// ============================================================
//
// Encerra so a conexao do modo Assistir (o programa continua):
// para as threads de video e audio (que avisam quem transmite
// que saimos), fecha o socket, para o decoder e zera tudo, pra
// uma nova conexao comecar limpa. Chamar pela thread da janela.
//
// Se a conexao ainda esta sendo montada, so marca pra ela
// desistir ao terminar.
// ============================================================

void ViewerDisconnect()
{
    if (g_connectBusy)
    {
        g_connectCancelled = true;

        return;
    }

    if (g_connectThread.joinable())
    {
        g_connectThread.join();
    }

    bool wasWatching =
        g_watching.exchange(false);

    // As threads acordam em ate 200 ms (select) e saem.
    if (g_receiverThread.joinable())
    {
        g_receiverThread.join();
    }

    if (g_audioReceiverThread.joinable())
    {
        g_audioReceiverThread.join();
    }

    g_decoder.Stop();

    if (g_udpSocket != INVALID_SOCKET)
    {
        closesocket(g_udpSocket);

        g_udpSocket =
            INVALID_SOCKET;
    }


    // --------------------------------------------------------
    // Estado do video
    // --------------------------------------------------------

    {
        std::lock_guard<std::mutex> lock(
            g_frameMutex
        );

        g_framePixels.clear();

        g_shownWidth = 0;
        g_shownHeight = 0;

        g_pendingVideoFrames.clear();

        g_videoTimestampQueue.clear();
    }

    {
        std::lock_guard<std::mutex> lock(
            g_syncMutex
        );

        g_haveSyncBaseline = false;
    }

    g_streamReceived = false;

    g_senderSendsAlive = false;

    g_lastVideoPacketMs = 0;

    g_frameWidth = 0;
    g_frameHeight = 0;

    g_haveResolutionFrameId = false;
    g_resolutionFrameId = 0;

    g_reassembler = VideoReassembler();

    SyncReassemblyStats();

    g_statLate = 0;

    g_fps = 0;

    {
        std::lock_guard<std::mutex> lock(
            g_uiMutex
        );

        g_versionWarning.clear();
    }

    g_uiState =
        UiState::Connect;

    if (wasWatching)
    {
        std::cout
            << "\nConexao encerrada (Assistir).\n";
    }
}


// ============================================================
// DrawReceiverUi
// ============================================================
// OpenShareMode
// ============================================================
//
// "Transmitir" na tela inicial. Na primeira vez liga o motor do
// modo (portas UDP, threads - ver sender.h); depois so volta pra
// tela dele.
// ============================================================

void OpenShareMode()
{
    std::string error;

    if (!sender::Open(
            g_hwnd,
            g_rendererDevice.Get(),
            g_rendererContext.Get(),
            error))
    {
        g_homeError = error;

        g_appMode = AppMode::Home;

        return;
    }

    g_homeError.clear();

    g_appMode = AppMode::Share;
}


// ============================================================
// SALA
// ============================================================
//
// Dentro da sala a tela alterna entre:
//
//   Lobby  a lista (quem transmite, quem esta, transmitir)
//   Watch  assistindo alguem (video + painel)
//   Share  as telas do modo Transmitir (opcoes, escolher a
//          tela, painel da transmissao)
//
// Assistir alguem = entrar na SESSAO dessa pessoa pelo caminho
// de sempre (StartConnect com o codigo da sessao). Trocar de
// tela = ViewerDisconnect + conectar na outra.
// ============================================================

enum class RoomView
{
    Lobby,
    Watch,
    Share
};

RoomClient g_room;

// Canal de voz da sala (ver voice_channel.h).
VoiceChannel g_voice;

RoomView g_roomView = RoomView::Lobby;

// Quem esta sendo assistido (vazio = ninguem). So a thread da
// janela mexe nestes.
std::string g_watchSessionCode;
std::string g_watchName;

// Acabou de clicar em "Assistir": vai pro video quando
// conectar.
bool g_watchAutoShow = false;

std::string g_roomNotice;
std::string g_roomError;


// ------------------------------------------------------------
// Chat, avisos e reconexao (so a thread da janela mexe)
// ------------------------------------------------------------

char g_chatInput[300]{};

// Chat aberto por cima do video.
bool g_videoChatOpen = false;

// Por o cursor no campo do chat no proximo frame.
bool g_chatFocus = false;

// Quantas mensagens ja foram vistas (chat visivel) e quantas ja
// viraram aviso.
uint64_t g_chatSeenTotal = 0;
uint64_t g_chatToastedTotal = 0;

// As mensagens que ja estavam na sala ao entrar (historico) nao
// viram avisos nem contam como novas.
bool g_chatBaselineDone = false;

std::vector<Toast> g_toasts;

// Estado anterior da sala, pra avisar quem entrou/saiu/comecou.
RoomState g_previousRoomState;
bool g_haveRoomBaseline = false;

// Reconexao automatica: tentativas seguidas sem imagem (o alvo
// fica em g_lastWatchTarget).
int g_reconnectAttempts = 0;


void AddToast(
    const std::string& text)
{
    double now = ImGui::GetTime();

    // Tira os que ja sumiram.
    g_toasts.erase(
        std::remove_if(
            g_toasts.begin(),
            g_toasts.end(),
            [now](const Toast& toast)
            {
                return now - toast.shownAt > 6.0;
            }),
        g_toasts.end());

    g_toasts.push_back(Toast{ text, now });
}


// ------------------------------------------------------------
// Tela inicial: nome, criar/entrar
// ------------------------------------------------------------

char g_nameInput[64]{};

char g_homeCodeInput[32]{};

enum class HomeResult
{
    None,
    Room,
    Session,
    Error
};

std::atomic<bool> g_homeBusy{ false };

std::atomic<HomeResult> g_homeResult{ HomeResult::None };

std::thread g_homeThread;

// Protegidos por g_uiMutex
std::string g_homeStatus;
std::string g_homeWorkerError;
std::string g_homeSessionCode;


std::string WideToUtf8(
    const std::wstring& text)
{
    if (text.empty())
        return std::string();

    int size =
        WideCharToMultiByte(
            CP_UTF8, 0,
            text.c_str(), static_cast<int>(text.size()),
            nullptr, 0, nullptr, nullptr);

    std::string result(static_cast<size_t>(size), '\0');

    WideCharToMultiByte(
        CP_UTF8, 0,
        text.c_str(), static_cast<int>(text.size()),
        result.data(), size, nullptr, nullptr);

    return result;
}


std::wstring Utf8ToWide(
    const std::string& text)
{
    if (text.empty())
        return std::wstring();

    int size =
        MultiByteToWideChar(
            CP_UTF8, 0,
            text.c_str(), static_cast<int>(text.size()),
            nullptr, 0);

    std::wstring result(static_cast<size_t>(size), L'\0');

    MultiByteToWideChar(
        CP_UTF8, 0,
        text.c_str(), static_cast<int>(text.size()),
        result.data(), size);

    return result;
}


// Nome salvo; na primeira vez, o usuario do Windows.
void LoadName()
{
    std::wstring name =
        ReceiverSettings().GetString(L"Nome", L"");

    if (name.empty())
    {
        wchar_t user[128]{};

        DWORD size = 128;

        if (GetUserNameW(user, &size))
        {
            name = user;
        }
    }

    std::string utf8 =
        WideToUtf8(name);

    strncpy_s(
        g_nameInput,
        utf8.c_str(),
        _TRUNCATE);
}


std::string CurrentName()
{
    std::string name = g_nameInput;

    size_t first = name.find_first_not_of(" \t");

    size_t last = name.find_last_not_of(" \t");

    name =
        first == std::string::npos
        ? std::string()
        : name.substr(first, last - first + 1);

    return name.empty() ? std::string("Sem nome") : name;
}


void SaveName()
{
    ReceiverSettings().SetString(
        L"Nome",
        Utf8ToWide(CurrentName()));
}


void HomeWorker(
    bool create,
    std::string code,
    std::string name)
{
    std::string error;

    if (create)
    {
        {
            std::lock_guard<std::mutex> lock(g_uiMutex);

            g_homeStatus = "Criando a sala";
        }

        if (g_room.Create(name, error))
        {
            g_homeResult = HomeResult::Room;
        }
        else
        {
            std::cout
                << "Sala: nao deu pra criar: "
                << error
                << "\n";

            std::lock_guard<std::mutex> lock(g_uiMutex);

            g_homeWorkerError =
                "Não foi possível criar a sala. Verifique sua "
                "internet e tente de novo.";

            g_homeResult = HomeResult::Error;
        }

        g_homeBusy = false;

        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_uiMutex);

        g_homeStatus = "Procurando o código";
    }

    RoomJoinResult result =
        g_room.Join(code, name, error);

    switch (result)
    {
    case RoomJoinResult::Joined:
        g_homeResult = HomeResult::Room;
        break;

    case RoomJoinResult::NotFound:
    {
        // Nao e sala: pode ser o codigo de quem transmite sem
        // sala (ou de uma versao sem salas).
        std::lock_guard<std::mutex> lock(g_uiMutex);

        g_homeSessionCode = code;

        g_homeResult = HomeResult::Session;
        break;
    }

    case RoomJoinResult::Full:
    {
        std::lock_guard<std::mutex> lock(g_uiMutex);

        g_homeWorkerError =
            "Essa sala está cheia (9 pessoas).";

        g_homeResult = HomeResult::Error;
        break;
    }

    default:
    {
        std::cout
            << "Sala: nao deu pra entrar: "
            << error
            << "\n";

        std::lock_guard<std::mutex> lock(g_uiMutex);

        g_homeWorkerError =
            "Não foi possível falar com o servidor. Verifique "
            "sua internet e tente de novo.";

        g_homeResult = HomeResult::Error;
        break;
    }
    }

    g_homeBusy = false;
}


void StartHomeWorker(
    bool create,
    const std::string& code)
{
    if (g_homeBusy.exchange(true))
        return;

    if (g_homeThread.joinable())
    {
        g_homeThread.join();
    }

    g_homeError.clear();

    g_homeResult = HomeResult::None;

    SaveName();

    g_homeThread =
        std::thread(
            HomeWorker,
            create,
            code,
            CurrentName());
}


// ------------------------------------------------------------
// Entrar / sair da sala
// ------------------------------------------------------------

void EnterRoom()
{
    g_room.Start();

    g_haveRoomBaseline = false;
    g_previousRoomState = RoomState{};

    g_chatSeenTotal = 0;
    g_chatToastedTotal = 0;
    g_chatBaselineDone = false;
    g_videoChatOpen = false;
    g_chatInput[0] = '\0';

    sender::SetRoomMode(true);

    g_roomView = RoomView::Lobby;

    g_roomNotice.clear();
    g_roomError.clear();

    g_watchSessionCode.clear();
    g_watchName.clear();

    g_appMode = AppMode::Room;
}


// message: aviso pra tela inicial (vazio = nenhum).
void LeaveRoom(
    const std::string& message)
{
    ViewerDisconnect();

    g_watchSessionCode.clear();
    g_watchName.clear();

    sender::StopSharing();

    sender::SetRoomMode(false);

    g_voice.Leave();

    g_room.Leave();

    g_homeError = message;

    g_appMode = AppMode::Home;
}


// "Assistir" numa pessoa da lista.
void RoomWatch(
    const RoomPresenter& presenter)
{
    if (presenter.sessionCode == g_watchSessionCode &&
        g_uiState == UiState::Connected)
    {
        g_roomView = RoomView::Watch;

        return;
    }

    // Conexao anterior ainda sendo montada: o botao fica
    // desabilitado ate ela terminar (ver RoomScreenModel).
    if (g_connectBusy)
        return;

    ViewerDisconnect();

    g_watchSessionCode = presenter.sessionCode;
    g_watchName = presenter.name;
    g_watchAutoShow = true;

    g_lastWatchTarget = presenter.sessionCode;
    g_reconnectAttempts = 0;

    g_roomNotice.clear();
    g_roomError.clear();

    strncpy_s(
        g_uiInput,
        presenter.sessionCode.c_str(),
        _TRUNCATE);

    std::cout
        << "Sala: assistindo "
        << presenter.name
        << " (sessao "
        << presenter.sessionCode
        << ")\n";

    StartConnect();
}


// "Voltar para a sala" assistindo alguem: para de assistir
// (libera a internet e a vaga de quem transmite).
void RoomStopWatching()
{
    ViewerDisconnect();

    g_watchSessionCode.clear();
    g_watchName.clear();

    g_watchAutoShow = false;

    g_roomView = RoomView::Lobby;
}


void OpenRoomShare()
{
    std::string error;

    if (!sender::Open(
            g_hwnd,
            g_rendererDevice.Get(),
            g_rendererContext.Get(),
            error))
    {
        g_roomError = error;

        return;
    }

    g_roomView = RoomView::Share;
}


// ------------------------------------------------------------
// RoomSync - a cada frame, na sala
// ------------------------------------------------------------
//
// Publica/retira a transmissao desta pessoa, acompanha a
// conexao com quem esta sendo assistido e percebe quando essa
// pessoa parou de transmitir ou a sala acabou.
// ------------------------------------------------------------

void RoomSync(
    const RoomState& state)
{
    std::string code;
    std::string token;
    std::string label;

    if (sender::GetPublishInfo(code, token, label))
    {
        g_room.SetPresenting(code, token, label);
    }
    else
    {
        g_room.SetPresenting("", "", "");
    }

    // Canal de voz: publica a sessao de voz desta pessoa e
    // conecta/desconecta de quem entrou/saiu da voz.
    {
        std::string voiceCode;
        std::string voiceToken;

        if (g_voice.GetPublishInfo(voiceCode, voiceToken))
        {
            g_room.SetVoice(voiceCode, voiceToken);
        }
        else
        {
            g_room.SetVoice("", "");
        }
    }

    if (g_room.IsLost())
    {
        LeaveRoom(
            "A sala foi encerrada (ou o servidor reiniciou). "
            "Crie outra ou entre de novo pelo código.");

        return;
    }

    g_voice.Sync(state, g_room.GetMemberId());


    // --------------------------------------------------------
    // Avisos: quem entrou, saiu, comecou a transmitir
    // --------------------------------------------------------

    std::string myId = g_room.GetMemberId();

    const RoomPresenter* newPresenter = nullptr;

    if (!g_haveRoomBaseline)
    {
        if (!state.members.empty())
        {
            g_previousRoomState = state;

            g_haveRoomBaseline = true;

            // Entrou numa sala onde so uma pessoa transmite:
            // ja assiste.
            int others = 0;

            for (const RoomPresenter& presenter : state.presenters)
            {
                if (presenter.memberId != myId)
                {
                    others++;
                    newPresenter = &presenter;
                }
            }

            if (others != 1)
            {
                newPresenter = nullptr;
            }
        }
    }
    else
    {
        auto hasMember =
            [](const RoomState& room, const std::string& id)
            {
                for (const RoomMember& m : room.members)
                    if (m.id == id) return true;
                return false;
            };

        auto hasPresenter =
            [](const RoomState& room, const std::string& session)
            {
                for (const RoomPresenter& p : room.presenters)
                    if (p.sessionCode == session) return true;
                return false;
            };

        // Lista vazia = resposta ainda nao chegou: nada muda.
        if (!state.members.empty())
        {
            for (const RoomMember& member : state.members)
            {
                if (member.id != myId &&
                    !hasMember(g_previousRoomState, member.id))
                {
                    AddToast(member.name + " entrou na sala");
                }
            }

            for (const RoomMember& member : g_previousRoomState.members)
            {
                if (member.id != myId &&
                    !hasMember(state, member.id))
                {
                    AddToast(member.name + " saiu da sala");
                }
            }

            int otherPresenters = 0;

            for (const RoomPresenter& presenter : state.presenters)
            {
                if (presenter.memberId == myId)
                    continue;

                otherPresenters++;

                if (!hasPresenter(
                        g_previousRoomState,
                        presenter.sessionCode))
                {
                    AddToast(
                        presenter.name +
                        " começou a transmitir" +
                        (presenter.label.empty()
                            ? std::string()
                            : ": " + presenter.label));

                    newPresenter = &presenter;
                }
            }

            // Assistir automatico so quando e a unica pessoa
            // transmitindo (com mais de uma, a pessoa escolhe).
            if (otherPresenters != 1)
            {
                newPresenter = nullptr;
            }

            g_previousRoomState = state;
        }
    }

    if (newPresenter &&
        g_roomView == RoomView::Lobby &&
        g_watchSessionCode.empty() &&
        !g_connectBusy)
    {
        RoomPresenter copy = *newPresenter;

        RoomWatch(copy);
    }

    if (g_watchSessionCode.empty())
        return;

    // A conexao falhou (o motivo fica em g_uiError).
    if (!g_connectBusy &&
        !g_watching &&
        g_uiState == UiState::Connect)
    {
        {
            std::lock_guard<std::mutex> lock(g_uiMutex);

            g_roomError =
                g_uiError.empty()
                ? "Não foi possível conectar a " + g_watchName + "."
                : g_uiError;
        }

        g_watchSessionCode.clear();
        g_watchName.clear();

        g_watchAutoShow = false;

        g_roomView = RoomView::Lobby;

        return;
    }

    // Conectou: mostra o video.
    if (g_watchAutoShow &&
        g_uiState == UiState::Connected)
    {
        g_watchAutoShow = false;

        if (g_roomView == RoomView::Lobby)
        {
            g_roomView = RoomView::Watch;
        }
    }

    // A pessoa parou de transmitir (ou saiu da sala).
    bool stillPresenting = false;

    for (const RoomPresenter& presenter : state.presenters)
    {
        if (presenter.sessionCode == g_watchSessionCode)
        {
            stillPresenting = true;

            break;
        }
    }

    if (!stillPresenting &&
        !g_room.IsOffline())
    {
        g_roomNotice =
            g_watchName + " parou de transmitir.";

        RoomStopWatching();
    }
}


// ------------------------------------------------------------
// CheckReconnect - a cada frame
// ------------------------------------------------------------
//
// Assistindo e nada chega de quem transmite (nem video, nem o
// sinal de vida) ha 5 s: a conexao caiu (internet oscilou, o
// roteador esqueceu o caminho...). Reconecta sozinho, ate 3
// vezes seguidas. So vale com quem manda sinal de vida - com
// versoes antigas, tela parada pareceria conexao caida.
// ------------------------------------------------------------

void CheckReconnect()
{
    if (g_uiState != UiState::Connected ||
        g_connectBusy ||
        !g_senderSendsAlive)
    {
        return;
    }

    if (g_streamReceived &&
        g_reconnectAttempts > 0 &&
        GetTickCount64() - g_lastVideoPacketMs.load() < 2000)
    {
        // Voltou a chegar: zera as tentativas.
        g_reconnectAttempts = 0;
    }

    ULONGLONG last = g_lastVideoPacketMs.load();

    if (last == 0 ||
        GetTickCount64() - last < 5000)
    {
        return;
    }

    bool inRoom =
        g_appMode == AppMode::Room &&
        !g_watchSessionCode.empty();

    std::string target =
        inRoom ? g_watchSessionCode : g_lastWatchTarget;

    if (target.empty())
        return;

    if (g_reconnectAttempts >= 3)
    {
        std::cout
            << "Reconexao: desistindo depois de 3 tentativas.\n";

        if (inRoom)
        {
            std::string name = g_watchName;

            RoomStopWatching();

            g_roomError =
                "A conexão com " + name + " caiu e não voltou. "
                "Tente assistir de novo.";
        }

        return;
    }

    g_reconnectAttempts++;

    std::cout
        << "Reconexao: nada chega ha 5 s - tentativa "
        << g_reconnectAttempts
        << ".\n";

    AddToast(
        inRoom
        ? "A conexão com " + g_watchName + " caiu. Reconectando..."
        : std::string("A conexão caiu. Reconectando..."));

    ViewerDisconnect();

    strncpy_s(g_uiInput, target.c_str(), _TRUNCATE);

    if (inRoom)
    {
        // Continua no video: a tela de espera aparece ate a
        // imagem voltar.
        g_watchAutoShow = false;
    }

    StartConnect();
}


// ------------------------------------------------------------
// Chat: mensagens novas
// ------------------------------------------------------------
//
// Monta as linhas pra tela e, se o chat nao estiver na tela,
// transforma as mensagens novas dos outros em avisos.
// ------------------------------------------------------------

std::vector<ChatLine> CollectChat(
    bool chatVisible,
    int& outUnread)
{
    uint64_t total = 0;

    std::vector<RoomChatMessage> messages =
        g_room.GetChat(total);

    if (!g_chatBaselineDone &&
        g_room.HeartbeatCount() > 0)
    {
        g_chatBaselineDone = true;

        g_chatToastedTotal = total;
        g_chatSeenTotal = total;
    }

    // Antes da primeira resposta: ainda nao ha o que avisar.
    if (!g_chatBaselineDone)
    {
        chatVisible = true;
    }

    std::string myId = g_room.GetMemberId();

    std::vector<ChatLine> lines;

    lines.reserve(messages.size());

    for (const RoomChatMessage& message : messages)
    {
        lines.push_back(
            ChatLine{
                message.name,
                message.text,
                message.memberId == myId });
    }

    // Avisos das mensagens novas (so as dos outros).
    if (!chatVisible &&
        total > g_chatToastedTotal)
    {
        uint64_t fresh = total - g_chatToastedTotal;

        size_t first =
            messages.size() > fresh
            ? messages.size() - static_cast<size_t>(fresh)
            : 0;

        for (size_t i = first; i < messages.size(); ++i)
        {
            if (messages[i].memberId != myId)
            {
                AddToast(
                    messages[i].name + ": " + messages[i].text);
            }
        }
    }

    g_chatToastedTotal = total;

    if (chatVisible)
    {
        g_chatSeenTotal = total;
    }

    // As suas nao contam como "nao lidas".
    int unread = 0;

    if (total > g_chatSeenTotal)
    {
        uint64_t fresh = total - g_chatSeenTotal;

        for (size_t i = messages.size();
             i > 0 && fresh > 0;
             --i, --fresh)
        {
            if (messages[i - 1].memberId != myId)
            {
                unread++;
            }
        }
    }

    outUnread = unread;

    return lines;
}


// ------------------------------------------------------------
// Titulo da janela (sempre pela thread da janela)
// ------------------------------------------------------------

void UpdateWindowTitle()
{
    if (!g_hwnd)
        return;

    std::wstring title =
        L"Stream Project " STREAM_APP_VERSION_W;

    if (g_appMode == AppMode::Room)
    {
        title += L" - sala " + Utf8ToWide(g_room.GetCode());
    }

    if (g_streamReceived &&
        g_frameWidth > 0)
    {
        wchar_t details[96]{};

        swprintf_s(
            details,
            L" - assistindo %ux%u - %d FPS",
            g_frameWidth,
            g_frameHeight,
            g_fps.load());

        title += details;
    }

    static std::wstring lastTitle;

    if (title != lastTitle)
    {
        lastTitle = title;

        SetWindowTextW(g_hwnd, title.c_str());
    }
}


// ============================================================
// DrawVideoOverlay
// ============================================================
//
// Painel sobre o video (volume, preencher, tela cheia...):
// aparece quando o mouse mexe e some sozinho depois de uns
// segundos parado. Numa sala, mostra quem transmite e
// "Voltar para a sala".
// ============================================================

void DrawVideoOverlay(
    ReceiverScreenActions& actions,
    bool inRoom,
    const std::string& presenterName,
    bool& outBackToRoom,
    const std::vector<std::string>& otherPresenters =
        std::vector<std::string>(),
    int unreadChat = 0,
    int* outSwitchTo = nullptr,
    bool* outToggleChat = nullptr)
{
    // ----------------------------------------------------
    // Painel sobre o video: aparece quando o mouse mexe e
    // some sozinho depois de uns segundos parado.
    // ----------------------------------------------------

    static double lastActivity = -100.0;

    static bool overlayHovered = false;

    ImGuiIO& io = ImGui::GetIO();

    double now = ImGui::GetTime();

    bool keyboardFree = !io.WantTextInput;

    if (io.MouseDelta.x != 0.0f ||
        io.MouseDelta.y != 0.0f ||
        overlayHovered)
    {
        lastActivity = now;
    }

    // Atalhos de teclado (tambem mostram o painel).
    if (keyboardFree)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_M, false))
        {
            g_muted = !g_muted;

            lastActivity = now;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        {
            g_volume += 0.05f;

            g_muted = false;

            lastActivity = now;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            g_volume -= 0.05f;

            lastActivity = now;
        }

        if (g_volume < 0.0f) g_volume = 0.0f;
        if (g_volume > 1.0f) g_volume = 1.0f;
    }

    // Visivel por 2,5s depois da ultima atividade, sumindo
    // ao longo de mais 0,5s.
    double idle = now - lastActivity;

    float alpha =
        idle < 2.5 ? 1.0f :
        idle < 3.0 ? static_cast<float>((3.0 - idle) / 0.5) :
        0.0f;

    StreamOverlayModel overlay;

    overlay.volume = &g_volume;
    overlay.muted = &g_muted;
    overlay.fillWindow = &g_fillWindow;
    overlay.fullscreen = g_fullscreen;
    overlay.collapsed = &g_overlayCollapsed;
    overlay.fps = g_fps.load();
    overlay.width = g_frameWidth;
    overlay.height = g_frameHeight;
    overlay.lostNetwork = g_statLostNetwork.load();
    overlay.recovered = g_statRecovered.load();
    overlay.late = g_statLate.load();
    overlay.alpha = alpha;

    overlay.inRoom = inRoom;
    overlay.presenterName = presenterName;
    overlay.otherPresenters = otherPresenters;
    overlay.unreadChat = unreadChat;
    overlay.voiceJoined = inRoom && g_voice.IsJoined();
    overlay.voiceMuted = g_voiceMuted;

    StreamOverlayActions overlayActions;

    overlayHovered =
        DrawStreamOverlay(
            overlay,
            overlayActions);

    if (overlayActions.toggleFullscreen)
    {
        actions.toggleFullscreen = true;
    }

    if (overlayActions.backToRoom)
    {
        outBackToRoom = true;
    }

    if (overlayActions.toggleVoiceMute)
    {
        g_voiceMuted = !g_voiceMuted;

        g_voice.SetMuted(g_voiceMuted);
    }

    if (outSwitchTo && overlayActions.switchTo >= 0)
    {
        *outSwitchTo = overlayActions.switchTo;
    }

    if (outToggleChat && overlayActions.toggleChat)
    {
        *outToggleChat = true;
    }
}


// ============================================================
//
// Chamada uma vez por frame, de dentro do RenderFrame, com o
// render target ja ativo. Durante o stream nao desenha nada -
// o video ocupa a janela inteira.
// ============================================================

void DrawReceiverUi()
{
    UiBeginFrame();

    UiState state =
        g_uiState.load();

    ReceiverScreenActions actions;

    AppMode mode =
        g_appMode.load();

    HomeScreenActions homeActions;

    bool shareGoHome = false;

    RoomScreenActions roomActions;

    bool backToRoom = false;

    // Sala: estado do servidor e o que a tela mostrou (pra
    // traduzir o clique em "Assistir" de volta pra pessoa).
    RoomState roomState;

    std::vector<RoomPresenter> shownPresenters;

    // Troca rapida pelo painel do video.
    std::vector<RoomPresenter> switchPresenters;

    int switchTo = -1;

    bool toggleChat = false;

    bool sendChat = false;

    // O chat ja foi processado neste frame (senao, as mensagens
    // novas viram avisos mesmo assim - ex.: transmitindo).
    bool chatHandled = false;

    if (mode == AppMode::Room)
    {
        roomState = g_room.GetState();

        RoomSync(roomState);

        // RoomSync pode ter saido da sala.
        mode = g_appMode.load();
    }

    // Criar/entrar terminou (thread de trabalho)?
    if (!g_homeBusy &&
        g_homeResult != HomeResult::None)
    {
        HomeResult result =
            g_homeResult.exchange(HomeResult::None);

        if (g_homeThread.joinable())
        {
            g_homeThread.join();
        }

        if (result == HomeResult::Room)
        {
            EnterRoom();

            roomState = g_room.GetState();
        }
        else if (result == HomeResult::Session)
        {
            // Codigo de quem transmite sem sala: o fluxo de
            // sempre do modo Assistir.
            std::string code;

            {
                std::lock_guard<std::mutex> lock(g_uiMutex);

                code = g_homeSessionCode;
            }

            strncpy_s(g_uiInput, code.c_str(), _TRUNCATE);

            g_appMode = AppMode::Watch;

            StartConnect();
        }
        else if (result == HomeResult::Error)
        {
            std::lock_guard<std::mutex> lock(g_uiMutex);

            g_homeError = g_homeWorkerError;
        }

        mode = g_appMode.load();
        state = g_uiState.load();
    }


    // ffmpeg com problema vale pros dois modos (os dois usam):
    // a explicacao aparece antes de tudo.
    if (state == UiState::FfmpegProblem)
    {
        std::string problem;

        {
            std::lock_guard<std::mutex> lock(
                g_uiMutex
            );

            problem =
                g_ffmpegProblem;
        }

        DrawFfmpegProblemScreen(
            problem,
            actions
        );
    }
    else if (mode == AppMode::Home)
    {
        HomeScreenModel home;

        home.name = g_nameInput;
        home.nameSize = sizeof(g_nameInput);
        home.code = g_homeCodeInput;
        home.codeSize = sizeof(g_homeCodeInput);
        home.busy = g_homeBusy;
        home.error = g_homeError;

        {
            std::lock_guard<std::mutex> lock(g_uiMutex);

            home.status = g_homeStatus;
        }

        DrawHomeScreen(
            home,
            homeActions
        );
    }
    else if (mode == AppMode::Share)
    {
        sender::Draw(
            shareGoHome
        );
    }
    else if (mode == AppMode::Room)
    {
        if (g_roomView == RoomView::Share)
        {
            sender::Draw(
                shareGoHome
            );
        }
        else if (g_roomView == RoomView::Watch)
        {
            // Outras pessoas transmitindo: troca rapida.
            std::string myId = g_room.GetMemberId();

            std::vector<std::string> others;

            for (const RoomPresenter& presenter :
                 roomState.presenters)
            {
                if (presenter.memberId == myId ||
                    presenter.sessionCode == g_watchSessionCode)
                {
                    continue;
                }

                others.push_back(
                    presenter.label.empty()
                    ? presenter.name
                    : presenter.name + "  ·  " + presenter.label);

                switchPresenters.push_back(presenter);
            }

            int unread = 0;

            std::vector<ChatLine> chat =
                CollectChat(g_videoChatOpen, unread);

            chatHandled = true;

            if (g_voice.IsJoined())
            {
                std::vector<std::string> speaking;

                for (const RoomMember& member : roomState.members)
                {
                    if (member.id != myId &&
                        g_voice.IsSpeaking(member.id))
                    {
                        speaking.push_back(member.name);
                    }
                }

                DrawVoiceSpeakers(speaking, g_voiceMuted);
            }

            if (g_streamReceived)
            {
                DrawVideoOverlay(
                    actions,
                    true,
                    g_watchName,
                    backToRoom,
                    others,
                    unread,
                    &switchTo,
                    &toggleChat);
            }
            else
            {
                std::string versionWarning;
                std::string status;

                {
                    std::lock_guard<std::mutex> lock(g_uiMutex);

                    versionWarning = g_versionWarning;

                    status =
                        g_uiState == UiState::Connected
                        ? std::string("Aguardando a imagem chegar")
                        : g_uiStatus;
                }

                DrawRoomWaitingScreen(
                    g_watchName,
                    status,
                    versionWarning,
                    backToRoom);
            }

            // Enter abre o chat (fora de campos de texto).
            if (!g_videoChatOpen &&
                !ImGui::GetIO().WantTextInput &&
                ImGui::IsKeyPressed(ImGuiKey_Enter, false))
            {
                toggleChat = true;
            }

            if (g_videoChatOpen)
            {
                bool close = false;

                if (DrawVideoChat(
                        chat,
                        g_chatInput,
                        sizeof(g_chatInput),
                        g_chatFocus,
                        close))
                {
                    sendChat = true;
                }

                g_chatFocus = false;

                if (close)
                {
                    toggleChat = true;
                }
            }
        }
        else
        {
            RoomScreenModel room;

            room.code = g_room.GetCode();

            std::string myId =
                g_room.GetMemberId();

            for (const RoomPresenter& presenter :
                 roomState.presenters)
            {
                RoomPresenterEntry entry;

                entry.name = presenter.name;
                entry.label = presenter.label;
                entry.isMe = presenter.memberId == myId;
                entry.watching =
                    !g_watchSessionCode.empty() &&
                    presenter.sessionCode == g_watchSessionCode;

                room.presenters.push_back(entry);

                shownPresenters.push_back(presenter);
            }

            for (const RoomMember& member : roomState.members)
            {
                room.members.push_back(
                    member.id == myId
                        ? member.name + " (você)"
                        : member.name);
            }

            room.sharing = sender::IsLive();

            if (room.sharing)
            {
                std::string code;
                std::string token;

                sender::GetPublishInfo(
                    code,
                    token,
                    room.sharingLabel);

                room.sharingViewers =
                    sender::ViewerCount();
            }

            room.connecting =
                g_connectBusy ||
                (!g_watchSessionCode.empty() &&
                 g_uiState == UiState::Connecting);

            room.connectingTo = g_watchName;
            room.notice = g_roomNotice;
            room.error = g_roomError;
            room.offline = g_room.IsOffline();

            for (const RoomMember& member : roomState.members)
            {
                bool live = false;

                for (const RoomPresenter& presenter :
                     roomState.presenters)
                {
                    if (presenter.memberId == member.id)
                    {
                        live = true;
                    }
                }

                room.memberLive.push_back(live);

                bool isMe = member.id == myId;

                MemberVoice voice = MemberVoice::Out;

                if (isMe)
                {
                    if (g_voice.IsJoined())
                    {
                        voice =
                            g_voice.IsTransmitting() &&
                            g_voice.MicLevel() > 0.02f
                            ? MemberVoice::Speaking
                            : MemberVoice::In;
                    }
                }
                else if (!member.voiceSession.empty())
                {
                    if (!g_voice.IsJoined())
                    {
                        voice = MemberVoice::In;
                    }
                    else if (!g_voice.IsConnected(member.id))
                    {
                        voice = MemberVoice::Connecting;
                    }
                    else
                    {
                        voice =
                            g_voice.IsSpeaking(member.id)
                            ? MemberVoice::Speaking
                            : MemberVoice::In;
                    }
                }

                room.memberIsMe.push_back(isMe);
                room.memberVoice.push_back(voice);
                room.memberVolume.push_back(
                    g_voice.PeerVolume(member.id));
            }

            room.voiceJoined = g_voice.IsJoined();
            room.voiceJoining = g_voice.IsJoining();
            room.voiceMuted = g_voiceMuted;
            room.voiceTransmitting = g_voice.IsTransmitting();
            room.voiceEchoCancel =
                !room.voiceJoined || g_voice.EchoCancellation();
            room.voiceMicLevel = g_voice.MicLevel();
            room.voicePushToTalk = g_voicePushToTalk;

            for (size_t i = 0; i < g_microphones.size(); ++i)
            {
                room.microphones.push_back(g_microphones[i].name);

                if (g_microphones[i].id == g_voiceMicrophone)
                {
                    room.microphone = static_cast<int>(i) + 1;
                }
            }

            for (size_t i = 0; i < g_speakers.size(); ++i)
            {
                room.speakers.push_back(g_speakers[i].name);

                if (g_speakers[i].id == g_voiceSpeaker)
                {
                    room.speaker = static_cast<int>(i) + 1;
                }
            }
            room.voiceError = g_voice.ErrorText();

            int unread = 0;

            room.chat = CollectChat(true, unread);
            room.chatInput = g_chatInput;
            room.chatInputSize = sizeof(g_chatInput);

            chatHandled = true;

            DrawRoomScreen(
                room,
                roomActions
            );

            if (roomActions.sendChat)
            {
                sendChat = true;
            }
        }
    }
    else if (state == UiState::Connect ||
             state == UiState::Connecting)
    {
        std::string error;
        std::string status;

        {
            std::lock_guard<std::mutex> lock(
                g_uiMutex
            );

            error =
                g_uiError;

            status =
                g_uiStatus;
        }

        DrawConnectScreen(
            g_uiInput,
            sizeof(g_uiInput),
            state == UiState::Connecting,
            status,
            error,
            actions
        );
    }
    else if (g_streamReceived)
    {
        bool unused = false;

        DrawVideoOverlay(
            actions,
            false,
            std::string(),
            unused);
    }
    else
    {
        std::string reply;

        {
            std::lock_guard<std::mutex> lock(
                g_replyMutex
            );

            reply =
                g_replyText;
        }

        std::string versionWarning;

        {
            std::lock_guard<std::mutex> lock(
                g_uiMutex
            );

            versionWarning =
                g_versionWarning;
        }

        DrawWaitingScreen(
            reply,
            versionWarning
        );
    }


    // Sala, sem o chat na tela (ex.: nas telas de transmitir):
    // mensagens novas viram avisos.
    if (g_appMode == AppMode::Room &&
        !chatHandled)
    {
        int unread = 0;

        CollectChat(false, unread);
    }

    DrawToasts(g_toasts);


    UiEndFrame();


    // Volume escolhido na tela -> reproducao de audio.
    g_audioPlayback.SetVolume(g_volume);

    g_audioPlayback.SetMuted(g_muted);

    SaveReceiverSettingsIfChanged();


    // Assistindo: tela acesa e sem economia de energia (o
    // Windows nao sabe que tem video rolando, porque ninguem
    // mexe no mouse).
    PowerSetActive(
        g_uiState == UiState::Connected ||
        sender::IsLive() ||
        g_voice.IsJoined());


    // --------------------------------------------------------
    // Navegacao entre os modos
    // --------------------------------------------------------

    // Tela inicial
    if (homeActions.createRoom)
    {
        StartHomeWorker(true, std::string());
    }

    if (homeActions.enter)
    {
        std::string code = g_homeCodeInput;

        size_t first = code.find_first_not_of(" \t");
        size_t last = code.find_last_not_of(" \t");

        code =
            first == std::string::npos
            ? std::string()
            : code.substr(first, last - first + 1);

        if (code.empty())
        {
            g_homeError =
                "Digite o código da sala (6 letras e números).";
        }
        else if (LooksLikeSessionCode(code))
        {
            for (char& c : code)
            {
                c = static_cast<char>(
                    toupper(static_cast<unsigned char>(c)));
            }

            StartHomeWorker(false, code);
        }
        else
        {
            // Convite manual ou IP: o fluxo direto do modo
            // Assistir.
            SaveName();

            strncpy_s(g_uiInput, code.c_str(), _TRUNCATE);

            g_homeError.clear();

            g_appMode = AppMode::Watch;

            StartConnect();
        }
    }

    if (homeActions.shareWithoutRoom)
    {
        OpenShareMode();
    }

    // Sala
    if (roomActions.watch >= 0 &&
        roomActions.watch <
            static_cast<int>(shownPresenters.size()))
    {
        RoomWatch(
            shownPresenters[roomActions.watch]);
    }

    if (roomActions.openShare)
    {
        OpenRoomShare();
    }

    if (roomActions.stopShare)
    {
        sender::StopSharing();
    }

    if (roomActions.voiceJoin)
    {
        g_voice.SetMuted(g_voiceMuted);

        g_voice.SetPushToTalkKey(
            VoicePushToTalkKey(g_voicePushToTalk));

        g_voice.SetMicrophone(g_voiceMicrophone);

        g_voice.SetSpeaker(g_voiceSpeaker);

        g_microphones = VoiceCapture::ListMicrophones();

        g_speakers = VoiceCapture::ListSpeakers();

        g_voice.Join();
    }

    if (roomActions.voiceRefreshDevices)
    {
        g_microphones = VoiceCapture::ListMicrophones();

        g_speakers = VoiceCapture::ListSpeakers();
    }

    if (roomActions.voiceSpeaker >= 0)
    {
        int choice = roomActions.voiceSpeaker;

        g_voiceSpeaker =
            choice > 0 &&
            choice <= static_cast<int>(g_speakers.size())
            ? g_speakers[choice - 1].id
            : std::wstring();

        ReceiverSettings().SetString(
            L"VozSaida", g_voiceSpeaker);

        g_voice.SetSpeaker(g_voiceSpeaker);
    }

    if (roomActions.voiceMicrophone >= 0)
    {
        int choice = roomActions.voiceMicrophone;

        g_voiceMicrophone =
            choice > 0 &&
            choice <= static_cast<int>(g_microphones.size())
            ? g_microphones[choice - 1].id
            : std::wstring();

        ReceiverSettings().SetString(
            L"VozMicrofone", g_voiceMicrophone);

        g_voice.SetMicrophone(g_voiceMicrophone);
    }

    if (roomActions.voiceLeave)
    {
        g_voice.Leave();
    }

    if (roomActions.voiceToggleMute)
    {
        g_voiceMuted = !g_voiceMuted;

        g_voice.SetMuted(g_voiceMuted);
    }

    if (roomActions.voicePushToTalk >= 0)
    {
        g_voicePushToTalk = roomActions.voicePushToTalk;

        g_voice.SetPushToTalkKey(
            VoicePushToTalkKey(g_voicePushToTalk));
    }

    if (roomActions.volumeMember >= 0 &&
        roomActions.volumeMember <
            static_cast<int>(roomState.members.size()))
    {
        g_voice.SetPeerVolume(
            roomState.members[roomActions.volumeMember].id,
            roomActions.volumeValue);
    }

    if (backToRoom)
    {
        RoomStopWatching();

        g_videoChatOpen = false;
    }

    if (switchTo >= 0 &&
        switchTo < static_cast<int>(switchPresenters.size()))
    {
        RoomPresenter target = switchPresenters[switchTo];

        RoomWatch(target);

        // Fica no video: a tela de espera aparece ate a imagem
        // da outra pessoa chegar.
        g_roomView = RoomView::Watch;
    }

    if (toggleChat)
    {
        g_videoChatOpen = !g_videoChatOpen;

        g_chatFocus = g_videoChatOpen;
    }

    if (sendChat &&
        g_chatInput[0] != '\0')
    {
        g_room.SendChat(g_chatInput);

        g_chatInput[0] = '\0';
    }

    CheckReconnect();

    if (roomActions.leave)
    {
        LeaveRoom(std::string());
    }

    // "Voltar" nas telas do modo Transmitir / Assistir
    if (shareGoHome)
    {
        if (g_appMode == AppMode::Room)
        {
            g_roomView = RoomView::Lobby;
        }
        else
        {
            g_appMode = AppMode::Home;
        }
    }

    if (actions.goHome)
    {
        g_appMode = AppMode::Home;
    }

    UpdateWindowTitle();


    // --------------------------------------------------------
    // Estatisticas no log, a cada 5s durante o stream (pra
    // diagnosticar travadas: rede x este computador).
    // --------------------------------------------------------

    static ULONGLONG lastStatsLog = 0;

    ULONGLONG nowMs = GetTickCount64();

    if (g_streamReceived &&
        nowMs - lastStatsLog >= 5000)
    {
        lastStatsLog = nowMs;

        std::cout
            << "[stats] "
            << g_frameWidth
            << "x"
            << g_frameHeight
            << " "
            << g_fps.load()
            << " FPS | perdidos na rede: "
            << g_statLostNetwork.load()
            << " | descartados esperando keyframe: "
            << g_statSkippedForKeyframe.load()
            << " | atrasados: "
            << g_statLate.load()
            << " | salvos por reenvio: "
            << g_statRecovered.load()
            << " | ida e volta: "
            << g_statRoundTripMs.load()
            << " ms\n";
    }


    // --------------------------------------------------------
    // O que o usuario pediu nesta tela
    // --------------------------------------------------------

    if (actions.connect)
    {
        StartConnect();
    }

    if (actions.toggleFullscreen)
    {
        // Nao chamar ToggleFullscreen() aqui: esta funcao roda
        // dentro do RenderFrame, com g_rendererMutex travado. A
        // troca muda o tamanho da janela, o Windows manda WM_SIZE
        // na hora (mesma thread) e o ResizeRenderer tentaria
        // travar o mesmo mutex de novo -> excecao e crash. A
        // mensagem e tratada depois que o frame termina.
        PostMessageW(
            g_hwnd,
            WM_APP_TOGGLE_FULLSCREEN,
            0,
            0
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
            g_uiState =
                UiState::Connect;
        }
        else
        {
            std::lock_guard<std::mutex> lock(
                g_uiMutex
            );

            g_ffmpegProblem =
                again;
        }
    }
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


        if (PixelsFit(
                g_framePixels,
                g_shownWidth,
                g_shownHeight))
        {
            width =
                g_shownWidth;

            height =
                g_shownHeight;


            if (EnsureVideoTexture(
                    width,
                    height))
            {
                // A janela agora e redesenhada a cada vsync
                // (por causa da interface). Copiar o frame pra
                // textura so quando ele MUDOU evita mover
                // megabytes por segundo a toa.
                static uint64_t uploadedFrameNumber = ~0ull;
                static uint32_t uploadedWidth = 0;
                static uint32_t uploadedHeight = 0;

                bool alreadyUploaded =
                    g_frameNumber == uploadedFrameNumber &&
                    width == uploadedWidth &&
                    height == uploadedHeight;

                if (alreadyUploaded)
                {
                    haveFrame =
                        true;
                }


                D3D11_MAPPED_SUBRESOURCE mapped{};


                HRESULT hr =
                    alreadyUploaded
                    ? E_ABORT
                    : g_rendererContext->Map(
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

                    uploadedFrameNumber =
                        g_frameNumber;

                    uploadedWidth =
                        width;

                    uploadedHeight =
                        height;
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


        // "Preencher": a mesma conta invertida - a imagem cresce
        // ate cobrir a janela inteira e o que passa da borda e
        // cortado (o shader desenha alem da tela e a placa de
        // video simplesmente descarta).
        bool imageIsWider =
            imageAspect > windowAspect;

        if (g_fillWindow)
        {
            imageIsWider = !imageIsWider;
        }


        if (imageIsWider)
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


    // Interface (telas de conexao, avisos) por cima do video.
    DrawReceiverUi();


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
    // A interface ve as mensagens primeiro (mouse, teclado,
    // texto digitado).
    if (UiHandleWindowMessage(
            hwnd,
            uMsg,
            wParam,
            lParam))
    {
        return 1;
    }


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


    case WM_APP_TOGGLE_FULLSCREEN:
    {
        ToggleFullscreen();

        return 0;
    }


    case WM_LBUTTONDBLCLK:
    {
        // Duplo clique no video: tela cheia (como nos players).
        if (g_streamReceived &&
            !UiWantsMouse())
        {
            ToggleFullscreen();
        }

        return 0;
    }

    case WM_KEYDOWN:
    {
        if (wParam == VK_F11)
        {
            ToggleFullscreen();

            return 0;
        }

        if (wParam == VK_ESCAPE &&
            g_fullscreen)
        {
            ToggleFullscreen();

            return 0;
        }


        // ====================================================
        // Tecla S: salva o frame exibido no momento em BMP,
        // sem passar por nenhuma ferramenta de print/foto -
        // pra diagnosticar problemas visuais direto do pixel
        // real que o receiver está desenhando.
        // ====================================================

        // (so durante o stream e fora de campos de texto - pra
        // nao disparar ao digitar um codigo com a letra S)
        if (wParam == 'S' &&
            g_streamReceived &&
            !UiWantsKeyboard())
        {
            SaveCurrentFrameAsBmp();
        }


        return 0;
    }


    case WM_DESTROY:
    {
        SaveReceiverWindowPlacement();

        PowerSetActive(false);


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

void ShowFatalError(
    const wchar_t* message)
{
    MessageBoxW(
        nullptr,
        message,
        L"Stream Project",
        MB_OK | MB_ICONERROR
    );
}


int AppMain(
    const wchar_t* commandLine)
{
    // ========================================================
    // LINHA DE COMANDO
    //
    // --esperar <pid>  esta copia foi aberta pelo botao "Abrir
    //                  como administrador": espera a copia
    //                  antiga fechar (senao as portas UDP e o
    //                  log ainda estao com ela).
    // --transmitir     abrir direto no modo Transmitir.
    // ========================================================

    bool startSharing = false;

    if (commandLine)
    {
        const wchar_t* waitFlag =
            wcsstr(commandLine, L"--esperar ");

        if (waitFlag)
        {
            DWORD oldPid =
                wcstoul(waitFlag + 10, nullptr, 10);

            HANDLE old =
                oldPid != 0
                ? OpenProcess(SYNCHRONIZE, FALSE, oldPid)
                : nullptr;

            if (old)
            {
                WaitForSingleObject(old, 10000);

                CloseHandle(old);
            }
        }

        startSharing =
            wcsstr(commandLine, L"--transmitir") != nullptr;
    }


    // ========================================================
    // COM / WinRT (multi-threaded)
    //
    // A captura de tela do modo Transmitir usa APIs WinRT, que
    // precisam disso antes de qualquer uso.
    // ========================================================

    CoInitializeEx(
        nullptr,
        COINIT_MULTITHREADED
    );


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


    // ========================================================
    // LOG EM ARQUIVO
    //
    // O programa nao tem mais console: tudo que antes aparecia
    // nele (std::cout / std::cerr) vai pra stream-project.log,
    // na pasta do programa. E o arquivo a enviar quando algo
    // der errado.
    //
    // Cada receiver aberto ao mesmo tempo escreve no SEU
    // arquivo (stream-project.log, stream-project-2.log...).
    // Dois programas nao podem gravar no mesmo arquivo: sem
    // isso, o segundo receiver falhava ao abrir o log e
    // encerrava logo na abertura.
    // ========================================================

    {
        // Descobre qual "numero" este receiver e, usando um
        // objeto nomeado do Windows que existe enquanto o
        // programa estiver aberto.
        int instance = 1;

        for (; instance <= 8; ++instance)
        {
            std::wstring name =
                L"StreamProjectInstance" +
                std::to_wstring(instance);

            HANDLE marker =
                CreateMutexW(
                    nullptr,
                    FALSE,
                    name.c_str()
                );

            if (marker &&
                GetLastError() != ERROR_ALREADY_EXISTS)
            {
                // Fica aberto de proposito ate o programa sair.
                break;
            }

            if (marker)
            {
                CloseHandle(marker);
            }
        }

        std::string logName =
            instance == 1
            ? "stream-project.log"
            : "stream-project-" +
              std::to_string(instance) +
              ".log";

        FILE* logFile = nullptr;

        if (freopen_s(
                &logFile,
                logName.c_str(),
                "w",
                stdout) != 0)
        {
            // Nao deu pra criar o arquivo (pasta sem permissao
            // de escrita, por exemplo): descarta o log, mas o
            // programa segue funcionando.
            freopen_s(
                &logFile,
                "NUL",
                "w",
                stdout
            );
        }

        // Sem buffer: se o programa fechar de repente, o log
        // ja esta no disco.
        setvbuf(
            stdout,
            nullptr,
            _IONBF,
            0
        );

        std::cerr.rdbuf(
            std::cout.rdbuf()
        );


        // Se o programa fechar por erro, deixa o motivo no log
        // e um .dmp pra diagnostico (ver crash_report.h).
        std::wstring dumpName =
            instance == 1
            ? L"stream-project-crash.dmp"
            : L"stream-project-" +
              std::to_wstring(instance) +
              L"-crash.dmp";

        InstallCrashReporter(
            dumpName.c_str()
        );
    }

    std::cout
        << "========== STREAM PROJECT "
        << STREAM_APP_VERSION
        << " ==========\n";

    PowerDisableThrottling();

    LoadReceiverSettings();

    LoadName();


    // Comecar com o log do decoder vazio a cada execucao.
    std::ofstream(
        "ffmpeg-decoder.log",
        std::ios::trunc
    );


    // Par de chaves desta execucao (criptografia do canal).
    if (!CryptoGenerateKeyPair(g_myKeys))
    {
        ShowFatalError(
            L"Falha ao gerar as chaves de criptografia."
        );

        return 1;
    }


    WSADATA wsaData{};

    if (WSAStartup(
            MAKEWORD(2, 2),
            &wsaData) != 0)
    {
        ShowFatalError(
            L"Falha ao iniciar a rede do Windows (Winsock)."
        );

        return 1;
    }


    // ========================================================
    // Janela
    // ========================================================

    const wchar_t CLASS_NAME[] =
        L"StreamProjectWindow";

    HINSTANCE hInstance =
        GetModuleHandleW(nullptr);

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

    // Sem isso o Windows nao manda WM_LBUTTONDBLCLK.
    wc.style =
        CS_DBLCLKS;

    wc.hbrBackground =
        static_cast<HBRUSH>(
            GetStockObject(BLACK_BRUSH));

    if (!RegisterClassW(&wc))
    {
        ShowFatalError(
            L"Falha ao registrar a janela."
        );

        WSACleanup();

        return 1;
    }

    g_hwnd =
        CreateWindowExW(
            0,
            CLASS_NAME,
            L"Stream Project " STREAM_APP_VERSION_W,
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
        ShowFatalError(
            L"Falha ao criar a janela."
        );

        WSACleanup();

        return 1;
    }

    if (!InitializeD3D11Renderer(g_hwnd))
    {
        ShowFatalError(
            L"Falha ao inicializar o Direct3D 11. Atualize o "
            L"driver da placa de vídeo e tente de novo."
        );

        WSACleanup();

        return 1;
    }

    if (!UiInit(
            g_hwnd,
            g_rendererDevice.Get(),
            g_rendererContext.Get()))
    {
        ShowFatalError(
            L"Falha ao inicializar a interface."
        );

        WSACleanup();

        return 1;
    }


    // Sem ffmpeg funcionando nao ha video - a interface abre
    // direto na tela que explica como resolver.
    {
        std::string ffmpegProblem;

        if (!CheckFfmpegAvailable(ffmpegProblem))
        {
            std::cout
                << "Problema com o ffmpeg: "
                << ffmpegProblem
                << "\n";

            g_ffmpegProblem =
                ffmpegProblem;

            g_uiState =
                UiState::FfmpegProblem;
        }
    }


    int showCommand =
        SW_SHOW;

    ReceiverSettings().RestoreWindowPlacement(
        g_hwnd,
        showCommand);

    ShowWindow(
        g_hwnd,
        showCommand
    );

    UpdateWindow(
        g_hwnd
    );


    // Reaberto como administrador pelo modo Transmitir: volta
    // direto pra ele.
    if (startSharing &&
        g_uiState != UiState::FfmpegProblem)
    {
        OpenShareMode();
    }


    // ========================================================
    // Laco principal
    //
    // Processa as mensagens da janela e redesenha (video +
    // interface) a cada vsync - o Present(1) do RenderFrame e
    // quem dita o ritmo.
    // ========================================================

    bool quit =
        false;

    while (!quit)
    {
        MSG msg{};

        while (PeekMessageW(
                &msg,
                nullptr,
                0,
                0,
                PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                quit =
                    true;

                break;
            }

            TranslateMessage(&msg);

            DispatchMessageW(&msg);
        }

        if (quit)
            break;


        // Minimizada: nada pra desenhar.
        if (IsIconic(g_hwnd))
        {
            Sleep(50);

            continue;
        }


        auto frameStart =
            std::chrono::steady_clock::now();

        RenderFrame();

        // Se o RenderFrame voltou sem esperar o vsync (janela
        // encoberta, por exemplo), cede a CPU em vez de girar
        // em falso.
        if (std::chrono::steady_clock::now() - frameStart <
            std::chrono::milliseconds(3))
        {
            Sleep(4);
        }
    }


    // ========================================================
    // Encerrar
    // ========================================================

    g_running =
        false;

    if (g_udpSocket != INVALID_SOCKET)
    {
        shutdown(
            g_udpSocket,
            SD_BOTH
        );
    }


    // Sala: avisa que saiu (senao os outros continuam vendo
    // esta pessoa por ~20 s).
    g_voice.Leave();

    if (g_room.IsActive())
    {
        g_room.Leave();
    }

    // Modo Transmitir: encerra a sessao, a captura e as threads
    // dele (se chegou a ser aberto).
    sender::Shutdown();

    // Criando/entrando numa sala (a espera pelo servidor pode
    // durar ate 90 s): nada a preservar, encerra direto.
    if (g_homeBusy)
    {
        std::cout
            << "\nEncerrado enquanto entrava numa sala.\n";

        ExitProcess(0);
    }

    if (g_homeThread.joinable())
    {
        g_homeThread.join();
    }


    // Fechou no meio de uma tentativa de conexao (a espera pelo
    // servidor pode durar ate 90s): nao ha nada a preservar,
    // encerra o processo direto em vez de ficar aguardando.
    if (g_connectBusy)
    {
        std::cout
            << "\nReceiver encerrado durante a conexao.\n";

        ExitProcess(0);
    }

    if (g_connectThread.joinable())
    {
        g_connectThread.join();
    }

    if (g_receiverThread.joinable())
    {
        g_receiverThread.join();
    }

    if (g_audioReceiverThread.joinable())
    {
        g_audioReceiverThread.join();
    }


    // Agora o decoder pode ser encerrado com segurança.
    g_decoder.Stop();


    if (g_udpSocket != INVALID_SOCKET)
    {
        closesocket(g_udpSocket);

        g_udpSocket =
            INVALID_SOCKET;
    }


    UiShutdown();

    WSACleanup();

    std::cout
        << "\nReceiver encerrado.\n";

    return 0;
}


// ============================================================
// Ponto de entrada sem console
// ============================================================
//
// O executavel e do tipo "janela" (WIN32 no CMake), entao o
// Windows nao abre console junto. O ponto de entrada desse
// tipo de programa e o wWinMain, que so repassa pro AppMain().
// ============================================================

int WINAPI wWinMain(
    HINSTANCE,
    HINSTANCE,
    PWSTR commandLine,
    int)
{
    return AppMain(commandLine);
}
