#pragma once

#include <winsock2.h>

#include <cstdint>

class UdpServer
{
public:
    UdpServer();

    ~UdpServer();

    bool Start(
        uint16_t port
    );

    bool WaitForClient();

    bool SendVideoData(
        const void* data,
        uint32_t dataSize,
        uint32_t width,
        uint32_t height
    );

    void Stop();

private:

    SOCKET m_socket;

    sockaddr_in m_clientAddress;

    bool m_clientKnown;

    uint32_t m_sequence;
};