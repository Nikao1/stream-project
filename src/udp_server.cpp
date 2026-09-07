#include "udp_server.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <iostream>
#include <cstring>
#include <vector>

#pragma comment(lib, "ws2_32.lib")


// ============================================================
// UDP
// ============================================================

constexpr uint32_t UDP_PACKET_SIZE =
    1400;


// ============================================================
// UDP HEADER
// ============================================================
//
// magic       4 bytes
// sequence    4 bytes
// payloadSize 4 bytes
// width       4 bytes
// height      4 bytes
// flags       4 bytes
//
// Total: 24 bytes
//
// O payload contém pedaços do fluxo MPEG-TS produzido
// pelo FFmpeg/NVENC.
// ============================================================

constexpr uint32_t UDP_HEADER_SIZE =
    24;


constexpr uint32_t UDP_PAYLOAD_SIZE =
    UDP_PACKET_SIZE -
    UDP_HEADER_SIZE;


constexpr uint32_t UDP_MAGIC =
    0x5354524D; // "STRM"


constexpr int UDP_SEND_BUFFER_SIZE =
    64 * 1024 * 1024;


// ============================================================
// Constructor
// ============================================================

UdpServer::UdpServer()
    : m_socket(INVALID_SOCKET),
      m_clientAddress{},
      m_clientKnown(false),
      m_sequence(0)
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
    // Reset estado
    // --------------------------------------------------------

    m_clientKnown =
        false;


    m_clientAddress =
        {};


    m_sequence =
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


    char buffer[64]{};


    sockaddr_in clientAddress{};


    int clientAddressSize =
        sizeof(clientAddress);


    std::cout
        << "UDP: aguardando HELLO...\n";


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


        // ----------------------------------------------------
        // 10038 / 10004 podem acontecer durante encerramento
        // ----------------------------------------------------

        if (error == WSAENOTSOCK ||
            error == WSAEINTR)
        {
            return false;
        }


        std::cout
            << "UDP: recvfrom HELLO falhou. Erro: "
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


    // --------------------------------------------------------
    // Verificar HELLO
    // --------------------------------------------------------

    if (std::strcmp(
            buffer,
            "HELLO"
        ) != 0)
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
    // Resetar sequence para nova conexao
    // --------------------------------------------------------

    m_sequence =
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
// SendVideoData
// ============================================================
//
// Recebe bytes MPEG-TS produzidos pelo FFmpeg.
//
// Exemplo:
//
// H264Encoder
//      |
//      v
// MPEG-TS bytes
//      |
//      v
// SendVideoData()
//      |
//      +--> UDP packet 0
//      +--> UDP packet 1
//      +--> UDP packet 2
//      +--> ...
//
// Cada pacote possui no maximo 1400 bytes.
// ============================================================

bool UdpServer::SendVideoData(
    const void* data,
    uint32_t dataSize,
    uint32_t width,
    uint32_t height)
{
    if (
        m_socket ==
            INVALID_SOCKET ||
        !m_clientKnown ||
        !data ||
        dataSize == 0 ||
        width == 0 ||
        height == 0)
    {
        return false;
    }


    const unsigned char* bytes =
        static_cast<
            const unsigned char*
        >(data);


    uint32_t offset =
        0;


    // --------------------------------------------------------
    // Reutilizar o mesmo buffer de pacote.
    //
    // Isso evita criar/destruir um vector a cada sendto().
    // --------------------------------------------------------

    std::vector<unsigned char> packet(
        UDP_PACKET_SIZE
    );


    while (offset < dataSize)
    {
        // ----------------------------------------------------
        // Quanto ainda falta enviar
        // ----------------------------------------------------

        uint32_t remaining =
            dataSize -
            offset;


        // ----------------------------------------------------
        // Tamanho deste payload
        // ----------------------------------------------------

        uint32_t payloadSize =
            remaining <
                UDP_PAYLOAD_SIZE
            ? remaining
            : UDP_PAYLOAD_SIZE;


        // ====================================================
        // HEADER
        // ====================================================

        uint32_t magic =
            htonl(
                UDP_MAGIC
            );


        uint32_t sequence =
            htonl(
                m_sequence++
            );


        uint32_t networkPayloadSize =
            htonl(
                payloadSize
            );


        uint32_t networkWidth =
            htonl(
                width
            );


        uint32_t networkHeight =
            htonl(
                height
            );


        // flags = 0
        uint32_t flags =
            htonl(0);


        // ----------------------------------------------------
        // magic
        // ----------------------------------------------------

        std::memcpy(
            packet.data() + 0,
            &magic,
            sizeof(uint32_t)
        );


        // ----------------------------------------------------
        // sequence
        // ----------------------------------------------------

        std::memcpy(
            packet.data() + 4,
            &sequence,
            sizeof(uint32_t)
        );


        // ----------------------------------------------------
        // payloadSize
        // ----------------------------------------------------

        std::memcpy(
            packet.data() + 8,
            &networkPayloadSize,
            sizeof(uint32_t)
        );


        // ----------------------------------------------------
        // width
        // ----------------------------------------------------

        std::memcpy(
            packet.data() + 12,
            &networkWidth,
            sizeof(uint32_t)
        );


        // ----------------------------------------------------
        // height
        // ----------------------------------------------------

        std::memcpy(
            packet.data() + 16,
            &networkHeight,
            sizeof(uint32_t)
        );


        // ----------------------------------------------------
        // flags
        // ----------------------------------------------------

        std::memcpy(
            packet.data() + 20,
            &flags,
            sizeof(uint32_t)
        );


        // ====================================================
        // PAYLOAD MPEG-TS
        // ====================================================

        std::memcpy(
            packet.data() +
                UDP_HEADER_SIZE,

            bytes +
                offset,

            payloadSize
        );


        // ====================================================
        // SEND UDP
        // ====================================================

        int packetSize =
            static_cast<int>(
                UDP_HEADER_SIZE +
                payloadSize
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


        // ----------------------------------------------------
        // Garantir que o datagrama inteiro foi enviado
        // ----------------------------------------------------

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


        // ----------------------------------------------------
        // Avancar no fluxo MPEG-TS
        // ----------------------------------------------------

        offset +=
            payloadSize;
    }


    return true;
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


    m_sequence =
        0;
}