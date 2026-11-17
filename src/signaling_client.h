#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// ============================================================
// SINALIZACAO
// ============================================================
//
// Conversa com o servidor de sinalizacao
// (signaling/signaling_server.py) por HTTP, usando WinHTTP -
// que ja vem no Windows, sem dependencia extra.
//
// O servidor de sinalizacao so apresenta as duas pontas:
//
//   stream-client   --> cria sessao, recebe um CODIGO curto
//   stream-receiver --> entra com o CODIGO, recebe o endereco
//                       de quem transmite
//
// Video e audio continuam indo direto de uma ponta pra outra
// (UDP) - nunca passam por esse servidor.
//
// Endereco do servidor: o padrao vem embutido no programa
// (DEFAULT_SIGNALING_URL, em signaling_client.cpp). A variavel
// de ambiente STREAM_SIGNALING_URL troca o servidor sem
// recompilar - ex.: http://127.0.0.1:8080 pra testar com o
// servidor rodando na propria maquina.
// ============================================================

std::string GetSignalingUrl();


// IP local "principal" desta maquina (o da interface que sai
// pra internet). Precisa do Winsock ja inicializado.
std::string GetPrimaryLocalIp();


// Um codigo de sessao tem 6 letras/numeros e nenhum ponto -
// e assim que o receiver diferencia "codigo" de "IP digitado".
bool LooksLikeSessionCode(
    const std::string& text
);


// ============================================================
// Lado do RECEIVER: entrar numa sessao pelo codigo
// ============================================================

struct SignalingHostInfo
{
    std::string publicIp;
    std::string localIp;

    // Portas locais (pra conexao na mesma rede)...
    uint16_t videoPort = 0;
    uint16_t audioPort = 0;

    // ...e portas publicas, vistas de fora do roteador do
    // host (pra conexao entre redes diferentes).
    uint16_t publicVideoPort = 0;
    uint16_t publicAudioPort = 0;

    // true = as duas pontas estao atras do mesmo roteador (ou
    // na mesma maquina) -> usar localIp em vez de publicIp.
    bool sameNetwork = false;
};

bool SignalingJoinSession(
    const std::string& code,
    SignalingHostInfo& outInfo,
    std::string& outError
);


// Informa ao host (via servidor) o endereco publico de um dos
// sockets do receiver, pra ele poder "furar" o NAT na nossa
// direcao (hole punching). Um aviso por canal (video/audio).
bool SignalingAnnounceViewer(
    const std::string& code,
    bool isAudio,
    const std::string& publicIp,
    uint16_t publicPort,
    std::string& outError
);


// ============================================================
// Lado do CLIENT (quem transmite): criar e manter a sessao
// ============================================================
//
// Start() cria a sessao e sobe uma thread que consulta o
// servidor a cada 2 segundos. Essa consulta faz duas coisas:
// mantem a sessao viva (se o host fechar o programa, ela
// expira sozinha) e traz o endereco publico do receiver que
// entrou - repassado pelo callback, pra fazer o hole punching.
// ============================================================

// isAudio, ip publico do receiver, porta publica do receiver
using SignalingViewerCallback =
    std::function<void(bool, const std::string&, uint16_t)>;

class SignalingHost
{
public:
    SignalingHost();
    ~SignalingHost();

    // Chamar antes de Start(). O callback roda na thread de
    // consulta, a cada 2s, enquanto houver um receiver
    // anunciado.
    void SetViewerCallback(
        SignalingViewerCallback callback
    );

    // stunIp vazio = STUN nao respondeu (o servidor usa o IP
    // de onde veio a requisicao HTTP).
    bool Start(
        uint16_t videoPort,
        uint16_t audioPort,
        const std::string& stunIp,
        uint16_t publicVideoPort,
        uint16_t publicAudioPort
    );

    void Stop();

    // Codigo atual da sessao (vazio se nao ha sessao).
    std::string GetCode();

private:
    bool CreateSession(
        std::string& outError
    );

    void KeepAliveLoop();

    std::mutex m_mutex;
    std::condition_variable m_cv;

    std::thread m_thread;

    std::atomic<bool> m_running;

    std::string m_code;
    std::string m_token;

    uint16_t m_videoPort;
    uint16_t m_audioPort;

    std::string m_stunIp;

    uint16_t m_publicVideoPort;
    uint16_t m_publicAudioPort;

    SignalingViewerCallback m_viewerCallback;
};
