#include "video_reassembly.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif


// ============================================================
// Ajustes
// ============================================================

namespace
{

// Pedaco que falta de um frame quando um frame MAIS NOVO ja
// comecou a chegar: quem transmite manda em ordem, entao ele se
// perdeu. Uma folga minima pra pacotes que chegam fora de ordem.
constexpr double REORDER_GRACE_MS = 3.0;

// Ultimo frame recebido incompleto e nada mais chegando: o
// final dele se perdeu.
constexpr double TAIL_TIMEOUT_MS = 20.0;

// Quantas vezes pedir o mesmo frame.
constexpr int MAX_NACKS_PER_FRAME = 4;

// Distancia maxima entre o frame esperado e um que chega. Mais
// que isso = ficamos muito tempo sem receber nada (ou quem
// transmite reiniciou): recomeca do frame que chegou.
constexpr uint32_t MAX_SPAN_FRAMES = 300;

// Um frame "do passado" mais distante que isso significa que
// quem transmite recomecou a contagem.
constexpr uint32_t RESTART_DISTANCE_FRAMES = 600;

double Clamp(
    double value,
    double low,
    double high)
{
    return std::max(low, std::min(high, value));
}

} // namespace


void VideoReassembler::SetCallbacks(
    DeliverFn deliver,
    NackFn nack)
{
    m_deliver = std::move(deliver);
    m_nack = std::move(nack);
}


void VideoReassembler::Reset()
{
    m_frames.clear();

    m_haveNext = false;

    m_waitingKeyframe = true;
}


// Pede de novo a cada ~1,5 ida-e-volta (o reenvio leva uma ida
// e volta pra chegar).
double VideoReassembler::RetryIntervalMs() const
{
    return Clamp(m_rttMs * 1.5 + 10.0, 25.0, 250.0);
}


// Espera no maximo umas 3 idas-e-voltas. Mais que isso trava a
// imagem por mais tempo do que esperar o proximo keyframe.
double VideoReassembler::GiveUpAfterMs() const
{
    return Clamp(m_rttMs * 3.0 + 80.0, 150.0, 450.0);
}


// ============================================================
// AddFragment
// ============================================================

void VideoReassembler::AddFragment(
    const VideoFrameFragmentHeader& header,
    const unsigned char* payload,
    double nowMs)
{
    uint32_t frameId =
        header.frameId;

    if (!m_haveNext)
    {
        m_haveNext = true;

        m_next = frameId;

        m_newest = frameId;
    }


    // --------------------------------------------------------
    // Frame anterior ao esperado
    // --------------------------------------------------------

    if (frameId < m_next)
    {
        if (m_next - frameId <= RESTART_DISTANCE_FRAMES)
        {
            // Ja entregue ou abandonado (ex.: reenvio que
            // chegou tarde demais, ou duplicado).
            return;
        }

        // Quem transmite recomecou do zero.
        Reset();

        m_haveNext = true;

        m_next = frameId;

        m_newest = frameId;
    }


    // --------------------------------------------------------
    // Frame muito a frente: ficamos muito tempo sem receber
    // --------------------------------------------------------

    if (frameId - m_next > MAX_SPAN_FRAMES)
    {
        m_lostFrames +=
            frameId - m_next;

        m_frames.clear();

        m_next = frameId;

        m_newest = frameId;

        m_waitingKeyframe = true;
    }


    if (frameId > m_newest)
    {
        m_newest = frameId;
    }


    // --------------------------------------------------------
    // Guarda o pedaco
    // --------------------------------------------------------

    Entry& entry =
        m_frames[frameId];

    if (!entry.known)
    {
        entry.known = true;

        entry.frameSize = header.frameSize;
        entry.fragCount = header.fragCount;
        entry.width = header.width;
        entry.height = header.height;
        entry.timestampMs = header.timestampMs;
        entry.isKeyframe =
            (header.flags & VIDEO_FLAG_KEYFRAME) != 0;

        entry.data.assign(header.frameSize, 0);

        entry.fragReceived.assign(header.fragCount, false);

        entry.fragReceivedCount = 0;
    }
    else if (entry.frameSize != header.frameSize ||
             entry.fragCount != header.fragCount)
    {
        // Pacote incoerente com os anteriores do mesmo frame.
        return;
    }

    if (header.fragIndex >= entry.fragCount ||
        entry.fragReceived[header.fragIndex])
    {
        // Duplicado (ex.: o original chegou atrasado E o
        // reenvio tambem).
        return;
    }

    const uint64_t fragOffset =
        static_cast<uint64_t>(header.fragIndex) *
        VIDEO_UDP_PAYLOAD_SIZE;

    if (fragOffset + header.fragSize > entry.frameSize)
    {
        return;
    }

    std::memcpy(
        entry.data.data() + fragOffset,
        payload,
        header.fragSize
    );

    entry.fragReceived[header.fragIndex] = true;

    entry.fragReceivedCount++;

    entry.lastPacketMs = nowMs;


    // --------------------------------------------------------
    // Chegou algo de um frame que pedimos de novo: mede a ida
    // e volta (o primeiro pedaco depois do primeiro pedido).
    // --------------------------------------------------------

    if (entry.nacks > 0 &&
        !entry.rttSampled)
    {
        entry.rttSampled = true;

        double sample =
            std::max(1.0, nowMs - entry.firstNackMs);

        m_rttMs =
            m_haveRtt
            ? m_rttMs * 0.8 + sample * 0.2
            : sample;

        m_haveRtt = true;
    }

    if (entry.Complete() &&
        entry.nacks > 0)
    {
        m_recoveredFrames++;
    }


    Deliver();
}


// ============================================================
// Deliver
// ============================================================
//
// Entrega, em ordem, tudo o que ja esta completo a partir de
// m_next.
// ============================================================

void VideoReassembler::Deliver()
{
    if (!m_haveNext)
        return;

    while (true)
    {
        // ----------------------------------------------------
        // Esperando keyframe: se ja ha um completo na fila,
        // pula direto pra ele (os frames antes dele seriam
        // descartados de qualquer jeito).
        // ----------------------------------------------------

        if (m_waitingKeyframe)
        {
            for (auto it = m_frames.lower_bound(m_next);
                 it != m_frames.end();
                 ++it)
            {
                if (it->second.Complete() &&
                    it->second.isKeyframe)
                {
                    uint32_t keyframeId = it->first;

                    m_skippedForKeyframe +=
                        keyframeId - m_next;

                    m_frames.erase(
                        m_frames.begin(),
                        m_frames.find(keyframeId));

                    m_next = keyframeId;

                    break;
                }
            }
        }

        auto it =
            m_frames.find(m_next);

        if (it == m_frames.end() ||
            !it->second.Complete())
        {
            // Esperando keyframe e o proximo e um frame comum
            // incompleto: nao adianta esperar o resto dele.
            if (m_waitingKeyframe &&
                it != m_frames.end() &&
                it->second.known &&
                !it->second.isKeyframe &&
                m_newest > m_next)
            {
                m_skippedForKeyframe++;

                m_frames.erase(it);

                m_next++;

                continue;
            }

            return;
        }

        Entry entry =
            std::move(it->second);

        m_frames.erase(it);

        m_next++;

        if (m_waitingKeyframe)
        {
            if (!entry.isKeyframe)
            {
                m_skippedForKeyframe++;

                continue;
            }

            m_waitingKeyframe = false;
        }

        if (m_deliver)
        {
            AssembledFrame frame;

            frame.frameId = m_next - 1;
            frame.width = entry.width;
            frame.height = entry.height;
            frame.timestampMs = entry.timestampMs;
            frame.isKeyframe = entry.isKeyframe;
            frame.data = std::move(entry.data);

            m_deliver(frame);
        }
    }
}


// ============================================================
// GiveUpNext
// ============================================================

void VideoReassembler::GiveUpNext()
{
    m_frames.erase(m_next);

    m_next++;

    m_lostFrames++;

    m_waitingKeyframe = true;

    if (m_newest < m_next)
    {
        m_newest = m_next;
    }
}


// ============================================================
// Service
// ============================================================

void VideoReassembler::Service(
    double nowMs)
{
    Deliver();

    if (!m_haveNext ||
        m_frames.empty())
    {
        return;
    }


    // --------------------------------------------------------
    // O que falta, do esperado ate o mais novo
    // --------------------------------------------------------

    for (uint32_t id = m_next;
         id <= m_newest &&
         id - m_next <= MAX_SPAN_FRAMES;
         ++id)
    {
        auto it = m_frames.find(id);

        if (it != m_frames.end() &&
            it->second.Complete())
        {
            continue;
        }

        bool lost;

        if (id < m_newest)
        {
            // Um frame mais novo ja esta chegando.
            lost =
                it == m_frames.end() ||
                !it->second.known ||
                nowMs - it->second.lastPacketMs >=
                    REORDER_GRACE_MS;
        }
        else
        {
            // O mais novo: o final dele pode ter se perdido.
            lost =
                it != m_frames.end() &&
                it->second.known &&
                nowMs - it->second.lastPacketMs >=
                    TAIL_TIMEOUT_MS;
        }

        if (!lost)
            continue;

        Entry& entry =
            it != m_frames.end()
            ? it->second
            : m_frames[id];

        if (entry.lossSinceMs < 0.0)
        {
            entry.lossSinceMs = nowMs;
        }

        // Esperando keyframe: frame comum nao serve, nem pede.
        if (m_waitingKeyframe &&
            entry.known &&
            !entry.isKeyframe)
        {
            continue;
        }

        if (entry.nacks < MAX_NACKS_PER_FRAME &&
            (entry.nacks == 0 ||
             nowMs - entry.lastNackMs >= RetryIntervalMs()))
        {
            SendNack(id, entry, nowMs);
        }
    }


    // --------------------------------------------------------
    // Desistir do esperado se o reenvio nao veio a tempo
    // --------------------------------------------------------

    while (m_haveNext)
    {
        auto it = m_frames.find(m_next);

        if (it == m_frames.end() ||
            it->second.Complete() ||
            it->second.lossSinceMs < 0.0 ||
            nowMs - it->second.lossSinceMs < GiveUpAfterMs())
        {
            break;
        }

        GiveUpNext();

        Deliver();
    }
}


// ============================================================
// SendNack
// ============================================================

void VideoReassembler::SendNack(
    uint32_t frameId,
    Entry& entry,
    double nowMs)
{
    uint8_t packet[VIDEO_NACK_MAX_SIZE];

    VideoNackRange ranges[VIDEO_NACK_MAX_RANGES];

    uint32_t rangeCount = 0;

    if (!entry.known)
    {
        // Nada chegou: pede o frame inteiro.
        ranges[0].first = 0;
        ranges[0].count = 0;

        rangeCount = 1;
    }
    else
    {
        uint32_t i = 0;

        while (i < entry.fragCount &&
               rangeCount < VIDEO_NACK_MAX_RANGES)
        {
            if (entry.fragReceived[i])
            {
                ++i;

                continue;
            }

            uint32_t first = i;

            while (i < entry.fragCount &&
                   !entry.fragReceived[i] &&
                   i - first < 0xFFFF)
            {
                ++i;
            }

            // Indices acima de 65535 nao cabem no pedido
            // (frame de mais de ~75 MB - nao acontece).
            if (first > 0xFFFF)
                break;

            ranges[rangeCount].first =
                static_cast<uint16_t>(first);

            ranges[rangeCount].count =
                static_cast<uint16_t>(i - first);

            rangeCount++;
        }
    }

    if (rangeCount == 0)
        return;

    VideoNackHeader header{};

    header.magic = htonl(VIDEO_NACK_MAGIC);
    header.frameId = htonl(frameId);
    header.rangeCount = htons(static_cast<uint16_t>(rangeCount));
    header.reserved = 0;

    std::memcpy(packet, &header, sizeof(header));

    for (uint32_t r = 0; r < rangeCount; ++r)
    {
        VideoNackRange network;

        network.first = htons(ranges[r].first);
        network.count = htons(ranges[r].count);

        std::memcpy(
            packet + sizeof(header) + r * sizeof(network),
            &network,
            sizeof(network));
    }

    if (entry.nacks == 0)
    {
        entry.firstNackMs = nowMs;
    }

    entry.nacks++;

    entry.lastNackMs = nowMs;

    m_nacksSent++;

    if (m_nack)
    {
        m_nack(
            packet,
            sizeof(header) + rangeCount * sizeof(VideoNackRange));
    }
}
