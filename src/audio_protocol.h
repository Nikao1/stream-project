#pragma once

#include <cstdint>

// ============================================================
// PROTOCOLO UDP DE AUDIO
// ============================================================
//
// Diferente do vídeo, um pacote Opus (~20ms de áudio) é
// sempre pequeno o suficiente pra caber em um único datagrama
// UDP, então NÃO precisamos de fragmentação/reassembly aqui -
// cada pacote de rede já é um pacote Opus completo.
//
// Header (16 bytes):
//
// magic        4 bytes - identifica o protocolo ("STA1")
// sequence     4 bytes - número incremental (detectar perda)
// timestampMs  4 bytes - timestamp de captura (uso futuro:
//                        sincronização A/V)
// payloadSize  4 bytes - tamanho do pacote Opus
// ============================================================

#pragma pack(push, 1)

struct AudioPacketHeader
{
    uint32_t magic;
    uint32_t sequence;
    uint32_t timestampMs;
    uint32_t payloadSize;
};

#pragma pack(pop)


constexpr uint32_t AUDIO_PROTOCOL_MAGIC =
    0x53544131; // "STA1"


constexpr uint32_t AUDIO_HEADER_SIZE =
    sizeof(AudioPacketHeader);


constexpr uint32_t AUDIO_UDP_PACKET_SIZE =
    1400;


constexpr uint32_t AUDIO_UDP_MAX_PAYLOAD =
    AUDIO_UDP_PACKET_SIZE -
    AUDIO_HEADER_SIZE;


// ============================================================
// Parâmetros de captura/codec
//
// 48 kHz estéreo é o padrão do Opus (taxa nativa interna,
// evita resample dentro do encoder). Frames de 20ms é o
// tamanho clássico usado por VoIP/WebRTC - bom equilíbrio
// entre latência e overhead de pacote.
// ============================================================

constexpr uint32_t AUDIO_SAMPLE_RATE =
    48000;

constexpr uint32_t AUDIO_CHANNELS =
    2;

constexpr uint32_t AUDIO_FRAME_MS =
    20;

constexpr uint32_t AUDIO_FRAME_SAMPLES =
    AUDIO_SAMPLE_RATE *
    AUDIO_FRAME_MS /
    1000; // 960 samples por frame, por canal

constexpr uint32_t AUDIO_BITRATE =
    128000;


constexpr uint16_t AUDIO_SERVER_UDP_PORT =
    5003; // client escuta aqui, esperando o HELLO do receiver
          // (mesmo esquema do video na porta 5001)

constexpr uint16_t AUDIO_UDP_PORT =
    5004; // receiver escuta aqui, recebendo audio do client
          // (mesmo esquema do video na porta 5002)
