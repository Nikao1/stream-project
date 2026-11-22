#include "voice_channel.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <opus.h>

#include <algorithm>
#include <cstring>
#include <iostream>

#include "audio_protocol.h"
#include "nat_traversal.h"
#include "udp_viewers.h"

// ============================================================
// VoicePeer - ouvir UMA pessoa
// ============================================================
//
// Entra na sessao de voz dessa pessoa como se fosse assistir a
// uma transmissao so de audio: pergunta o endereco ao servidor,
// abre o caminho no roteador (STUN + anuncio), manda o HELLO
// cifrado e recebe a voz, que vai pro mixador.
// ============================================================

class VoicePeer
{
public:
    VoicePeer(
        std::string memberId,
        std::string sessionCode,
        const CryptoKeyPair& myKeys,
        VoiceMixer& mixer)
        : m_memberId(std::move(memberId)),
          m_sessionCode(std::move(sessionCode)),
          m_keys(myKeys),
          m_mixer(mixer)
    {
        m_running = true;

        m_thread = std::thread(&VoicePeer::Run, this);
    }

    ~VoicePeer()
    {
        m_running = false;

        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    const std::string& SessionCode() const
    {
        return m_sessionCode;
    }

    bool IsReceiving() const
    {
        return m_receiving;
    }

    // Pede pra parar sem esperar (a thread pode estar presa
    // numa chamada ao servidor por alguns segundos).
    void RequestStop()
    {
        m_running = false;
    }

    // A thread ja terminou: destruir agora nao trava.
    bool IsFinished() const
    {
        return m_finished;
    }

private:
    void Run()
    {
        RunConnection();

        m_receiving = false;
        m_finished = true;
    }

    void RunConnection();

    std::string m_memberId;
    std::string m_sessionCode;

    CryptoKeyPair m_keys;

    VoiceMixer& m_mixer;

    std::thread m_thread;

    std::atomic<bool> m_running{ false };
    std::atomic<bool> m_receiving{ false };
    std::atomic<bool> m_finished{ false };
};


namespace
{

bool MakeAddress(
    const std::string& ip,
    uint16_t port,
    sockaddr_in& out)
{
    out = sockaddr_in{};

    out.sin_family = AF_INET;
    out.sin_port = htons(port);

    return
        port != 0 &&
        inet_pton(AF_INET, ip.c_str(), &out.sin_addr) == 1;
}

bool SameAddress(
    const sockaddr_in& a,
    const sockaddr_in& b)
{
    return
        a.sin_addr.s_addr == b.sin_addr.s_addr &&
        a.sin_port == b.sin_port;
}

// Espera ate "ms" ou ate parar de rodar.
bool SleepWhile(
    const std::atomic<bool>& running,
    int ms)
{
    for (int waited = 0; waited < ms && running; waited += 50)
    {
        Sleep(50);
    }

    return running;
}

} // namespace


void VoicePeer::RunConnection()
{
    // --------------------------------------------------------
    // Endereco de quem fala (tenta de novo se o servidor
    // demorar - ex.: a sessao acabou de ser criada)
    // --------------------------------------------------------

    SignalingHostInfo host;

    std::string error;

    bool found = false;

    for (int attempt = 0; attempt < 5 && m_running; ++attempt)
    {
        if (SignalingJoinSession(m_sessionCode, host, error))
        {
            found = true;

            break;
        }

        SleepWhile(m_running, 2000);
    }

    if (!found)
    {
        std::cout
            << "Voz: nao deu pra conectar na voz "
            << m_sessionCode << ": " << error << "\n";

        return;
    }

    uint8_t hostKey[CRYPTO_KEY_SIZE];

    uint8_t sessionKey[CRYPTO_KEY_SIZE];

    if (!CryptoFromHex(host.hostKey, hostKey, CRYPTO_KEY_SIZE) ||
        !CryptoDeriveSessionKey(
            m_keys.secretKey,
            hostKey,
            hostKey,
            m_keys.publicKey,
            sessionKey))
    {
        return;
    }


    // --------------------------------------------------------
    // Socket + caminho pela internet
    // --------------------------------------------------------

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (sock == INVALID_SOCKET)
        return;

    sockaddr_in local{};

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = 0;

    if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0)
    {
        closesocket(sock);

        return;
    }

    StunResult stun = StunDiscover(sock);

    if (stun.ok)
    {
        SignalingAnnounceViewer(
            m_sessionCode,
            true,
            stun.publicIp,
            stun.publicPort,
            error);
    }

    // Mesma rede: IP/porta locais; senao, os publicos. O outro
    // caminho fica de alternativa (o HELLO vai pros dois).
    sockaddr_in primary{};
    sockaddr_in alternate{};

    bool havePrimary =
        host.sameNetwork
        ? MakeAddress(host.localIp, host.audioPort, primary)
        : MakeAddress(host.publicIp, host.publicAudioPort, primary);

    bool haveAlternate =
        host.sameNetwork
        ? MakeAddress(host.publicIp, host.publicAudioPort, alternate)
        : MakeAddress(host.localIp, host.audioPort, alternate);

    if (!havePrimary)
    {
        closesocket(sock);

        return;
    }

    if (haveAlternate && SameAddress(alternate, primary))
    {
        haveAlternate = false;
    }

    uint8_t hello[CRYPTO_HELLO_AUTH_SIZE];

    CryptoBuildAuthHello(
        m_keys.publicKey,
        sessionKey,
        CRYPTO_CHANNEL_AUDIO,
        hello);


    // --------------------------------------------------------
    // Opus: decodifica direto em 48 kHz estereo (o formato do
    // mixador), seja qual for o formato de quem fala.
    // --------------------------------------------------------

    int opusError = 0;

    OpusDecoder* decoder =
        opus_decoder_create(
            VoiceMixer::SAMPLE_RATE,
            VoiceMixer::CHANNELS,
            &opusError);

    if (!decoder)
    {
        closesocket(sock);

        return;
    }

    std::vector<float> pcm(5760 * VoiceMixer::CHANNELS);

    std::vector<char> packet(AUDIO_UDP_PACKET_SIZE + CRYPTO_OVERHEAD);

    std::vector<uint8_t> plain(AUDIO_UDP_PACKET_SIZE);

    ULONGLONG lastSent = 0;

    bool haveSequence = false;

    uint32_t lastSequence = 0;

    sockaddr_in chosen = primary;

    std::cout
        << "Voz: conectando em " << m_sessionCode << "\n";

    while (m_running)
    {
        ULONGLONG now = GetTickCount64();

        // Sem audio ainda: HELLO a cada segundo (pros dois
        // caminhos). Depois: sinal de vida a cada 5 s.
        if (!m_receiving && now - lastSent >= 1000)
        {
            sendto(sock, reinterpret_cast<const char*>(hello),
                   sizeof(hello), 0,
                   reinterpret_cast<sockaddr*>(&primary),
                   sizeof(primary));

            if (haveAlternate)
            {
                sendto(sock, reinterpret_cast<const char*>(hello),
                       sizeof(hello), 0,
                       reinterpret_cast<sockaddr*>(&alternate),
                       sizeof(alternate));
            }

            lastSent = now;
        }
        else if (m_receiving && now - lastSent >= 5000)
        {
            sendto(sock, "KEEP", 4, 0,
                   reinterpret_cast<sockaddr*>(&chosen),
                   sizeof(chosen));

            lastSent = now;
        }

        fd_set readSet;

        FD_ZERO(&readSet);
        FD_SET(sock, &readSet);

        timeval timeout{ 0, 100000 };

        if (select(0, &readSet, nullptr, nullptr, &timeout) <= 0)
            continue;

        sockaddr_in from{};

        int fromSize = sizeof(from);

        int received =
            recvfrom(sock, packet.data(),
                     static_cast<int>(packet.size()), 0,
                     reinterpret_cast<sockaddr*>(&from), &fromSize);

        if (received <= 0)
            continue;

        size_t plainSize = 0;

        if (!CryptoOpen(
                sessionKey,
                CRYPTO_CHANNEL_AUDIO,
                reinterpret_cast<const uint8_t*>(packet.data()),
                static_cast<size_t>(received),
                plain.data(),
                plainSize) ||
            plainSize < AUDIO_HEADER_SIZE)
        {
            continue;
        }

        AudioPacketHeader header;

        std::memcpy(&header, plain.data(), sizeof(header));

        if (ntohl(header.magic) != AUDIO_PROTOCOL_MAGIC)
            continue;

        uint32_t payloadSize = ntohl(header.payloadSize);

        uint32_t sequence = ntohl(header.sequence);

        if (payloadSize == 0 ||
            AUDIO_HEADER_SIZE + payloadSize != plainSize)
        {
            continue;
        }

        if (!m_receiving)
        {
            m_receiving = true;

            // Seguimos pelo caminho por onde a voz chegou.
            chosen = from;

            std::cout
                << "Voz: ouvindo " << m_sessionCode << "\n";
        }

        // Pacote antigo/repetido: ignora.
        if (haveSequence &&
            static_cast<int32_t>(sequence - lastSequence) <= 0)
        {
            continue;
        }

        // Pacotes perdidos: o Opus "inventa" uma continuacao
        // suave (no maximo 3, pra nao soar estranho).
        if (haveSequence)
        {
            uint32_t missing = sequence - lastSequence - 1;

            for (uint32_t i = 0; i < missing && i < 3; ++i)
            {
                int frames =
                    opus_decode_float(
                        decoder, nullptr, 0, pcm.data(),
                        VoiceMixer::SAMPLE_RATE / 50, 0);

                if (frames > 0)
                {
                    m_mixer.Push(m_memberId, pcm.data(), frames);
                }
            }
        }

        haveSequence = true;

        lastSequence = sequence;

        int frames =
            opus_decode_float(
                decoder,
                plain.data() + AUDIO_HEADER_SIZE,
                static_cast<opus_int32>(payloadSize),
                pcm.data(),
                5760,
                0);

        if (frames > 0)
        {
            m_mixer.Push(m_memberId, pcm.data(), frames);
        }
    }

    // Avisa que saimos (libera a vaga na hora).
    sendto(sock, "BYE1", 4, 0,
           reinterpret_cast<sockaddr*>(&chosen), sizeof(chosen));

    opus_decoder_destroy(decoder);

    closesocket(sock);
}


// ============================================================
// VoiceChannel
// ============================================================

VoiceChannel::VoiceChannel() = default;


VoiceChannel::~VoiceChannel()
{
    Leave();

    // Aqui sim espera (fim do programa).
    std::vector<std::unique_ptr<VoicePeer>> dying;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        dying.swap(m_dying);
    }

    dying.clear();
}


void VoiceChannel::Join()
{
    if (m_joining || m_joined)
        return;

    if (m_joinThread.joinable())
    {
        m_joinThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_error.clear();
    }

    m_joining = true;

    m_joinThread =
        std::thread(&VoiceChannel::JoinWorker, this);
}


void VoiceChannel::JoinWorker()
{
    std::string error;

    bool ok = CryptoGenerateKeyPair(m_keys);

    // ---- Saida de som (baixo atraso) + mixador ----
    //
    // Antes do microfone: o cancelamento de eco precisa de algo
    // saindo nas caixas de som pra funcionar (a saida toca
    // silencio quando ninguem fala).
    if (ok)
    {
        m_playback.SetLowLatency(true);

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            m_playback.SetDevice(m_speaker);
        }

        ok = m_playback.Start();

        if (!ok)
        {
            error = "Não foi possível abrir a saída de som.";
        }
    }

    if (ok)
    {
        m_mixer.Start(
            [this](const std::vector<float>& block)
            {
                m_playback.PushSamples(block);
            });
    }

    // ---- Codificador de voz ----
    if (ok)
    {
        int opusError = 0;

        m_encoder =
            opus_encoder_create(
                VoiceCapture::SAMPLE_RATE,
                1,
                OPUS_APPLICATION_VOIP,
                &opusError);

        if (!m_encoder)
        {
            ok = false;

            error = "Falha no codificador de voz.";
        }
        else
        {
            opus_encoder_ctl(m_encoder, OPUS_SET_BITRATE(24000));

            // Correcao embutida pra perdas pequenas na rede.
            opus_encoder_ctl(m_encoder, OPUS_SET_INBAND_FEC(1));
            opus_encoder_ctl(m_encoder, OPUS_SET_PACKET_LOSS_PERC(5));
        }
    }

    // ---- Canal desta pessoa (porta livre qualquer) ----
    if (ok)
    {
        m_server.SetHostKeys(&m_keys);

        ok = m_server.Start(0);

        if (!ok)
        {
            error = "Não foi possível abrir a porta de rede da voz.";
        }
    }

    if (ok)
    {
        m_serviceRunning = true;

        m_serviceThread =
            std::thread(
                [this]()
                {
                    while (m_serviceRunning)
                    {
                        if (!m_server.Service())
                        {
                            Sleep(100);
                        }
                    }
                });

        // Hole punching: quem vai ouvir manda o endereco pelo
        // servidor; abrimos o caminho na direcao dele.
        m_signaling.SetViewerCallback(
            [this](bool, const std::string& ip, uint16_t port)
            {
                m_server.PunchTo(ip, port);
            });

        uint16_t port = m_server.GetLocalPort();

        const StunResult& stun = m_server.GetStunResult();

        ok =
            m_signaling.Start(
                port,
                port,
                stun.ok ? stun.publicIp : std::string(),
                stun.ok ? stun.publicPort : 0,
                stun.ok ? stun.publicPort : 0,
                CryptoToHex(m_keys.publicKey, CRYPTO_KEY_SIZE));

        if (!ok)
        {
            error =
                "Não foi possível registrar a voz no servidor. "
                "Verifique sua internet.";
        }
    }

    // ---- Microfone ----
    if (ok)
    {
        m_startMs = GetTickCount64();

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            m_capture.SetDevice(m_microphone);
            m_capture.SetSpeaker(m_speaker);
        }

        m_capture.Start(
            [this](const float* samples, size_t count)
            {
                OnMicFrame(samples, count);
            });

        m_joined = true;

        std::cout
            << "Voz: entrou (sessao "
            << m_signaling.GetCode() << ")\n";
    }
    else
    {
        std::cout << "Voz: " << error << "\n";

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            m_error = error;
        }

        // Desfaz o que chegou a abrir.
        m_serviceRunning = false;

        m_server.Stop();

        if (m_serviceThread.joinable())
        {
            m_serviceThread.join();
        }

        m_signaling.Stop();

        m_mixer.Stop();

        m_playback.Stop();

        if (m_encoder)
        {
            opus_encoder_destroy(m_encoder);

            m_encoder = nullptr;
        }
    }

    m_joining = false;
}


void VoiceChannel::OnMicFrame(
    const float* samples,
    size_t count)
{
    // Ligar/desligar microfone pelo atalho (borda de descida).
    int toggleKey = m_muteToggleKey;

    if (toggleKey != 0)
    {
        bool down = (GetAsyncKeyState(toggleKey) & 0x8000) != 0;

        if (down && !m_muteToggleWasDown)
        {
            m_muted = !m_muted;

            std::cout
                << "Voz: microfone "
                << (m_muted ? "silenciado" : "ligado")
                << " pelo atalho.\n";
        }

        m_muteToggleWasDown = down;
    }

    // Segurar para falar: a tecla e lida aqui (a cada 20 ms),
    // funciona com o jogo em primeiro plano.
    int key = m_pushToTalkKey;

    bool keyDown =
        key == 0 ||
        (GetAsyncKeyState(key) & 0x8000) != 0;

    bool transmit = !m_muted && keyDown;

    m_transmitting = transmit;

    if (!transmit ||
        !m_encoder ||
        count != VoiceCapture::FRAME_SAMPLES)
    {
        return;
    }

    unsigned char encoded[400];

    opus_int32 size =
        opus_encode_float(
            m_encoder,
            samples,
            static_cast<int>(count),
            encoded,
            sizeof(encoded));

    if (size <= 0)
        return;

    uint32_t timestamp =
        static_cast<uint32_t>(GetTickCount64() - m_startMs);

    m_server.SendAudioPacket(
        encoded,
        static_cast<uint32_t>(size),
        timestamp);
}


void VoiceChannel::StopPeers()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& entry : m_peers)
    {
        entry.second->RequestStop();

        m_dying.push_back(std::move(entry.second));
    }

    m_peers.clear();
}


// Destroi as conexoes que ja terminaram (sem travar quem chama).
void VoiceChannel::ReapPeers()
{
    std::vector<std::unique_ptr<VoicePeer>> finished;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (auto it = m_dying.begin(); it != m_dying.end();)
        {
            if ((*it)->IsFinished())
            {
                finished.push_back(std::move(*it));

                it = m_dying.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    finished.clear();
}


void VoiceChannel::Leave()
{
    if (m_joinThread.joinable())
    {
        m_joinThread.join();
    }

    if (!m_joined.exchange(false))
        return;

    std::cout << "Voz: saindo.\n";

    m_capture.Stop();

    m_transmitting = false;

    StopPeers();

    m_signaling.Stop();

    m_serviceRunning = false;

    m_server.Stop();

    if (m_serviceThread.joinable())
    {
        m_serviceThread.join();
    }

    m_mixer.Stop();

    m_playback.Stop();

    if (m_encoder)
    {
        opus_encoder_destroy(m_encoder);

        m_encoder = nullptr;
    }
}


std::string VoiceChannel::ErrorText()
{
    if (m_joined && m_capture.HasError())
    {
        return m_capture.ErrorText();
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    return m_error;
}


void VoiceChannel::Sync(
    const RoomState& state,
    const std::string& myMemberId)
{
    ReapPeers();

    if (!m_joined)
        return;

    std::lock_guard<std::mutex> lock(m_mutex);

    // Lista vazia = resposta da sala ainda nao chegou.
    if (state.members.empty())
        return;

    // Quem saiu da voz (ou da sala), ou trocou de sessao: para
    // sem esperar (vai pra fila de encerramento).
    for (auto it = m_peers.begin(); it != m_peers.end();)
    {
        bool keep = false;

        for (const RoomMember& member : state.members)
        {
            if (member.id == it->first &&
                member.voiceSession == it->second->SessionCode())
            {
                keep = true;

                break;
            }
        }

        if (keep)
        {
            ++it;
        }
        else
        {
            it->second->RequestStop();

            m_mixer.Remove(it->first);

            m_dying.push_back(std::move(it->second));

            it = m_peers.erase(it);
        }
    }

    // Quem entrou na voz.
    for (const RoomMember& member : state.members)
    {
        if (member.id == myMemberId ||
            member.voiceSession.empty() ||
            m_peers.count(member.id))
        {
            continue;
        }

        auto volume = m_volumes.find(member.id);

        m_mixer.SetGain(
            member.id,
            volume == m_volumes.end() ? 1.0f : volume->second);

        m_peers[member.id] =
            std::make_unique<VoicePeer>(
                member.id,
                member.voiceSession,
                m_keys,
                m_mixer);
    }
}


bool VoiceChannel::GetPublishInfo(
    std::string& outSessionCode,
    std::string& outSessionToken)
{
    if (!m_joined)
        return false;

    outSessionCode = m_signaling.GetCode();

    outSessionToken = m_signaling.GetToken();

    return
        !outSessionCode.empty() &&
        !outSessionToken.empty();
}


void VoiceChannel::SetMuted(
    bool muted)
{
    m_muted = muted;
}


void VoiceChannel::SetPushToTalkKey(
    int virtualKey)
{
    m_pushToTalkKey = virtualKey;
}


void VoiceChannel::SetMicrophone(
    const std::wstring& deviceId)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (deviceId == m_microphone)
            return;

        m_microphone = deviceId;
    }

    if (!m_joined)
        return;

    // Reabre a captura no microfone novo (a conexao com os
    // outros continua).
    m_capture.Stop();

    m_capture.SetDevice(deviceId);

    m_capture.Start(
        [this](const float* samples, size_t count)
        {
            OnMicFrame(samples, count);
        });
}


void VoiceChannel::SetSpeaker(
    const std::wstring& deviceId)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (deviceId == m_speaker)
            return;

        m_speaker = deviceId;
    }

    if (!m_joined)
        return;

    // Reabre a saida no dispositivo novo (o mixador continua
    // mandando; o que chegar no meio da troca se perde)...
    m_playback.Stop();

    m_playback.SetDevice(deviceId);

    m_playback.Start();

    // ...e o microfone, pro cancelamento de eco olhar a saida
    // nova.
    m_capture.Stop();

    m_capture.SetSpeaker(deviceId);

    m_capture.Start(
        [this](const float* samples, size_t count)
        {
            OnMicFrame(samples, count);
        });
}


float VoiceChannel::MicLevel() const
{
    return m_joined ? m_capture.Level() : 0.0f;
}


bool VoiceChannel::EchoCancellation() const
{
    return m_joined && m_capture.EchoCancellation();
}


void VoiceChannel::SetPeerVolume(
    const std::string& memberId,
    float volume)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        m_volumes[memberId] = volume;
    }

    m_mixer.SetGain(memberId, volume);
}


float VoiceChannel::PeerVolume(
    const std::string& memberId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto found = m_volumes.find(memberId);

    return found == m_volumes.end() ? 1.0f : found->second;
}


bool VoiceChannel::IsSpeaking(
    const std::string& memberId)
{
    return m_mixer.GetLevel(memberId) > 0.02f;
}


bool VoiceChannel::IsConnected(
    const std::string& memberId)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto found = m_peers.find(memberId);

    return
        found != m_peers.end() &&
        found->second->IsReceiving();
}
