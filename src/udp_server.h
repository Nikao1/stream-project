#pragma once

#include <winsock2.h>

#include <cstdint>
#include <string>

#include "nat_traversal.h"

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

    // Endereco publico deste socket (descoberto por STUN no
    // Start). ok=false se nao houve resposta.
    const StunResult& GetStunResult() const
    {
        return m_stun;
    }

    // Hole punching: abre o nosso roteador pro endereco
    // publico do receiver.
    void PunchTo(
        const std::string& ip,
        uint16_t port
    );

    // Descarta pacotes de entrada parados (keepalives).
    void DrainIncoming();

private:

    StunResult m_stun;

    SOCKET m_socket;

    sockaddr_in m_clientAddress;

    bool m_clientKnown;

    uint32_t m_frameId;
};