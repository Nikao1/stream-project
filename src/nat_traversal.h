#pragma once

#include <winsock2.h>

#include <cstdint>
#include <string>
#include <vector>

#include "udp_viewers.h"
#include "stream_crypto.h"

// ============================================================
// NAT TRAVERSAL (STUN + hole punching)
// ============================================================
//
// Problema: atras de um roteador, ninguem de fora consegue
// mandar pacote pra voce - o roteador (NAT) so deixa entrar
// resposta de algo que VOCE enviou antes.
//
// Solucao (hole punching):
//
//  1. Cada ponta pergunta a um servidor STUN qual e o seu
//     endereco publico (IP:porta do lado de fora do roteador).
//  2. As pontas trocam esses enderecos pelo servidor de
//     sinalizacao.
//  3. As duas enviam pacotes uma pra outra ao mesmo tempo.
//     Cada roteador ve "meu usuario mandou pacote pra la" e
//     passa a aceitar o que vem de la -> caminho direto aberto.
//
// Limite conhecido: com NAT "simetrico" (comum em rede movel
// 4G/5G e algumas redes corporativas) a porta publica muda
// por destino e o furo nao funciona - so um relay resolveria.
// ============================================================

struct StunResult
{
    bool ok = false;

    std::string publicIp;

    uint16_t publicPort = 0;

    // true = dois servidores STUN viram portas diferentes
    // (NAT simetrico) -> conexao direta provavelmente falha.
    bool symmetric = false;
};


// Descobre o endereco publico DESTE socket (ja com bind).
// Tem que ser o mesmo socket que vai trafegar video/audio,
// porque o mapeamento do roteador e por socket.
//
// Bloqueia por ate ~3s. Chamar antes de qualquer outra thread
// comecar a ler do socket.
StunResult StunDiscover(
    SOCKET socket
);


// Reenvia um pedido STUN sem esperar resposta. Serve so pra
// manter o mapeamento do roteador vivo enquanto ninguem
// conectou (roteadores esquecem portas UDP paradas).
void StunSendKeepAlive(
    SOCKET socket
);


// Envia alguns pacotes minusculos ("PUNCH") pro endereco
// publico da outra ponta: e o "furo" no nosso roteador. O
// conteudo nao importa - a outra ponta ignora.
void UdpSendPunch(
    SOCKET socket,
    const std::string& ip,
    uint16_t port
);


// Atende os espectadores de um socket do client. Espera ate
// ~200 ms por pacotes e processa o que chegar:
//
//   "HEL2" -> HELLO com prova de que o espectador entrou pelo
//             codigo da sessao: entra na lista (ou renova)
//   "HEL1" -> HELLO sem prova (conexao direta por IP): so e
//             aceito se allowAnonymous; respondemos com a
//             nossa chave publica ("HKEY")
//   "KEEP" -> espectador conhecido renova o prazo
//   resto  -> ignorado (inclusive o "HELLO" de versoes antigas,
//             que nao tinham criptografia)
//
// Cada espectador entra com a SUA chave de sessao, calculada a
// partir da chave publica que veio no HELLO dele.
//
// Tambem remove quem expirou e, enquanto nao ha ninguem
// conectado, mantem o mapeamento do roteador vivo (keepalive
// STUN a cada 15s, se stunKeepAlive for true).
//
// outJoined/outLeft recebem quem entrou e quem saiu nesta
// chamada. Retorna false quando o socket e fechado.
bool UdpServiceViewers(
    SOCKET socket,
    UdpViewerList& viewers,
    const CryptoKeyPair& hostKeys,
    uint8_t channel,
    bool allowAnonymous,
    bool stunKeepAlive,
    ULONGLONG& lastStunKeepAlive,
    std::vector<sockaddr_in>& outJoined,
    std::vector<sockaddr_in>& outLeft
);


// "1.2.3.4:5678" a partir de um sockaddr_in (pra logs).
std::string EndpointToString(
    const sockaddr_in& address
);
