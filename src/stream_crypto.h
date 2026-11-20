#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// ============================================================
// CRIPTOGRAFIA DO CANAL
// ============================================================
//
// Objetivos:
//
//  1. Ninguem no caminho (provedor, Wi-Fi publico, quem
//     capturar os pacotes) consegue ver o video ou ouvir o
//     audio.
//  2. So assiste quem tem o codigo da sessao.
//  3. Pacote adulterado ou forjado e descartado.
//
// Como funciona:
//
//  - Cada ponta cria, ao abrir o programa, um par de chaves
//    X25519 (uma secreta, uma publica). Nada fica em disco.
//  - A chave publica de quem transmite viaja pelo servidor de
//    sinalizacao junto com o codigo da sessao. A do receiver
//    vai no proprio HELLO.
//  - Com a secreta de um lado e a publica do outro, as duas
//    pontas calculam a MESMA chave de sessao (Diffie-Hellman)
//    sem que ela trafegue. Cada espectador tem a sua.
//  - Todo pacote de video/audio e cifrado e autenticado com
//    XChaCha20-Poly1305 usando essa chave.
//
// Biblioteca: Monocypher (pequena, auditada, dominio publico).
//
// Limite conhecido: quem controla o servidor de sinalizacao
// poderia, em teoria, trocar as chaves publicas no caminho e
// se colocar no meio. Contra quem so observa a rede - o caso
// comum - a protecao e completa.
// ============================================================

constexpr size_t CRYPTO_KEY_SIZE = 32;

// Bytes a mais em cada pacote cifrado:
// 4 (marca "STE1") + 8 (contador) + 16 (autenticacao).
constexpr size_t CRYPTO_OVERHEAD = 28;

// Canal: entra no nonce, pra que video e audio (que dividem a
// mesma chave de sessao) nunca repitam um nonce.
constexpr uint8_t CRYPTO_CHANNEL_VIDEO = 1;
constexpr uint8_t CRYPTO_CHANNEL_AUDIO = 2;

// Pedidos de reenvio do receiver pra quem transmite (NACK - ver
// video_protocol.h). Canal proprio: os dois lados usam a mesma
// chave, e cada um conta os proprios pacotes - sem canal
// separado, os nonces poderiam se repetir.
constexpr uint8_t CRYPTO_CHANNEL_VIDEO_FEEDBACK = 3;


struct CryptoKeyPair
{
    uint8_t secretKey[CRYPTO_KEY_SIZE];
    uint8_t publicKey[CRYPTO_KEY_SIZE];
};


// Bytes aleatorios do sistema operacional.
bool CryptoRandomBytes(
    uint8_t* out,
    size_t size
);

bool CryptoGenerateKeyPair(
    CryptoKeyPair& outKeys
);


// Chave de sessao entre quem transmite (host) e um espectador
// (viewer). Cada ponta passa a PROPRIA secreta e a publica da
// OUTRA; as duas publicas entram na conta sempre na mesma
// ordem, entao os dois lados chegam ao mesmo resultado.
//
// Retorna false se a chave publica recebida for invalida.
bool CryptoDeriveSessionKey(
    const uint8_t mySecretKey[CRYPTO_KEY_SIZE],
    const uint8_t theirPublicKey[CRYPTO_KEY_SIZE],
    const uint8_t hostPublicKey[CRYPTO_KEY_SIZE],
    const uint8_t viewerPublicKey[CRYPTO_KEY_SIZE],
    uint8_t outSessionKey[CRYPTO_KEY_SIZE]
);


// ============================================================
// HELLO
// ============================================================
//
// "HEL2" + chave publica do viewer + prova (16 bytes)
//
//   A prova so pode ser calculada por quem conhece a chave
//   publica do host - ou seja, por quem entrou pelo codigo da
//   sessao. E o que impede um estranho que descobriu o IP de
//   pedir o stream.
//
// "HEL1" + chave publica do viewer
//
//   Sem prova: pra conexao direta por IP, quando nao ha
//   servidor de sinalizacao. O host responde com
//   "HKEY" + chave publica dele. O stream sai cifrado do mesmo
//   jeito, mas qualquer um que alcance o IP pode entrar - por
//   isso o host so aceita quando NAO ha sessao por codigo.
// ============================================================

constexpr size_t CRYPTO_HELLO_AUTH_SIZE = 4 + 32 + 16;
constexpr size_t CRYPTO_HELLO_ANON_SIZE = 4 + 32;
constexpr size_t CRYPTO_HOST_KEY_PACKET_SIZE = 4 + 32;


void CryptoBuildAuthHello(
    const uint8_t viewerPublicKey[CRYPTO_KEY_SIZE],
    const uint8_t sessionKey[CRYPTO_KEY_SIZE],
    uint8_t channel,
    uint8_t outPacket[CRYPTO_HELLO_AUTH_SIZE]
);

// packet aponta pro HELLO recebido (ja conferidos tamanho e
// marca "HEL2"); sessionKey e a derivada a partir da chave
// publica que veio nele.
bool CryptoVerifyAuthHello(
    const uint8_t packet[CRYPTO_HELLO_AUTH_SIZE],
    const uint8_t sessionKey[CRYPTO_KEY_SIZE],
    uint8_t channel
);

void CryptoBuildAnonHello(
    const uint8_t viewerPublicKey[CRYPTO_KEY_SIZE],
    uint8_t outPacket[CRYPTO_HELLO_ANON_SIZE]
);

void CryptoBuildHostKeyPacket(
    const uint8_t hostPublicKey[CRYPTO_KEY_SIZE],
    uint8_t outPacket[CRYPTO_HOST_KEY_PACKET_SIZE]
);

// true se os 4 primeiros bytes forem a marca dada ("HEL2",
// "HEL1", "HKEY", "STE1").
bool CryptoHasTag(
    const void* packet,
    size_t size,
    const char tag[4]
);


// ============================================================
// Pacotes de dados
// ============================================================
//
// "STE1" + contador (8 bytes) + conteudo cifrado + autenticacao
//
// O contador nunca pode se repetir com a mesma chave: quem
// envia usa um numero que so cresce.
// ============================================================

// outPacket precisa ter plainSize + CRYPTO_OVERHEAD bytes.
// Devolve o tamanho do pacote cifrado.
size_t CryptoSeal(
    const uint8_t sessionKey[CRYPTO_KEY_SIZE],
    uint8_t channel,
    uint64_t counter,
    const uint8_t* plain,
    size_t plainSize,
    uint8_t* outPacket
);

// outPlain precisa ter packetSize - CRYPTO_OVERHEAD bytes.
// Retorna false se o pacote nao for nosso, estiver adulterado
// ou tiver sido cifrado com outra chave.
bool CryptoOpen(
    const uint8_t sessionKey[CRYPTO_KEY_SIZE],
    uint8_t channel,
    const uint8_t* packet,
    size_t packetSize,
    uint8_t* outPlain,
    size_t& outPlainSize
);


// ============================================================
// Hex (pra chave publica viajar pelo servidor de sinalizacao)
// ============================================================

std::string CryptoToHex(
    const uint8_t* data,
    size_t size
);

bool CryptoFromHex(
    const std::string& hex,
    uint8_t* out,
    size_t size
);
