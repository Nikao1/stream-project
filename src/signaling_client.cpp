#include "signaling_client.h"
#include "room_crypto.h"
#include "version.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>

#include <chrono>
#include <utility>
#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")


namespace
{

constexpr int POLL_INTERVAL_SECONDS = 2;


// Servidor de sinalizacao padrao (hospedado no Render, plano
// gratuito). Quem quiser usar outro servidor - o proprio, ou
// um local pra desenvolvimento - define a variavel de ambiente
// STREAM_SIGNALING_URL, sem precisar recompilar.
constexpr const char* DEFAULT_SIGNALING_URL =
    "https://stream-signaling-2uaz.onrender.com";


std::wstring ToWide(
    const std::string& text)
{
    // URLs, caminhos e codigos aqui sao sempre ASCII.
    return std::wstring(
        text.begin(),
        text.end()
    );
}


// ------------------------------------------------------------
// Resposta do servidor: linhas "chave=valor"
// ------------------------------------------------------------

std::map<std::string, std::string> ParseKeyValues(
    const std::string& body)
{
    std::map<std::string, std::string> result;

    std::istringstream stream(body);

    std::string line;

    while (std::getline(stream, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }

        size_t equals = line.find('=');

        if (equals == std::string::npos)
            continue;

        result[line.substr(0, equals)] =
            line.substr(equals + 1);
    }

    return result;
}


uint16_t ParsePort(
    const std::string& text)
{
    int value = std::atoi(text.c_str());

    if (value < 1 || value > 65535)
        return 0;

    return static_cast<uint16_t>(value);
}


// "1.2.3.4:5678" -> ip + porta
bool ParseEndpoint(
    const std::string& text,
    std::string& outIp,
    uint16_t& outPort)
{
    size_t colon = text.find(':');

    if (colon == std::string::npos || colon == 0)
        return false;

    outIp = text.substr(0, colon);
    outPort = ParsePort(text.substr(colon + 1));

    return outPort != 0;
}


// ------------------------------------------------------------
// HttpPost
//
// POST simples (corpo em formato de formulario). Funciona com
// http:// e https:// - o WinHTTP cuida do TLS.
//
// Retorna false so em falha de REDE; erro do servidor (404
// etc.) retorna true com o status preenchido.
// ------------------------------------------------------------

bool HttpPost(
    const std::string& path,
    const std::string& body,
    DWORD& outStatus,
    std::string& outResponse,
    std::string& outError)
{
    outStatus = 0;
    outResponse.clear();

    std::wstring url =
        ToWide(GetSignalingUrl());

    wchar_t host[256]{};

    URL_COMPONENTS parts{};

    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = 255;

    if (!WinHttpCrackUrl(
            url.c_str(),
            0,
            0,
            &parts))
    {
        outError =
            "endereco do servidor de sinalizacao invalido: " +
            GetSignalingUrl();

        return false;
    }

    bool secure =
        parts.nScheme == INTERNET_SCHEME_HTTPS;

    HINTERNET session = nullptr;
    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;

    bool ok = false;

    do
    {
        session =
            WinHttpOpen(
                L"Chegae/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );

        if (!session)
            break;

        // resolver / conectar / enviar / receber (ms)
        WinHttpSetTimeouts(
            session,
            5000,
            8000,
            8000,
            20000
        );

        connection =
            WinHttpConnect(
                session,
                host,
                parts.nPort,
                0
            );

        if (!connection)
            break;

        std::wstring widePath =
            ToWide(path);

        request =
            WinHttpOpenRequest(
                connection,
                L"POST",
                widePath.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                secure ? WINHTTP_FLAG_SECURE : 0
            );

        if (!request)
            break;

        const wchar_t* headers =
            L"Content-Type: application/x-www-form-urlencoded\r\n";

        if (!WinHttpSendRequest(
                request,
                headers,
                static_cast<DWORD>(-1),
                body.empty()
                    ? WINHTTP_NO_REQUEST_DATA
                    : const_cast<char*>(body.data()),
                static_cast<DWORD>(body.size()),
                static_cast<DWORD>(body.size()),
                0))
        {
            break;
        }

        if (!WinHttpReceiveResponse(
                request,
                nullptr))
        {
            break;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);

        if (!WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_STATUS_CODE |
                    WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &status,
                &statusSize,
                WINHTTP_NO_HEADER_INDEX))
        {
            break;
        }

        outStatus = status;

        // As respostas sao minusculas; 64 KB e um teto de
        // seguranca contra um servidor malcomportado.
        char buffer[1024];

        bool readFailed = false;

        for (;;)
        {
            DWORD read = 0;

            if (!WinHttpReadData(
                    request,
                    buffer,
                    sizeof(buffer),
                    &read))
            {
                readFailed = true;
                break;
            }

            if (read == 0)
                break;

            outResponse.append(buffer, read);

            if (outResponse.size() > 64 * 1024)
                break;
        }

        if (readFailed)
            break;

        ok = true;
    }
    while (false);

    if (!ok)
    {
        outError =
            "sem resposta do servidor de sinalizacao (" +
            GetSignalingUrl() +
            ", erro " +
            std::to_string(GetLastError()) +
            ")";
    }

    if (request)
        WinHttpCloseHandle(request);

    if (connection)
        WinHttpCloseHandle(connection);

    if (session)
        WinHttpCloseHandle(session);

    return ok;
}

// ------------------------------------------------------------
// HttpPostWaking
//
// Igual ao HttpPost, mas com paciencia: hospedagens gratuitas
// "dormem" depois de um tempo sem uso e levam ate ~1 minuto
// pra acordar na primeira requisicao. Enquanto a resposta nao
// for a esperada (precisa conter "expectKey="), tenta de novo
// por ate 90 segundos.
//
// Com o servidor na propria maquina (testes) nao ha o que
// esperar: falhou, falhou.
// ------------------------------------------------------------

bool HttpPostWaking(
    const std::string& path,
    const std::string& body,
    const std::string& expectKey,
    DWORD& outStatus,
    std::string& outResponse,
    std::string& outError)
{
    std::string url = GetSignalingUrl();

    // "none" = sinalizacao desligada de proposito (ver
    // SignalingIsDisabled): tambem nao ha o que esperar.
    bool isLocal =
        url == "none" ||
        url.find("127.0.0.1") != std::string::npos ||
        url.find("localhost") != std::string::npos;

    ULONGLONG start = GetTickCount64();

    bool announced = false;

    for (;;)
    {
        bool ok =
            HttpPost(
                path,
                body,
                outStatus,
                outResponse,
                outError
            );

        // Resposta de verdade do nosso servidor (sucesso ou
        // um erro dele, como 404 de sessao inexistente).
        bool answered =
            ok &&
            ((outStatus == 200 &&
              outResponse.find(expectKey + "=") !=
                  std::string::npos) ||
             outStatus == 400 ||
             outStatus == 404 ||
             outStatus == 409 ||
             outStatus == 426 ||
             outStatus == 429);

        if (answered || isLocal)
            return ok;

        if (GetTickCount64() - start >= 90000)
        {
            if (ok)
            {
                outError =
                    "o servidor de sinalizacao nao respondeu "
                    "como esperado (HTTP " +
                    std::to_string(outStatus) +
                    ")";
            }

            return false;
        }

        if (!announced)
        {
            std::cout
                << "Servidor de sinalizacao acordando - isso "
                << "pode levar ate 1 minuto...\n";

            announced = true;
        }

        Sleep(3000);
    }
}

} // namespace


// ============================================================
// GetSignalingUrl
// ============================================================

std::string GetSignalingUrl()
{
    char value[512]{};

    DWORD length =
        GetEnvironmentVariableA(
            "STREAM_SIGNALING_URL",
            value,
            sizeof(value)
        );

    if (length > 0 && length < sizeof(value))
    {
        std::string url(value);

        while (!url.empty() && url.back() == '/')
        {
            url.pop_back();
        }

        if (!url.empty())
            return url;
    }

    return DEFAULT_SIGNALING_URL;
}


// ============================================================
// SignalingIsDisabled
// ============================================================

bool SignalingIsDisabled()
{
    return GetSignalingUrl() == "none";
}


// ============================================================
// GetPrimaryLocalIp
// ============================================================
//
// Truque classico: "conectar" um socket UDP a um endereco
// externo NAO envia nenhum pacote, mas faz o Windows escolher
// a interface de saida - e ai getsockname() devolve o IP local
// dela.
// ============================================================

std::string GetPrimaryLocalIp()
{
    std::string result = "127.0.0.1";

    SOCKET probe =
        socket(
            AF_INET,
            SOCK_DGRAM,
            IPPROTO_UDP
        );

    if (probe == INVALID_SOCKET)
        return result;

    sockaddr_in target{};

    target.sin_family = AF_INET;
    target.sin_port = htons(53);

    inet_pton(
        AF_INET,
        "8.8.8.8",
        &target.sin_addr
    );

    if (connect(
            probe,
            reinterpret_cast<sockaddr*>(&target),
            sizeof(target)) == 0)
    {
        sockaddr_in local{};

        int localSize = sizeof(local);

        if (getsockname(
                probe,
                reinterpret_cast<sockaddr*>(&local),
                &localSize) == 0)
        {
            char ip[INET_ADDRSTRLEN]{};

            if (inet_ntop(
                    AF_INET,
                    &local.sin_addr,
                    ip,
                    sizeof(ip)))
            {
                result = ip;
            }
        }
    }

    closesocket(probe);

    return result;
}


// ============================================================
// LooksLikeSessionCode
// ============================================================

bool LooksLikeSessionCode(
    const std::string& text)
{
    if (text.size() != 6)
        return false;

    for (char c : text)
    {
        bool letter =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z');

        bool digit =
            c >= '0' && c <= '9';

        if (!letter && !digit)
            return false;
    }

    return true;
}


// ============================================================
// SignalingJoinSession
// ============================================================

bool SignalingJoinSession(
    const std::string& code,
    SignalingHostInfo& outInfo,
    std::string& outError)
{
    DWORD status = 0;

    std::string response;

    if (!HttpPostWaking(
            "/sessions/" + code + "/join",
            "",
            "host_local_ip",
            status,
            response,
            outError))
    {
        return false;
    }

    if (status == 404)
    {
        outError =
            "sessao nao encontrada - confira o codigo "
            "(ou a transmissao ja foi encerrada)";

        return false;
    }

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return false;
    }

    auto fields =
        ParseKeyValues(response);

    outInfo.publicIp = fields["host_public_ip"];
    outInfo.localIp = fields["host_local_ip"];
    outInfo.videoPort = ParsePort(fields["video_port"]);
    outInfo.audioPort = ParsePort(fields["audio_port"]);
    outInfo.sameNetwork = fields["same_network"] == "1";
    outInfo.hostKey = fields["host_key"];
    outInfo.hostVersion = fields["host_version"];

    outInfo.publicVideoPort =
        ParsePort(fields["public_video_port"]);

    outInfo.publicAudioPort =
        ParsePort(fields["public_audio_port"]);

    // Servidor antigo (sem STUN): assume porta publica = local.
    if (outInfo.publicVideoPort == 0)
        outInfo.publicVideoPort = outInfo.videoPort;

    if (outInfo.publicAudioPort == 0)
        outInfo.publicAudioPort = outInfo.audioPort;

    if (outInfo.publicIp.empty() ||
        outInfo.localIp.empty() ||
        outInfo.videoPort == 0 ||
        outInfo.audioPort == 0)
    {
        outError =
            "resposta invalida do servidor de sinalizacao";

        return false;
    }

    return true;
}


// ============================================================
// SignalingAnnounceViewer
// ============================================================

bool SignalingAnnounceViewer(
    const std::string& code,
    bool isAudio,
    const std::string& publicIp,
    uint16_t publicPort,
    std::string& outError)
{
    std::string body =
        std::string("channel=") +
        (isAudio ? "audio" : "video") +
        "&ip=" + publicIp +
        "&port=" + std::to_string(publicPort);

    DWORD status = 0;

    std::string response;

    if (!HttpPost(
            "/sessions/" + code + "/announce",
            body,
            status,
            response,
            outError))
    {
        return false;
    }

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return false;
    }

    return true;
}


// ============================================================
// SignalingHost
// ============================================================

SignalingHost::SignalingHost()
    : m_running(false),
      m_videoPort(0),
      m_audioPort(0),
      m_publicVideoPort(0),
      m_publicAudioPort(0)
{
}


SignalingHost::~SignalingHost()
{
    Stop();
}


bool SignalingHost::CreateSession(
    std::string& outError)
{
    std::string body =
        "local_ip=" + GetPrimaryLocalIp() +
        "&video_port=" + std::to_string(m_videoPort) +
        "&audio_port=" + std::to_string(m_audioPort) +
        "&public_video_port=" +
            std::to_string(m_publicVideoPort) +
        "&public_audio_port=" +
            std::to_string(m_publicAudioPort);

    if (!m_stunIp.empty())
    {
        body += "&stun_ip=" + m_stunIp;
    }

    if (!m_hostKeyHex.empty())
    {
        body += "&host_key=" + m_hostKeyHex;
    }

    body += "&app_version=" STREAM_APP_VERSION;

    DWORD status = 0;

    std::string response;

    if (!HttpPostWaking(
            "/sessions",
            body,
            "code",
            status,
            response,
            outError))
    {
        return false;
    }

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return false;
    }

    auto fields =
        ParseKeyValues(response);

    if (fields["code"].empty() ||
        fields["token"].empty())
    {
        outError =
            "resposta invalida do servidor de sinalizacao";

        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    m_code = fields["code"];
    m_token = fields["token"];

    return true;
}


void SignalingHost::SetViewerCallback(
    SignalingViewerCallback callback)
{
    m_viewerCallback = std::move(callback);
}


bool SignalingHost::Start(
    uint16_t videoPort,
    uint16_t audioPort,
    const std::string& stunIp,
    uint16_t publicVideoPort,
    uint16_t publicAudioPort,
    const std::string& hostKeyHex)
{
    if (m_running)
        return true;

    if (SignalingIsDisabled())
        return false;

    m_hostKeyHex = hostKeyHex;

    m_videoPort = videoPort;
    m_audioPort = audioPort;

    m_stunIp = stunIp;

    m_publicVideoPort =
        publicVideoPort != 0 ? publicVideoPort : videoPort;

    m_publicAudioPort =
        publicAudioPort != 0 ? publicAudioPort : audioPort;

    std::string error;

    if (!CreateSession(error))
    {
        std::cout
            << "Sinalizacao: " << error << "\n";

        return false;
    }

    m_running = true;

    m_thread =
        std::thread(
            &SignalingHost::KeepAliveLoop,
            this
        );

    return true;
}


void SignalingHost::KeepAliveLoop()
{
    while (m_running)
    {
        {
            std::unique_lock<std::mutex> lock(m_mutex);

            m_cv.wait_for(
                lock,
                std::chrono::seconds(
                    POLL_INTERVAL_SECONDS),
                [this] { return !m_running.load(); }
            );
        }

        if (!m_running)
            break;

        std::string code;
        std::string token;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            code = m_code;
            token = m_token;
        }

        DWORD status = 0;

        std::string response;
        std::string error;

        if (!HttpPost(
                "/sessions/" + code + "/keepalive",
                "token=" + token,
                status,
                response,
                error))
        {
            // Falha de rede passageira: tenta de novo no
            // proximo ciclo, sem poluir o console.
            continue;
        }

        if (status == 404 && m_running)
        {
            // O servidor esqueceu a sessao (reiniciou, ou
            // ficamos tempo demais sem rede): cria outra.
            if (CreateSession(error))
            {
                std::cout
                    << "\nSinalizacao: a sessao anterior "
                    << "expirou. NOVO CODIGO: "
                    << GetCode()
                    << "\n";
            }

            continue;
        }

        if (status != 200 || !m_viewerCallback)
            continue;

        // Um receiver entrou e anunciou seus enderecos
        // publicos: repassa pra quem faz o hole punching.
        auto fields =
            ParseKeyValues(response);

        // Cada campo e uma lista "ip:porta,ip:porta" - um
        // item por espectador que anunciou ha pouco.
        for (int channel = 0; channel < 2; ++channel)
        {
            bool isAudio = channel == 1;

            std::istringstream list(
                fields[isAudio
                    ? "viewer_audio"
                    : "viewer_video"]);

            std::string item;

            while (std::getline(list, item, ','))
            {
                std::string ip;

                uint16_t port = 0;

                if (ParseEndpoint(item, ip, port))
                {
                    m_viewerCallback(isAudio, ip, port);
                }
            }
        }
    }
}


void SignalingHost::Stop()
{
    bool wasRunning = false;

    {
        // Com o mutex: garante que a thread de keepalive nao
        // perca o aviso entre checar m_running e dormir.
        std::lock_guard<std::mutex> lock(m_mutex);

        wasRunning =
            m_running.exchange(false);
    }

    m_cv.notify_all();

    if (m_thread.joinable())
    {
        m_thread.join();
    }

    if (!wasRunning)
        return;

    // Avisa o servidor que a sessao acabou (se falhar, ela
    // expira sozinha em ~1 minuto).
    std::string code;
    std::string token;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        code = m_code;
        token = m_token;

        m_code.clear();
        m_token.clear();
    }

    if (!code.empty())
    {
        DWORD status = 0;

        std::string response;
        std::string error;

        HttpPost(
            "/sessions/" + code + "/close",
            "token=" + token,
            status,
            response,
            error
        );
    }
}


std::string SignalingHost::GetCode()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return m_code;
}


std::string SignalingHost::GetToken()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    return m_code.empty() ? std::string() : m_token;
}


// ============================================================
// SALAS
// ============================================================

namespace
{

// Nome/descricao no corpo da requisicao: %XX pra tudo que nao
// e letra/numero ASCII (o nome pode ter acento, espaco, &...).
std::string UrlEncode(
    const std::string& text)
{
    static const char* hex = "0123456789ABCDEF";

    std::string result;

    for (unsigned char c : text)
    {
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
        {
            result += static_cast<char>(c);
        }
        else
        {
            result += '%';
            result += hex[c >> 4];
            result += hex[c & 0xF];
        }
    }

    return result;
}


std::string UrlDecode(
    const std::string& text)
{
    auto hexValue = [](char c) -> int
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };

    std::string result;

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '%' &&
            i + 2 < text.size())
        {
            int high = hexValue(text[i + 1]);
            int low = hexValue(text[i + 2]);

            if (high >= 0 && low >= 0)
            {
                result += static_cast<char>(high * 16 + low);

                i += 2;

                continue;
            }
        }

        result += text[i];
    }

    return result;
}


// "a|b|c" -> {"a", "b", "c"}
std::vector<std::string> SplitBars(
    const std::string& text)
{
    std::vector<std::string> parts;

    size_t start = 0;

    for (;;)
    {
        size_t bar = text.find('|', start);

        if (bar == std::string::npos)
        {
            parts.push_back(text.substr(start));

            return parts;
        }

        parts.push_back(text.substr(start, bar - start));

        start = bar + 1;
    }
}


// Estado da sala vindo do servidor (ver signaling_server.py).
void ParseRoomState(
    std::map<std::string, std::string>& fields,
    RoomState& outState)
{
    outState = RoomState{};

    outState.title = UrlDecode(fields["room_title"]);

    int members =
        std::atoi(fields["member_count"].c_str());

    for (int i = 0; i < members && i < 64; ++i)
    {
        std::vector<std::string> parts =
            SplitBars(fields["member_" + std::to_string(i)]);

        if (parts.size() < 2)
            continue;

        outState.members.push_back(
            RoomMember{
                parts[0],
                UrlDecode(parts[1]),
                parts.size() >= 3 ? parts[2] : std::string(),
                parts.size() >= 4 ? parts[3] : std::string() });
    }

    int presenters =
        std::atoi(fields["presenter_count"].c_str());

    for (int i = 0; i < presenters && i < 64; ++i)
    {
        std::vector<std::string> parts =
            SplitBars(fields["presenter_" + std::to_string(i)]);

        if (parts.size() < 4)
            continue;

        outState.presenters.push_back(
            RoomPresenter{
                parts[0],
                parts[1],
                UrlDecode(parts[2]),
                UrlDecode(parts[3]),
                parts.size() > 4 ? parts[4] : std::string() });
    }
}


std::string MemberBody(
    const RoomMembership& membership)
{
    return
        "member_id=" + membership.memberId +
        "&token=" + membership.token;
}


std::string RoomPath(
    const RoomMembership& membership,
    const char* action)
{
    return
        "/rooms/" + membership.code + "/" + action;
}

} // namespace


bool SignalingCreateRoom(
    const std::string& name,
    const std::string& title,
    const std::string& saltHex,
    RoomMembership& outMembership,
    RoomState& outState,
    std::string& outError)
{
    DWORD status = 0;

    std::string response;

    if (!HttpPostWaking(
            "/rooms",
            "name=" + UrlEncode(name) +
                (saltHex.empty() ? std::string()
                                 : "&e2e=1&salt=" + saltHex) +
                (title.empty() ? std::string()
                               : "&title=" + UrlEncode(title)),
            "code",
            status,
            response,
            outError))
    {
        return false;
    }

    if (status != 200)
    {
        outError =
            status == 404
            ? "o servidor de sinalizacao ainda nao tem salas "
              "(versao antiga do servidor)"
            : "servidor de sinalizacao respondeu HTTP " +
              std::to_string(status);

        return false;
    }

    auto fields =
        ParseKeyValues(response);

    outMembership.code = fields["code"];
    outMembership.memberId = fields["member_id"];
    outMembership.token = fields["token"];

    // Servidor antigo ignora a criptografia de ponta a ponta:
    // melhor nao criar a sala do que criar uma sem protecao.
    if (!saltHex.empty() && fields["e2e"] != "1")
    {
        outError =
            "o servidor de sinalizacao esta desatualizado (sem "
            "criptografia de ponta a ponta)";

        return false;
    }

    if (outMembership.code.empty() ||
        outMembership.memberId.empty() ||
        outMembership.token.empty())
    {
        outError =
            "resposta invalida do servidor de sinalizacao";

        return false;
    }

    ParseRoomState(fields, outState);

    return true;
}


RoomJoinResult SignalingRoomInfo(
    const std::string& code,
    bool& outEncrypted,
    std::string& outSaltHex,
    std::string& outError)
{
    outEncrypted = false;
    outSaltHex.clear();

    DWORD status = 0;

    std::string response;

    if (!HttpPostWaking(
            "/rooms/" + code + "/info",
            std::string(),
            "e2e",
            status,
            response,
            outError))
    {
        return RoomJoinResult::Error;
    }

    // Sem sala (ou servidor antigo, sem esta rota): quem chama
    // tenta como sessao.
    if (status == 404)
        return RoomJoinResult::NotFound;

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return RoomJoinResult::Error;
    }

    auto fields = ParseKeyValues(response);

    outEncrypted = fields["e2e"] == "1";
    outSaltHex = fields["salt"];

    return RoomJoinResult::Joined;
}


RoomJoinResult SignalingJoinRoom(
    const std::string& code,
    const std::string& name,
    bool encrypted,
    RoomMembership& outMembership,
    RoomState& outState,
    std::string& outError)
{
    DWORD status = 0;

    std::string response;

    if (!HttpPostWaking(
            "/rooms/" + code + "/join",
            "name=" + UrlEncode(name) +
                (encrypted ? "&e2e=1" : ""),
            "member_id",
            status,
            response,
            outError))
    {
        return RoomJoinResult::Error;
    }

    // 404 = sem sala com esse codigo. Servidor antigo (sem
    // salas) tambem responde 404 - nos dois casos quem chama
    // tenta o codigo como sessao.
    if (status == 404)
        return RoomJoinResult::NotFound;

    if (status == 409)
        return RoomJoinResult::Full;

    if (status == 426)
        return RoomJoinResult::NeedsKey;

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return RoomJoinResult::Error;
    }

    auto fields =
        ParseKeyValues(response);

    outMembership.code = code;
    outMembership.memberId = fields["member_id"];
    outMembership.token = fields["token"];

    if (outMembership.memberId.empty() ||
        outMembership.token.empty())
    {
        outError =
            "resposta invalida do servidor de sinalizacao";

        return RoomJoinResult::Error;
    }

    ParseRoomState(fields, outState);

    return RoomJoinResult::Joined;
}


bool SignalingRoomHeartbeat(
    const RoomMembership& membership,
    int chatAfter,
    RoomState& outState,
    std::vector<RoomChatMessage>& outChat,
    bool& outGone,
    std::string& outError)
{
    outGone = false;

    outChat.clear();

    DWORD status = 0;

    std::string response;

    if (!HttpPost(
            RoomPath(membership, "heartbeat"),
            MemberBody(membership) +
                "&chat_after=" + std::to_string(chatAfter),
            status,
            response,
            outError))
    {
        return false;
    }

    if (status == 404)
    {
        outGone = true;

        outError = "fora da sala";

        return false;
    }

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return false;
    }

    auto fields =
        ParseKeyValues(response);

    ParseRoomState(fields, outState);

    int count =
        std::atoi(fields["chat_count"].c_str());

    for (int i = 0; i < count && i < 100; ++i)
    {
        std::vector<std::string> parts =
            SplitBars(fields["chat_" + std::to_string(i)]);

        if (parts.size() < 4)
            continue;

        RoomChatMessage message;

        message.id = std::atoi(parts[0].c_str());
        message.memberId = parts[1];
        message.name = UrlDecode(parts[2]);
        message.text = UrlDecode(parts[3]);

        outChat.push_back(message);
    }

    return true;
}


bool SignalingRoomChat(
    const RoomMembership& membership,
    const std::string& text,
    std::string& outError)
{
    DWORD status = 0;

    std::string response;

    if (!HttpPost(
            RoomPath(membership, "chat"),
            MemberBody(membership) +
                "&text=" + UrlEncode(text),
            status,
            response,
            outError))
    {
        return false;
    }

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return false;
    }

    return true;
}


bool SignalingRoomTitle(
    const RoomMembership& membership,
    const std::string& title,
    std::string& outError)
{
    DWORD status = 0;

    std::string response;

    if (!HttpPost(
            RoomPath(membership, "title"),
            MemberBody(membership) +
                "&title=" + UrlEncode(title),
            status,
            response,
            outError))
    {
        return false;
    }

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return false;
    }

    return true;
}


bool SignalingRoomPresent(
    const RoomMembership& membership,
    const std::string& sessionCode,
    const std::string& sessionToken,
    const std::string& label,
    std::string& outError)
{
    DWORD status = 0;

    std::string response;

    if (!HttpPost(
            RoomPath(membership, "present"),
            MemberBody(membership) +
                "&session_code=" + sessionCode +
                "&session_token=" + sessionToken +
                "&label=" + UrlEncode(label),
            status,
            response,
            outError))
    {
        return false;
    }

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return false;
    }

    return true;
}


// (Base64UrlEncode/Decode: room_crypto.h)


namespace
{

bool PostRoomImage(
    const RoomMembership& membership,
    const char* action,
    const std::vector<uint8_t>& jpeg,
    std::string& outError)
{
    DWORD status = 0;

    std::string response;

    std::string body = MemberBody(membership);

    if (!jpeg.empty())
    {
        // Base64 urlsafe nao precisa de escape no formulario
        // (o '=' do fim e aceito no valor).
        body += "&image=" + Base64UrlEncode(jpeg);
    }

    if (!HttpPost(
            RoomPath(membership, action),
            body,
            status,
            response,
            outError))
    {
        return false;
    }

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return false;
    }

    return true;
}


bool GetRoomImage(
    const RoomMembership& membership,
    const char* action,
    const std::string& targetMemberId,
    std::vector<uint8_t>& outJpeg,
    std::string& outVersion,
    std::string& outError)
{
    outJpeg.clear();
    outVersion.clear();

    DWORD status = 0;

    std::string response;

    if (!HttpPost(
            RoomPath(membership, action),
            MemberBody(membership) + "&target=" + targetMemberId,
            status,
            response,
            outError))
    {
        return false;
    }

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return false;
    }

    auto fields = ParseKeyValues(response);

    outVersion = fields["version"];

    if (!Base64UrlDecode(fields["image"], outJpeg) ||
        outJpeg.size() < 2)
    {
        outError = "previa invalida";

        return false;
    }

    return true;
}


} // namespace


bool SignalingRoomThumb(
    const RoomMembership& membership,
    const std::vector<uint8_t>& jpeg,
    std::string& outError)
{
    return PostRoomImage(membership, "thumb", jpeg, outError);
}


bool SignalingRoomGetThumb(
    const RoomMembership& membership,
    const std::string& targetMemberId,
    std::vector<uint8_t>& outJpeg,
    std::string& outVersion,
    std::string& outError)
{
    return GetRoomImage(
        membership, "thumb_get", targetMemberId,
        outJpeg, outVersion, outError);
}


bool SignalingRoomAvatar(
    const RoomMembership& membership,
    const std::vector<uint8_t>& image,
    std::string& outError)
{
    return PostRoomImage(membership, "avatar", image, outError);
}


bool SignalingRoomGetAvatar(
    const RoomMembership& membership,
    const std::string& targetMemberId,
    std::vector<uint8_t>& outImage,
    std::string& outVersion,
    std::string& outError)
{
    return GetRoomImage(
        membership, "avatar_get", targetMemberId,
        outImage, outVersion, outError);
}


bool SignalingRoomVoice(
    const RoomMembership& membership,
    const std::string& sessionCode,
    const std::string& sessionToken,
    std::string& outError)
{
    DWORD status = 0;

    std::string response;

    std::string body =
        MemberBody(membership);

    if (!sessionCode.empty())
    {
        body +=
            "&session_code=" + sessionCode +
            "&session_token=" + sessionToken;
    }

    if (!HttpPost(
            RoomPath(membership, "voice"),
            body,
            status,
            response,
            outError))
    {
        return false;
    }

    if (status != 200)
    {
        outError =
            "servidor de sinalizacao respondeu HTTP " +
            std::to_string(status);

        return false;
    }

    return true;
}


bool SignalingRoomStop(
    const RoomMembership& membership,
    std::string& outError)
{
    DWORD status = 0;

    std::string response;

    if (!HttpPost(
            RoomPath(membership, "stop"),
            MemberBody(membership),
            status,
            response,
            outError))
    {
        return false;
    }

    return status == 200;
}


bool SignalingRoomLeave(
    const RoomMembership& membership,
    std::string& outError)
{
    DWORD status = 0;

    std::string response;

    if (!HttpPost(
            RoomPath(membership, "leave"),
            MemberBody(membership),
            status,
            response,
            outError))
    {
        return false;
    }

    return status == 200;
}
