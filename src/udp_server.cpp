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
      m_clientAddress{},
      m_clientKnown(false),
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

    m_clientKnown =
        false;


    m_clientAddress =
        {};


    m_frameId =
        0;


    std::cout
        << "UDP server iniciado na porta "
        << port
        << "\n";


    return true;
}


// ============================================================
// WaitForClient
// ============================================================

bool UdpServer::WaitForClient()
{
    if (m_socket ==
        INVALID_SOCKET)
    {
        return false;
    }


    sockaddr_in clientAddress{};


    std::cout
        << "UDP: aguardando HELLO...\n";


    // Espera o HELLO ignorando qualquer outro pacote, e mantem
    // o mapeamento do roteador vivo enquanto isso.
    if (!UdpWaitForHello(
            m_socket,
            m_stun.ok,
            clientAddress))
    {
        return false;
    }


    // --------------------------------------------------------
    // Guardar endereco do receiver
    // --------------------------------------------------------

    m_clientAddress =
        clientAddress;


    m_clientKnown =
        true;


    // --------------------------------------------------------
    // Resetar frameId para nova conexao
    // --------------------------------------------------------

    m_frameId =
        0;


    // --------------------------------------------------------
    // Mostrar IP
    // --------------------------------------------------------

    char ip[INET_ADDRSTRLEN]{};


    inet_ntop(
        AF_INET,
        &clientAddress.sin_addr,
        ip,
        sizeof(ip)
    );


    std::cout
        << "UDP receiver conectado: "
        << ip
        << ":"
        << ntohs(
            clientAddress.sin_port
        )
        << "\n";


    return true;
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
        !m_clientKnown ||
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


        int sent =
            sendto(
                m_socket,

                reinterpret_cast<
                    const char*
                >(
                    packet.data()
                ),

                packetSize,

                0,

                reinterpret_cast<
                    sockaddr*
                >(
                    &m_clientAddress
                ),

                sizeof(
                    m_clientAddress
                )
            );


        if (sent ==
            SOCKET_ERROR)
        {
            int error =
                WSAGetLastError();


            std::cout
                << "\nUDP sendto() falhou. Erro: "
                << error
                << "\n";


            // ------------------------------------------------
            // Receiver provavelmente desconectou.
            // ------------------------------------------------

            m_clientKnown =
                false;


            return false;
        }


        if (sent != packetSize)
        {
            std::cout
                << "\nUDP: datagrama enviado parcialmente.\n"
                << "Esperado: "
                << packetSize
                << "\n"
                << "Enviado: "
                << sent
                << "\n";


            return false;
        }


        offset +=
            fragSize;


        // Pacing: pausa curta entre rajadas (menos no fim).
        if ((fragIndex + 1) % PACING_BURST_PACKETS == 0 &&
            fragIndex + 1 < fragCount)
        {
            Sleep(1);
        }
    }


    return true;
}


// ============================================================
// PunchTo / DrainIncoming
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


void UdpServer::DrainIncoming()
{
    UdpDrain(m_socket);
}


// ============================================================
// Stop
// ============================================================

void UdpServer::Stop()
{
    m_clientKnown =
        false;


    m_clientAddress =
        {};


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