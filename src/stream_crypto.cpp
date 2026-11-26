// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#include "stream_crypto.h"

#include <cstring>

extern "C"
{
#include "monocypher.h"
}

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <sys/random.h>
#endif


namespace
{

constexpr char TAG_HELLO_AUTH[4] = { 'H', 'E', 'L', '2' };
constexpr char TAG_HELLO_ANON[4] = { 'H', 'E', 'L', '1' };
constexpr char TAG_HOST_KEY[4]   = { 'H', 'K', 'E', 'Y' };
constexpr char TAG_DATA[4]       = { 'S', 'T', 'E', '1' };

// Tipo de uso da chave: entra no nonce pra que a prova do
// HELLO e os pacotes de dados nunca dividam um nonce.
constexpr uint8_t PURPOSE_DATA = 1;
constexpr uint8_t PURPOSE_HELLO = 2;


// Nonce de 24 bytes (XChaCha20):
//   [0] finalidade  [1] canal  [2..9] contador  [10..23] zero
void BuildNonce(
    uint8_t purpose,
    uint8_t channel,
    uint64_t counter,
    uint8_t outNonce[24])
{
    std::memset(outNonce, 0, 24);

    outNonce[0] = purpose;
    outNonce[1] = channel;

    for (int i = 0; i < 8; ++i)
    {
        outNonce[2 + i] =
            static_cast<uint8_t>(counter >> (56 - 8 * i));
    }
}

} // namespace


// ============================================================
// Aleatoriedade e chaves
// ============================================================

bool CryptoRandomBytes(
    uint8_t* out,
    size_t size)
{
#ifdef _WIN32

    return
        BCryptGenRandom(
            nullptr,
            out,
            static_cast<ULONG>(size),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0;

#else

    size_t done = 0;

    while (done < size)
    {
        ssize_t got =
            getrandom(out + done, size - done, 0);

        if (got <= 0)
            return false;

        done += static_cast<size_t>(got);
    }

    return true;

#endif
}


bool CryptoGenerateKeyPair(
    CryptoKeyPair& outKeys)
{
    if (!CryptoRandomBytes(
            outKeys.secretKey,
            CRYPTO_KEY_SIZE))
    {
        return false;
    }

    crypto_x25519_public_key(
        outKeys.publicKey,
        outKeys.secretKey
    );

    return true;
}


bool CryptoDeriveSessionKey(
    const uint8_t mySecretKey[CRYPTO_KEY_SIZE],
    const uint8_t theirPublicKey[CRYPTO_KEY_SIZE],
    const uint8_t hostPublicKey[CRYPTO_KEY_SIZE],
    const uint8_t viewerPublicKey[CRYPTO_KEY_SIZE],
    uint8_t outSessionKey[CRYPTO_KEY_SIZE])
{
    uint8_t shared[32];

    crypto_x25519(
        shared,
        mySecretKey,
        theirPublicKey
    );

    // Chave publica invalida (de "ordem baixa") faz o segredo
    // dar tudo zero - recusar, senao a chave de sessao seria
    // previsivel.
    static const uint8_t zero[32] = {};

    if (crypto_verify32(shared, zero) == 0)
    {
        crypto_wipe(shared, sizeof(shared));

        return false;
    }

    // O segredo cru do X25519 nao deve ser usado direto como
    // chave: passa por um hash junto com as duas chaves
    // publicas e um rotulo do protocolo.
    static const char label[] = "stream-project v1 session key";

    uint8_t material[sizeof(label) + 32 * 3];

    size_t offset = 0;

    std::memcpy(material + offset, label, sizeof(label));
    offset += sizeof(label);

    std::memcpy(material + offset, shared, 32);
    offset += 32;

    std::memcpy(material + offset, hostPublicKey, 32);
    offset += 32;

    std::memcpy(material + offset, viewerPublicKey, 32);
    offset += 32;

    crypto_blake2b(
        outSessionKey,
        CRYPTO_KEY_SIZE,
        material,
        offset
    );

    crypto_wipe(shared, sizeof(shared));
    crypto_wipe(material, sizeof(material));

    return true;
}


// ============================================================
// HELLO
// ============================================================

bool CryptoHasTag(
    const void* packet,
    size_t size,
    const char tag[4])
{
    return
        size >= 4 &&
        std::memcmp(packet, tag, 4) == 0;
}


void CryptoBuildAuthHello(
    const uint8_t viewerPublicKey[CRYPTO_KEY_SIZE],
    const uint8_t sessionKey[CRYPTO_KEY_SIZE],
    uint8_t channel,
    uint8_t outPacket[CRYPTO_HELLO_AUTH_SIZE])
{
    std::memcpy(outPacket, TAG_HELLO_AUTH, 4);
    std::memcpy(outPacket + 4, viewerPublicKey, 32);

    uint8_t nonce[24];

    BuildNonce(PURPOSE_HELLO, channel, 0, nonce);

    // "Cifrar" um texto vazio so pra obter a autenticacao: ela
    // cobre a marca + a chave publica (dados associados).
    crypto_aead_lock(
        nullptr,
        outPacket + 36,
        sessionKey,
        nonce,
        outPacket,
        36,
        nullptr,
        0
    );
}


bool CryptoVerifyAuthHello(
    const uint8_t packet[CRYPTO_HELLO_AUTH_SIZE],
    const uint8_t sessionKey[CRYPTO_KEY_SIZE],
    uint8_t channel)
{
    uint8_t nonce[24];

    BuildNonce(PURPOSE_HELLO, channel, 0, nonce);

    return
        crypto_aead_unlock(
            nullptr,
            packet + 36,
            sessionKey,
            nonce,
            packet,
            36,
            nullptr,
            0) == 0;
}


void CryptoBuildAnonHello(
    const uint8_t viewerPublicKey[CRYPTO_KEY_SIZE],
    uint8_t outPacket[CRYPTO_HELLO_ANON_SIZE])
{
    std::memcpy(outPacket, TAG_HELLO_ANON, 4);
    std::memcpy(outPacket + 4, viewerPublicKey, 32);
}


void CryptoBuildHostKeyPacket(
    const uint8_t hostPublicKey[CRYPTO_KEY_SIZE],
    uint8_t outPacket[CRYPTO_HOST_KEY_PACKET_SIZE])
{
    std::memcpy(outPacket, TAG_HOST_KEY, 4);
    std::memcpy(outPacket + 4, hostPublicKey, 32);
}


// ============================================================
// Pacotes de dados
// ============================================================

size_t CryptoSeal(
    const uint8_t sessionKey[CRYPTO_KEY_SIZE],
    uint8_t channel,
    uint64_t counter,
    const uint8_t* plain,
    size_t plainSize,
    uint8_t* outPacket)
{
    std::memcpy(outPacket, TAG_DATA, 4);

    for (int i = 0; i < 8; ++i)
    {
        outPacket[4 + i] =
            static_cast<uint8_t>(counter >> (56 - 8 * i));
    }

    uint8_t nonce[24];

    BuildNonce(PURPOSE_DATA, channel, counter, nonce);

    // Layout: marca(4) contador(8) cifrado(plainSize) mac(16).
    // A marca e o contador vao como dados associados: nao sao
    // secretos, mas ficam protegidos contra adulteracao.
    crypto_aead_lock(
        outPacket + 12,
        outPacket + 12 + plainSize,
        sessionKey,
        nonce,
        outPacket,
        12,
        plain,
        plainSize
    );

    return plainSize + CRYPTO_OVERHEAD;
}


bool CryptoOpen(
    const uint8_t sessionKey[CRYPTO_KEY_SIZE],
    uint8_t channel,
    const uint8_t* packet,
    size_t packetSize,
    uint8_t* outPlain,
    size_t& outPlainSize)
{
    if (packetSize < CRYPTO_OVERHEAD ||
        std::memcmp(packet, TAG_DATA, 4) != 0)
    {
        return false;
    }

    uint64_t counter = 0;

    for (int i = 0; i < 8; ++i)
    {
        counter = (counter << 8) | packet[4 + i];
    }

    size_t plainSize =
        packetSize - CRYPTO_OVERHEAD;

    uint8_t nonce[24];

    BuildNonce(PURPOSE_DATA, channel, counter, nonce);

    if (crypto_aead_unlock(
            outPlain,
            packet + 12 + plainSize,
            sessionKey,
            nonce,
            packet,
            12,
            packet + 12,
            plainSize) != 0)
    {
        return false;
    }

    outPlainSize = plainSize;

    return true;
}


// ============================================================
// Hex
// ============================================================

std::string CryptoToHex(
    const uint8_t* data,
    size_t size)
{
    static const char digits[] = "0123456789abcdef";

    std::string hex;

    hex.reserve(size * 2);

    for (size_t i = 0; i < size; ++i)
    {
        hex.push_back(digits[data[i] >> 4]);
        hex.push_back(digits[data[i] & 0x0F]);
    }

    return hex;
}


bool CryptoFromHex(
    const std::string& hex,
    uint8_t* out,
    size_t size)
{
    if (hex.size() != size * 2)
        return false;

    auto nibble =
        [](char c) -> int
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };

    for (size_t i = 0; i < size; ++i)
    {
        int high = nibble(hex[i * 2]);
        int low = nibble(hex[i * 2 + 1]);

        if (high < 0 || low < 0)
            return false;

        out[i] =
            static_cast<uint8_t>((high << 4) | low);
    }

    return true;
}
