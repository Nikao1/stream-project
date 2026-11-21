#pragma once

#include <winsock2.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

    // Esquece todos os espectadores (a transmissao acabou;
    // quem quiser assistir de novo manda HELLO pra sessao nova).
    void ClearViewers()
    {
        m_viewers.Clear();
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

    // Pacotes reenviados a pedido dos espectadores (NACK) desde
    // o inicio - pro log.
    uint64_t GetRetransmittedPackets() const
    {
        return m_retransmittedPackets.load();
    }

private:

    // --------------------------------------------------------
    // Historico pra reenvio (NACK)
    // --------------------------------------------------------
    //
    // Os frames do ultimo ~1 s ficam guardados. Quando um
    // espectador avisa que faltou um pedaco, reenviamos so
    // aquele pedaco, so pra ele.
    // --------------------------------------------------------

    struct SentFrame
    {
        uint32_t frameId = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t flags = 0;
        uint32_t timestampMs = 0;
        ULONGLONG sentAtMs = 0;

        // Compartilhado: o reenvio usa uma copia do ponteiro e
        // solta a trava do historico antes de mandar.
        std::shared_ptr<const std::vector<unsigned char>> data;
    };

    void RememberFrame(
        SentFrame frame
    );

    // Trata um pacote do espectador que nao e HELLO/KEEP.
    void HandleViewerPacket(
        const sockaddr_in& from,
        const uint8_t* packet,
        size_t size
    );

    // Monta o pacote (header + pedaco) de um fragmento. Devolve
    // o tamanho.
    static int BuildFragment(
        const SentFrame& frame,
        uint32_t fragIndex,
        uint32_t fragCount,
        unsigned char* outPacket
    );

    // Cifra e envia um pacote pra um espectador.
    void SendTo(
        const UdpViewer& viewer,
        const unsigned char* packet,
        int packetSize,
        unsigned char* sealedBuffer
    );

    std::mutex m_historyMutex;

    std::deque<SentFrame> m_history;

    size_t m_historyBytes = 0;

    // Limite de reenvio por espectador (contra pedidos em
    // excesso - ex.: rede muito ruim - nao virarem uma
    // enxurrada que piora tudo).
    struct RetransmitBudget
    {
        sockaddr_in address{};
        ULONGLONG windowStartMs = 0;
        uint32_t packetsInWindow = 0;
    };

    std::vector<RetransmitBudget> m_budgets;

    std::atomic<uint64_t> m_retransmittedPackets{ 0 };

    // Ultimo envio de video (ou sinal de vida), pra mandar o
    // sinal de vida quando a tela fica parada.
    std::atomic<ULONGLONG> m_lastSendMs{ 0 };

    void SendAlivePings();

    StunResult m_stun;

    SOCKET m_socket;

    UdpViewerList m_viewers;

    const CryptoKeyPair* m_hostKeys = nullptr;

    std::atomic<bool> m_allowAnonymous{ false };

    // Contador dos pacotes cifrados: nunca repete enquanto o
    // programa roda (e as chaves mudam a cada execucao).
    // (atomico: o envio normal e o reenvio rodam em threads
    // diferentes)
    std::atomic<uint64_t> m_packetCounter{ 0 };

    ULONGLONG m_lastStunKeepAlive;

    uint32_t m_frameId;
};