// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#include "invite_code.h"

#include <cstdio>
#include <cstring>
#include <vector>


namespace
{

const char HOST_PREFIX[] = "SPH1.";
const char VIEWER_PREFIX[] = "SPV1.";

constexpr uint8_t FORMAT_VERSION = 1;

const char BASE64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789-_";


std::string Base64UrlEncode(
    const std::vector<uint8_t>& data)
{
    std::string out;

    size_t i = 0;

    while (i + 3 <= data.size())
    {
        uint32_t chunk =
            (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];

        out.push_back(BASE64URL[(chunk >> 18) & 63]);
        out.push_back(BASE64URL[(chunk >> 12) & 63]);
        out.push_back(BASE64URL[(chunk >> 6) & 63]);
        out.push_back(BASE64URL[chunk & 63]);

        i += 3;
    }

    size_t rest = data.size() - i;

    if (rest == 1)
    {
        uint32_t chunk = data[i] << 16;

        out.push_back(BASE64URL[(chunk >> 18) & 63]);
        out.push_back(BASE64URL[(chunk >> 12) & 63]);
    }
    else if (rest == 2)
    {
        uint32_t chunk =
            (data[i] << 16) | (data[i + 1] << 8);

        out.push_back(BASE64URL[(chunk >> 18) & 63]);
        out.push_back(BASE64URL[(chunk >> 12) & 63]);
        out.push_back(BASE64URL[(chunk >> 6) & 63]);
    }

    return out;
}


int Base64UrlValue(
    char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;

    return -1;
}


bool Base64UrlDecode(
    const std::string& text,
    std::vector<uint8_t>& out)
{
    out.clear();

    // 1 caractere sobrando nunca forma um byte.
    if (text.size() % 4 == 1)
        return false;

    uint32_t buffer = 0;

    int bits = 0;

    for (char c : text)
    {
        int value = Base64UrlValue(c);

        if (value < 0)
            return false;

        buffer = (buffer << 6) | static_cast<uint32_t>(value);

        bits += 6;

        if (bits >= 8)
        {
            bits -= 8;

            out.push_back(
                static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }

    return true;
}


// Fletcher-16: verificacao simples, suficiente pra pegar texto
// cortado ou com caractere trocado ao copiar e colar.
uint16_t Checksum(
    const std::vector<uint8_t>& data)
{
    uint32_t a = 0;
    uint32_t b = 0;

    for (uint8_t byte : data)
    {
        a = (a + byte) % 255;
        b = (b + a) % 255;
    }

    return static_cast<uint16_t>((b << 8) | a);
}


void PutU16(
    std::vector<uint8_t>& out,
    uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value >> 8));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}


uint16_t GetU16(
    const uint8_t* data)
{
    return static_cast<uint16_t>((data[0] << 8) | data[1]);
}


// Tira espacos e quebras de linha (mensageiros as vezes quebram
// um texto longo em duas linhas).
std::string StripWhitespace(
    const std::string& text)
{
    std::string out;

    for (char c : text)
    {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
        {
            out.push_back(c);
        }
    }

    return out;
}


bool HasPrefix(
    const std::string& text,
    const char* prefix)
{
    std::string clean = StripWhitespace(text);

    size_t length = std::strlen(prefix);

    return
        clean.size() > length &&
        clean.compare(0, length, prefix) == 0;
}


std::string Pack(
    const char* prefix,
    std::vector<uint8_t> payload)
{
    PutU16(payload, Checksum(payload));

    return prefix + Base64UrlEncode(payload);
}


bool Unpack(
    const std::string& text,
    const char* prefix,
    size_t expectedPayloadSize,
    std::vector<uint8_t>& outPayload)
{
    if (!HasPrefix(text, prefix))
        return false;

    std::string clean = StripWhitespace(text);

    std::vector<uint8_t> raw;

    if (!Base64UrlDecode(
            clean.substr(std::strlen(prefix)),
            raw))
    {
        return false;
    }

    if (raw.size() != expectedPayloadSize + 2)
        return false;

    uint16_t stored = GetU16(raw.data() + expectedPayloadSize);

    raw.resize(expectedPayloadSize);

    if (Checksum(raw) != stored)
        return false;

    if (raw[0] != FORMAT_VERSION)
        return false;

    outPayload = std::move(raw);

    return true;
}

} // namespace


// ============================================================
// Convite (host)
// ============================================================
//
// versao(1) ipPublico(4) portaVideo(2) portaAudio(2)
// ipLocal(4) portaVideoLocal(2) portaAudioLocal(2) chave(32)
// ============================================================

constexpr size_t HOST_PAYLOAD_SIZE = 1 + 4 + 2 + 2 + 4 + 2 + 2 + 32;

std::string EncodeHostInvite(
    const HostInvite& invite)
{
    std::vector<uint8_t> payload;

    payload.push_back(FORMAT_VERSION);

    payload.insert(
        payload.end(), invite.publicIp, invite.publicIp + 4);

    PutU16(payload, invite.publicVideoPort);
    PutU16(payload, invite.publicAudioPort);

    payload.insert(
        payload.end(), invite.localIp, invite.localIp + 4);

    PutU16(payload, invite.localVideoPort);
    PutU16(payload, invite.localAudioPort);

    payload.insert(
        payload.end(), invite.hostKey, invite.hostKey + 32);

    return Pack(HOST_PREFIX, std::move(payload));
}


bool DecodeHostInvite(
    const std::string& text,
    HostInvite& outInvite)
{
    std::vector<uint8_t> payload;

    if (!Unpack(text, HOST_PREFIX, HOST_PAYLOAD_SIZE, payload))
        return false;

    const uint8_t* p = payload.data() + 1;

    std::memcpy(outInvite.publicIp, p, 4);
    p += 4;

    outInvite.publicVideoPort = GetU16(p);
    p += 2;

    outInvite.publicAudioPort = GetU16(p);
    p += 2;

    std::memcpy(outInvite.localIp, p, 4);
    p += 4;

    outInvite.localVideoPort = GetU16(p);
    p += 2;

    outInvite.localAudioPort = GetU16(p);
    p += 2;

    std::memcpy(outInvite.hostKey, p, 32);

    return true;
}


// ============================================================
// Resposta (viewer)
// ============================================================
//
// versao(1) ipPublico(4) portaVideo(2) portaAudio(2)
// ============================================================

constexpr size_t VIEWER_PAYLOAD_SIZE = 1 + 4 + 2 + 2;

std::string EncodeViewerReply(
    const ViewerReply& reply)
{
    std::vector<uint8_t> payload;

    payload.push_back(FORMAT_VERSION);

    payload.insert(
        payload.end(), reply.publicIp, reply.publicIp + 4);

    PutU16(payload, reply.publicVideoPort);
    PutU16(payload, reply.publicAudioPort);

    return Pack(VIEWER_PREFIX, std::move(payload));
}


bool DecodeViewerReply(
    const std::string& text,
    ViewerReply& outReply)
{
    std::vector<uint8_t> payload;

    if (!Unpack(text, VIEWER_PREFIX, VIEWER_PAYLOAD_SIZE, payload))
        return false;

    const uint8_t* p = payload.data() + 1;

    std::memcpy(outReply.publicIp, p, 4);
    p += 4;

    outReply.publicVideoPort = GetU16(p);
    p += 2;

    outReply.publicAudioPort = GetU16(p);

    return true;
}


bool LooksLikeHostInvite(
    const std::string& text)
{
    return HasPrefix(text, HOST_PREFIX);
}


bool LooksLikeViewerReply(
    const std::string& text)
{
    return HasPrefix(text, VIEWER_PREFIX);
}


// ============================================================
// IPv4
// ============================================================

bool ParseIpv4(
    const std::string& text,
    uint8_t outIp[4])
{
    unsigned int parts[4];

    char extra = 0;

    if (std::sscanf(
            text.c_str(),
            "%3u.%3u.%3u.%3u%c",
            &parts[0],
            &parts[1],
            &parts[2],
            &parts[3],
            &extra) != 4)
    {
        return false;
    }

    for (int i = 0; i < 4; ++i)
    {
        if (parts[i] > 255)
            return false;

        outIp[i] = static_cast<uint8_t>(parts[i]);
    }

    return true;
}


std::string FormatIpv4(
    const uint8_t ip[4])
{
    char text[16];

    std::snprintf(
        text,
        sizeof(text),
        "%u.%u.%u.%u",
        ip[0],
        ip[1],
        ip[2],
        ip[3]
    );

    return text;
}


bool IsZeroIpv4(
    const uint8_t ip[4])
{
    return
        ip[0] == 0 && ip[1] == 0 &&
        ip[2] == 0 && ip[3] == 0;
}
