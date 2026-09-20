#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>

// ============================================================
// GpuVendor
//
// Detectado via DXGI em Start() - decide qual encoder de
// hardware usar (NVENC/AMF/QSV), com fallback pra libx264
// (software) se nenhum for reconhecido ou falhar ao iniciar.
// ============================================================

enum class GpuVendor
{
    Nvidia,
    Amd,
    Intel,
    Unknown
};

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
        uint32_t bitrateMbps = 8,
        uint32_t outputHeight = 0 // 0 = nao reescalar (nativo)
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

    // Tamanho REAL de saída do encoder (depois de aplicar a
    // escala, se houver) - use isso pro header de rede, não o
    // tamanho de entrada.
    uint32_t GetOutputWidth() const
    {
        return m_actualOutputWidth;
    }

    uint32_t GetOutputHeight() const
    {
        return m_actualOutputHeight;
    }

    void Stop();

    bool IsRunning() const;

private:
    bool StartProcess(
        bool useSoftwareFallback = false
    );

    std::string BuildFfmpegCommand(
        bool useSoftwareFallback
    );

    void ReaderThread();

    // Consome m_nalBuffer, monta access units completos e
    // empilha em m_frameQueue. Chamado com m_outputMutex já
    // travado.
    void ExtractAccessUnits();

    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_fps;
    uint32_t m_bitrateMbps;

    uint32_t m_outputHeight; // 0 = nativo, sem reescalar

    uint32_t m_actualOutputWidth;
    uint32_t m_actualOutputHeight;

    GpuVendor m_gpuVendor;

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