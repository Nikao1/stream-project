#pragma once

#include <windows.h>

#include <cstdint>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>

// ============================================================
// EncodedFrame
//
// Um frame H264 completo (access unit) em Annex B, pronto
// para ser fragmentado e enviado pela rede.
//
// Para um keyframe, "data" inclui SPS + PPS + slice IDR.
// Para um frame delta, "data" é só o slice.
// ============================================================

struct EncodedFrame
{
    std::vector<unsigned char> data;
    bool isKeyframe = false;
};

class H264Encoder
{
public:
    H264Encoder();
    ~H264Encoder();

    bool Start(
        uint32_t width,
        uint32_t height,
        uint32_t fps,
        uint32_t bitrateMbps = 8
    );

    bool EncodeFrame(
        const void* bgraData,
        uint32_t dataSize
    );

    // Retorna um frame completo por chamada. Chamar em loop
    // até retornar false para esvaziar a fila (pode haver
    // mais de um frame pronto entre uma chamada e outra).
    bool GetFrame(
        EncodedFrame& outFrame
    );

    void Stop();

    bool IsRunning() const;

private:
    bool StartProcess();

    void ReaderThread();

    // Consome m_nalBuffer, monta access units completos e
    // empilha em m_frameQueue. Chamado com m_outputMutex já
    // travado.
    void ExtractAccessUnits();

    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_fps;
    uint32_t m_bitrateMbps;

    HANDLE m_process;
    HANDLE m_stdinWrite;
    HANDLE m_stdoutRead;

    std::thread m_readerThread;

    std::mutex m_outputMutex;

    // Bytes crus do Annex B ainda não agrupados em access units.
    std::vector<unsigned char> m_nalBuffer;

    // Access unit sendo montado no momento.
    std::vector<unsigned char> m_currentAccessUnit;
    bool m_currentAccessUnitKeyframe;

    // Frames completos, prontos para GetFrame().
    std::deque<EncodedFrame> m_frameQueue;

    std::atomic<bool> m_running;
};