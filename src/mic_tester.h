#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "audio_playback.h"
#include "noise_suppressor.h"
#include "voice_capture.h"

// ============================================================
// TESTE DO MICROFONE (fora da voz)
// ============================================================
//
// Nas configuracoes: abre o microfone escolhido (com a mesma
// reducao de ruido da voz), mostra o nivel e, se pedido, toca a
// propria voz na saida escolhida ("ouvir minha voz").
//
// Abrir/fechar microfone e saida leva alguns centesimos de
// segundo: tudo acontece numa thread propria (a janela nao
// trava). Nada roda quando o teste esta desligado.
// ============================================================

class MicTester
{
public:
    MicTester() = default;
    ~MicTester();

    // Liga/desliga o teste com estes dispositivos (vazio =
    // padrao). Pode chamar a cada frame: so age quando muda.
    void Set(
        bool running,
        const std::wstring& microphone,
        const std::wstring& speaker,
        bool loopback,
        bool noiseSuppression
    );

    bool IsRunning() const
    {
        return m_capturing;
    }

    // Nivel do microfone (0..1), depois da reducao de ruido.
    float Level() const
    {
        return m_level;
    }

    // Probabilidade de voz (-1 = sem reducao de ruido).
    float VoiceActivity() const
    {
        return m_voiceActivity;
    }

private:
    struct Wanted
    {
        bool running = false;
        std::wstring microphone;
        std::wstring speaker;
        bool loopback = false;

        bool operator==(const Wanted& other) const
        {
            return running == other.running &&
                   microphone == other.microphone &&
                   speaker == other.speaker &&
                   loopback == other.loopback;
        }
    };

    void Loop();

    void OnFrame(
        const float* samples,
        size_t count
    );

    void StopAll();

    std::mutex m_mutex;
    std::condition_variable m_cv;

    Wanted m_wanted;
    Wanted m_applied;

    bool m_dirty = false;
    bool m_quit = false;

    std::thread m_thread;

    // So a thread do teste mexe nestes.
    std::unique_ptr<VoiceCapture> m_capture;
    std::unique_ptr<AudioPlayback> m_playback;
    std::unique_ptr<NoiseSuppressor> m_denoiser;

    std::atomic<bool> m_capturing{ false };
    std::atomic<bool> m_loopback{ false };
    std::atomic<bool> m_noiseSuppression{ true };

public:
    void SetNoiseStrength(
        int strength)
    {
        m_noiseStrength = strength;
    }

private:
    std::atomic<int> m_noiseStrength{ 1 };
    std::atomic<float> m_level{ 0.0f };
    std::atomic<float> m_voiceActivity{ -1.0f };

    // Ultima amostra do bloco anterior (interpolacao).
    float m_lastSample = 0.0f;
};
