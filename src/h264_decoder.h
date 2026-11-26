// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

class H264Decoder
{
public:
    H264Decoder();

    ~H264Decoder();

    bool Start(
        uint32_t width,
        uint32_t height
    );

    bool WriteData(
        const void* data,
        uint32_t size
    );

    bool GetFrame(
        std::vector<unsigned char>& frame
    );

    void Stop();

    bool IsRunning() const;

    // true (padrao): decodifica pela GPU (D3D11VA).
    // false: decodifica pela CPU - mais pesado, mas funciona em
    // qualquer maquina. Vale a partir do proximo Start().
    void SetHardwareDecode(
        bool enabled)
    {
        m_hardwareDecode = enabled;
    }

private:

    bool StartProcess();

    void ReaderThread();

    bool m_hardwareDecode = true;

    uint32_t m_width;
    uint32_t m_height;

    HANDLE m_process;

    HANDLE m_stdinWrite;

    HANDLE m_stdoutRead;

    std::thread m_readerThread;

    std::mutex m_frameMutex;

    std::vector<unsigned char> m_decoderBuffer;

    std::vector<unsigned char> m_latestFrame;

    std::atomic<bool> m_running;
};