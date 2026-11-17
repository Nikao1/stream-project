#include "udp_server.h"
#include "video_protocol.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>

#include <iostream>
#include <cstring>
#include <mutex>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")


// ============================================================
// Pacing (ritmo de envio)
// ============================================================
//
// Um keyframe vira dezenas ou centenas de pacotes. Despejar
// tudo de uma vez funciona na rede local, mas na internet o
// roteador (ou a rede movel) descarta parte da rajada - e um
// unico pacote perdido inutiliza o frame inteiro.
//
// Solucao: a cada PACING_BURST_PACKETS pacotes, uma pausa de
// ~1 ms. Frames pequenos (a maioria) saem sem pausa nenhuma;
// um keyframe grande ganha poucos ms de atraso.
// ============================================================

constexpr uint32_t PACING_BURST_PACKETS =
    12;


// ============================================================
// UDP
// ============================================================
//
// O protocolo (header de 36 bytes, orientado a frame) está
// definido em video_protocol.h e é compartilhado com o
// receiver.
// ============================================================

constexpr int UDP_SEND_BUFFER_SIZE =
    64 * 1024 * 1024;


// ============================================================
// Constructor
// ============================================================

UdpServer::UdpServer()
    : m_socket(INVALID_SOCKET),
      m_lastStunKeepAlive(0),
      m_frameId(0)
{
}


// ============================================================
// Destructor
// ============================================================

UdpServer::~UdpServer()
{
    Stop();
}


// ============================================================
// Start
// ============================================================

bool UdpServer::Start(
    uint16_t port)
{
    // --------------------------------------------------------
    // Criar socket UDP
    // --------------------------------------------------------

    m_socket =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );


    if (m_socket ==
        INVALID_SOCKET)
    {
        std::cout
            << "UDP: falha ao criar socket. Erro: "
            << WSAGetLastError()
            << "\n";

        return false;
    }


    // --------------------------------------------------------
    // Aumentar buffer de envio
    // --------------------------------------------------------

    int sendBuffer =
        UDP_SEND_BUFFER_SIZE;


    if (setsockopt(
            m_socket,
            SOL_SOCKET,
            SO_SNDBUF,
            reinterpret_cast<const char*>(
                &sendBuffer
            ),
            sizeof(sendBuffer)
        ) == SOCKET_ERROR)
    {
        std::cout
            << "UDP: aviso - SO_SNDBUF falhou. Erro: "
            << WSAGetLastError()
            << "\n";
    }
    else
    {
        std::cout
            << "UDP: buffer de envio configurado para "
            << UDP_SEND_BUFFER_SIZE / (1024 * 1024)
            << " MB.\n";
    }


    // --------------------------------------------------------
    // Endereco local
    // --------------------------------------------------------

    sockaddr_in address{};


    address.sin_family =
        AF_INET;


    address.sin_addr.s_addr =
        htonl(INADDR_ANY);


    address.sin_port =
        htons(port);


    // --------------------------------------------------------
    // Bind
    // --------------------------------------------------------

    if (bind(
            m_socket,
            reinterpret_cast<sockaddr*>(
                &address
            ),
            sizeof(address)
        ) == SOCKET_ERROR)
    {
        std::cout
            << "UDP: falha no bind. Erro: "
            << WSAGetLastError()
            << "\n";


        closesocket(
            m_socket
        );


        m_socket =
            INVALID_SOCKET;


        return false;
    }


    // --------------------------------------------------------
    // STUN: descobrir o endereco publico deste socket
    // --------------------------------------------------------

    m_stun =
        StunDiscover(m_socket);


    if (m_stun.ok)
    {
        std::cout
            << "UDP: endereco publico (STUN): "
            << m_stun.publicIp
            << ":"
            << m_stun.publicPort
            << "\n";
    }
    else
    {
        std::cout
            << "UDP: STUN sem resposta - conexao so pela "
            << "rede local.\n";
    }


    // --------------------------------------------------------
    // Reset estado
    // --------------------------------------------------------

    m_viewers.Clear();


    m_lastStunKeepAlive =
        GetTickCount64();


    m_frameId =
        0;


    std::cout
        << "UDP server iniciado na porta "
        << port
        << "\n";


    return true;
}


// ============================================================
// Service
// ============================================================

bool UdpServer::Service()
{
    std::vector<sockaddr_in> joined;
    std::vector<sockaddr_in> left;


    if (!m_hostKeys)
    {
        return false;
    }


    bool ok =
        UdpServiceViewers(
            m_socket,
            m_viewers,
            *m_hostKeys,
            CRYPTO_CHANNEL_VIDEO,
            m_allowAnonymous,
            m_stun.ok,
            m_lastStunKeepAlive,
            joined,
            left
        );


    for (const sockaddr_in& address : joined)
    {
        std::cout
            << "\n========================================\n"
            << "ESPECTADOR CONECTADO (video): "
            << EndpointToString(address)
            << "\n"
            << "Assistindo agora: "
            << m_viewers.Count()
            << " de "
            << MAX_VIEWERS
            << "\n"
            << "========================================\n";
    }


    for (const sockaddr_in& address : left)
    {
        std::cout
            << "\nEspectador saiu (video): "
            << EndpointToString(address)
            << " - restam "
            << m_viewers.Count()
            << "\n";
    }


    return ok;
}


// ============================================================
// SendVideoFrame
// ============================================================
//
// Recebe um frame H264 (Annex B) completo e o fragmenta em
// pacotes UDP de no máximo 1400 bytes, todos marcados com o
// mesmo frameId.
//
// H264Encoder::GetFrame()
//      |
//      v
// frame Annex B completo (SPS/PPS/IDR ou só slice)
//      |
//      v
// SendVideoFrame()
//      |
//      +--> UDP fragmento 0/N  (frameId = X)
//      +--> UDP fragmento 1/N  (frameId = X)
//      +--> ...
//      +--> UDP fragmento N-1/N (frameId = X)
//
// O receiver só entrega o frame ao decoder quando os N
// fragmentos chegarem; caso contrário, descarta o frame
// inteiro (nunca escreve dado parcial/corrompido).
// ============================================================

bool UdpServer::SendVideoFrame(
    const void* frameData,
    uint32_t frameSize,
    uint32_t width,
    uint32_t height,
    bool isKeyframe,
    uint32_t timestampMs)
{
    if (
        m_socket ==
            INVALID_SOCKET ||
        !frameData ||
        frameSize == 0 ||
        width == 0 ||
        height == 0)
    {
        return false;
    }


    const unsigned char* bytes =
        static_cast<
            const unsigned char*
        >(frameData);


    // --------------------------------------------------------
    // Quantos fragmentos esse frame vai precisar.
    // --------------------------------------------------------

    uint32_t fragCount =
        (frameSize +
         VIDEO_UDP_PAYLOAD_SIZE -
         1) /
        VIDEO_UDP_PAYLOAD_SIZE;


    if (fragCount == 0)
    {
        fragCount = 1;
    }


    // Foto da lista de espectadores neste instante: o frame
    // inteiro vai pros mesmos destinos, mesmo que alguem entre
    // ou saia no meio do envio.
    UdpViewer viewers[MAX_VIEWERS];

    size_t viewerCount =
        m_viewers.Snapshot(viewers);

    if (viewerCount == 0)
    {
        return false;
    }


    uint32_t frameId =
        m_frameId++;


    // Sleep(1) so dura ~1 ms de verdade com o timer do Windows
    // em resolucao de 1 ms (o padrao e ~15 ms).
    static std::once_flag timerResolutionOnce;

    std::call_once(
        timerResolutionOnce,
        [] { timeBeginPeriod(1); }
    );


    uint32_t flags =
        isKeyframe
        ? VIDEO_FLAG_KEYFRAME
        : 0;


    // --------------------------------------------------------
    // Reutilizar o mesmo buffer de pacote entre fragmentos.
    // --------------------------------------------------------

    std::vector<unsigned char> packet(
        VIDEO_UDP_PACKET_SIZE
    );

    // Versao cifrada do pacote (uma por espectador, cada um
    // com a sua chave).
    std::vector<unsigned char> sealed(
        VIDEO_UDP_PACKET_SIZE +
        CRYPTO_OVERHEAD
    );


    uint32_t offset =
        0;


    for (uint32_t fragIndex = 0;
         fragIndex < fragCount;
         ++fragIndex)
    {
        uint32_t remaining =
            frameSize -
            offset;


        uint32_t fragSize =
            remaining <
                VIDEO_UDP_PAYLOAD_SIZE
            ? remaining
            : VIDEO_UDP_PAYLOAD_SIZE;


        // ====================================================
        // HEADER
        // ====================================================

        VideoFrameFragmentHeader header{};

        header.magic =
            htonl(VIDEO_PROTOCOL_MAGIC);

        header.frameId =
            htonl(frameId);

        header.fragIndex =
            htonl(fragIndex);

        header.fragCount =
            htonl(fragCount);

        header.frameSize =
            htonl(frameSize);

        header.fragSize =
            htonl(fragSize);

        header.width =
            htonl(width);

        header.height =
            htonl(height);

        header.flags =
            htonl(flags);

        header.timestampMs =
            htonl(timestampMs);


        std::memcpy(
            packet.data(),
            &header,
            VIDEO_HEADER_SIZE
        );


        // ====================================================
        // PAYLOAD
        // ====================================================

        std::memcpy(
            packet.data() +
                VIDEO_HEADER_SIZE,

            bytes +
                offset,

            fragSize
        );


        // ====================================================
        // SEND UDP
        // ====================================================

        int packetSize =
            static_cast<int>(
                VIDEO_HEADER_SIZE +
                fragSize
            );


        // Um erro de envio pra UM espectador nao derruba os
        // outros: quem sumiu de verdade para de dar sinal de
        // vida e sai da lista pelo prazo (ver Service).
        for (size_t v = 0; v < viewerCount; ++v)
        {
            size_t sealedSize =
                CryptoSeal(
                    viewers[v].sessionKey,
                    CRYPTO_CHANNEL_VIDEO,
                    m_packetCounter++,
                    packet.data(),
                    static_cast<size_t>(packetSize),
                    sealed.data()
                );

            sendto(
                m_socket,

                reinterpret_cast<
                    const char*
                >(
                    sealed.data()
                ),

                static_cast<int>(sealedSize),

                0,

                reinterpret_cast<
                    sockaddr*
                >(
                    &viewers[v].address
                ),

                sizeof(viewers[v].address)
            );
        }


        offset +=
            fragSize;


        // Pacing: pausa curta entre rajadas (menos no fim).
        // (a rajada e por espectador: cada caminho ve no maximo
        // PACING_BURST_PACKETS pacotes seguidos)
        if ((fragIndex + 1) % PACING_BURST_PACKETS == 0 &&
            fragIndex + 1 < fragCount)
        {
            Sleep(1);
        }
    }


    return true;
}


// ============================================================
// PunchTo
// ============================================================

void UdpServer::PunchTo(
    const std::string& ip,
    uint16_t port)
{
    UdpSendPunch(
        m_socket,
        ip,
        port
    );
}


// ============================================================
// Stop
// ============================================================

void UdpServer::Stop()
{
    m_viewers.Clear();


    if (m_socket !=
        INVALID_SOCKET)
    {
        // ----------------------------------------------------
        // shutdown antes do closesocket.
        //
        // UDP nao possui conexao, mas isso ajuda a interromper
        // recvfrom() quando estamos encerrando o programa.
        // ----------------------------------------------------

        shutdown(
            m_socket,
            SD_BOTH
        );


        closesocket(
            m_socket
        );


        m_socket =
            INVALID_SOCKET;
    }


    m_frameId =
        0;
}