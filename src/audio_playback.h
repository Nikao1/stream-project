// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>

#include <cstdint>
#include <string>
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

    // Volume de 0.0 a 1.0 (posicao do controle na tela) e
    // mudo. Podem ser chamados de qualquer thread, a qualquer
    // momento.
    void SetVolume(
        float volume
    );

    void SetMuted(
        bool muted)
    {
        m_muted = muted;
    }

    // Modo de baixo atraso (voz): ~20 ms na placa de som em vez
    // de ~200 ms, e no maximo ~120 ms na fila. Chamar antes de
    // Start().
    void SetLowLatency(
        bool lowLatency)
    {
        m_lowLatency = lowLatency;
    }

    // Saida de som (id do Windows; vazio = padrao). Chamar antes
    // de Start(). Se o dispositivo sumiu, usa o padrao.
    void SetDevice(
        const std::wstring& deviceId)
    {
        m_deviceId = deviceId;
    }

private:
    void RenderThread();

    std::wstring m_deviceId;

    IMMDeviceEnumerator* m_deviceEnumerator;
    IMMDevice* m_device;
    IAudioClient* m_audioClient;
    IAudioRenderClient* m_renderClient;

    std::thread m_thread;

    std::mutex m_mutex;

    // Amostras estéreo intercaladas aguardando reprodução.
    std::deque<float> m_pcmQueue;

    std::atomic<bool> m_running;

    // Ganho aplicado em cada amostra (ver SetVolume).
    std::atomic<float> m_gain{ 1.0f };

    std::atomic<bool> m_muted{ false };

    bool m_comInitialized;

    bool m_lowLatency = false;
};
