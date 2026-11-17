#pragma once

#include <winsock2.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "nat_traversal.h"

class UdpServer
{
public:
    UdpServer();

    ~UdpServer();

    // Par de chaves de quem transmite (ver stream_crypto.h).
    // Chamar antes de Start(); o objeto precisa continuar
    // existindo enquanto o servidor rodar.
    void SetHostKeys(
        const CryptoKeyPair* hostKeys)
    {
        m_hostKeys = hostKeys;
    }

    // true = aceita espectador sem prova do codigo da sessao
    // (conexao direta por IP). So ligar quando NAO houver
    // sessao por codigo.
    void SetAllowAnonymous(
        bool allow)
    {
        m_allowAnonymous = allow;
    }

    bool Start(
        uint16_t port
    );

    // Atende os espectadores: quem entrou (HELLO), quem segue
    // vivo (KEEP) e quem expirou. Chamar em laco, numa thread
    // propria - espera ate ~200 ms por pacotes. Retorna false
    // se o socket estiver fechado.
    bool Service();

    // Quantos espectadores estao conectados agora.
    size_t GetViewerCount()
    {
        return m_viewers.Count();
    }

    // Fragmenta e envia um frame H264 (Annex B) completo pra
    // TODOS os espectadores conectados, usando o protocolo
    // orientado a frame (video_protocol.h). Retorna false se
    // nao houver ninguem conectado.
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

private:

    StunResult m_stun;

    SOCKET m_socket;

    UdpViewerList m_viewers;

    const CryptoKeyPair* m_hostKeys = nullptr;

    std::atomic<bool> m_allowAnonymous{ false };

    // Contador dos pacotes cifrados: nunca repete enquanto o
    // programa roda (e as chaves mudam a cada execucao).
    uint64_t m_packetCounter = 0;

    ULONGLONG m_lastStunKeepAlive;

    uint32_t m_frameId;
};