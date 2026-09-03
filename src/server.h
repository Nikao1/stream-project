#pragma once

#include <winsock2.h>
#include <cstdint>
#include <string>

class TcpServer
{
public:

    TcpServer();

    ~TcpServer();

    bool Start(
        unsigned short port
    );

    bool WaitForClient();

    bool Send(
        const void* data,
        int size
    );

    bool SendFrameNumber(
        uint32_t frameNumber
    );

    bool SendFramePixels(
    const void* data,
    uint32_t dataSize,
    uint32_t width,
    uint32_t height
    );

    bool SendFile(const std::string& filename);

    bool IsClientConnected() const;

    void Stop();

private:

    SOCKET m_serverSocket;

    SOCKET m_clientSocket;

    bool m_initialized;

};
