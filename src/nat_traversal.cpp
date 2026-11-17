#include "nat_traversal.h"
#include "stun_message.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>
#include <mutex>
#include <random>

#pragma comment(lib, "ws2_32.lib")


namespace
{

// Servidores STUN publicos e gratuitos. Usamos dois so pra
// comparar a porta vista por cada um (deteccao de NAT
// simetrico); um so ja basta pra descobrir o endereco.
struct StunServer
{
    const char* host;
    const char* port;
};

constexpr StunServer STUN_SERVERS[] =
{
    { "stun.l.google.com", "19302" },
    { "stun.cloudflare.com", "3478" },
};

constexpr int STUN_ATTEMPTS = 3;

constexpr int STUN_ATTEMPT_TIMEOUT_MS = 500;


// Endereco ja resolvido do ultimo servidor que respondeu -
// usado pelo keepalive, pra nao consultar DNS toda vez.
std::mutex g_keepAliveMutex;

sockaddr_in g_keepAliveServer{};

bool g_keepAliveServerKnown = false;


bool ResolveServer(
    const StunServer& server,
    sockaddr_in& outAddress)
{
    addrinfo hints{};

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    addrinfo* result = nullptr;

    if (getaddrinfo(
            server.host,
            server.port,
            &hints,
            &result) != 0 ||
        !result)
    {
        return false;
    }

    std::memcpy(
        &outAddress,
        result->ai_addr,
        sizeof(outAddress)
    );

    freeaddrinfo(result);

    return true;
}


void RandomTransactionId(
    unsigned char* outId)
{
    std::random_device device;

    for (size_t i = 0; i < STUN_TRANSACTION_ID_SIZE; ++i)
    {
        outId[i] =
            static_cast<unsigned char>(device() & 0xFF);
    }
}


bool QueryServer(
    SOCKET socket,
    const sockaddr_in& serverAddress,
    std::string& outIp,
    uint16_t& outPort)
{
    for (int attempt = 0; attempt < STUN_ATTEMPTS; ++attempt)
    {
        unsigned char transactionId[STUN_TRANSACTION_ID_SIZE];

        RandomTransactionId(transactionId);

        unsigned char request[STUN_HEADER_SIZE];

        StunBuildBindingRequest(
            transactionId,
            request
        );

        if (sendto(
                socket,
                reinterpret_cast<const char*>(request),
                static_cast<int>(sizeof(request)),
                0,
                reinterpret_cast<const sockaddr*>(
                    &serverAddress),
                sizeof(serverAddress)) == SOCKET_ERROR)
        {
            return false;
        }

        ULONGLONG deadline =
            GetTickCount64() + STUN_ATTEMPT_TIMEOUT_MS;

        for (;;)
        {
            ULONGLONG now = GetTickCount64();

            if (now >= deadline)
                break;

            fd_set readSet{};

            FD_ZERO(&readSet);
            FD_SET(socket, &readSet);

            timeval timeout{};

            timeout.tv_sec = 0;
            timeout.tv_usec =
                static_cast<long>((deadline - now) * 1000);

            int ready =
                select(
                    0,
                    &readSet,
                    nullptr,
                    nullptr,
                    &timeout
                );

            if (ready == SOCKET_ERROR)
                return false;

            if (ready == 0)
                break;

            unsigned char response[512];

            int received =
                recvfrom(
                    socket,
                    reinterpret_cast<char*>(response),
                    static_cast<int>(sizeof(response)),
                    0,
                    nullptr,
                    nullptr
                );

            // Erro (ex.: WSAECONNRESET) ou pacote que nao e a
            // nossa resposta: segue esperando ate o prazo.
            if (received <= 0)
                continue;

            if (StunParseBindingResponse(
                    response,
                    static_cast<size_t>(received),
                    transactionId,
                    outIp,
                    outPort))
            {
                return true;
            }
        }
    }

    return false;
}

} // namespace


// ============================================================
// StunDiscover
// ============================================================

StunResult StunDiscover(
    SOCKET socket)
{
    StunResult result;

    if (socket == INVALID_SOCKET)
        return result;

    int answers = 0;

    for (const StunServer& server : STUN_SERVERS)
    {
        sockaddr_in address{};

        if (!ResolveServer(server, address))
            continue;

        std::string ip;

        uint16_t port = 0;

        if (!QueryServer(socket, address, ip, port))
            continue;

        ++answers;

        if (answers == 1)
        {
            result.ok = true;
            result.publicIp = ip;
            result.publicPort = port;

            std::lock_guard<std::mutex> lock(
                g_keepAliveMutex);

            g_keepAliveServer = address;
            g_keepAliveServerKnown = true;
        }
        else if (port != result.publicPort ||
                 ip != result.publicIp)
        {
            result.symmetric = true;
        }
    }

    return result;
}


// ============================================================
// StunSendKeepAlive
// ============================================================

void StunSendKeepAlive(
    SOCKET socket)
{
    if (socket == INVALID_SOCKET)
        return;

    sockaddr_in address{};

    {
        std::lock_guard<std::mutex> lock(
            g_keepAliveMutex);

        if (!g_keepAliveServerKnown)
            return;

        address = g_keepAliveServer;
    }

    unsigned char transactionId[STUN_TRANSACTION_ID_SIZE];

    RandomTransactionId(transactionId);

    unsigned char request[STUN_HEADER_SIZE];

    StunBuildBindingRequest(
        transactionId,
        request
    );

    sendto(
        socket,
        reinterpret_cast<const char*>(request),
        static_cast<int>(sizeof(request)),
        0,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address)
    );
}


// ============================================================
// UdpSendPunch
// ============================================================

void UdpSendPunch(
    SOCKET socket,
    const std::string& ip,
    uint16_t port)
{
    if (socket == INVALID_SOCKET || port == 0)
        return;

    sockaddr_in address{};

    address.sin_family = AF_INET;
    address.sin_port = htons(port);

    if (inet_pton(
            AF_INET,
            ip.c_str(),
            &address.sin_addr) != 1)
    {
        return;
    }

    for (int i = 0; i < 3; ++i)
    {
        sendto(
            socket,
            "PUNCH",
            5,
            0,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)
        );
    }
}


// ============================================================
// EndpointToString
// ============================================================

std::string EndpointToString(
    const sockaddr_in& address)
{
    char ip[INET_ADDRSTRLEN]{};

    inet_ntop(
        AF_INET,
        &address.sin_addr,
        ip,
        sizeof(ip)
    );

    return
        std::string(ip) +
        ":" +
        std::to_string(ntohs(address.sin_port));
}


// ============================================================
// UdpServiceViewers
// ============================================================

bool UdpServiceViewers(
    SOCKET socket,
    UdpViewerList& viewers,
    const CryptoKeyPair& hostKeys,
    uint8_t channel,
    bool allowAnonymous,
    bool stunKeepAlive,
    ULONGLONG& lastStunKeepAlive,
    std::vector<sockaddr_in>& outJoined,
    std::vector<sockaddr_in>& outLeft)
{
    if (socket == INVALID_SOCKET)
        return false;

    // Primeira espera: ate 200 ms. Depois, so o que ja estiver
    // no buffer (espera zero), com um teto pra nao prender a
    // thread se chegar uma enxurrada.
    long waitMicroseconds = 200000;

    for (int packets = 0; packets < 64; ++packets)
    {
        fd_set readSet{};

        FD_ZERO(&readSet);
        FD_SET(socket, &readSet);

        timeval timeout{};

        timeout.tv_sec = 0;
        timeout.tv_usec = waitMicroseconds;

        int ready =
            select(
                0,
                &readSet,
                nullptr,
                nullptr,
                &timeout
            );

        // Socket fechado por Stop() -> encerrar.
        if (ready == SOCKET_ERROR)
            return false;

        if (ready == 0)
            break;

        waitMicroseconds = 0;

        char buffer[2048]{};

        sockaddr_in from{};

        int fromSize = sizeof(from);

        int received =
            recvfrom(
                socket,
                buffer,
                static_cast<int>(sizeof(buffer)),
                0,
                reinterpret_cast<sockaddr*>(&from),
                &fromSize
            );

        if (received == SOCKET_ERROR)
        {
            int error = WSAGetLastError();

            // WSAECONNRESET: um pacote que enviamos antes
            // (PUNCH, ou stream pra quem ja saiu) bateu numa
            // porta fechada - normal. WSAEMSGSIZE: pacote
            // maior que o buffer - ignorar.
            if (error == WSAECONNRESET ||
                error == WSAEMSGSIZE)
            {
                continue;
            }

            return false;
        }

        ULONGLONG now = GetTickCount64();

        const uint8_t* bytes =
            reinterpret_cast<const uint8_t*>(buffer);

        size_t size =
            static_cast<size_t>(received);

        bool authHello =
            size == CRYPTO_HELLO_AUTH_SIZE &&
            CryptoHasTag(bytes, size, "HEL2");

        bool anonHello =
            allowAnonymous &&
            size == CRYPTO_HELLO_ANON_SIZE &&
            CryptoHasTag(bytes, size, "HEL1");

        if (authHello || anonHello)
        {
            // A chave publica do espectador vem logo depois
            // da marca. Com ela + a nossa secreta sai a chave
            // de sessao DESTE espectador.
            const uint8_t* viewerPublicKey =
                bytes + 4;

            uint8_t sessionKey[CRYPTO_KEY_SIZE];

            if (!CryptoDeriveSessionKey(
                    hostKeys.secretKey,
                    viewerPublicKey,
                    hostKeys.publicKey,
                    viewerPublicKey,
                    sessionKey))
            {
                continue;
            }

            // Prova errada = nao conhece a nossa chave
            // publica = nao entrou pelo codigo. Ignora.
            if (authHello &&
                !CryptoVerifyAuthHello(
                    bytes,
                    sessionKey,
                    channel))
            {
                continue;
            }

            if (viewers.Touch(from, now, sessionKey) ==
                UdpViewerList::TouchResult::Added)
            {
                outJoined.push_back(from);
            }

            if (anonHello)
            {
                uint8_t reply[CRYPTO_HOST_KEY_PACKET_SIZE];

                CryptoBuildHostKeyPacket(
                    hostKeys.publicKey,
                    reply
                );

                sendto(
                    socket,
                    reinterpret_cast<const char*>(reply),
                    static_cast<int>(sizeof(reply)),
                    0,
                    reinterpret_cast<const sockaddr*>(&from),
                    sizeof(from)
                );
            }
        }
        else if (received == 4 &&
                 std::memcmp(buffer, "KEEP", 4) == 0)
        {
            viewers.Touch(from, now);
        }
    }

    ULONGLONG now = GetTickCount64();

    viewers.Prune(now, outLeft);

    if (stunKeepAlive &&
        viewers.Count() == 0 &&
        now - lastStunKeepAlive >= 15000)
    {
        StunSendKeepAlive(socket);

        lastStunKeepAlive = now;
    }

    return true;
}
