// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#include "updater.h"

#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "signaling_client.h"
#include "update_key.h"
#include "update_signature.h"
#include "version.h"

namespace updater
{

namespace
{

constexpr const char* RELEASES_PAGE =
    "https://github.com/Nikao1/stream-project/releases";

// Instalador maior que isso nao e nosso (o normal e ~10 MB).
constexpr size_t MAX_INSTALLER_BYTES = 200u * 1024u * 1024u;

// AppId do instalador (installer/Colae.iss) - pra saber se esta
// copia foi instalada por ele.
constexpr const wchar_t* UNINSTALL_KEY =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\"
    L"{6E2B7C4A-9D31-4F8E-A5C2-3B7D1E0F9A64}_is1";

// Estado no heap, nunca destruido: uma thread que ainda esteja
// esperando a rede quando o programa fecha nao encontra nada
// desmontado (e o std::thread nao derruba o programa na saida).
struct State
{
    std::mutex mutex;

    std::atomic<Status> status{ Status::Idle };
    std::atomic<float> progress{ 0.0f };
    std::atomic<bool> stop{ false };

    std::thread worker;

    std::map<std::string, std::string> info;
    std::string error;
    std::wstring installerPath;
};

State& S()
{
    static State* state = new State();

    return *state;
}


std::wstring Widen(const std::string& text)
{
    if (text.empty())
        return std::wstring();

    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);

    std::wstring out(static_cast<size_t>(size), L'\0');

    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);

    return out;
}


std::string Info(const char* key)
{
    std::lock_guard<std::mutex> lock(S().mutex);

    auto it = S().info.find(key);

    return it == S().info.end() ? std::string() : it->second;
}


void Fail(const std::string& text)
{
    {
        std::lock_guard<std::mutex> lock(S().mutex);

        S().error = text;
    }

    std::cout << "Atualizacao: " << text << "\n";

    S().status = Status::Failed;
}


// So links do GitHub do projeto (a assinatura ja protege; isto e
// so pra nao sair baixando de qualquer lugar).
bool TrustedUrl(const std::string& url)
{
    return url.rfind("https://github.com/Nikao1/", 0) == 0;
}


bool InstalledByInstaller()
{
    wchar_t location[MAX_PATH]{};
    DWORD size = sizeof(location);

    if (RegGetValueW(
            HKEY_CURRENT_USER, UNINSTALL_KEY, L"InstallLocation",
            RRF_RT_REG_SZ, nullptr, location, &size) != ERROR_SUCCESS)
    {
        return false;
    }

    wchar_t exe[MAX_PATH]{};

    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH))
        return false;

    std::wstring folder(exe);

    size_t slash = folder.find_last_of(L"\\/");

    if (slash != std::wstring::npos)
        folder.resize(slash);

    std::wstring installed(location);

    while (!installed.empty() && (installed.back() == L'\\' || installed.back() == L'/'))
        installed.pop_back();

    return _wcsicmp(folder.c_str(), installed.c_str()) == 0;
}


void JoinWorker()
{
    if (S().worker.joinable())
        S().worker.join();
}


// ------------------------------------------------------------
// Download (WinHTTP; segue o redirecionamento do GitHub)
// ------------------------------------------------------------

bool Download(
    const std::string& url,
    std::vector<uint8_t>& out,
    std::string& outError)
{
    std::wstring wideUrl = Widen(url);

    wchar_t host[256]{};
    wchar_t path[2048]{};

    URL_COMPONENTS parts{};

    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = 255;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2047;

    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS)
    {
        outError = "link de download inválido";

        return false;
    }

    HINTERNET session = WinHttpOpen(
        L"Colae/" STREAM_APP_VERSION_W,
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;

    bool ok = false;

    do
    {
        if (!session)
            break;

        WinHttpSetTimeouts(session, 10000, 15000, 15000, 30000);

        connection = WinHttpConnect(session, host, parts.nPort, 0);

        if (!connection)
            break;

        request = WinHttpOpenRequest(
            connection, L"GET", path, nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE);

        if (!request)
            break;

        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request, nullptr))
        {
            break;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);

        WinHttpQueryHeaders(
            request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
            WINHTTP_NO_HEADER_INDEX);

        if (status != 200)
        {
            outError = "o download respondeu HTTP " + std::to_string(status);

            break;
        }

        DWORD total = 0;
        DWORD totalSize = sizeof(total);

        if (!WinHttpQueryHeaders(
                request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &total, &totalSize,
                WINHTTP_NO_HEADER_INDEX))
        {
            total = 0;
        }

        std::vector<uint8_t> buffer(64 * 1024);

        bool failed = false;

        for (;;)
        {
            if (S().stop)
            {
                failed = true;
                break;
            }

            DWORD read = 0;

            if (!WinHttpReadData(request, buffer.data(), static_cast<DWORD>(buffer.size()), &read))
            {
                failed = true;
                break;
            }

            if (read == 0)
                break;

            out.insert(out.end(), buffer.begin(), buffer.begin() + read);

            if (out.size() > MAX_INSTALLER_BYTES)
            {
                outError = "arquivo grande demais";
                failed = true;
                break;
            }

            if (total > 0)
                S().progress = static_cast<float>(out.size()) / static_cast<float>(total);
        }

        ok = !failed && !out.empty();
    }
    while (false);

    if (!ok && outError.empty())
        outError = "falha no download (verifique a internet)";

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);

    return ok;
}


bool ReadAllBytes(const std::wstring& path, std::vector<uint8_t>& out)
{
    FILE* file = _wfopen(path.c_str(), L"rb");

    if (!file)
        return false;

    uint8_t buffer[65536];

    size_t read = 0;

    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        out.insert(out.end(), buffer, buffer + read);

        if (out.size() > MAX_INSTALLER_BYTES)
            break;
    }

    std::fclose(file);

    return true;
}


// O arquivo confere com o resumo e a assinatura anunciados?
bool Verified(const std::vector<uint8_t>& data)
{
    std::string hash = UpdateHashHex(data.data(), data.size());

    return
        hash == Info("hash") &&
        UpdateVerify(COLAE_UPDATE_PUBLIC_KEY, Info("versao"), hash, Info("assinatura"));
}


void CheckWorker()
{
    std::map<std::string, std::string> fields;
    std::string error;

    if (!SignalingGetLatestVersion(fields, error))
    {
        Fail("sem resposta do servidor");

        return;
    }

    std::string latest = fields.count("versao") ? fields["versao"] : std::string();

    {
        std::lock_guard<std::mutex> lock(S().mutex);

        S().info = fields;
        S().error.clear();
    }

    if (!latest.empty() && IsNewerVersion(latest, STREAM_APP_VERSION))
    {
        std::cout << "Atualizacao: versao " << latest << " disponivel.\n";

        S().status = Status::Available;
    }
    else
    {
        S().status = Status::UpToDate;
    }
}


void DownloadWorker()
{
    S().progress = 0.0f;

    std::string version = Info("versao");
    std::string url = Info("url");

    std::vector<uint8_t> data;
    std::string error;

    if (!Download(url, data, error))
    {
        Fail(error);

        return;
    }

    if (!Verified(data))
    {
        Fail("o arquivo baixado não passou na verificação de segurança - não foi instalado");

        return;
    }

    // %TEMP%\Colae-atualizacao\Instalar-Colae-<versao>.exe
    wchar_t temp[MAX_PATH]{};

    GetTempPathW(MAX_PATH, temp);

    std::wstring folder = std::wstring(temp) + L"Colae-atualizacao";

    CreateDirectoryW(folder.c_str(), nullptr);

    std::wstring path = folder + L"\\Instalar-Colae-" + Widen(version) + L".exe";

    FILE* file = _wfopen(path.c_str(), L"wb");

    if (!file ||
        std::fwrite(data.data(), 1, data.size(), file) != data.size())
    {
        if (file)
            std::fclose(file);

        Fail("não consegui salvar o instalador");

        return;
    }

    std::fclose(file);

    {
        std::lock_guard<std::mutex> lock(S().mutex);

        S().installerPath = path;
    }

    S().progress = 1.0f;

    std::cout << "Atualizacao: instalador " << version << " baixado e conferido.\n";

    S().status = Status::Ready;
}

} // namespace


bool IsNewerVersion(
    const std::string& a,
    const std::string& b)
{
    auto parse =
        [](const std::string& text)
        {
            std::vector<long> parts;

            long value = 0;
            bool any = false;

            for (char c : text)
            {
                if (c >= '0' && c <= '9')
                {
                    value = value * 10 + (c - '0');
                    any = true;
                }
                else if (c == '.')
                {
                    parts.push_back(value);
                    value = 0;
                }
                else
                {
                    break;
                }
            }

            if (any)
                parts.push_back(value);

            return parts;
        };

    std::vector<long> left = parse(a);
    std::vector<long> right = parse(b);

    size_t count = (std::max)(left.size(), right.size());

    for (size_t i = 0; i < count; ++i)
    {
        long l = i < left.size() ? left[i] : 0;
        long r = i < right.size() ? right[i] : 0;

        if (l != r)
            return l > r;
    }

    return false;
}


void CheckAsync()
{
    Status status = S().status;

    if (status == Status::Checking ||
        status == Status::Downloading ||
        status == Status::Ready)
    {
        return;
    }

    JoinWorker();

    S().status = Status::Checking;

    S().worker = std::thread(CheckWorker);
}


Status GetStatus()
{
    return S().status;
}


std::string GetLatestVersion()
{
    return Info("versao");
}


std::string GetNotes()
{
    return Info("notas");
}


std::string ErrorText()
{
    std::lock_guard<std::mutex> lock(S().mutex);

    return S().error;
}


float DownloadProgress()
{
    return S().progress;
}


bool CanAutoInstall()
{
    static const bool installed = InstalledByInstaller();

    return
        installed &&
        COLAE_UPDATE_PUBLIC_KEY[0] != '\0' &&
        TrustedUrl(Info("url")) &&
        Info("hash").size() == 128 &&
        !Info("assinatura").empty();
}


void StartDownload()
{
    if (S().status != Status::Available || !CanAutoInstall())
        return;

    JoinWorker();

    S().status = Status::Downloading;

    S().worker = std::thread(DownloadWorker);
}


bool LaunchInstaller()
{
    if (S().status != Status::Ready)
        return false;

    std::wstring path;

    {
        std::lock_guard<std::mutex> lock(S().mutex);

        path = S().installerPath;
    }

    // Confere de novo o arquivo salvo (ninguem trocou no meio).
    std::vector<uint8_t> data;

    if (!ReadAllBytes(path, data) || !Verified(data))
    {
        Fail("o instalador salvo não passou na verificação de segurança");

        return false;
    }

    SHELLEXECUTEINFOW info{};

    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"open";
    info.lpFile = path.c_str();
    info.lpParameters = L"/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /ATUALIZAR";
    info.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&info))
    {
        Fail("não consegui abrir o instalador");

        return false;
    }

    std::cout << "Atualizacao: instalador aberto, fechando o programa.\n";

    return true;
}


void OpenReleasePage()
{
    std::string page = Info("pagina");

    if (!TrustedUrl(page))
        page = RELEASES_PAGE;

    ShellExecuteW(nullptr, L"open", Widen(page).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}


void Shutdown()
{
    S().stop = true;

    // Ainda esperando a rede: nao segura o fechamento do programa
    // (o estado fica vivo ate o processo acabar).
    Status status = S().status;

    if (status == Status::Checking || status == Status::Downloading)
    {
        if (S().worker.joinable())
            S().worker.detach();

        return;
    }

    JoinWorker();
}

} // namespace updater
