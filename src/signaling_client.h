#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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


// true quando STREAM_SIGNALING_URL=none: o usuario escolheu nao
// usar servidor de sinalizacao (a conexao e feita por convite
// manual - ver invite_code.h).
bool SignalingIsDisabled();


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

    // Chave publica de quem transmite, em hex (pra criptografia
    // do canal). Vazia = client antigo, sem criptografia.
    std::string hostKey;

    // Versao do programa de quem transmite. Vazia = versao
    // antiga (ou servidor antigo), que nao informava.
    std::string hostVersion;

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
        uint16_t publicAudioPort,
        const std::string& hostKeyHex
    );

    void Stop();

    // Codigo atual da sessao (vazio se nao ha sessao).
    std::string GetCode();

    // Prova de que a sessao e nossa (pra anunciar a transmissao
    // numa sala - ver SignalingRoomPresent). Vazio sem sessao.
    std::string GetToken();

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

    std::string m_hostKeyHex;

    uint16_t m_publicVideoPort;
    uint16_t m_publicAudioPort;

    SignalingViewerCallback m_viewerCallback;
};


// ============================================================
// SALAS
// ============================================================
//
// Uma sala junta varias pessoas; qualquer uma pode transmitir
// e quem assiste escolhe quem ver. O servidor so guarda quem
// esta na sala e o codigo de SESSAO de cada um que transmite:
// pra assistir, o programa entra nessa sessao pelo caminho de
// sempre (SignalingJoinSession). Video e audio continuam indo
// direto de um computador pro outro.
//
// As funcoes abaixo sao chamadas de threads de trabalho (podem
// demorar enquanto o servidor gratuito "acorda").
// ============================================================

struct RoomMember
{
    std::string id;
    std::string name;
};

struct RoomPresenter
{
    std::string memberId;

    // Codigo da SESSAO de quem transmite (pra assistir).
    std::string sessionCode;

    std::string name;

    // O que esta sendo compartilhado (ex.: "Tela 1").
    std::string label;
};

struct RoomState
{
    std::vector<RoomMember> members;

    // Na ordem em que comecaram a transmitir.
    std::vector<RoomPresenter> presenters;
};

struct RoomChatMessage
{
    int id = 0;

    std::string memberId;
    std::string name;
    std::string text;
};

struct RoomMembership
{
    std::string code;
    std::string memberId;
    std::string token;
};

enum class RoomJoinResult
{
    Joined,
    NotFound,   // nao ha sala com esse codigo (pode ser de sessao)
    Full,
    Error
};

bool SignalingCreateRoom(
    const std::string& name,
    RoomMembership& outMembership,
    RoomState& outState,
    std::string& outError
);

RoomJoinResult SignalingJoinRoom(
    const std::string& code,
    const std::string& name,
    RoomMembership& outMembership,
    RoomState& outState,
    std::string& outError
);

// outGone = o servidor nao conhece mais este membro (sala
// fechou, ou ficamos tempo demais sem dar sinal de vida).
// chatAfter: traz tambem as mensagens de chat com id maior que
// esse (0 = as mais recentes guardadas no servidor).
bool SignalingRoomHeartbeat(
    const RoomMembership& membership,
    int chatAfter,
    RoomState& outState,
    std::vector<RoomChatMessage>& outChat,
    bool& outGone,
    std::string& outError
);

bool SignalingRoomChat(
    const RoomMembership& membership,
    const std::string& text,
    std::string& outError
);

bool SignalingRoomPresent(
    const RoomMembership& membership,
    const std::string& sessionCode,
    const std::string& sessionToken,
    const std::string& label,
    std::string& outError
);

bool SignalingRoomStop(
    const RoomMembership& membership,
    std::string& outError
);

bool SignalingRoomLeave(
    const RoomMembership& membership,
    std::string& outError
);
