// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#pragma once

// ============================================================
// CHAVE PUBLICA DAS ATUALIZACOES
// ============================================================
//
// So o instalador assinado com a chave privada correspondente e
// instalado pela atualizacao automatica (ver update_signature.h).
//
// Para gerar o par de chaves (uma vez so):
//
//     colae-assinar gerar-chave C:\caminho\seguro\colae.chave
//
// e cole aqui a chave PUBLICA que ele mostrar. A privada fica so
// no seu computador (nunca no repositorio).
//
// Vazia = sem atualizacao automatica: o programa so avisa que tem
// versao nova e abre a pagina de downloads.
// ============================================================

constexpr const char* COLAE_UPDATE_PUBLIC_KEY = "";
