#include "server.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdint>
#include <fstream>
#include <vector>
#include <iostream>

#pragma comment(lib, "ws2_32.lib")

TcpServer::TcpServer()
    : m_serverSocket(INVALID_SOCKET),
      m_clientSocket(INVALID_SOCKET),
      m_initialized(false)
{
}

TcpServer::~TcpServer()
{
    Stop();
}

bool TcpServer::Start(unsigned short port)
{
    WSADATA wsaData{};

    if (WSAStartup(
        MAKEWORD(2, 2),
        &wsaData) != 0)
    {
        return false;
    }

    m_initialized = true;

    m_serverSocket =
        socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );

    if (m_serverSocket == INVALID_SOCKET)
    {
        Stop();
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
        m_serverSocket,
        reinterpret_cast<sockaddr*>(&address),
        sizeof(address)) == SOCKET_ERROR)
    {
        Stop();
        return false;
    }

    if (listen(
        m_serverSocket,
        1) == SOCKET_ERROR)
    {
        Stop();
        return false;
    }

    return true;
}

bool TcpServer::WaitForClient()
{
    if (m_serverSocket == INVALID_SOCKET)
        return false;

    sockaddr_in clientAddress{};

    int clientAddressSize =
        sizeof(clientAddress);

    m_clientSocket =
        accept(
            m_serverSocket,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientAddressSize
        );

    if (m_clientSocket == INVALID_SOCKET)
        return false;

    return true;
}

bool TcpServer::Send(
    const void* data,
    int size)
{
    if (m_clientSocket == INVALID_SOCKET)
        return false;

    if (data == nullptr || size <= 0)
        return false;

    const char* buffer =
        static_cast<const char*>(data);

    int totalSent = 0;

    while (totalSent < size)
    {
        int sent =
            send(
                m_clientSocket,
                buffer + totalSent,
                size - totalSent,
                0
            );

        if (sent == SOCKET_ERROR)
            return false;

        if (sent == 0)
            return false;

        totalSent += sent;
    }

    return true;
}

bool TcpServer::SendFrameNumber(
    uint32_t frameNumber)
{
    uint32_t networkFrameNumber =
        htonl(frameNumber);

    return Send(
        &networkFrameNumber,
        sizeof(networkFrameNumber)
    );
}


// ========================================================
// ENVIAR FRAME RAW
// ========================================================

bool TcpServer::SendFramePixels(
    const void* data,
    uint32_t dataSize,
    uint32_t width,
    uint32_t height)
{
    if (m_clientSocket == INVALID_SOCKET)
        return false;

    if (data == nullptr || dataSize == 0)
        return false;

    // ====================================================
    // Cabeçalho do frame
    // ====================================================

    uint32_t networkWidth =
        htonl(width);

    uint32_t networkHeight =
        htonl(height);

    uint32_t networkDataSize =
        htonl(dataSize);

    // ====================================================
    // Enviar largura
    // ====================================================

    if (!Send(
        &networkWidth,
        sizeof(networkWidth)))
    {
        return false;
    }

    // ====================================================
    // Enviar altura
    // ====================================================

    if (!Send(
        &networkHeight,
        sizeof(networkHeight)))
    {
        return false;
    }

    // ====================================================
    // Enviar tamanho dos pixels
    // ====================================================

    if (!Send(
        &networkDataSize,
        sizeof(networkDataSize)))
    {
        return false;
    }

    // ====================================================
    // Enviar pixels
    // ====================================================

    if (!Send(
        data,
        static_cast<int>(dataSize)))
    {
        return false;
    }

    return true;
}


// ========================================================
// ENVIAR ARQUIVO
// ========================================================

bool TcpServer::SendFile(
    const std::string& filename)
{
    if (m_clientSocket == INVALID_SOCKET)
        return false;

    // ========================================================
    // Abrir arquivo
    // ========================================================

    std::ifstream file(
        filename,
        std::ios::binary |
        std::ios::ate
    );

    if (!file)
        return false;

    // ========================================================
    // Obter tamanho
    // ========================================================

    std::streamsize fileSize =
        file.tellg();

    if (fileSize <= 0)
        return false;

    file.seekg(
        0,
        std::ios::beg
    );

    // ========================================================
    // Limite de segurança
    // ========================================================

    if (
        static_cast<uint64_t>(fileSize) >
        static_cast<uint64_t>(UINT32_MAX))
    {
        return false;
    }

    uint32_t size =
        static_cast<uint32_t>(fileSize);

    // ========================================================
    // Ler arquivo
    // ========================================================

    std::vector<char> buffer(
        static_cast<size_t>(fileSize)
    );

    if (!file.read(
        buffer.data(),
        fileSize))
    {
        return false;
    }

    // ========================================================
    // Enviar tamanho
    // ========================================================

    uint32_t networkSize =
        htonl(size);

    if (!Send(
        &networkSize,
        sizeof(networkSize)))
    {
        return false;
    }

    // ========================================================
    // Enviar conteúdo
    // ========================================================

    if (!Send(
        buffer.data(),
        static_cast<int>(buffer.size())))
    {
        return false;
    }

    return true;
}

bool TcpServer::IsClientConnected() const
{
    return m_clientSocket != INVALID_SOCKET;
}

void TcpServer::Stop()
{
    if (m_clientSocket != INVALID_SOCKET)
    {
        shutdown(
            m_clientSocket,
            SD_BOTH
        );

        closesocket(
            m_clientSocket
        );

        m_clientSocket =
            INVALID_SOCKET;
    }

    if (m_serverSocket != INVALID_SOCKET)
    {
        closesocket(
            m_serverSocket
        );

        m_serverSocket =
            INVALID_SOCKET;
    }

    if (m_initialized)
    {
        WSACleanup();

        m_initialized = false;
    }
}
