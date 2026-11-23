#pragma once

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// MIXADOR DO CANAL DE VOZ
// ============================================================
//
// Junta a voz de cada pessoa (48 kHz, estereo, float) num som
// so, a cada 10 ms, com o volume de cada uma.
//
// Cada pessoa tem uma fila propria que absorve a variacao da
// rede ("jitter"): a voz so comeca a tocar com ~60 ms
// guardados, e se a fila esvaziar (pacotes atrasados), espera
// encher de novo em vez de picotar. Se acumular demais (rede
// voltou de uma vez), descarta o mais antigo pra o atraso nao
// crescer.
//
// Nao mexe com placa de som: entrega o resultado por callback
// (o programa manda pro AudioPlayback em modo de baixo atraso).
// ============================================================

class VoiceMixer
{
public:
    // 10 ms de audio misturado (480 quadros estereo = 960 floats).
    using OutputFn =
        std::function<void(const std::vector<float>&)>;

    static constexpr int SAMPLE_RATE = 48000;
    static constexpr int CHANNELS = 2;
    static constexpr int BLOCK_FRAMES = SAMPLE_RATE / 100;

    ~VoiceMixer();

    // Comeca a misturar (thread propria, a cada 10 ms).
    void Start(
        OutputFn output
    );

    void Stop();

    // Audio decodificado de uma pessoa (estereo intercalado).
    void Push(
        const std::string& id,
        const float* samples,
        size_t frames
    );

    void Remove(
        const std::string& id
    );

    // 0.0 a 2.0 (1.0 = normal).
    void SetGain(
        const std::string& id,
        float gain
    );

    // Nivel recente da voz dessa pessoa (0.0 a ~1.0), pra
    // mostrar quem esta falando.
    float GetLevel(
        const std::string& id
    );

    // Mistura UM bloco de 10 ms (a thread chama sozinha; exposto
    // pros testes).
    void MixBlock(
        std::vector<float>& out
    );

    // Nivel do que sai nas caixas de som (0..1), segurado por uns
    // 300 ms (o eco chega atrasado). Pra supressao de eco.
    float OutputLevel() const
    {
        return m_outputLevel;
    }

private:
    struct Source
    {
        std::deque<float> queue;

        // false = esperando encher a fila antes de tocar.
        bool playing = false;

        float gain = 1.0f;

        float level = 0.0f;
    };

    void Loop();

    std::mutex m_mutex;

    std::map<std::string, Source> m_sources;

    // Volume escolhido pra quem ainda nao mandou audio.
    std::map<std::string, float> m_gains;

    OutputFn m_output;

    std::thread m_thread;

    std::atomic<bool> m_running{ false };

    std::atomic<float> m_outputLevel{ 0.0f };
};
