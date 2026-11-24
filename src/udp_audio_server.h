#pragma once

#include <winsock2.h>

#include <atomic>
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

    // Chaves e modo anonimo: igual ao UdpServer (video).
    void SetHostKeys(
        const CryptoKeyPair* hostKeys)
    {
        m_hostKeys = hostKeys;
    }

    void SetAllowAnonymous(
        bool allow)
    {
        m_allowAnonymous = allow;
    }

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

    // Porta local em uso (Start(0) deixa o Windows escolher uma
    // livre - ex.: canal de voz). 0 se nao iniciado.
    uint16_t GetLocalPort() const
    {
        sockaddr_in bound{};

        int size = sizeof(bound);

        if (m_socket == INVALID_SOCKET ||
            getsockname(
                m_socket,
                reinterpret_cast<sockaddr*>(&bound),
                &size) != 0)
        {
            return 0;
        }

        return ntohs(bound.sin_port);
    }

    // Quantos podem se conectar (canal de voz: mais que uma
    // transmissao - ver MAX_LIST_CAPACITY).
    void SetViewerLimit(
        size_t limit)
    {
        m_viewers.SetLimit(limit);
    }

    // Esquece todos os espectadores (ver UdpServer).
    void ClearViewers()
    {
        m_viewers.Clear();
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

    // Estado (canal de voz), sem audio: pacote com payloadSize
    // 0 e as flags no lugar do timestamp (bit 0 = microfone
    // desligado). Versoes antigas descartam (payloadSize 0).
    bool SendStatusPacket(
        uint32_t flags
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

    const CryptoKeyPair* m_hostKeys = nullptr;

    std::atomic<bool> m_allowAnonymous{ false };

    uint64_t m_packetCounter = 0;

    ULONGLONG m_lastStunKeepAlive;

    uint32_t m_sequence;
};
