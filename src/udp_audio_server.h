#pragma once

#include <winsock2.h>

#include <cstdint>
#include <string>

#include "nat_traversal.h"

// ============================================================
// UdpAudioServer
//
// Envia pacotes Opus para o receiver, em SOCKET e THREAD
// separados do vídeo - evita que áudio fique enfileirado
// atrás do processamento de frames de vídeo (head-of-line
// blocking).
// ============================================================

class UdpAudioServer
{
public:
    UdpAudioServer();
    ~UdpAudioServer();

    bool Start(
        uint16_t port
    );

    bool WaitForClient();

    // Envia um pacote Opus (já codificado) completo. Não
    // fragmenta - pacotes Opus típicos (20ms, ~128kbps) cabem
    // tranquilamente em um único datagrama UDP.
    bool SendAudioPacket(
        const void* opusData,
        uint32_t opusSize,
        uint32_t timestampMs
    );

    void Stop();

    // Mesmo esquema do UdpServer (video): endereco publico por
    // STUN, hole punching e descarte de keepalives.
    const StunResult& GetStunResult() const
    {
        return m_stun;
    }

    void PunchTo(
        const std::string& ip,
        uint16_t port
    );

    void DrainIncoming();

private:

    StunResult m_stun;

    SOCKET m_socket;

    sockaddr_in m_clientAddress;

    bool m_clientKnown;

    uint32_t m_sequence;
};