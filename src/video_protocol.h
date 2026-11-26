// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

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


// 1200 bytes (e nao 1400): redes moveis (4G/5G), VPNs e
// tuneis tem MTU menor que o da rede local. Um pacote maior
// que o MTU do caminho e fragmentado pelo roteador - e basta
// perder um pedaco pra perder o pacote inteiro. 1200 passa
// inteiro em praticamente qualquer rede.
constexpr uint32_t VIDEO_UDP_PACKET_SIZE =
    1200;


constexpr uint32_t VIDEO_UDP_PAYLOAD_SIZE =
    VIDEO_UDP_PACKET_SIZE -
    VIDEO_HEADER_SIZE;


constexpr uint32_t VIDEO_FLAG_KEYFRAME =
    0x1;

// ============================================================
// PEDIDO DE REENVIO (NACK) - receiver -> quem transmite
// ============================================================
//
// Quando falta um pedaco de um frame, o receiver pede de volta
// SO os fragmentos que faltaram, em vez de esperar o proximo
// keyframe (ate 1 s de imagem parada). Quem transmite guarda
// os frames do ultimo ~1 s pra poder reenviar.
//
// Vai cifrado com a chave de sessao do espectador, no canal
// CRYPTO_CHANNEL_VIDEO_FEEDBACK (ver stream_crypto.h).
//
// Conteudo (antes de cifrar):
//
//   magic       4 bytes  "NAK1"
//   frameId     4 bytes
//   rangeCount  2 bytes  quantos intervalos vem a seguir
//   reserved    2 bytes  (zero)
//   intervalos  rangeCount x { first 2 bytes, count 2 bytes }
//
// Intervalo { 0, 0 } = "o frame inteiro" (nenhum pedaco dele
// chegou, entao o receiver nem sabe quantos fragmentos tem).
// Versoes antigas do client simplesmente ignoram o pedido.
// ============================================================

// ============================================================
// SINAL DE VIDA - quem transmite -> espectadores
// ============================================================
//
// A captura do Windows so gera imagem quando a tela muda: uma
// tela parada nao manda nada. Pra o receiver saber a diferenca
// entre "tela parada" e "conexao caiu", quem transmite manda
// este pacote (cifrado, 4 bytes) a cada segundo sem video.
// Versoes antigas do receiver descartam (pacote pequeno demais
// pra ser video).
// ============================================================

constexpr char VIDEO_ALIVE_TAG[4] = { 'S', 'T', 'P', '1' };

constexpr uint64_t VIDEO_ALIVE_INTERVAL_MS = 1000;

// Resposta de quem transmite a um HELLO quando ja ha
// MAX_VIEWERS assistindo: "cheio" (cifrado com a chave daquele
// espectador - ninguem de fora consegue forjar). Quem tenta
// entrar continua mandando HELLO e entra quando vagar.
constexpr char VIEWERS_FULL_TAG[4] = { 'F', 'U', 'L', '1' };


#pragma pack(push, 1)

struct VideoNackHeader
{
    uint32_t magic;
    uint32_t frameId;
    uint16_t rangeCount;
    uint16_t reserved;
};

struct VideoNackRange
{
    uint16_t first;
    uint16_t count;
};

#pragma pack(pop)


constexpr uint32_t VIDEO_NACK_MAGIC =
    0x4E414B31; // "NAK1"


constexpr uint32_t VIDEO_NACK_MAX_RANGES =
    64;


constexpr uint32_t VIDEO_NACK_MAX_SIZE =
    sizeof(VideoNackHeader) +
    VIDEO_NACK_MAX_RANGES * sizeof(VideoNackRange);
