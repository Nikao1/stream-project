// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#pragma once

// ============================================================
// RELATORIO DE CRASH
// ============================================================
//
// Quando o programa fecha sozinho por um erro, normalmente nao
// sobra nenhuma pista - e fica impossivel corrigir algo que so
// acontece no computador de outra pessoa.
//
// Com isto instalado, um crash deixa dois rastros na pasta do
// programa:
//
//   - uma linha no log (ex.: stream-receiver.log) com o tipo
//     do erro e o ponto do programa onde aconteceu;
//   - um arquivo .dmp com o estado do programa naquele
//     instante. Aberto no Visual Studio (junto com o .pdb da
//     mesma compilacao), mostra a linha exata do codigo.
//
// Chamar uma vez, logo no inicio do programa, depois de
// configurar o log.
// ============================================================

// dumpFileName: nome do arquivo .dmp a criar na pasta atual
// (ex.: L"stream-receiver-crash.dmp").
void InstallCrashReporter(
    const wchar_t* dumpFileName
);
