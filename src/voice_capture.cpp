#include "voice_capture.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mediaobj.h>
#include <propsys.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

// ============================================================
// Identificadores do Voice Capture DSP (wmcodecdsp.h). Escritos
// aqui pra nao depender de bibliotecas extras do SDK (e
// compilar igual em qualquer versao dele).
// ============================================================

namespace
{

const CLSID CLSID_VoiceCaptureDsp =
    { 0x745057c7, 0xf353, 0x4f2d,
      { 0xa7, 0xee, 0x58, 0x43, 0x44, 0x77, 0x73, 0x0e } };

const IID IID_MediaObject =
    { 0xd8ad0f58, 0x5494, 0x4102,
      { 0x97, 0xc5, 0xec, 0x79, 0x8e, 0x59, 0xbc, 0xf4 } };

const IID IID_MediaBuffer =
    { 0x59eff8b9, 0x938c, 0x4a26,
      { 0x82, 0xf2, 0x95, 0xcb, 0x84, 0xcd, 0xc8, 0x37 } };

const IID IID_PropertyStoreLocal =
    { 0x886d8eeb, 0x8cf2, 0x4446,
      { 0x8d, 0x02, 0xcd, 0xba, 0x1d, 0xbd, 0xcf, 0x99 } };

const GUID MEDIATYPE_AudioLocal =
    { 0x73647561, 0x0000, 0x0010,
      { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

const GUID MEDIASUBTYPE_PcmLocal =
    { 0x00000001, 0x0000, 0x0010,
      { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

const GUID FORMAT_WaveFormatExLocal =
    { 0x05589f81, 0xc356, 0x11ce,
      { 0xbf, 0x01, 0x00, 0xaa, 0x00, 0x55, 0x59, 0x5a } };

// MFPKEY_WMAAECMA_SYSTEM_MODE ({6f52c567-...}, PID_FIRST_USABLE+0)
const PROPERTYKEY PKEY_AecSystemMode =
    { { 0x6f52c567, 0x0360, 0x4bd2,
        { 0x96, 0x17, 0xcc, 0xbf, 0x14, 0x21, 0xc9, 0x39 } },
      2 };

// MFPKEY_WMAAECMA_DEVICE_INDEXES (mesmo GUID, PID_FIRST_USABLE+2):
// (indice da saida << 16) | indice do microfone, na ordem do
// EnumAudioEndpoints(..., DEVICE_STATE_ACTIVE).
const PROPERTYKEY PKEY_AecDeviceIndexes =
    { { 0x6f52c567, 0x0360, 0x4bd2,
        { 0x96, 0x17, 0xcc, 0xbf, 0x14, 0x21, 0xc9, 0x39 } },
      4 };

// PKEY_Device_FriendlyName (functiondiscoverykeys_devpkey.h)
const PROPERTYKEY PKEY_DeviceFriendlyName =
    { { 0xa45c254e, 0xdf1c, 0x4efd,
        { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } },
      14 };

std::string WideToUtf8Local(
    const std::wstring& text)
{
    if (text.empty())
        return std::string();

    int size =
        WideCharToMultiByte(
            CP_UTF8, 0, text.c_str(),
            static_cast<int>(text.size()),
            nullptr, 0, nullptr, nullptr);

    std::string out(size > 0 ? size : 0, '\0');

    if (size > 0)
    {
        WideCharToMultiByte(
            CP_UTF8, 0, text.c_str(),
            static_cast<int>(text.size()),
            out.data(), size, nullptr, nullptr);
    }

    return out;
}

// Posicao de um dispositivo na lista de ativos (-1 = nao achou).
// deviceId vazio = o padrao do Windows desse tipo.
int DeviceIndex(
    IMMDeviceEnumerator* enumerator,
    EDataFlow flow,
    const std::wstring& deviceId)
{
    std::wstring wanted = deviceId;

    if (wanted.empty())
    {
        IMMDevice* device = nullptr;

        if (FAILED(enumerator->GetDefaultAudioEndpoint(
                flow, eConsole, &device)))
        {
            return -1;
        }

        LPWSTR id = nullptr;

        if (SUCCEEDED(device->GetId(&id)) && id)
        {
            wanted = id;

            CoTaskMemFree(id);
        }

        device->Release();
    }

    IMMDeviceCollection* devices = nullptr;

    if (FAILED(enumerator->EnumAudioEndpoints(
            flow, DEVICE_STATE_ACTIVE, &devices)))
    {
        return -1;
    }

    int found = -1;

    UINT count = 0;

    devices->GetCount(&count);

    for (UINT i = 0; i < count && found < 0; ++i)
    {
        IMMDevice* device = nullptr;

        if (FAILED(devices->Item(i, &device)))
            continue;

        LPWSTR id = nullptr;

        if (SUCCEEDED(device->GetId(&id)) && id)
        {
            if (wanted == id)
            {
                found = static_cast<int>(i);
            }

            CoTaskMemFree(id);
        }

        device->Release();
    }

    devices->Release();

    return found;
}

// AEC_SYSTEM_MODE::SINGLE_CHANNEL_AEC
constexpr LONG AEC_MODE_SINGLE_CHANNEL = 0;

// Sem nada saindo pelas caixas de som o DSP nao entrega audio
// (o eco precisa de referencia). O canal de voz mantem a saida
// tocando (silencio), entao isso so acontece por instantes.
constexpr DWORD DSP_DATA_BUFFER_INCOMPLETE = 0x01000000;


// IMediaBuffer minimo: o DSP escreve a voz processada aqui.
class SimpleMediaBuffer : public IMediaBuffer
{
public:
    explicit SimpleMediaBuffer(
        DWORD capacity)
        : m_data(capacity)
    {
    }

    STDMETHODIMP QueryInterface(
        REFIID iid,
        void** out) override
    {
        if (!out)
            return E_POINTER;

        if (IsEqualIID(iid, IID_IUnknown) ||
            IsEqualIID(iid, IID_MediaBuffer))
        {
            *out = static_cast<IMediaBuffer*>(this);

            AddRef();

            return S_OK;
        }

        *out = nullptr;

        return E_NOINTERFACE;
    }

    // Vive na pilha de quem usa: a contagem so precisa existir.
    STDMETHODIMP_(ULONG) AddRef() override
    {
        return ++m_refs;
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        return --m_refs;
    }

    STDMETHODIMP SetLength(
        DWORD length) override
    {
        if (length > m_data.size())
            return E_INVALIDARG;

        m_length = length;

        return S_OK;
    }

    STDMETHODIMP GetMaxLength(
        DWORD* out) override
    {
        if (!out)
            return E_POINTER;

        *out = static_cast<DWORD>(m_data.size());

        return S_OK;
    }

    STDMETHODIMP GetBufferAndLength(
        BYTE** buffer,
        DWORD* length) override
    {
        if (buffer)
            *buffer = m_data.data();

        if (length)
            *length = m_length;

        return S_OK;
    }

    const BYTE* Data() const { return m_data.data(); }

    DWORD Length() const { return m_length; }

private:
    std::vector<BYTE> m_data;

    DWORD m_length = 0;

    ULONG m_refs = 1;
};

} // namespace


VoiceCapture::~VoiceCapture()
{
    Stop();
}


std::vector<VoiceCapture::Device> VoiceCapture::ListMicrophones()
{
    return ListDevices(eCapture);
}


std::vector<VoiceCapture::Device> VoiceCapture::ListSpeakers()
{
    return ListDevices(eRender);
}


std::vector<VoiceCapture::Device> VoiceCapture::ListDevices(
    int dataFlow)
{
    std::vector<Device> result;

    HRESULT com =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    IMMDeviceEnumerator* enumerator = nullptr;

    IMMDeviceCollection* devices = nullptr;

    if (SUCCEEDED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator))) &&
        SUCCEEDED(enumerator->EnumAudioEndpoints(
            static_cast<EDataFlow>(dataFlow),
            DEVICE_STATE_ACTIVE,
            &devices)))
    {
        UINT count = 0;

        devices->GetCount(&count);

        for (UINT i = 0; i < count; ++i)
        {
            IMMDevice* device = nullptr;

            if (FAILED(devices->Item(i, &device)))
                continue;

            Device entry;

            LPWSTR id = nullptr;

            if (SUCCEEDED(device->GetId(&id)) && id)
            {
                entry.id = id;

                CoTaskMemFree(id);
            }

            IPropertyStore* store = nullptr;

            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)))
            {
                PROPVARIANT name;

                PropVariantInit(&name);

                if (SUCCEEDED(store->GetValue(
                        PKEY_DeviceFriendlyName, &name)) &&
                    name.vt == VT_LPWSTR &&
                    name.pwszVal)
                {
                    entry.name = WideToUtf8Local(name.pwszVal);
                }

                PropVariantClear(&name);

                store->Release();
            }

            if (entry.name.empty())
            {
                entry.name =
                    (dataFlow == eCapture ? "Microfone " : "Saída ") +
                    std::to_string(i + 1);
            }

            if (!entry.id.empty())
            {
                result.push_back(std::move(entry));
            }

            device->Release();
        }
    }

    if (devices)
    {
        devices->Release();
    }

    if (enumerator)
    {
        enumerator->Release();
    }

    if (SUCCEEDED(com))
    {
        CoUninitialize();
    }

    return result;
}


void VoiceCapture::Start(
    FrameFn onFrame)
{
    if (m_running.exchange(true))
        return;

    m_onFrame = std::move(onFrame);

    m_hasError = false;
    m_errorKind = 0;
    m_echoCancellation = false;
    m_pending.clear();

    m_thread =
        std::thread(&VoiceCapture::Run, this);
}


void VoiceCapture::Stop()
{
    m_running = false;

    if (m_thread.joinable())
    {
        m_thread.join();
    }

    m_level = 0.0f;
}


std::string VoiceCapture::ErrorText() const
{
    switch (m_errorKind.load())
    {
    case 1:
        return
            "O Windows bloqueou o microfone. Em Configurações > "
            "Privacidade e segurança > Microfone, permita o "
            "acesso para aplicativos da área de trabalho.";

    case 2:
        return "Nenhum microfone encontrado.";

    default:
        return
            "Não foi possível abrir o microfone. Veja o "
            "stream-project.log.";
    }
}


// ============================================================
// Deliver: junta em blocos de 20 ms e mede o nivel
// ============================================================

void VoiceCapture::Deliver(
    const float* samples,
    size_t count)
{
    m_pending.insert(
        m_pending.end(),
        samples,
        samples + count);

    size_t offset = 0;

    while (m_pending.size() - offset >= FRAME_SAMPLES)
    {
        const float* frame = m_pending.data() + offset;

        double energy = 0.0;

        for (int i = 0; i < FRAME_SAMPLES; ++i)
        {
            energy += static_cast<double>(frame[i]) * frame[i];
        }

        float rms =
            static_cast<float>(std::sqrt(energy / FRAME_SAMPLES));

        m_level =
            (std::max)(rms, m_level.load() * 0.85f);

        if (m_onFrame)
        {
            m_onFrame(frame, FRAME_SAMPLES);
        }

        offset += FRAME_SAMPLES;
    }

    m_pending.erase(
        m_pending.begin(),
        m_pending.begin() + offset);
}


// ============================================================
// Run
// ============================================================

void VoiceCapture::Run()
{
    HRESULT com =
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (!RunEchoCancelled() &&
        m_running)
    {
        std::cout
            << "Voz: cancelamento de eco indisponivel - usando o "
            << "microfone simples (recomende fone).\n";

        m_echoCancellation = false;

        if (!RunPlain() && m_running)
        {
            m_hasError = true;
        }
    }

    if (SUCCEEDED(com))
    {
        CoUninitialize();
    }
}


// ============================================================
// RunEchoCancelled - Voice Capture DSP (modo "source": ele mesmo
// le o microfone e a referencia do que sai nas caixas de som)
// ============================================================

bool VoiceCapture::RunEchoCancelled()
{
    IMediaObject* dsp = nullptr;

    HRESULT hr =
        CoCreateInstance(
            CLSID_VoiceCaptureDsp,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_MediaObject,
            reinterpret_cast<void**>(&dsp));

    if (FAILED(hr) || !dsp)
    {
        std::cout
            << "Voz: Voice Capture DSP indisponivel (0x"
            << std::hex << hr << std::dec << ").\n";

        return false;
    }

    IPropertyStore* properties = nullptr;

    hr =
        dsp->QueryInterface(
            IID_PropertyStoreLocal,
            reinterpret_cast<void**>(&properties));

    if (SUCCEEDED(hr) && properties)
    {
        // Obrigatorio antes de usar: so cancelamento de eco (com
        // reducao de ruido e ganho automatico, que ja vem
        // ligados). Microfone e saida: os padrao do Windows.
        PROPVARIANT value;

        PropVariantInit(&value);

        value.vt = VT_I4;
        value.lVal = AEC_MODE_SINGLE_CHANNEL;

        hr = properties->SetValue(PKEY_AecSystemMode, value);

        PropVariantClear(&value);

        // Microfone escolhido: diz ao DSP qual usar (e a saida
        // padrao, que e onde a voz dos outros toca - a
        // referencia do eco).
        if (SUCCEEDED(hr) &&
            (!m_deviceId.empty() || !m_speakerId.empty()))
        {
            IMMDeviceEnumerator* enumerator = nullptr;

            if (SUCCEEDED(CoCreateInstance(
                    __uuidof(MMDeviceEnumerator),
                    nullptr,
                    CLSCTX_ALL,
                    __uuidof(IMMDeviceEnumerator),
                    reinterpret_cast<void**>(&enumerator))))
            {
                int mic =
                    DeviceIndex(enumerator, eCapture, m_deviceId);

                int speaker =
                    DeviceIndex(enumerator, eRender, m_speakerId);

                // Saida escolhida sumiu: a padrao (o playback
                // faz o mesmo).
                if (speaker < 0)
                {
                    speaker =
                        DeviceIndex(enumerator, eRender, std::wstring());
                }

                if (mic < 0)
                {
                    mic =
                        DeviceIndex(enumerator, eCapture, std::wstring());
                }

                enumerator->Release();

                if (mic >= 0 && speaker >= 0)
                {
                    PROPVARIANT indexes;

                    PropVariantInit(&indexes);

                    indexes.vt = VT_I4;
                    indexes.lVal =
                        static_cast<LONG>(
                            (static_cast<unsigned long>(speaker) << 16) |
                            (static_cast<unsigned long>(mic) & 0xFFFF));

                    hr = properties->SetValue(
                        PKEY_AecDeviceIndexes, indexes);

                    PropVariantClear(&indexes);
                }
                else
                {
                    std::cout
                        << "Voz: dispositivos de som nao encontrados - "
                        << "usando os padrao.\n";
                }
            }
        }

        properties->Release();
    }

    if (FAILED(hr))
    {
        std::cout
            << "Voz: nao deu pra configurar o DSP (0x"
            << std::hex << hr << std::dec << ").\n";

        dsp->Release();

        return false;
    }


    // --------------------------------------------------------
    // Formato de saida: 16 kHz, mono, PCM 16 bits
    // --------------------------------------------------------

    DMO_MEDIA_TYPE type{};

    type.majortype = MEDIATYPE_AudioLocal;
    type.subtype = MEDIASUBTYPE_PcmLocal;
    type.bFixedSizeSamples = TRUE;
    type.bTemporalCompression = FALSE;
    type.lSampleSize = 0;
    type.formattype = FORMAT_WaveFormatExLocal;
    type.cbFormat = sizeof(WAVEFORMATEX);
    type.pbFormat =
        static_cast<BYTE*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));

    if (!type.pbFormat)
    {
        dsp->Release();

        return false;
    }

    WAVEFORMATEX* format =
        reinterpret_cast<WAVEFORMATEX*>(type.pbFormat);

    ZeroMemory(format, sizeof(WAVEFORMATEX));

    format->wFormatTag = WAVE_FORMAT_PCM;
    format->nChannels = 1;
    format->nSamplesPerSec = SAMPLE_RATE;
    format->wBitsPerSample = 16;
    format->nBlockAlign = 2;
    format->nAvgBytesPerSec = SAMPLE_RATE * 2;

    hr = dsp->SetOutputType(0, &type, 0);

    CoTaskMemFree(type.pbFormat);

    if (SUCCEEDED(hr))
    {
        hr = dsp->AllocateStreamingResources();
    }

    if (FAILED(hr))
    {
        std::cout
            << "Voz: DSP recusou o formato (0x"
            << std::hex << hr << std::dec << ").\n";

        // Sem permissao de microfone o erro aparece aqui.
        if (hr == E_ACCESSDENIED)
        {
            m_errorKind = 1;
        }

        dsp->Release();

        return false;
    }

    std::cout
        << "Voz: microfone com cancelamento de eco do Windows.\n";

    m_echoCancellation = true;


    // --------------------------------------------------------
    // Laco: a cada 10 ms, tudo o que o DSP tiver processado
    // --------------------------------------------------------

    SimpleMediaBuffer buffer(SAMPLE_RATE * 2);

    std::vector<float> samples;

    ULONGLONG started = GetTickCount64();

    bool gotAudio = false;

    bool ok = true;

    while (m_running)
    {
        DWORD status = 0;

        DMO_OUTPUT_DATA_BUFFER output{};

        output.pBuffer = &buffer;

        do
        {
            buffer.SetLength(0);

            output.dwStatus = 0;

            hr = dsp->ProcessOutput(0, 1, &output, &status);

            if (FAILED(hr))
                break;

            DWORD bytes = buffer.Length();

            if (bytes >= 2)
            {
                gotAudio = true;

                const int16_t* pcm =
                    reinterpret_cast<const int16_t*>(buffer.Data());

                size_t count = bytes / 2;

                samples.resize(count);

                for (size_t i = 0; i < count; ++i)
                {
                    samples[i] = pcm[i] / 32768.0f;
                }

                Deliver(samples.data(), count);
            }
        }
        while (output.dwStatus & DSP_DATA_BUFFER_INCOMPLETE);

        // Nunca entregou nada em 4 s: esse DSP nao funciona
        // neste PC (o microfone simples assume).
        if (!gotAudio &&
            GetTickCount64() - started > 4000)
        {
            std::cout
                << "Voz: o DSP nao entregou audio (ultimo erro 0x"
                << std::hex << hr << std::dec << ").\n";

            ok = false;

            break;
        }

        Sleep(10);
    }

    dsp->FreeStreamingResources();

    dsp->Release();

    return ok;
}


// ============================================================
// RunPlain - microfone padrao de comunicacao, sem processamento
// ============================================================

bool VoiceCapture::RunPlain()
{
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;
    IAudioCaptureClient* capture = nullptr;

    bool ok = false;

    HRESULT hr =
        CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator));

    if (SUCCEEDED(hr))
    {
        hr = E_FAIL;

        if (!m_deviceId.empty())
        {
            hr = enumerator->GetDevice(m_deviceId.c_str(), &device);
        }

        if (FAILED(hr))
        {
            hr =
                enumerator->GetDefaultAudioEndpoint(
                    eCapture,
                    eCommunications,
                    &device);
        }

        if (FAILED(hr))
        {
            m_errorKind = 2;
        }
    }

    if (SUCCEEDED(hr))
    {
        hr =
            device->Activate(
                __uuidof(IAudioClient),
                CLSCTX_ALL,
                nullptr,
                reinterpret_cast<void**>(&client));
    }

    if (SUCCEEDED(hr))
    {
        // Pedimos 16 kHz mono float; o Windows converte.
        WAVEFORMATEX format{};

        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 1;
        format.nSamplesPerSec = SAMPLE_RATE;
        format.wBitsPerSample = 32;
        format.nBlockAlign = 4;
        format.nAvgBytesPerSec = SAMPLE_RATE * 4;

        constexpr DWORD AUTO_CONVERT = 0x80000000;  // AUTOCONVERTPCM
        constexpr DWORD GOOD_RESAMPLE = 0x08000000; // SRC_DEFAULT_QUALITY

        hr =
            client->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUTO_CONVERT | GOOD_RESAMPLE,
                100 * 10000, // 100 ms
                0,
                &format,
                nullptr);

        if (hr == E_ACCESSDENIED)
        {
            m_errorKind = 1;
        }
    }

    if (SUCCEEDED(hr))
    {
        hr =
            client->GetService(
                __uuidof(IAudioCaptureClient),
                reinterpret_cast<void**>(&capture));
    }

    if (SUCCEEDED(hr))
    {
        hr = client->Start();
    }

    if (SUCCEEDED(hr))
    {
        ok = true;

        std::cout
            << "Voz: microfone simples (sem cancelamento de "
            << "eco).\n";

        while (m_running)
        {
            UINT32 packet = 0;

            while (SUCCEEDED(capture->GetNextPacketSize(&packet)) &&
                   packet > 0)
            {
                BYTE* data = nullptr;

                UINT32 frames = 0;

                DWORD flags = 0;

                if (FAILED(capture->GetBuffer(
                        &data, &frames, &flags, nullptr, nullptr)))
                {
                    break;
                }

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT)
                {
                    std::vector<float> silence(frames, 0.0f);

                    Deliver(silence.data(), frames);
                }
                else
                {
                    Deliver(
                        reinterpret_cast<const float*>(data),
                        frames);
                }

                capture->ReleaseBuffer(frames);
            }

            Sleep(10);
        }

        client->Stop();
    }
    else
    {
        std::cout
            << "Voz: nao deu pra abrir o microfone (0x"
            << std::hex << hr << std::dec << ").\n";
    }

    if (capture) capture->Release();
    if (client) client->Release();
    if (device) device->Release();
    if (enumerator) enumerator->Release();

    return ok;
}
