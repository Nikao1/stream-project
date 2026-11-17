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

    // Atende os espectadores (HELLO/KEEP/expirados) - mesmo
    // esquema do UdpServer::Service().
    bool Service();

    size_t GetViewerCount()
    {
        return m_viewers.Count();
    }

    // Envia um pacote Opus (já codificado) completo pra todos
    // os espectadores conectados. Não
    // fragmenta - pacotes Opus típicos (20ms, ~128kbps) cabem
    // tranquilamente em um único datagrama UDP.
    bool SendAudioPacket(
        const void* opusData,
        uint32_t opusSize,
        uint32_t timestampMs
    );

    void Stop();

    // Mesmo esquema do UdpServer (video): endereco publico por
    // STUN e hole punching.
    const StunResult& GetStunResult() const
    {
        return m_stun;
    }

    void PunchTo(
        const std::string& ip,
        uint16_t port
    );

private:

    StunResult m_stun;

    SOCKET m_socket;

    UdpViewerList m_viewers;

    ULONGLONG m_lastStunKeepAlive;

    uint32_t m_sequence;
};
