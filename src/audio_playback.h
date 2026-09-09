#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <cstdint>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <atomic>

// ============================================================
// AudioPlayback
//
// Toca áudio PCM float32 estéreo via WASAPI, no dispositivo de
// saída padrão do Windows.
//
// Em modo compartilhado (shared mode), o Windows Audio Engine
// converte automaticamente de 48kHz/estéreo/float pra o que o
// dispositivo físico realmente precisar - diferente da
// captura em loopback, aqui não estamos presos ao mix format
// nativo do dispositivo.
// ============================================================

class AudioPlayback
{
public:
    AudioPlayback();
    ~AudioPlayback();

    bool Start();

    void Stop();

    bool IsRunning() const;

    // Enfileira um bloco de PCM float32 estéreo intercalado
    // pra tocar (tipicamente AUDIO_FRAME_SAMPLES amostras por
    // canal, vindo do AudioDecoder).
    void PushSamples(
        const std::vector<float>& samples
    );

private:
    void RenderThread();

    IMMDeviceEnumerator* m_deviceEnumerator;
    IMMDevice* m_device;
    IAudioClient* m_audioClient;
    IAudioRenderClient* m_renderClient;

    std::thread m_thread;

    std::mutex m_mutex;

    // Amostras estéreo intercaladas aguardando reprodução.
    std::deque<float> m_pcmQueue;

    std::atomic<bool> m_running;

    bool m_comInitialized;
};
