// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#pragma once

#include <cstdint>
#include <string>

// ============================================================
// CONVITE MANUAL (conexao sem servidor de sinalizacao)
// ============================================================
//
// Plano B pra quando o servidor de sinalizacao estiver fora do
// ar, ou pra quem nao quiser depender dele. As duas pontas
// trocam, por qualquer mensageiro, dois textos:
//
//   1. CONVITE   (quem transmite -> quem assiste)   "SPH1...."
//      Enderecos de quem transmite (publico e local) e a chave
//      publica dele.
//
//   2. RESPOSTA  (quem assiste -> quem transmite)   "SPV1...."
//      Endereco publico de quem assiste, pra quem transmite
//      abrir o roteador na direcao dele (hole punching). So e
//      necessaria entre redes diferentes.
//
// E exatamente a informacao que o servidor de sinalizacao
// trocaria - aqui ela viaja colada no chat.
//
// Formato: prefixo + base64url de (versao + campos + 2 bytes de
// verificacao, pra detectar texto cortado ou alterado ao
// copiar). Este arquivo e puro (sem Winsock).
// ============================================================

struct HostInvite
{
    // 0.0.0.0 = endereco publico desconhecido (STUN falhou);
    // nesse caso so a rede local funciona.
    uint8_t publicIp[4] = {};

    uint16_t publicVideoPort = 0;
    uint16_t publicAudioPort = 0;

    uint8_t localIp[4] = {};

    uint16_t localVideoPort = 0;
    uint16_t localAudioPort = 0;

    uint8_t hostKey[32] = {};
};


struct ViewerReply
{
    uint8_t publicIp[4] = {};

    uint16_t publicVideoPort = 0;
    uint16_t publicAudioPort = 0;
};


std::string EncodeHostInvite(
    const HostInvite& invite
);

bool DecodeHostInvite(
    const std::string& text,
    HostInvite& outInvite
);

std::string EncodeViewerReply(
    const ViewerReply& reply
);

bool DecodeViewerReply(
    const std::string& text,
    ViewerReply& outReply
);


// So olham o prefixo - servem pra decidir "isso e um convite?"
// antes de tentar decodificar.
bool LooksLikeHostInvite(
    const std::string& text
);

bool LooksLikeViewerReply(
    const std::string& text
);


// "192.168.0.10" <-> 4 bytes
bool ParseIpv4(
    const std::string& text,
    uint8_t outIp[4]
);

std::string FormatIpv4(
    const uint8_t ip[4]
);

bool IsZeroIpv4(
    const uint8_t ip[4]
);
