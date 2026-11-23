#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// SONS DE AVISO
// ============================================================
//
// Pra quem esta no jogo (sem ver o programa) saber o que
// aconteceu. Gerados aqui mesmo (tons curtos com fade) - nada
// de arquivos de audio: 0 KB no programa e nenhum som copiado
// de outro app.
//
// Regra: subindo = ligou / entrou; descendo = desligou / saiu.
// ============================================================

enum class UiSound
{
    MicOn,        // duas notas subindo
    MicOff,       // as mesmas, descendo
    VoiceJoin,    // tres notas subindo
    VoiceLeave,   // tres notas descendo
    StreamStart,  // "ding" de sino
    ChatMessage   // "pop" curtinho
};

// Amostras do som: 48 kHz, estereo intercalado, float -1..1
// (volume cheio - o player aplica o volume escolhido).
std::vector<float> UiSoundSamples(
    UiSound sound
);


// ------------------------------------------------------------
// Toca os sons numa saida de som propria (a mesma de "Ouvir
// em"). Nao entra na transmissao de quem compartilha a tela (o
// programa exclui o proprio som da captura).
// ------------------------------------------------------------

class AudioPlayback;

class UiSoundPlayer
{
public:
    UiSoundPlayer();
    ~UiSoundPlayer();

    // deviceId vazio = saida padrao. Pode chamar de novo pra
    // trocar a saida.
    void Start(
        const std::wstring& deviceId
    );

    // Igual, numa thread (trocar a saida sem travar a janela).
    void StartAsync(
        const std::wstring& deviceId
    );

    void Stop();

    bool IsRunning() const
    {
        return m_running;
    }

    // 0..1
    void SetVolume(
        float volume)
    {
        m_volume = volume;
    }

    void SetEnabled(
        bool enabled)
    {
        m_enabled = enabled;
    }

    bool IsEnabled() const
    {
        return m_enabled;
    }

    // Mistura com o que ja estiver tocando.
    void Play(
        UiSound sound
    );

private:
    void Loop();

    // Com m_lifecycle travado.
    void StartLocked(
        const std::wstring& deviceId
    );

    void StopLocked();

    std::mutex m_lifecycle;

    std::thread m_starter;

    std::mutex m_mutex;

    // O que falta tocar (estereo intercalado).
    std::vector<float> m_pending;
    size_t m_position = 0;

    AudioPlayback* m_playback = nullptr;

    std::thread m_thread;

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_enabled{ true };
    std::atomic<float> m_volume{ 0.6f };
};
