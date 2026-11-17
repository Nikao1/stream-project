#include "signaling_client.h"

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
                L"StreamProject/1.0",
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

    bool isLocal =
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
    uint16_t publicAudioPort)
{
    if (m_running)
        return true;

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

        std::string ip;

        uint16_t port = 0;

        if (ParseEndpoint(fields["viewer_video"], ip, port))
        {
            m_viewerCallback(false, ip, port);
        }

        if (ParseEndpoint(fields["viewer_audio"], ip, port))
        {
            m_viewerCallback(true, ip, port);
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
