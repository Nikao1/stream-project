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

    // Fragmenta e envia um frame H264 (Annex B) completo,
    // usando o protocolo orientado a frame (video_protocol.h).
    bool SendVideoFrame(
        const void* frameData,
        uint32_t frameSize,
        uint32_t width,
        uint32_t height,
        bool isKeyframe,
        uint32_t timestampMs
    );

    void Stop();

private:

    SOCKET m_socket;

    sockaddr_in m_clientAddress;

    bool m_clientKnown;

    uint32_t m_frameId;
};