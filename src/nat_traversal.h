#pragma once

#include <winsock2.h>

#include <cstdint>
#include <string>

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


// Descarta o que estiver parado no buffer de entrada do socket
// (keepalives da outra ponta, respostas STUN atrasadas), sem
// bloquear.
void UdpDrain(
    SOCKET socket
);


// Espera um "HELLO" da outra ponta neste socket e devolve de
// onde ele veio. Ignora qualquer outro pacote. Enquanto
// espera, mantem o mapeamento do roteador vivo (keepalive
// STUN a cada 15s) se stunKeepAlive for true.
//
// Retorna false quando o socket e fechado (encerramento).
bool UdpWaitForHello(
    SOCKET socket,
    bool stunKeepAlive,
    sockaddr_in& outClientAddress
);
