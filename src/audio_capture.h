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
// AudioCapture
//
// Captura o áudio que está tocando no Windows via WASAPI
// *loopback* (não é microfone - é o "monitor" do dispositivo
// de saída padrão, tipo o que o OBS usa pra "audio do
// sistema").
//
// Entrega blocos fixos de PCM float32 estéreo, prontos pra
// entrar no encoder Opus (GetFrame() devolve exatamente
// AUDIO_FRAME_SAMPLES amostras por canal a cada chamada bem
// sucedida).
//
// LIMITAÇÃO CONHECIDA: assume que o dispositivo padrão expõe
// IEEE float. Se a taxa de amostragem nativa do dispositivo
// não for 48kHz, o áudio ainda é enviado nessa taxa (sem
// resample) - na prática a grande maioria dos dispositivos
// Windows modernos já usa 48kHz nativamente, mas isso é uma
// simplificação a revisar se algum dispositivo específico
// soar errado (mais rápido/lento).
// ============================================================

class AudioCapture
{
public:
    AudioCapture();
    ~AudioCapture();

    bool Start();

    void Stop();

    bool IsRunning() const;

    // Devolve um bloco de exatamente AUDIO_FRAME_SAMPLES
    // amostras por canal (estéreo intercalado), se houver um
    // pronto. Chamar em loop até retornar false.
    bool GetFrame(
        std::vector<float>& outSamples
    );

private:
    void CaptureThread();

    IMMDeviceEnumerator* m_deviceEnumerator;
    IMMDevice* m_device;
    IAudioClient* m_audioClient;
    IAudioCaptureClient* m_captureClient;

    WAVEFORMATEX* m_mixFormat;

    std::thread m_thread;

    std::mutex m_mutex;

    // Amostras acumuladas ainda não agrupadas em frames de
    // AUDIO_FRAME_SAMPLES (já convertidas pra estéreo).
    std::vector<float> m_pendingSamples;

    std::deque<std::vector<float>> m_frameQueue;

    std::atomic<bool> m_running;

    bool m_comInitialized;
};
