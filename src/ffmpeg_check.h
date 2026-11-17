#pragma once

#include <string>

// ============================================================
// CheckFfmpegAvailable
// ============================================================
//
// O client (encoder) e o receiver (decoder) dependem de um
// ffmpeg.exe funcionando - na mesma pasta do programa ou no
// PATH. Se ele faltar ou estiver quebrado, o processo filho
// morre calado e o sintoma e so "tela preta".
//
// Esta checagem roda "ffmpeg -version" na abertura do programa
// e diz, em texto claro, se ha algo errado.
//
// Caso real que motivou isso: copiar o ffmpeg.exe instalado
// pelo Chocolatey leva so um atalho de poucos KB, que nao
// funciona em outra maquina.
// ============================================================

bool CheckFfmpegAvailable(
    std::string& outProblem
);


// Imprime no console a explicacao e como corrigir.
void PrintFfmpegHelp(
    const std::string& problem
);
