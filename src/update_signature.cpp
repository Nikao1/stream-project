// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#include "update_signature.h"

#include "monocypher.h"

std::string UpdateToHex(
    const uint8_t* data,
    size_t size)
{
    static const char digits[] = "0123456789abcdef";

    std::string text;

    text.reserve(size * 2);

    for (size_t i = 0; i < size; ++i)
    {
        text.push_back(digits[data[i] >> 4]);
        text.push_back(digits[data[i] & 15]);
    }

    return text;
}


bool UpdateFromHex(
    const std::string& text,
    uint8_t* out,
    size_t size)
{
    if (text.size() != size * 2)
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
        int high = nibble(text[i * 2]);
        int low = nibble(text[i * 2 + 1]);

        if (high < 0 || low < 0)
            return false;

        out[i] = static_cast<uint8_t>(high * 16 + low);
    }

    return true;
}


std::string UpdateHashHex(
    const uint8_t* data,
    size_t size)
{
    uint8_t hash[64];

    crypto_blake2b(hash, sizeof(hash), data, size);

    return UpdateToHex(hash, sizeof(hash));
}


std::string UpdateSignedMessage(
    const std::string& version,
    const std::string& hashHex)
{
    return "colae-atualizacao\n" + version + "\n" + hashHex + "\n";
}


bool UpdateVerify(
    const std::string& publicKeyHex,
    const std::string& version,
    const std::string& hashHex,
    const std::string& signatureHex)
{
    uint8_t publicKey[32];
    uint8_t signature[64];

    if (!UpdateFromHex(publicKeyHex, publicKey, sizeof(publicKey)) ||
        !UpdateFromHex(signatureHex, signature, sizeof(signature)) ||
        hashHex.size() != 128 ||
        version.empty())
    {
        return false;
    }

    std::string message = UpdateSignedMessage(version, hashHex);

    return crypto_eddsa_check(
        signature,
        publicKey,
        reinterpret_cast<const uint8_t*>(message.data()),
        message.size()) == 0;
}
