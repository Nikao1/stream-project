#include "room_crypto.h"

#include <cstdlib>
#include <cstring>

#include "monocypher.h"
#include "stream_crypto.h"

namespace
{

// Mesmo alfabeto do servidor (sem 0/O, 1/I/L).
const char CODE_ALPHABET[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";

constexpr size_t CODE_LENGTH = 6;

const char BASE64_URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

constexpr size_t NONCE_SIZE = 24;
constexpr size_t MAC_SIZE = 16;

const char TEXT_PREFIX[] = "e1.";

const uint8_t BYTES_MAGIC[4] = { 'S', 'P', 'E', '1' };

// Argon2id: 16 MiB, 3 passadas. Adivinhar o segredo (8 letras
// de 31 = ~40 bits) custaria seculos de processamento.
constexpr uint32_t ARGON2_BLOCKS = 16 * 1024;
constexpr uint32_t ARGON2_PASSES = 3;


bool IsCodeChar(
    char c)
{
    return std::strchr(CODE_ALPHABET, c) != nullptr && c != '\0';
}


// nonce | mac | cifrado (o cifrado tem o tamanho do texto).
std::vector<uint8_t> Seal(
    const RoomKey& key,
    const std::string& context,
    const uint8_t* plain,
    size_t size)
{
    std::vector<uint8_t> out(NONCE_SIZE + MAC_SIZE + size);

    uint8_t* nonce = out.data();
    uint8_t* mac = nonce + NONCE_SIZE;
    uint8_t* cipher = mac + MAC_SIZE;

    if (!CryptoRandomBytes(nonce, NONCE_SIZE))
        return {};

    crypto_aead_lock(
        cipher, mac, key.key, nonce,
        reinterpret_cast<const uint8_t*>(context.data()), context.size(),
        plain, size);

    return out;
}


bool Open(
    const RoomKey& key,
    const std::string& context,
    const uint8_t* sealed,
    size_t size,
    std::vector<uint8_t>& outPlain)
{
    outPlain.clear();

    if (!key.valid || size < NONCE_SIZE + MAC_SIZE)
        return false;

    const uint8_t* nonce = sealed;
    const uint8_t* mac = nonce + NONCE_SIZE;
    const uint8_t* cipher = mac + MAC_SIZE;

    size_t cipherSize = size - NONCE_SIZE - MAC_SIZE;

    outPlain.resize(cipherSize);

    if (crypto_aead_unlock(
            outPlain.data(), mac, key.key, nonce,
            reinterpret_cast<const uint8_t*>(context.data()), context.size(),
            cipher, cipherSize) != 0)
    {
        outPlain.clear();

        return false;
    }

    return true;
}

} // namespace


bool ParseRoomInvite(
    const std::string& input,
    std::string& outCode,
    std::string& outSecret)
{
    outCode.clear();
    outSecret.clear();

    // So letras/numeros, em maiusculas (ignora espacos, tracos).
    std::string clean;

    for (char c : input)
    {
        if (c == ' ' || c == '-' || c == '\t' || c == '.')
            continue;

        if (c >= 'a' && c <= 'z')
        {
            c = static_cast<char>(c - 'a' + 'A');
        }

        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return false;

        clean += c;
    }

    if (clean.size() != CODE_LENGTH &&
        clean.size() != CODE_LENGTH + ROOM_SECRET_LENGTH)
    {
        return false;
    }

    outCode = clean.substr(0, CODE_LENGTH);

    // O segredo usa o alfabeto sem letras parecidas; letra fora
    // dele = foi digitado errado.
    if (clean.size() > CODE_LENGTH)
    {
        outSecret = clean.substr(CODE_LENGTH);

        for (char c : outSecret)
        {
            if (!IsCodeChar(c))
                return false;
        }
    }

    return true;
}


std::string RoomSecretGenerate()
{
    const size_t alphabet = sizeof(CODE_ALPHABET) - 1;

    std::string secret;

    while (secret.size() < ROOM_SECRET_LENGTH)
    {
        uint8_t bytes[32];

        if (!CryptoRandomBytes(bytes, sizeof(bytes)))
            return std::string();

        for (uint8_t b : bytes)
        {
            // Rejeita o resto pra nao favorecer letras.
            if (b >= 256 - (256 % alphabet))
                continue;

            secret += CODE_ALPHABET[b % alphabet];

            if (secret.size() == ROOM_SECRET_LENGTH)
                break;
        }
    }

    return secret;
}


std::string RoomSaltGenerate()
{
    uint8_t salt[16];

    if (!CryptoRandomBytes(salt, sizeof(salt)))
        return std::string();

    return CryptoToHex(salt, sizeof(salt));
}


bool RoomKeyDerive(
    const std::string& secret,
    const std::string& saltHex,
    RoomKey& outKey)
{
    outKey = RoomKey{};

    uint8_t salt[16];

    if (secret.size() != ROOM_SECRET_LENGTH ||
        !CryptoFromHex(saltHex, salt, sizeof(salt)))
    {
        return false;
    }

    void* work =
        std::malloc(static_cast<size_t>(ARGON2_BLOCKS) * 1024);

    if (!work)
        return false;

    crypto_argon2_config config{};

    config.algorithm = CRYPTO_ARGON2_ID;
    config.nb_blocks = ARGON2_BLOCKS;
    config.nb_passes = ARGON2_PASSES;
    config.nb_lanes = 1;

    crypto_argon2_inputs inputs{};

    inputs.pass = reinterpret_cast<const uint8_t*>(secret.data());
    inputs.pass_size = static_cast<uint32_t>(secret.size());
    inputs.salt = salt;
    inputs.salt_size = sizeof(salt);

    crypto_argon2(
        outKey.key, sizeof(outKey.key), work,
        config, inputs, crypto_argon2_no_extras);

    crypto_wipe(work, static_cast<size_t>(ARGON2_BLOCKS) * 1024);

    std::free(work);

    outKey.valid = true;

    return true;
}


std::string RoomSealText(
    const RoomKey& key,
    const std::string& context,
    const std::string& plain)
{
    if (!key.valid)
        return plain;

    std::vector<uint8_t> sealed =
        Seal(
            key, context,
            reinterpret_cast<const uint8_t*>(plain.data()),
            plain.size());

    if (sealed.empty())
        return std::string();

    return TEXT_PREFIX + Base64UrlEncode(sealed);
}


bool RoomOpenText(
    const RoomKey& key,
    const std::string& context,
    const std::string& sealed,
    std::string& outPlain)
{
    outPlain.clear();

    // Sala sem chave (criada por versao antiga): texto puro.
    if (!key.valid)
    {
        outPlain = sealed;

        return true;
    }

    // Com chave, texto puro NAO vale: seria o servidor (ou
    // alguem sem o segredo) tentando se passar por alguem.
    if (sealed.compare(0, sizeof(TEXT_PREFIX) - 1, TEXT_PREFIX) != 0)
        return false;

    std::vector<uint8_t> bytes;

    if (!Base64UrlDecode(sealed.substr(sizeof(TEXT_PREFIX) - 1), bytes))
        return false;

    std::vector<uint8_t> plain;

    if (!Open(key, context, bytes.data(), bytes.size(), plain))
        return false;

    outPlain.assign(plain.begin(), plain.end());

    return true;
}


std::vector<uint8_t> RoomSealBytes(
    const RoomKey& key,
    const std::string& context,
    const std::vector<uint8_t>& plain)
{
    if (!key.valid)
        return plain;

    std::vector<uint8_t> sealed =
        Seal(key, context, plain.data(), plain.size());

    if (sealed.empty())
        return {};

    std::vector<uint8_t> out(BYTES_MAGIC, BYTES_MAGIC + 4);

    out.insert(out.end(), sealed.begin(), sealed.end());

    return out;
}


bool RoomOpenBytes(
    const RoomKey& key,
    const std::string& context,
    const std::vector<uint8_t>& sealed,
    std::vector<uint8_t>& outPlain)
{
    outPlain.clear();

    if (!key.valid)
    {
        outPlain = sealed;

        return true;
    }

    if (sealed.size() < 4 ||
        std::memcmp(sealed.data(), BYTES_MAGIC, 4) != 0)
    {
        return false;
    }

    return Open(key, context, sealed.data() + 4, sealed.size() - 4, outPlain);
}


std::string Base64UrlEncode(
    const std::vector<uint8_t>& data)
{
    std::string out;

    out.reserve((data.size() + 2) / 3 * 4);

    for (size_t i = 0; i < data.size(); i += 3)
    {
        uint32_t chunk = static_cast<uint32_t>(data[i]) << 16;

        if (i + 1 < data.size()) chunk |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < data.size()) chunk |= data[i + 2];

        out += BASE64_URL[(chunk >> 18) & 63];
        out += BASE64_URL[(chunk >> 12) & 63];
        out += i + 1 < data.size() ? BASE64_URL[(chunk >> 6) & 63] : '=';
        out += i + 2 < data.size() ? BASE64_URL[chunk & 63] : '=';
    }

    return out;
}


bool Base64UrlDecode(
    const std::string& text,
    std::vector<uint8_t>& out)
{
    out.clear();

    uint32_t buffer = 0;

    int bits = 0;

    for (char c : text)
    {
        int value;

        if (c >= 'A' && c <= 'Z') value = c - 'A';
        else if (c >= 'a' && c <= 'z') value = c - 'a' + 26;
        else if (c >= '0' && c <= '9') value = c - '0' + 52;
        else if (c == '-' || c == '+') value = 62;
        else if (c == '_' || c == '/') value = 63;
        else if (c == '=' || c == '\r' || c == '\n') continue;
        else return false;

        buffer = (buffer << 6) | static_cast<uint32_t>(value);

        bits += 6;

        if (bits >= 8)
        {
            bits -= 8;

            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }

    return true;
}
