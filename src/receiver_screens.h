#pragma once

#include <cstddef>
#include <string>

// ============================================================
// TELAS DO RECEIVER
// ============================================================
//
// So desenho (Dear ImGui puro): cada funcao recebe os dados que
// precisa mostrar e devolve, em ReceiverScreenActions, o que o
// usuario pediu. Quem executa as acoes (conectar, abrir o
// navegador...) e o receiver.cpp.
//
// Separado assim, da pra desenhar as telas fora do programa -
// e o que permite gerar uma previa delas sem rodar no Windows.
// ============================================================

struct ReceiverScreenActions
{
    bool connect = false;

    bool openDownloadPage = false;

    bool recheckFfmpeg = false;
};


// ffmpeg.exe ausente ou quebrado: explica e ensina a resolver.
void DrawFfmpegProblemScreen(
    const std::string& problem,
    ReceiverScreenActions& actions
);


// Tela inicial: campo pro codigo da sessao (ou convite / IP).
// busy = uma tentativa de conexao esta em andamento.
void DrawConnectScreen(
    char* input,
    size_t inputSize,
    bool busy,
    const std::string& status,
    const std::string& error,
    ReceiverScreenActions& actions
);


// Conectado, esperando o primeiro frame. reply = texto da
// RESPOSTA do convite manual (vazio = nao mostrar).
void DrawWaitingScreen(
    const std::string& reply
);


// ------------------------------------------------------------
// Painel sobre o video (volume + estatisticas)
// ------------------------------------------------------------

struct StreamOverlayModel
{
    // 0.0 a 1.0; a tela altera direto.
    float* volume = nullptr;

    bool* muted = nullptr;

    int fps = 0;

    unsigned width = 0;
    unsigned height = 0;

    // Frames perdidos na rede e descartados por atraso (ver
    // g_statLostNetwork / g_statLate no receiver).
    unsigned long long lostNetwork = 0;
    unsigned long long late = 0;

    // 0 = invisivel, 1 = totalmente visivel (o painel some
    // aos poucos quando o mouse fica parado).
    float alpha = 1.0f;
};

// Devolve true se o mouse esta em cima do painel (pra ele nao
// sumir enquanto a pessoa mexe no volume).
bool DrawStreamOverlay(
    StreamOverlayModel& model
);
