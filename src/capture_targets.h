#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

// ============================================================
// O QUE DA PRA COMPARTILHAR (monitores e janelas)
// ============================================================
//
// Lista as telas e as janelas abertas pro seletor da interface
// - o mesmo papel da caixa "escolher tela" do Windows, mas
// desenhada pelo programa. Alem do visual, isso da ao programa
// a janela escolhida (o seletor do Windows nao informa), que e
// o que permite capturar o som so dela.
//
// So Win32 puro - quem transforma a escolha numa captura e o
// main.cpp (Windows Graphics Capture).
// ============================================================

enum class CaptureTargetKind
{
    Monitor,
    Window
};


struct CaptureTarget
{
    CaptureTargetKind kind = CaptureTargetKind::Monitor;

    HMONITOR monitor = nullptr;

    HWND window = nullptr;

    // Texto principal (UTF-8): "Tela 1" / titulo da janela.
    std::string title;

    // Texto secundario (UTF-8): "1920 x 1080 · principal" /
    // nome do programa.
    std::string detail;

    // Processo que toca o som da janela (0 em monitores). Pra
    // apps da Microsoft Store e o processo do app, nao o do
    // "hospedeiro" da janela.
    DWORD processId = 0;

    bool minimized = false;
};


// Monitores, do principal pros demais.
std::vector<CaptureTarget> ListCaptureMonitors();

// Janelas que aparecem no Alt+Tab, na ordem em que estao na
// tela (a mais recente primeiro). "exclude" = janela a deixar
// de fora (a do proprio programa).
std::vector<CaptureTarget> ListCaptureWindows(
    HWND exclude
);

// true se a janela ainda existe (pode ter sido fechada entre a
// listagem e o clique).
bool CaptureTargetStillValid(
    const CaptureTarget& target
);


// ------------------------------------------------------------
// Som de uma janela so
// ------------------------------------------------------------

// true se este Windows sabe capturar o som de um processo
// especifico (Windows 11 / build 20348 ou mais novo).
bool SupportsPerWindowAudio();
