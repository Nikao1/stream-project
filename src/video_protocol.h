#pragma once

#include <cstdint>

// ============================================================
// PROTOCOLO UDP DE VIDEO - ORIENTADO A FRAME
// ============================================================
//
// Substitui o esquema antigo (bytes MPEG-TS fatiados sem
// noção de frame). Agora cada pacote UDP carrega um FRAGMENTO
// de um FRAME H.264 (Annex B) específico, identificado por
// frameId.
//
// O receiver só entrega o frame para o decoder quando todos
// os fragmentos daquele frameId chegaram. Se faltar algum
// fragmento, o frame inteiro é descartado (nunca é escrito
// parcialmente/corrompido no decoder).
//
// Header (40 bytes):
//
// magic       4 bytes  - identifica o protocolo ("STR2")
// frameId     4 bytes  - identificador incremental do frame
// fragIndex   4 bytes  - índice deste fragmento (0-based)
// fragCount   4 bytes  - quantidade total de fragmentos do frame
// frameSize   4 bytes  - tamanho total do frame (todos os fragmentos)
// fragSize    4 bytes  - tamanho do payload deste fragmento
// width       4 bytes  - largura do frame
// height      4 bytes  - altura do frame
// flags       4 bytes  - bit0 = keyframe (contém SPS/PPS/IDR)
// timestampMs 4 bytes  - momento da captura, relativo ao início
//                        do stream (mesmo relógio usado pelo
//                        áudio - ver audio_protocol.h) - usado
//                        pra sincronização A/V no receiver
//
// Total: 40 bytes
// ============================================================

#pragma pack(push, 1)

struct VideoFrameFragmentHeader
{
    uint32_t magic;
    uint32_t frameId;
    uint32_t fragIndex;
    uint32_t fragCount;
    uint32_t frameSize;
    uint32_t fragSize;
    uint32_t width;
    uint32_t height;
    uint32_t flags;
    uint32_t timestampMs;
};

#pragma pack(pop)


constexpr uint32_t VIDEO_PROTOCOL_MAGIC =
    0x53545232; // "STR2" - v2 do protocolo (framing por frame)


constexpr uint32_t VIDEO_HEADER_SIZE =
    sizeof(VideoFrameFragmentHeader);


constexpr uint32_t VIDEO_UDP_PACKET_SIZE =
    1400;


constexpr uint32_t VIDEO_UDP_PAYLOAD_SIZE =
    VIDEO_UDP_PACKET_SIZE -
    VIDEO_HEADER_SIZE;


constexpr uint32_t VIDEO_FLAG_KEYFRAME =
    0x1;