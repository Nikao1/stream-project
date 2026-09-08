#include "udp_audio_server.h"
#include "audio_protocol.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")


// ============================================================
// Constructor
// ============================================================

UdpAudioServer::UdpAudioServer()
    : m_socket(INVALID_SOCKET),
      m_clientAddress{},
      m_clientKnown(false),
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


    m_clientKnown =
        false;

    m_clientAddress =
        {};

    m_sequence =
        0;


    std::cout
        << "UDP audio server iniciado na porta "
        << port
        << "\n";


    return true;
}


// ============================================================
// WaitForClient
// ============================================================

bool UdpAudioServer::WaitForClient()
{
    if (m_socket ==
        INVALID_SOCKET)
    {
        return false;
    }


    char buffer[64]{};

    sockaddr_in clientAddress{};

    int clientAddressSize =
        sizeof(clientAddress);


    std::cout
        << "UDP audio: aguardando HELLO...\n";


    int received =
        recvfrom(
            m_socket,
            buffer,
            sizeof(buffer) - 1,
            0,
            reinterpret_cast<sockaddr*>(
                &clientAddress
            ),
            &clientAddressSize
        );


    if (received ==
        SOCKET_ERROR)
    {
        int error =
            WSAGetLastError();


        if (error == WSAENOTSOCK ||
            error == WSAEINTR)
        {
            return false;
        }


        std::cout
            << "UDP audio: recvfrom HELLO falhou. Erro: "
            << error
            << "\n";


        return false;
    }


    if (received <= 0)
    {
        return false;
    }


    buffer[received] =
        '\0';


    if (std::strcmp(
            buffer,
            "HELLO"
        ) != 0)
    {
        return false;
    }


    m_clientAddress =
        clientAddress;

    m_clientKnown =
        true;

    m_sequence =
        0;


    char ip[INET_ADDRSTRLEN]{};


    inet_ntop(
        AF_INET,
        &clientAddress.sin_addr,
        ip,
        sizeof(ip)
    );


    std::cout
        << "UDP audio: receiver conectado: "
        << ip
        << ":"
        << ntohs(
            clientAddress.sin_port
        )
        << "\n";


    return true;
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
        !m_clientKnown ||
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


    int sent =
        sendto(
            m_socket,

            packet,

            packetSize,

            0,

            reinterpret_cast<sockaddr*>(
                &m_clientAddress
            ),

            sizeof(
                m_clientAddress
            )
        );


    if (sent !=
        packetSize)
    {
        return false;
    }


    return true;
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


    m_clientKnown =
        false;

    m_sequence =
        0;
}
