#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// MICROFONE DO CANAL DE VOZ
// ============================================================
//
// Captura a voz em 16 kHz mono (qualidade de chamada) e entrega
// em blocos de 20 ms (320 amostras).
//
// Primeiro tenta o "Voice Capture DSP" do proprio Windows, que
// ja faz o que uma chamada precisa:
//   - cancelamento de eco: tira do microfone o que sai das
//     caixas de som (a voz dos outros e o som da transmissao),
//     pra quem esta sem fone nao devolver tudo em eco;
//   - reducao de ruido e ganho automatico.
// Usa o microfone escolhido (ou o padrao do Windows) e a saida
// de som padrao (onde o programa toca o som).
//
// Se nao der (driver estranho, Windows sem o componente), cai
// pro microfone simples - ai a recomendacao e usar fone.
// ============================================================

class VoiceCapture
{
public:
    // 320 amostras float (-1..1), 16 kHz mono.
    using FrameFn =
        std::function<void(const float*, size_t)>;

    static constexpr int SAMPLE_RATE = 16000;
    static constexpr int FRAME_SAMPLES = SAMPLE_RATE / 50;

    ~VoiceCapture();

    struct Device
    {
        std::wstring id;   // identificador do Windows
        std::string name;  // nome amigavel (UTF-8)
    };

    // Microfones ativos agora (pode demorar alguns ms - nao
    // chamar a cada frame).
    static std::vector<Device> ListMicrophones();

    // Microfone a usar no proximo Start (vazio = padrao do
    // Windows).
    void SetDevice(
        const std::wstring& deviceId)
    {
        m_deviceId = deviceId;
    }

    // Comeca a capturar (thread propria). O resultado de abrir o
    // microfone so se sabe um pouco depois: ver HasError().
    void Start(
        FrameFn onFrame
    );

    void Stop();

    // Cancelamento de eco do Windows funcionando.
    bool EchoCancellation() const
    {
        return m_echoCancellation;
    }

    // Nivel recente do microfone (0..1) - pra mostrar que ele
    // esta captando.
    float Level() const
    {
        return m_level;
    }

    // Nao deu pra abrir o microfone (explicacao pra tela).
    bool HasError() const
    {
        return m_hasError;
    }

    std::string ErrorText() const;

private:
    void Run();

    bool RunEchoCancelled();

    bool RunPlain();

    void Deliver(
        const float* samples,
        size_t count
    );

    FrameFn m_onFrame;

    std::wstring m_deviceId;

    std::thread m_thread;

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_echoCancellation{ false };
    std::atomic<bool> m_hasError{ false };
    std::atomic<float> m_level{ 0.0f };

    // 0 = generico; 1 = sem permissao (privacidade do Windows);
    // 2 = nenhum microfone.
    std::atomic<int> m_errorKind{ 0 };

    std::vector<float> m_pending;
};
