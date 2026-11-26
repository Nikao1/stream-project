// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#include "power_mode.h"

#include <windows.h>

#include <iostream>

namespace
{

// Definidos aqui (e nao dos headers do SDK) pra compilar com
// qualquer versao do SDK; valores da documentacao da
// SetProcessInformation / PROCESS_POWER_THROTTLING_STATE.
constexpr int PROCESS_INFO_POWER_THROTTLING = 4; // ProcessPowerThrottling

constexpr ULONG THROTTLING_CURRENT_VERSION = 1;
constexpr ULONG THROTTLING_EXECUTION_SPEED = 0x1;
constexpr ULONG THROTTLING_IGNORE_TIMER_RESOLUTION = 0x4;

struct PowerThrottlingState
{
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
};

using SetProcessInformationFn =
    BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);

} // namespace


void PowerDisableThrottling()
{
    // So existe a partir do Windows 10 1709: procura em tempo
    // de execucao em vez de depender dela.
    auto setProcessInformation =
        reinterpret_cast<SetProcessInformationFn>(
            reinterpret_cast<void*>(
                GetProcAddress(
                    GetModuleHandleW(L"kernel32.dll"),
                    "SetProcessInformation")));

    if (!setProcessInformation)
        return;

    // ControlMask = o que estamos decidindo; StateMask = 0 =
    // "nao frear". Inclui a resolucao do timer: sem isso, em
    // economia, o Windows ignora o timeBeginPeriod(1) e o
    // ritmo de envio dos pacotes (Sleep de 1 ms) desanda.
    PowerThrottlingState state{};

    state.Version = THROTTLING_CURRENT_VERSION;
    state.ControlMask =
        THROTTLING_EXECUTION_SPEED |
        THROTTLING_IGNORE_TIMER_RESOLUTION;
    state.StateMask = 0;

    if (setProcessInformation(
            GetCurrentProcess(),
            PROCESS_INFO_POWER_THROTTLING,
            &state,
            sizeof(state)))
    {
        std::cout
            << "Energia: modo de eficiencia desligado para este "
            << "programa.\n";
    }
}


void PowerSetActive(
    bool active)
{
    static int current = -1;

    int wanted = active ? 1 : 0;

    if (wanted == current)
        return;

    bool first = current < 0;

    current = wanted;

    // Comecando no estado normal: nada a fazer.
    if (first && !active)
        return;

    if (active)
    {
        // Tela acesa e PC acordado enquanto esta thread pedir
        // (ES_CONTINUOUS = vale ate mandarmos parar).
        SetThreadExecutionState(
            ES_CONTINUOUS |
            ES_SYSTEM_REQUIRED |
            ES_DISPLAY_REQUIRED);

        std::cout
            << "Energia: tela e computador mantidos acordados.\n";
    }
    else
    {
        SetThreadExecutionState(
            ES_CONTINUOUS);

        std::cout
            << "Energia: comportamento normal.\n";
    }
}
