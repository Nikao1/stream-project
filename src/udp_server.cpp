#include "udp_server.h"
#include "video_protocol.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>

#include <iostream>
#include <cstring>
#include <mutex>
#include <vector>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "winmm.lib")


// ============================================================
// Pacing (ritmo de envio)
// ============================================================
//
// Um keyframe vira dezenas ou centenas de pacotes. Despejar
// tudo de uma vez funciona na rede local, mas na internet o
// roteador (ou a rede movel) descarta parte da rajada - e um
// unico pacote perdido inutiliza o frame inteiro.
//
// Solucao: a cada PACING_BURST_PACKETS pacotes, uma pausa de
// ~1 ms. Frames pequenos (a maioria) saem sem pausa nenhuma;
// um keyframe grande ganha poucos ms de atraso.
// ============================================================

constexpr uint32_t PACING_BURST_PACKETS =
    12;


// ============================================================
// Reenvio (NACK)
// ============================================================

// Quanto tempo um frame enviado fica guardado pra reenvio.
// Passado isso, o receiver ja desistiu dele (espera no maximo
// ~400 ms).
constexpr ULONGLONG HISTORY_MS =
    1000;

// Teto de memoria do historico (keyframes em 4K sao grandes).
constexpr size_t HISTORY_MAX_BYTES =
    32 * 1024 * 1024;

// Reenvio por espectador: no maximo isto por segundo (~1,7 MB/s,
// cerca de 14 Mbps - o bastante pra refazer um keyframe grande,
// mas nao pra afogar uma rede que ja esta perdendo pacotes)...
constexpr uint32_t RETRANSMIT_MAX_PACKETS_PER_SECOND =
    1500;

// ...e no maximo isto por pedido.
constexpr uint32_t RETRANSMIT_MAX_PACKETS_PER_NACK =
    600;


// ============================================================
// UDP
// ============================================================
//
// O protocolo (header de 36 bytes, orientado a frame) está
// definido em video_protocol.h e é compartilhado com o
// receiver.
// ============================================================

constexpr int UDP_SEND_BUFFER_SIZE =
    64 * 1024 * 1024;


// ============================================================
// Constructor
// ============================================================

UdpServer::UdpServer()
    : m_socket(INVALID_SOCKET),
      m_lastStunKeepAlive(0),
      m_frameId(0)
{
}


// ============================================================
// Destructor
// ============================================================

UdpServer::~UdpServer()
{
    Stop();
}


// ============================================================
// Start
// ============================================================

bool UdpServer::Start(
    uint16_t port)
{
    // --------------------------------------------------------
    // Criar socket UDP
    // --------------------------------------------------------

    m_socket =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );


    if (m_socket ==
        INVALID_SOCKET)
    {
        std::cout
            << "UDP: falha ao criar socket. Erro: "
            << WSAGetLastError()
            << "\n";

        return false;
    }


    // --------------------------------------------------------
    // Aumentar buffer de envio
    // --------------------------------------------------------

    int sendBuffer =
        UDP_SEND_BUFFER_SIZE;


    if (setsockopt(
            m_socket,
            SOL_SOCKET,
            SO_SNDBUF,
            reinterpret_cast<const char*>(
                &sendBuffer
            ),
            sizeof(sendBuffer)
        ) == SOCKET_ERROR)
    {
        std::cout
            << "UDP: aviso - SO_SNDBUF falhou. Erro: "
            << WSAGetLastError()
            << "\n";
    }
    else
    {
        std::cout
            << "UDP: buffer de envio configurado para "
            << UDP_SEND_BUFFER_SIZE / (1024 * 1024)
            << " MB.\n";
    }


    // --------------------------------------------------------
    // Endereco local
    // --------------------------------------------------------

    sockaddr_in address{};


    address.sin_family =
        AF_INET;


    address.sin_addr.s_addr =
        htonl(INADDR_ANY);


    address.sin_port =
        htons(port);


    // --------------------------------------------------------
    // Bind
    // --------------------------------------------------------

    if (bind(
            m_socket,
            reinterpret_cast<sockaddr*>(
                &address
            ),
            sizeof(address)
        ) == SOCKET_ERROR)
    {
        std::cout
            << "UDP: falha no bind. Erro: "
            << WSAGetLastError()
            << "\n";


        closesocket(
            m_socket
        );


        m_socket =
            INVALID_SOCKET;


        return false;
    }


    // --------------------------------------------------------
    // STUN: descobrir o endereco publico deste socket
    // --------------------------------------------------------

    m_stun =
        StunDiscover(m_socket);


    if (m_stun.ok)
    {
        std::cout
            << "UDP: endereco publico (STUN): "
            << m_stun.publicIp
            << ":"
            << m_stun.publicPort
            << "\n";
    }
    else
    {
        std::cout
            << "UDP: STUN sem resposta - conexao so pela "
            << "rede local.\n";
    }


    // --------------------------------------------------------
    // Reset estado
    // --------------------------------------------------------

    m_viewers.Clear();


    m_lastStunKeepAlive =
        GetTickCount64();


    m_frameId =
        0;


    std::cout
        << "UDP server iniciado na porta "
        << port
        << "\n";


    return true;
}


// ============================================================
// Service
// ============================================================

bool UdpServer::Service()
{
    std::vector<sockaddr_in> joined;
    std::vector<sockaddr_in> left;


    if (!m_hostKeys)
    {
        return false;
    }


    bool ok =
        UdpServiceViewers(
            m_socket,
            m_viewers,
            *m_hostKeys,
            CRYPTO_CHANNEL_VIDEO,
            m_allowAnonymous,
            m_stun.ok,
            m_lastStunKeepAlive,
            joined,
            left,
            [this](
                const sockaddr_in& from,
                const uint8_t* packet,
                size_t size)
            {
                HandleViewerPacket(from, packet, size);
            }
        );


    SendAlivePings();


    for (const sockaddr_in& address : joined)
    {
        std::cout
            << "\n========================================\n"
            << "ESPECTADOR CONECTADO (video): "
            << EndpointToString(address)
            << "\n"
            << "Assistindo agora: "
            << m_viewers.Count()
            << " de "
            << MAX_VIEWERS
            << "\n"
            << "========================================\n";
    }


    for (const sockaddr_in& address : left)
    {
        std::cout
            << "\nEspectador saiu (video): "
            << EndpointToString(address)
            << " - restam "
            << m_viewers.Count()
            << "\n";
    }


    return ok;
}


// ============================================================
// BuildFragment
// ============================================================
//
// Header (video_protocol.h) + o pedaco fragIndex do frame.
// ============================================================

int UdpServer::BuildFragment(
    const SentFrame& frame,
    uint32_t fragIndex,
    uint32_t fragCount,
    unsigned char* outPacket)
{
    const std::vector<unsigned char>& bytes =
        *frame.data;

    uint32_t frameSize =
        static_cast<uint32_t>(bytes.size());

    uint32_t offset =
        fragIndex * VIDEO_UDP_PAYLOAD_SIZE;

    uint32_t remaining =
        frameSize - offset;

    uint32_t fragSize =
        remaining < VIDEO_UDP_PAYLOAD_SIZE
        ? remaining
        : VIDEO_UDP_PAYLOAD_SIZE;

    VideoFrameFragmentHeader header{};

    header.magic = htonl(VIDEO_PROTOCOL_MAGIC);
    header.frameId = htonl(frame.frameId);
    header.fragIndex = htonl(fragIndex);
    header.fragCount = htonl(fragCount);
    header.frameSize = htonl(frameSize);
    header.fragSize = htonl(fragSize);
    header.width = htonl(frame.width);
    header.height = htonl(frame.height);
    header.flags = htonl(frame.flags);
    header.timestampMs = htonl(frame.timestampMs);

    std::memcpy(
        outPacket,
        &header,
        VIDEO_HEADER_SIZE
    );

    std::memcpy(
        outPacket + VIDEO_HEADER_SIZE,
        bytes.data() + offset,
        fragSize
    );

    return static_cast<int>(
        VIDEO_HEADER_SIZE + fragSize);
}


// ============================================================
// SendTo
// ============================================================

void UdpServer::SendTo(
    const UdpViewer& viewer,
    const unsigned char* packet,
    int packetSize,
    unsigned char* sealedBuffer)
{
    size_t sealedSize =
        CryptoSeal(
            viewer.sessionKey,
            CRYPTO_CHANNEL_VIDEO,
            m_packetCounter.fetch_add(1),
            packet,
            static_cast<size_t>(packetSize),
            sealedBuffer
        );

    // Um erro de envio pra UM espectador nao derruba os
    // outros: quem sumiu de verdade para de dar sinal de vida
    // e sai da lista pelo prazo (ver Service).
    sendto(
        m_socket,
        reinterpret_cast<const char*>(sealedBuffer),
        static_cast<int>(sealedSize),
        0,
        reinterpret_cast<const sockaddr*>(&viewer.address),
        sizeof(viewer.address)
    );
}


// ============================================================
// SendVideoFrame
// ============================================================
//
// Recebe um frame H264 (Annex B) completo e o fragmenta em
// pacotes UDP de no máximo VIDEO_UDP_PACKET_SIZE bytes, todos
// marcados com o mesmo frameId.
//
// H264Encoder::GetFrame()
//      |
//      v
// frame Annex B completo (SPS/PPS/IDR ou só slice)
//      |
//      v
// SendVideoFrame()
//      |
//      +--> UDP fragmento 0/N  (frameId = X)
//      +--> UDP fragmento 1/N  (frameId = X)
//      +--> ...
//      +--> UDP fragmento N-1/N (frameId = X)
//
// O frame tambem fica guardado no historico: se um pedaco se
// perder, o receiver pede de novo (HandleViewerPacket).
// ============================================================

bool UdpServer::SendVideoFrame(
    const void* frameData,
    uint32_t frameSize,
    uint32_t width,
    uint32_t height,
    bool isKeyframe,
    uint32_t timestampMs)
{
    if (
        m_socket ==
            INVALID_SOCKET ||
        !frameData ||
        frameSize == 0 ||
        width == 0 ||
        height == 0)
    {
        return false;
    }


    // Foto da lista de espectadores neste instante: o frame
    // inteiro vai pros mesmos destinos, mesmo que alguem entre
    // ou saia no meio do envio.
    UdpViewer viewers[MAX_LIST_CAPACITY];

    size_t viewerCount =
        m_viewers.Snapshot(viewers);

    if (viewerCount == 0)
    {
        return false;
    }


    // Sleep(1) so dura ~1 ms de verdade com o timer do Windows
    // em resolucao de 1 ms (o padrao e ~15 ms).
    static std::once_flag timerResolutionOnce;

    std::call_once(
        timerResolutionOnce,
        [] { timeBeginPeriod(1); }
    );


    const unsigned char* bytes =
        static_cast<const unsigned char*>(frameData);

    SentFrame frame;

    frame.frameId = m_frameId++;
    frame.width = width;
    frame.height = height;
    frame.flags = isKeyframe ? VIDEO_FLAG_KEYFRAME : 0;
    frame.timestampMs = timestampMs;
    frame.sentAtMs = GetTickCount64();
    frame.data =
        std::make_shared<const std::vector<unsigned char>>(
            bytes,
            bytes + frameSize);


    uint32_t fragCount =
        (frameSize +
         VIDEO_UDP_PAYLOAD_SIZE -
         1) /
        VIDEO_UDP_PAYLOAD_SIZE;


    unsigned char packet[VIDEO_UDP_PACKET_SIZE];

    // Versao cifrada do pacote (uma por espectador, cada um
    // com a sua chave).
    unsigned char sealed[VIDEO_UDP_PACKET_SIZE + CRYPTO_OVERHEAD];


    for (uint32_t fragIndex = 0;
         fragIndex < fragCount;
         ++fragIndex)
    {
        int packetSize =
            BuildFragment(
                frame,
                fragIndex,
                fragCount,
                packet);

        for (size_t v = 0; v < viewerCount; ++v)
        {
            SendTo(
                viewers[v],
                packet,
                packetSize,
                sealed);
        }


        // Pacing: pausa curta entre rajadas (menos no fim).
        // (a rajada e por espectador: cada caminho ve no maximo
        // PACING_BURST_PACKETS pacotes seguidos)
        if ((fragIndex + 1) % PACING_BURST_PACKETS == 0 &&
            fragIndex + 1 < fragCount)
        {
            Sleep(1);
        }
    }


    RememberFrame(
        std::move(frame));

    m_lastSendMs = GetTickCount64();


    return true;
}


// ============================================================
// SendAlivePings
// ============================================================
//
// Tela parada (nenhum video saindo ha 1 s): manda o sinal de
// vida pra cada espectador (ver VIDEO_ALIVE_TAG).
// ============================================================

void UdpServer::SendAlivePings()
{
    ULONGLONG now = GetTickCount64();

    if (now - m_lastSendMs.load() < VIDEO_ALIVE_INTERVAL_MS)
        return;

    m_lastSendMs = now;

    UdpViewer viewers[MAX_LIST_CAPACITY];

    size_t count = m_viewers.Snapshot(viewers);

    unsigned char sealed[sizeof(VIDEO_ALIVE_TAG) + CRYPTO_OVERHEAD];

    for (size_t i = 0; i < count; ++i)
    {
        SendTo(
            viewers[i],
            reinterpret_cast<const unsigned char*>(VIDEO_ALIVE_TAG),
            static_cast<int>(sizeof(VIDEO_ALIVE_TAG)),
            sealed);
    }
}


// ============================================================
// RememberFrame
// ============================================================

void UdpServer::RememberFrame(
    SentFrame frame)
{
    std::lock_guard<std::mutex> lock(
        m_historyMutex);

    m_historyBytes +=
        frame.data->size();

    m_history.push_back(
        std::move(frame));

    ULONGLONG now =
        GetTickCount64();

    // Sai o que passou do prazo ou do teto de memoria (mas o
    // frame mais novo sempre fica).
    while (m_history.size() > 1 &&
           (now - m_history.front().sentAtMs > HISTORY_MS ||
            m_historyBytes > HISTORY_MAX_BYTES))
    {
        m_historyBytes -=
            m_history.front().data->size();

        m_history.pop_front();
    }
}


// ============================================================
// HandleViewerPacket
// ============================================================
//
// Pedido de reenvio (NACK) de um espectador: decifra com a
// chave DELE (assim ninguem de fora consegue pedir reenvios),
// acha o frame no historico e reenvia so os fragmentos
// pedidos, so pra ele.
// ============================================================

void UdpServer::HandleViewerPacket(
    const sockaddr_in& from,
    const uint8_t* packet,
    size_t size)
{
    if (size < CRYPTO_OVERHEAD + sizeof(VideoNackHeader) ||
        size > CRYPTO_OVERHEAD + VIDEO_NACK_MAX_SIZE)
    {
        return;
    }

    UdpViewer viewer;

    if (!m_viewers.Find(from, viewer))
        return;

    uint8_t plain[VIDEO_NACK_MAX_SIZE];

    size_t plainSize = 0;

    if (!CryptoOpen(
            viewer.sessionKey,
            CRYPTO_CHANNEL_VIDEO_FEEDBACK,
            packet,
            size,
            plain,
            plainSize) ||
        plainSize < sizeof(VideoNackHeader))
    {
        return;
    }

    VideoNackHeader header;

    std::memcpy(&header, plain, sizeof(header));

    uint32_t rangeCount =
        ntohs(header.rangeCount);

    if (ntohl(header.magic) != VIDEO_NACK_MAGIC ||
        rangeCount == 0 ||
        rangeCount > VIDEO_NACK_MAX_RANGES ||
        plainSize !=
            sizeof(VideoNackHeader) +
            rangeCount * sizeof(VideoNackRange))
    {
        return;
    }

    uint32_t frameId =
        ntohl(header.frameId);


    // --------------------------------------------------------
    // Acha o frame (copia o ponteiro e solta a trava)
    // --------------------------------------------------------

    SentFrame frame;

    bool found = false;

    {
        std::lock_guard<std::mutex> lock(
            m_historyMutex);

        for (const SentFrame& candidate : m_history)
        {
            if (candidate.frameId == frameId)
            {
                frame = candidate;

                found = true;

                break;
            }
        }
    }

    // Velho demais (ja saiu do historico): o receiver vai
    // desistir dele e esperar o proximo keyframe.
    if (!found)
        return;

    uint32_t fragCount =
        static_cast<uint32_t>(
            (frame.data->size() +
             VIDEO_UDP_PAYLOAD_SIZE -
             1) /
            VIDEO_UDP_PAYLOAD_SIZE);


    // --------------------------------------------------------
    // Limite por espectador
    // --------------------------------------------------------

    ULONGLONG now =
        GetTickCount64();

    RetransmitBudget* budget = nullptr;

    for (RetransmitBudget& candidate : m_budgets)
    {
        if (candidate.address.sin_addr.s_addr ==
                from.sin_addr.s_addr &&
            candidate.address.sin_port == from.sin_port)
        {
            budget = &candidate;

            break;
        }
    }

    if (!budget)
    {
        // Lista pequena: espectadores que ja sairam vao sendo
        // trocados pelos novos.
        if (m_budgets.size() >= MAX_VIEWERS * 2)
        {
            m_budgets.erase(m_budgets.begin());
        }

        m_budgets.push_back(RetransmitBudget{});

        budget = &m_budgets.back();

        budget->address = from;

        budget->windowStartMs = now;
    }

    if (now - budget->windowStartMs >= 1000)
    {
        budget->windowStartMs = now;

        budget->packetsInWindow = 0;
    }


    // --------------------------------------------------------
    // Reenvia
    // --------------------------------------------------------

    unsigned char out[VIDEO_UDP_PACKET_SIZE];

    unsigned char sealed[VIDEO_UDP_PACKET_SIZE + CRYPTO_OVERHEAD];

    uint32_t sentNow = 0;

    for (uint32_t r = 0; r < rangeCount; ++r)
    {
        VideoNackRange range;

        std::memcpy(
            &range,
            plain + sizeof(VideoNackHeader) +
                r * sizeof(VideoNackRange),
            sizeof(range));

        uint32_t first = ntohs(range.first);

        uint32_t count = ntohs(range.count);

        // { 0, 0 } = o frame inteiro.
        if (first == 0 && count == 0)
        {
            count = fragCount;
        }

        for (uint32_t i = first;
             i < first + count && i < fragCount;
             ++i)
        {
            if (sentNow >= RETRANSMIT_MAX_PACKETS_PER_NACK ||
                budget->packetsInWindow >=
                    RETRANSMIT_MAX_PACKETS_PER_SECOND)
            {
                return;
            }

            int packetSize =
                BuildFragment(
                    frame,
                    i,
                    fragCount,
                    out);

            SendTo(
                viewer,
                out,
                packetSize,
                sealed);

            sentNow++;

            budget->packetsInWindow++;

            m_retransmittedPackets++;

            if (sentNow % PACING_BURST_PACKETS == 0)
            {
                Sleep(1);
            }
        }
    }
}


// ============================================================
// PunchTo
// ============================================================

void UdpServer::PunchTo(
    const std::string& ip,
    uint16_t port)
{
    UdpSendPunch(
        m_socket,
        ip,
        port
    );
}


// ============================================================
// Stop
// ============================================================

void UdpServer::Stop()
{
    m_viewers.Clear();

    {
        std::lock_guard<std::mutex> lock(
            m_historyMutex);

        m_history.clear();

        m_historyBytes = 0;
    }


    if (m_socket !=
        INVALID_SOCKET)
    {
        // ----------------------------------------------------
        // shutdown antes do closesocket.
        //
        // UDP nao possui conexao, mas isso ajuda a interromper
        // recvfrom() quando estamos encerrando o programa.
        // ----------------------------------------------------

        shutdown(
            m_socket,
            SD_BOTH
        );


        closesocket(
            m_socket
        );


        m_socket =
            INVALID_SOCKET;
    }


    m_frameId =
        0;
}