// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <vector>

#include "video_protocol.h"

// ============================================================
// MONTAGEM DOS FRAMES + PEDIDO DE REENVIO (NACK)
// ============================================================
//
// Recebe os fragmentos de video na ordem em que a rede entrega
// e devolve frames inteiros, NA ORDEM, prontos pro decoder.
//
// Varios frames podem estar em montagem ao mesmo tempo. Se um
// pedaco de um frame se perder, os frames seguintes ficam
// esperando enquanto pedimos o pedaco de novo pra quem
// transmite (NACK). Sem isso, qualquer pacote perdido deixava
// a imagem parada ate o proximo keyframe (ate 1 s).
//
// Se o reenvio nao chegar a tempo (algumas vezes o tempo de ida
// e volta da rede), desiste do frame e volta ao comportamento
// antigo: espera o proximo keyframe.
//
// Nao mexe com rede nem com decoder: quem usa passa os
// fragmentos e o horario, e recebe de volta (por callback) os
// frames prontos e os pedidos de reenvio ja montados - o que
// permite testar a logica com perda simulada.
// ============================================================

struct AssembledFrame
{
    uint32_t frameId = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t timestampMs = 0;
    bool isKeyframe = false;

    std::vector<unsigned char> data;
};


class VideoReassembler
{
public:
    // Frame completo, na ordem. Depois de uma perda, o primeiro
    // entregue e sempre um keyframe.
    using DeliverFn =
        std::function<void(AssembledFrame&)>;

    // Pedido de reenvio pronto (conteudo de video_protocol.h,
    // ainda sem cifrar).
    using NackFn =
        std::function<void(const uint8_t*, size_t)>;

    void SetCallbacks(
        DeliverFn deliver,
        NackFn nack
    );

    // Esquece tudo (ex.: mudou a resolucao). Volta a esperar
    // keyframe.
    void Reset();

    // O decoder foi reiniciado: so serve keyframe daqui pra
    // frente.
    void RequireKeyframe()
    {
        m_waitingKeyframe = true;
    }

    // header ja convertido pra ordem do host e validado
    // (tamanhos, indices). nowMs: relogio em ms com fracao.
    void AddFragment(
        const VideoFrameFragmentHeader& header,
        const unsigned char* payload,
        double nowMs
    );

    // Chamar com frequencia (a cada pacote e a cada poucos ms
    // sem pacote): detecta perdas, pede reenvio, desiste.
    void Service(
        double nowMs
    );

    // Ha frames em montagem/espera (quem chama deve chamar
    // Service() com mais frequencia).
    bool HasPending() const
    {
        return !m_frames.empty();
    }


    // --------------------------------------------------------
    // Estatisticas (desde o inicio)
    // --------------------------------------------------------

    // Frames perdidos de vez (o reenvio nao veio a tempo).
    uint64_t LostFrames() const { return m_lostFrames; }

    // Frames que chegaram, mas foram pulados esperando keyframe.
    uint64_t SkippedForKeyframe() const { return m_skippedForKeyframe; }

    // Frames salvos pelo reenvio.
    uint64_t RecoveredFrames() const { return m_recoveredFrames; }

    uint64_t NacksSent() const { return m_nacksSent; }

    // Estimativa do tempo de ida e volta ate quem transmite.
    double RoundTripMs() const { return m_rttMs; }

private:
    struct Entry
    {
        // false = nenhum pedaco chegou ainda (so sabemos que o
        // frame existe porque um mais novo apareceu).
        bool known = false;

        uint32_t frameSize = 0;
        uint32_t fragCount = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t timestampMs = 0;
        bool isKeyframe = false;

        std::vector<unsigned char> data;
        std::vector<bool> fragReceived;
        uint32_t fragReceivedCount = 0;

        double lastPacketMs = 0.0;

        // Quando notamos que falta algo (-1 = ainda nao).
        double lossSinceMs = -1.0;

        int nacks = 0;
        double firstNackMs = 0.0;
        double lastNackMs = 0.0;
        bool rttSampled = false;

        bool Complete() const
        {
            return known &&
                   fragReceivedCount == fragCount;
        }
    };

    void Deliver();

    void SendNack(
        uint32_t frameId,
        Entry& entry,
        double nowMs
    );

    // Pula o frame m_next (perdido) e passa a esperar keyframe.
    void GiveUpNext();

    double RetryIntervalMs() const;

    double GiveUpAfterMs() const;

    DeliverFn m_deliver;
    NackFn m_nack;

    std::map<uint32_t, Entry> m_frames;

    bool m_haveNext = false;

    // Proximo frame a entregar.
    uint32_t m_next = 0;

    // Mais novo ja visto.
    uint32_t m_newest = 0;

    bool m_waitingKeyframe = true;

    double m_rttMs = 80.0;

    bool m_haveRtt = false;

    uint64_t m_lostFrames = 0;
    uint64_t m_skippedForKeyframe = 0;
    uint64_t m_recoveredFrames = 0;
    uint64_t m_nacksSent = 0;
};
