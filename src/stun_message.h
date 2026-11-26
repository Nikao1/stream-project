// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

// ============================================================
// MENSAGENS STUN (RFC 5389) - so o minimo que usamos
// ============================================================
//
// STUN responde uma pergunta simples: "com qual IP e porta
// meu pacote chegou ai na internet?". E assim que cada ponta
// descobre seu endereco PUBLICO (o do lado de fora do
// roteador), necessario pro hole punching.
//
// Este header e puro (sem Winsock) de proposito: so monta o
// pedido e interpreta a resposta.
// ============================================================

constexpr size_t STUN_HEADER_SIZE = 20;

constexpr size_t STUN_TRANSACTION_ID_SIZE = 12;

constexpr uint32_t STUN_MAGIC_COOKIE = 0x2112A442;


// Monta um "Binding Request" (20 bytes, sem atributos).
inline void StunBuildBindingRequest(
    const unsigned char* transactionId,
    unsigned char* outPacket)
{
    outPacket[0] = 0x00;    // tipo: Binding Request (0x0001)
    outPacket[1] = 0x01;
    outPacket[2] = 0x00;    // tamanho dos atributos: 0
    outPacket[3] = 0x00;
    outPacket[4] = 0x21;    // magic cookie
    outPacket[5] = 0x12;
    outPacket[6] = 0xA4;
    outPacket[7] = 0x42;

    std::memcpy(
        outPacket + 8,
        transactionId,
        STUN_TRANSACTION_ID_SIZE
    );
}


// Interpreta um "Binding Success Response" e extrai o endereco
// publico (XOR-MAPPED-ADDRESS, ou MAPPED-ADDRESS em servidores
// antigos). So aceita se o transactionId for o do nosso pedido.
inline bool StunParseBindingResponse(
    const unsigned char* data,
    size_t size,
    const unsigned char* transactionId,
    std::string& outIp,
    uint16_t& outPort)
{
    if (size < STUN_HEADER_SIZE)
        return false;

    // tipo: Binding Success Response (0x0101)
    if (data[0] != 0x01 || data[1] != 0x01)
        return false;

    if (data[4] != 0x21 || data[5] != 0x12 ||
        data[6] != 0xA4 || data[7] != 0x42)
    {
        return false;
    }

    if (std::memcmp(
            data + 8,
            transactionId,
            STUN_TRANSACTION_ID_SIZE) != 0)
    {
        return false;
    }

    size_t attributesSize =
        (static_cast<size_t>(data[2]) << 8) | data[3];

    size_t end =
        STUN_HEADER_SIZE + attributesSize;

    if (end > size)
        return false;

    bool found = false;

    size_t offset = STUN_HEADER_SIZE;

    while (offset + 4 <= end)
    {
        uint16_t type =
            static_cast<uint16_t>(
                (data[offset] << 8) | data[offset + 1]);

        size_t length =
            (static_cast<size_t>(data[offset + 2]) << 8) |
            data[offset + 3];

        const unsigned char* value =
            data + offset + 4;

        if (offset + 4 + length > end)
            break;

        // valor: [0]=reservado [1]=familia (1 = IPv4)
        //        [2..3]=porta  [4..7]=endereco
        bool isXorMapped = type == 0x0020;
        bool isMapped = type == 0x0001;

        if ((isXorMapped || (isMapped && !found)) &&
            length >= 8 &&
            value[1] == 0x01)
        {
            unsigned char raw[6];

            std::memcpy(raw, value + 2, 6);

            if (isXorMapped)
            {
                // porta XOR os 2 primeiros bytes do cookie;
                // endereco XOR o cookie inteiro.
                raw[0] ^= 0x21;
                raw[1] ^= 0x12;
                raw[2] ^= 0x21;
                raw[3] ^= 0x12;
                raw[4] ^= 0xA4;
                raw[5] ^= 0x42;
            }

            outPort =
                static_cast<uint16_t>(
                    (raw[0] << 8) | raw[1]);

            char ip[16];

            std::snprintf(
                ip,
                sizeof(ip),
                "%u.%u.%u.%u",
                raw[2],
                raw[3],
                raw[4],
                raw[5]
            );

            outIp = ip;

            found = true;

            if (isXorMapped)
                return true;
        }

        // atributos sao alinhados em multiplos de 4 bytes
        offset += 4 + ((length + 3) & ~static_cast<size_t>(3));
    }

    return found;
}
