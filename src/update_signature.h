// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// ============================================================
// ASSINATURA DAS ATUALIZACOES
// ============================================================
//
// Cada instalador publicado e assinado com a chave privada do
// autor (que nunca sai do computador dele). O programa traz so a
// chave PUBLICA (update_key.h) e, antes de rodar um instalador
// baixado, confere:
//
//   1. o resumo (BLAKE2b-512) do arquivo baixado bate com o
//      "hash" anunciado;
//   2. a assinatura (EdDSA, Monocypher) de
//      "colae-atualizacao\n<versao>\n<hash>\n" e valida pra essa
//      chave publica.
//
// Assim, mesmo que alguem invada o servidor ou a pagina de
// downloads, nao consegue fazer o programa instalar outra coisa.
// Usado pelo programa e pela ferramenta colae-assinar.
// ============================================================

// Resumo BLAKE2b-512 de um arquivo inteiro, em hexadecimal (128
// caracteres).
std::string UpdateHashHex(
    const uint8_t* data,
    size_t size
);

// O texto que e assinado.
std::string UpdateSignedMessage(
    const std::string& version,
    const std::string& hashHex
);

// true = assinatura valida pra essa chave publica (hex de 32
// bytes), versao e resumo.
bool UpdateVerify(
    const std::string& publicKeyHex,
    const std::string& version,
    const std::string& hashHex,
    const std::string& signatureHex
);

// Hexadecimal <-> bytes (false se o texto nao tiver exatamente
// "size" bytes validos).
std::string UpdateToHex(
    const uint8_t* data,
    size_t size
);

bool UpdateFromHex(
    const std::string& text,
    uint8_t* out,
    size_t size
);
