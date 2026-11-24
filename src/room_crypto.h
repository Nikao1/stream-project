#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ============================================================
// CRIPTOGRAFIA DE PONTA A PONTA DA SALA
// ============================================================
//
// O codigo da sala tem duas partes: "RWRSAP-K7F9QM2X".
//
//   - RWRSAP   = endereco da sala no servidor;
//   - K7F9QM2X = SEGREDO: nunca vai pro servidor, so passa de
//                pessoa pra pessoa junto com o codigo.
//
// Do segredo sai (Argon2id + um "sal" aleatorio guardado no
// servidor) a chave da sala. Com ela o programa tranca, antes
// de enviar, tudo o que e conteudo: nomes, chat, o que cada um
// esta transmitindo e as previas. O servidor (e a hospedagem
// dele) so guarda dados embaralhados - XChaCha20-Poly1305, a
// mesma familia de cifra das transmissoes.
//
// O servidor continua vendo o que precisa pra funcionar: que a
// sala existe, quantas pessoas, os codigos das sessoes e os
// enderecos de internet (pra conectar as pessoas).
// ============================================================

// Letras do segredo (mesmo alfabeto dos codigos: sem 0/O, 1/I/L).
constexpr size_t ROOM_SECRET_LENGTH = 8;

struct RoomKey
{
    bool valid = false;

    uint8_t key[32]{};
};

// Separa "rwrsap-k7f9qm2x" / "RWRSAP K7F9QM2X" / "RWRSAPK7F9QM2X"
// em codigo (6) e segredo (8), em maiusculas. So o codigo (6)
// tambem vale (segredo vazio). Aceita tambem o link de convite
// (".../j#RWRSAP-K7F9QM2X", "chegae://sala/..."). false =
// nao e um codigo de sala.
bool ParseRoomInvite(
    const std::string& input,
    std::string& outCode,
    std::string& outSecret
);

// Segredo novo (ROOM_SECRET_LENGTH letras/numeros).
std::string RoomSecretGenerate();

// Sal novo (16 bytes, em hex).
std::string RoomSaltGenerate();

// Chave da sala (Argon2id, ~16 MB, alguns centesimos de
// segundo - chamar fora da thread da janela).
bool RoomKeyDerive(
    const std::string& secret,
    const std::string& saltHex,
    RoomKey& outKey
);

// Texto trancado: "e1." + base64url(nonce | mac | cifrado).
// context (ex.: "chat|<id>") amarra o texto ao lugar dele: um
// servidor malicioso nao consegue trocar um campo por outro.
std::string RoomSealText(
    const RoomKey& key,
    const std::string& context,
    const std::string& plain
);

// false = nao veio trancado com esta chave (ou foi alterado).
bool RoomOpenText(
    const RoomKey& key,
    const std::string& context,
    const std::string& sealed,
    std::string& outPlain
);

// Igual, pra bytes (previa): "SPE1" | nonce | mac | cifrado.
std::vector<uint8_t> RoomSealBytes(
    const RoomKey& key,
    const std::string& context,
    const std::vector<uint8_t>& plain
);

bool RoomOpenBytes(
    const RoomKey& key,
    const std::string& context,
    const std::vector<uint8_t>& sealed,
    std::vector<uint8_t>& outPlain
);

// Base64 "urlsafe" (- e _) com '=' no fim.
std::string Base64UrlEncode(
    const std::vector<uint8_t>& data
);

bool Base64UrlDecode(
    const std::string& text,
    std::vector<uint8_t>& out
);
