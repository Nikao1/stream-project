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
// AudioCapture
//
// Captura o áudio que está tocando no Windows via WASAPI
// *loopback* (não é microfone - é o "monitor" do dispositivo
// de saída padrão, tipo o que o OBS usa pra "audio do
// sistema").
//
// Por padrão usa o dispositivo de saída PADRÃO do Windows.
// Para testes na mesma máquina (client + receiver no mesmo
// PC), isso causa um loop de feedback, já que o receiver
// também toca no dispositivo padrão. Use
// ListDevices() + SelectDevice() antes de Start() pra
// escolher outro dispositivo (ex: um Virtual Audio Cable),
// separando a fonte capturada da saída real dos alto-falantes.
//
// LIMITAÇÃO CONHECIDA: assume que o dispositivo escolhido
// expõe IEEE float. Se a taxa de amostragem nativa do
// dispositivo não for 48kHz, o áudio ainda é enviado nessa
// taxa (sem resample) - na prática a grande maioria dos
// dispositivos Windows modernos já usa 48kHz nativamente, mas
// isso é uma simplificação a revisar se algum dispositivo
// específico soar errado (mais rápido/lento).
// ============================================================

struct AudioOutputDevice
{
    std::wstring id;

    std::string name;
};


class AudioCapture
{
public:
    AudioCapture();
    ~AudioCapture();

    // Lista os dispositivos de SAÍDA ativos (nome em UTF-8),
    // pra interface oferecer a escolha da fonte do loopback.
    static bool ListDevices(
        std::vector<AudioOutputDevice>& outDevices
    );

    // Escolhe a fonte do loopback pelo id devolvido por
    // ListDevices. Vazio = dispositivo padrão do Windows.
    // Chamar antes de Start().
    void SelectDevice(
        const std::wstring& deviceId)
    {
        m_selectedDeviceId = deviceId;
    }

    // Captura so o som de um processo (e dos processos que ele
    // abriu - ex.: as abas de um navegador), em vez do som do
    // dispositivo inteiro. 0 = volta ao dispositivo. Exige
    // Windows 11 / build 20348+ (ver SupportsPerWindowAudio);
    // se nao der, a captura cai pro dispositivo sozinha.
    // Vale a partir do proximo Start().
    void SelectProcess(
        DWORD processId)
    {
        m_targetProcessId = processId;
    }

    // true se a captura em andamento e de um processo so (a
    // por processo pode ter falhado e caido pro dispositivo).
    bool IsCapturingProcess() const
    {
        return m_capturingProcess;
    }

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

    // Vazio = usar o dispositivo de saída padrão do Windows.
    std::wstring m_selectedDeviceId;

    // 0 = som do dispositivo; outro valor = so deste processo.
    std::atomic<DWORD> m_targetProcessId{ 0 };

    std::atomic<bool> m_capturingProcess{ false };

    // true = som do computador sem o deste programa (ver Start).
    std::atomic<bool> m_excludingSelf{ false };

    // Captura por processo e avisada por evento (a do
    // dispositivo e consultada em intervalos).
    HANDLE m_captureEvent = nullptr;

    std::thread m_thread;

    std::mutex m_mutex;

    // Amostras acumuladas ainda não agrupadas em frames de
    // AUDIO_FRAME_SAMPLES (já convertidas pra estéreo).
    std::vector<float> m_pendingSamples;

    std::deque<std::vector<float>> m_frameQueue;

    std::atomic<bool> m_running;

    bool m_comInitialized;
};