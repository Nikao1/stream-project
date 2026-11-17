#include "udp_audio_server.h"
#include "audio_protocol.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <cstring>
#include <vector>

#pragma comment(lib, "ws2_32.lib")


// ============================================================
// Constructor
// ============================================================

UdpAudioServer::UdpAudioServer()
    : m_socket(INVALID_SOCKET),
      m_lastStunKeepAlive(0),
      m_sequence(0)
{
}


// ============================================================
// Destructor
// ============================================================

UdpAudioServer::~UdpAudioServer()
{
    Stop();
}


// ============================================================
// Start
// ============================================================

bool UdpAudioServer::Start(
    uint16_t port)
{
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
            << "UDP audio: falha ao criar socket. Erro: "
            << WSAGetLastError()
            << "\n";

        return false;
    }


    sockaddr_in address{};

    address.sin_family =
        AF_INET;

    address.sin_addr.s_addr =
        htonl(INADDR_ANY);

    address.sin_port =
        htons(port);


    if (bind(
            m_socket,
            reinterpret_cast<sockaddr*>(
                &address
            ),
            sizeof(address)
        ) == SOCKET_ERROR)
    {
        std::cout
            << "UDP audio: falha no bind. Erro: "
            << WSAGetLastError()
            << "\n";


        closesocket(
            m_socket
        );

        m_socket =
            INVALID_SOCKET;

        return false;
    }


    m_stun =
        StunDiscover(m_socket);


    if (m_stun.ok)
    {
        std::cout
            << "UDP audio: endereco publico (STUN): "
            << m_stun.publicIp
            << ":"
            << m_stun.publicPort
            << "\n";
    }
    else
    {
        std::cout
            << "UDP audio: STUN sem resposta - conexao so "
            << "pela rede local.\n";
    }


    m_viewers.Clear();

    m_lastStunKeepAlive =
        GetTickCount64();

    m_sequence =
        0;


    std::cout
        << "UDP audio server iniciado na porta "
        << port
        << "\n";


    return true;
}


// ============================================================
// Service
// ============================================================

bool UdpAudioServer::Service()
{
    std::vector<sockaddr_in> joined;
    std::vector<sockaddr_in> left;


    bool ok =
        UdpServiceViewers(
            m_socket,
            m_viewers,
            m_stun.ok,
            m_lastStunKeepAlive,
            joined,
            left
        );


    for (const sockaddr_in& address : joined)
    {
        std::cout
            << "Espectador conectado (audio): "
            << EndpointToString(address)
            << "\n";
    }


    for (const sockaddr_in& address : left)
    {
        std::cout
            << "Espectador saiu (audio): "
            << EndpointToString(address)
            << "\n";
    }


    return ok;
}


// ============================================================
// SendAudioPacket
// ============================================================

bool UdpAudioServer::SendAudioPacket(
    const void* opusData,
    uint32_t opusSize,
    uint32_t timestampMs)
{
    if (
        m_socket ==
            INVALID_SOCKET ||
        !opusData ||
        opusSize == 0 ||
        opusSize >
            AUDIO_UDP_MAX_PAYLOAD)
    {
        return false;
    }


    char packet[
        AUDIO_UDP_PACKET_SIZE
    ];


    AudioPacketHeader header{};

    header.magic =
        htonl(AUDIO_PROTOCOL_MAGIC);

    header.sequence =
        htonl(m_sequence++);

    header.timestampMs =
        htonl(timestampMs);

    header.payloadSize =
        htonl(opusSize);


    std::memcpy(
        packet,
        &header,
        AUDIO_HEADER_SIZE
    );


    std::memcpy(
        packet +
            AUDIO_HEADER_SIZE,

        opusData,

        opusSize
    );


    int packetSize =
        static_cast<int>(
            AUDIO_HEADER_SIZE +
            opusSize
        );


    sockaddr_in viewers[MAX_VIEWERS];

    size_t viewerCount =
        m_viewers.Snapshot(viewers);

    if (viewerCount == 0)
    {
        return false;
    }


    for (size_t v = 0; v < viewerCount; ++v)
    {
        sendto(
            m_socket,

            packet,

            packetSize,

            0,

            reinterpret_cast<sockaddr*>(
                &viewers[v]
            ),

            sizeof(viewers[v])
        );
    }


    return true;
}


// ============================================================
// PunchTo
// ============================================================

void UdpAudioServer::PunchTo(
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

void UdpAudioServer::Stop()
{
    if (m_socket !=
        INVALID_SOCKET)
    {
        closesocket(
            m_socket
        );

        m_socket =
            INVALID_SOCKET;
    }


    m_viewers.Clear();

    m_sequence =
        0;
}
