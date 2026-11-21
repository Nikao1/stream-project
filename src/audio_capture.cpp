#include "audio_capture.h"
#include "audio_protocol.h"

#include <mmdeviceapi.h>
#include <audioclient.h>

#define INITGUID
#include <functiondiscoverykeys_devpkey.h>

#include <iostream>

#pragma comment(lib, "ole32.lib")


// ============================================================
// Captura de som de UM processo (Windows 11 / build 20348+)
// ============================================================
//
// Em vez de abrir um dispositivo de saida, pede ao Windows um
// "dispositivo virtual" que contem so o som de um processo (e
// dos que ele abriu). As estruturas abaixo sao copias exatas
// das do Windows SDK (audioclientactivationparams.h) - ficam
// aqui pra o projeto compilar mesmo com um SDK mais antigo.
// ============================================================

namespace
{

const wchar_t PROCESS_LOOPBACK_DEVICE[] =
    L"VAD\\Process_Loopback";

enum : int
{
    ACTIVATION_TYPE_PROCESS_LOOPBACK = 1,
    LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
    LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
};

struct ProcessLoopbackActivationParams
{
    int activationType;

    DWORD targetProcessId;

    int loopbackMode;
};


// IIDs fixos das interfaces envolvidas (iguais aos do SDK).
const IID IID_ActivateCompletionHandler =
    { 0x41d949ab, 0x9862, 0x444a,
      { 0x80, 0xf6, 0xc2, 0x61, 0x33, 0x4d, 0xa5, 0xeb } };

const IID IID_AgileObject =
    { 0x94ea2b94, 0xe9cc, 0x49e0,
      { 0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90 } };


// O Windows avisa por callback quando o dispositivo virtual
// esta pronto. Esta classe so guarda o resultado e sinaliza um
// evento, pra quem pediu poder esperar.
class ActivationHandler :
    public IActivateAudioInterfaceCompletionHandler
{
public:

    ActivationHandler()
        : m_refCount(1),
          m_done(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
    }

    virtual ~ActivationHandler()
    {
        if (m_done)
        {
            CloseHandle(m_done);
        }
    }

    HANDLE DoneEvent() const
    {
        return m_done;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(
        REFIID riid,
        void** object) override
    {
        if (!object)
            return E_POINTER;

        // IAgileObject: avisa ao Windows que este objeto pode
        // ser chamado de qualquer thread (exigido aqui).
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, IID_ActivateCompletionHandler) ||
            IsEqualIID(riid, IID_AgileObject))
        {
            *object =
                static_cast<
                    IActivateAudioInterfaceCompletionHandler*>(
                        this);

            AddRef();

            return S_OK;
        }

        *object = nullptr;

        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return
            static_cast<ULONG>(
                InterlockedIncrement(&m_refCount));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        LONG count =
            InterlockedDecrement(&m_refCount);

        if (count == 0)
        {
            delete this;
        }

        return static_cast<ULONG>(count);
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(
        IActivateAudioInterfaceAsyncOperation*) override
    {
        SetEvent(m_done);

        return S_OK;
    }

private:

    LONG m_refCount;

    HANDLE m_done;
};


using ActivateAudioInterfaceAsyncFn =
    HRESULT (WINAPI*)(
        LPCWSTR,
        REFIID,
        PROPVARIANT*,
        IActivateAudioInterfaceCompletionHandler*,
        IActivateAudioInterfaceAsyncOperation**);


// Pede o IAudioClient que captura so o som do processo.
// Carrega a funcao do Windows em tempo de execucao: em sistemas
// sem suporte ela simplesmente falha, sem impedir o programa de
// abrir.
HRESULT ActivateProcessLoopbackClient(
    DWORD processId,
    bool exclude,
    IAudioClient** outClient)
{
    *outClient = nullptr;

    HMODULE mmdevapi =
        LoadLibraryW(L"mmdevapi.dll");

    if (!mmdevapi)
        return HRESULT_FROM_WIN32(GetLastError());

    ActivateAudioInterfaceAsyncFn activate =
        reinterpret_cast<ActivateAudioInterfaceAsyncFn>(
            reinterpret_cast<void*>(
                GetProcAddress(
                    mmdevapi,
                    "ActivateAudioInterfaceAsync")));

    if (!activate)
        return E_NOTIMPL;

    ProcessLoopbackActivationParams params{};

    params.activationType =
        ACTIVATION_TYPE_PROCESS_LOOPBACK;

    params.targetProcessId =
        processId;

    params.loopbackMode =
        exclude
        ? LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE
        : LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT activationParams{};

    activationParams.vt = VT_BLOB;
    activationParams.blob.cbSize = sizeof(params);
    activationParams.blob.pBlobData =
        reinterpret_cast<BYTE*>(&params);

    ActivationHandler* handler =
        new ActivationHandler();

    IActivateAudioInterfaceAsyncOperation* operation =
        nullptr;

    HRESULT hr =
        activate(
            PROCESS_LOOPBACK_DEVICE,
            __uuidof(IAudioClient),
            &activationParams,
            handler,
            &operation
        );

    if (SUCCEEDED(hr))
    {
        // Normalmente responde em milissegundos.
        if (WaitForSingleObject(
                handler->DoneEvent(),
                5000) != WAIT_OBJECT_0)
        {
            hr = HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        }
        else
        {
            HRESULT activateResult = E_FAIL;

            IUnknown* activated = nullptr;

            hr =
                operation->GetActivateResult(
                    &activateResult,
                    &activated
                );

            if (SUCCEEDED(hr))
            {
                hr = activateResult;
            }

            if (SUCCEEDED(hr) && activated)
            {
                hr =
                    activated->QueryInterface(
                        __uuidof(IAudioClient),
                        reinterpret_cast<void**>(outClient)
                    );
            }

            if (activated)
            {
                activated->Release();
            }
        }
    }

    if (operation)
    {
        operation->Release();
    }

    handler->Release();

    return hr;
}

} // namespace


// ============================================================
// Helper: wstring -> string em UTF-8 (nomes de dispositivo vão
// pra interface e pro log).
// ============================================================

namespace
{
    std::string NarrowString(
        const std::wstring& wide)
    {
        if (wide.empty())
        {
            return std::string();
        }


        int size =
            WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.c_str(),
                -1,
                nullptr,
                0,
                nullptr,
                nullptr
            );


        std::string result(
            size > 0
            ? size - 1
            : 0,
            '\0'
        );


        if (size > 0)
        {
            WideCharToMultiByte(
                CP_UTF8,
                0,
                wide.c_str(),
                -1,
                result.data(),
                size,
                nullptr,
                nullptr
            );
        }


        return result;
    }
}


// ============================================================
// ListDevices
// ============================================================
//
// Devolve os dispositivos de SAÍDA ativos, pra interface
// oferecer a escolha de qual vai ser a fonte do loopback. Útil
// pra testar client e receiver na mesma máquina sem loop de
// feedback: aponte a captura pra um Virtual Audio Cable (ex:
// VB-CABLE) em vez do dispositivo real de saída que o receiver
// usa pra tocar.
// ============================================================

bool AudioCapture::ListDevices(
    std::vector<AudioOutputDevice>& outDevices)
{
    outDevices.clear();

    HRESULT hr =
        CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED
        );


    bool comInitializedHere =
        SUCCEEDED(hr);


    if (FAILED(hr) &&
        hr != RPC_E_CHANGED_MODE)
    {
        std::cout
            << "AudioCapture: CoInitializeEx falhou "
            << "(selecao de dispositivo).\n";

        return false;
    }


    IMMDeviceEnumerator* enumerator =
        nullptr;


    hr =
        CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(
                &enumerator
            )
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao enumerar "
            << "dispositivos. Usando padrao.\n";

        if (comInitializedHere)
            CoUninitialize();

        return false;
    }


    IMMDeviceCollection* collection =
        nullptr;


    hr =
        enumerator->EnumAudioEndpoints(
            eRender,
            DEVICE_STATE_ACTIVE,
            &collection
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao listar "
            << "dispositivos. Usando padrao.\n";

        enumerator->Release();

        if (comInitializedHere)
            CoUninitialize();

        return false;
    }


    UINT count =
        0;


    collection->GetCount(
        &count
    );


    for (UINT i = 0;
         i < count;
         ++i)
    {
        IMMDevice* dev =
            nullptr;


        if (FAILED(
                collection->Item(
                    i,
                    &dev
                )))
        {
            continue;
        }


        LPWSTR id =
            nullptr;


        dev->GetId(
            &id
        );


        std::wstring deviceId =
            id
            ? std::wstring(id)
            : std::wstring();


        if (id)
        {
            CoTaskMemFree(id);
        }


        std::string name =
            "(sem nome)";


        IPropertyStore* store =
            nullptr;


        if (SUCCEEDED(
                dev->OpenPropertyStore(
                    STGM_READ,
                    &store
                )))
        {
            PROPVARIANT varName{};


            if (SUCCEEDED(
                    store->GetValue(
                        PKEY_Device_FriendlyName,
                        &varName
                    )) &&
                varName.pwszVal)
            {
                name =
                    NarrowString(
                        varName.pwszVal
                    );
            }


            PropVariantClear(
                &varName
            );


            store->Release();
        }


        outDevices.push_back(
            { deviceId, name }
        );


        dev->Release();
    }


    collection->Release();

    enumerator->Release();


    if (comInitializedHere)
    {
        CoUninitialize();
    }


    return true;
}


// ============================================================
// Constructor / Destructor
// ============================================================

AudioCapture::AudioCapture()
    : m_deviceEnumerator(nullptr),
      m_device(nullptr),
      m_audioClient(nullptr),
      m_captureClient(nullptr),
      m_mixFormat(nullptr),
      m_running(false),
      m_comInitialized(false)
{
}


AudioCapture::~AudioCapture()
{
    Stop();
}


// ============================================================
// Start
// ============================================================

bool AudioCapture::Start()
{
    if (m_running)
    {
        return true;
    }


    m_running =
        true;


    m_thread =
        std::thread(
            &AudioCapture::CaptureThread,
            this
        );


    return true;
}


// ============================================================
// IsRunning
// ============================================================

bool AudioCapture::IsRunning() const
{
    return m_running;
}


// ============================================================
// GetFrame
// ============================================================

bool AudioCapture::GetFrame(
    std::vector<float>& outSamples)
{
    std::lock_guard<std::mutex> lock(
        m_mutex
    );


    if (m_frameQueue.empty())
    {
        return false;
    }


    outSamples =
        std::move(
            m_frameQueue.front()
        );


    m_frameQueue.pop_front();


    return true;
}


// ============================================================
// CaptureThread
// ============================================================
//
// Roda inteiramente nesta thread: inicialização COM, setup do
// WASAPI, loop de captura, e limpeza no final. COM é "por
// thread", então tudo isso precisa acontecer na mesma thread.
// ============================================================

void AudioCapture::CaptureThread()
{
    // ============================================================
    // COM
    // ============================================================

    HRESULT hr =
        CoInitializeEx(
            nullptr,
            COINIT_MULTITHREADED
        );


    m_comInitialized =
        SUCCEEDED(hr);


    if (FAILED(hr) &&
        hr != RPC_E_CHANGED_MODE)
    {
        std::cout
            << "AudioCapture: CoInitializeEx falhou. HRESULT: "
            << hr
            << "\n";

        m_running =
            false;

        return;
    }


    // ============================================================
    // Device enumerator
    // ============================================================

    m_capturingProcess =
        false;

    m_excludingSelf =
        false;


    // --------------------------------------------------------
    // Som por processo
    //
    // - Janela escolhida: so o som do programa dela.
    // - "Som do computador" no dispositivo padrao: tudo MENOS
    //   este programa. Quem transmite e assiste ao mesmo tempo
    //   (sala) nao devolve pros outros o som que esta ouvindo -
    //   sem isso, o audio voltaria em eco.
    //
    // Se der certo, pula toda a parte de dispositivo abaixo.
    // Se nao der (Windows antigo, processo ja fechado...), cai
    // pro som do dispositivo, como antes.
    // --------------------------------------------------------

    DWORD loopbackProcess =
        m_targetProcessId;

    bool excludeSelf =
        loopbackProcess == 0 &&
        m_selectedDeviceId.empty();

    if (excludeSelf)
    {
        loopbackProcess =
            GetCurrentProcessId();
    }

    if (loopbackProcess != 0)
    {
        hr =
            ActivateProcessLoopbackClient(
                loopbackProcess,
                excludeSelf,
                &m_audioClient
            );

        if (SUCCEEDED(hr) && m_audioClient)
        {
            // Aqui nao ha "mix format" pra consultar: o formato
            // e o que pedirmos, e o Windows converte. Pedimos
            // exatamente o que o resto do programa usa (48 kHz,
            // estereo, float) - sem conversao nossa depois.
            WAVEFORMATEX* format =
                static_cast<WAVEFORMATEX*>(
                    CoTaskMemAlloc(sizeof(WAVEFORMATEX)));

            if (format)
            {
                ZeroMemory(format, sizeof(WAVEFORMATEX));

                format->wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
                format->nChannels = AUDIO_CHANNELS;
                format->nSamplesPerSec = AUDIO_SAMPLE_RATE;
                format->wBitsPerSample = 32;
                format->nBlockAlign =
                    static_cast<WORD>(
                        format->nChannels *
                        format->wBitsPerSample / 8);
                format->nAvgBytesPerSec =
                    format->nSamplesPerSec *
                    format->nBlockAlign;

                m_mixFormat = format;
            }

            m_captureEvent =
                CreateEventW(nullptr, FALSE, FALSE, nullptr);

            constexpr REFERENCE_TIME processBufferDuration =
                200 * 10000; // 200ms

            hr =
                (m_mixFormat && m_captureEvent)
                ? m_audioClient->Initialize(
                      AUDCLNT_SHAREMODE_SHARED,
                      AUDCLNT_STREAMFLAGS_LOOPBACK |
                      AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                      AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                      processBufferDuration,
                      0,
                      m_mixFormat,
                      nullptr)
                : E_OUTOFMEMORY;

            if (SUCCEEDED(hr))
            {
                hr =
                    m_audioClient->SetEventHandle(
                        m_captureEvent);
            }

            if (SUCCEEDED(hr))
            {
                m_capturingProcess =
                    !excludeSelf;

                m_excludingSelf =
                    excludeSelf;

                if (excludeSelf)
                {
                    std::cout
                        << "AudioCapture: capturando o som do "
                        << "computador, menos o deste programa.\n";
                }
                else
                {
                    std::cout
                        << "AudioCapture: capturando so o som do "
                        << "processo "
                        << m_targetProcessId
                        << ".\n";
                }

                goto client_ready;
            }
        }

        std::cout
            << "AudioCapture: nao deu pra capturar o som por "
            << "processo (HRESULT "
            << hr
            << ") - usando o som do dispositivo.\n";

        if (m_audioClient)
        {
            m_audioClient->Release();
            m_audioClient = nullptr;
        }

        if (m_mixFormat)
        {
            CoTaskMemFree(m_mixFormat);
            m_mixFormat = nullptr;
        }

        if (m_captureEvent)
        {
            CloseHandle(m_captureEvent);
            m_captureEvent = nullptr;
        }
    }


    hr =
        CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(
                &m_deviceEnumerator
            )
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao criar "
            << "IMMDeviceEnumerator. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    // ============================================================
    // Dispositivo: o selecionado manualmente (se houver), ou
    // o de saida padrao do Windows (o que vai em loopback)
    // ============================================================

    if (!m_selectedDeviceId.empty())
    {
        hr =
            m_deviceEnumerator->GetDevice(
                m_selectedDeviceId.c_str(),
                &m_device
            );


        if (FAILED(hr))
        {
            std::cout
                << "AudioCapture: falha ao abrir "
                << "dispositivo selecionado - caindo "
                << "para o padrao. HRESULT: "
                << hr
                << "\n";
        }
    }


    if (!m_device)
    {
        hr =
            m_deviceEnumerator->GetDefaultAudioEndpoint(
                eRender,
                eConsole,
                &m_device
            );
    }


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao obter dispositivo "
            << "de audio. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    // ============================================================
    // IAudioClient
    // ============================================================

    hr =
        m_device->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(
                &m_audioClient
            )
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao ativar IAudioClient. "
            << "HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    hr =
        m_audioClient->GetMixFormat(
            &m_mixFormat
        );


    if (FAILED(hr) ||
        !m_mixFormat)
    {
        std::cout
            << "AudioCapture: falha ao obter mix format. "
            << "HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    std::cout
        << "AudioCapture: dispositivo padrao - "
        << m_mixFormat->nSamplesPerSec
        << " Hz, "
        << m_mixFormat->nChannels
        << " canal(is), "
        << m_mixFormat->wBitsPerSample
        << " bits.\n";


    if (m_mixFormat->nSamplesPerSec !=
        AUDIO_SAMPLE_RATE)
    {
        std::cout
            << "AudioCapture: AVISO - dispositivo nao esta em "
            << AUDIO_SAMPLE_RATE
            << " Hz. Audio sera enviado sem resample "
            << "(pode soar errado).\n";
    }


    // ============================================================
    // Inicializar em modo loopback
    //
    // Buffer de 200ms - folga suficiente pro polling, sem
    // acumular atraso perceptível.
    // ============================================================

    {
        constexpr REFERENCE_TIME bufferDuration =
            200 *
            10000; // 200ms em unidades de 100ns


        hr =
            m_audioClient->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_LOOPBACK,
                bufferDuration,
                0,
                m_mixFormat,
                nullptr
            );
    }


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao inicializar "
            << "IAudioClient (loopback). HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


client_ready:

    hr =
        m_audioClient->GetService(
            __uuidof(IAudioCaptureClient),
            reinterpret_cast<void**>(
                &m_captureClient
            )
        );


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao obter "
            << "IAudioCaptureClient. HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    hr =
        m_audioClient->Start();


    if (FAILED(hr))
    {
        std::cout
            << "AudioCapture: falha ao iniciar captura. "
            << "HRESULT: "
            << hr
            << "\n";

        goto cleanup;
    }


    std::cout
        << "AudioCapture: captura de audio iniciada ("
        << (m_capturingProcess
            ? "so a janela escolhida"
            : m_excludingSelf
            ? "som do computador, sem o deste programa"
            : "som do dispositivo")
        << ").\n";


    // ============================================================
    // LOOP DE CAPTURA
    // ============================================================

    while (m_running)
    {
        UINT32 packetLength =
            0;


        hr =
            m_captureClient->GetNextPacketSize(
                &packetLength
            );


        if (FAILED(hr))
        {
            break;
        }


        while (packetLength != 0)
        {
            BYTE* data =
                nullptr;

            UINT32 numFrames =
                0;

            DWORD flags =
                0;


            hr =
                m_captureClient->GetBuffer(
                    &data,
                    &numFrames,
                    &flags,
                    nullptr,
                    nullptr
                );


            if (FAILED(hr))
            {
                break;
            }


            // ================================================
            // Converter para estereo float e acumular
            // ================================================

            {
                std::lock_guard<std::mutex> lock(
                    m_mutex
                );


                bool silent =
                    (flags &
                     AUDCLNT_BUFFERFLAGS_SILENT) != 0;


                const float* samples =
                    reinterpret_cast<const float*>(
                        data
                    );


                uint32_t srcChannels =
                    m_mixFormat->nChannels;


                for (UINT32 i = 0;
                     i < numFrames;
                     ++i)
                {
                    float left =
                        0.0f;

                    float right =
                        0.0f;


                    if (!silent &&
                        samples)
                    {
                        left =
                            samples[
                                i * srcChannels + 0];


                        right =
                            (srcChannels >= 2)
                            ? samples[
                                  i * srcChannels + 1]
                            : left;
                    }


                    m_pendingSamples.push_back(
                        left
                    );

                    m_pendingSamples.push_back(
                        right
                    );
                }


                // ------------------------------------------------
                // Extrair frames completos de AUDIO_FRAME_SAMPLES
                // ------------------------------------------------

                constexpr size_t samplesPerFrame =
                    AUDIO_FRAME_SAMPLES *
                    AUDIO_CHANNELS;


                while (m_pendingSamples.size() >=
                       samplesPerFrame)
                {
                    std::vector<float> frame(
                        m_pendingSamples.begin(),
                        m_pendingSamples.begin() +
                            samplesPerFrame
                    );


                    m_pendingSamples.erase(
                        m_pendingSamples.begin(),
                        m_pendingSamples.begin() +
                            samplesPerFrame
                    );


                    m_frameQueue.push_back(
                        std::move(frame)
                    );


                    // --------------------------------------------
                    // Limite de seguranca - nao deixa acumular
                    // atraso se o envio nao acompanhar.
                    // --------------------------------------------

                    constexpr size_t MAX_QUEUED_AUDIO_FRAMES =
                        10;


                    while (m_frameQueue.size() >
                           MAX_QUEUED_AUDIO_FRAMES)
                    {
                        m_frameQueue.pop_front();
                    }
                }
            }


            m_captureClient->ReleaseBuffer(
                numFrames
            );


            hr =
                m_captureClient->GetNextPacketSize(
                    &packetLength
                );


            if (FAILED(hr))
            {
                break;
            }
        }


        // Por processo: o Windows avisa quando ha som novo.
        // Por dispositivo: consulta a cada 5 ms, como antes.
        if (m_captureEvent)
        {
            WaitForSingleObject(
                m_captureEvent,
                50
            );
        }
        else
        {
            Sleep(5);
        }
    }


    m_audioClient->Stop();


cleanup:

    if (m_captureClient)
    {
        m_captureClient->Release();
        m_captureClient = nullptr;
    }

    if (m_audioClient)
    {
        m_audioClient->Release();
        m_audioClient = nullptr;
    }

    if (m_device)
    {
        m_device->Release();
        m_device = nullptr;
    }

    if (m_deviceEnumerator)
    {
        m_deviceEnumerator->Release();
        m_deviceEnumerator = nullptr;
    }

    if (m_mixFormat)
    {
        CoTaskMemFree(
            m_mixFormat
        );

        m_mixFormat = nullptr;
    }

    if (m_captureEvent)
    {
        CloseHandle(m_captureEvent);
        m_captureEvent = nullptr;
    }

    m_capturingProcess =
        false;

    if (m_comInitialized)
    {
        CoUninitialize();

        m_comInitialized = false;
    }


    m_running =
        false;
}


// ============================================================
// Stop
// ============================================================

void AudioCapture::Stop()
{
    m_running =
        false;


    if (m_thread.joinable())
    {
        m_thread.join();
    }


    std::lock_guard<std::mutex> lock(
        m_mutex
    );


    m_pendingSamples.clear();

    m_frameQueue.clear();
}