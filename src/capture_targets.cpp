// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#include "capture_targets.h"

#include <dwmapi.h>

#include <algorithm>

#pragma comment(lib, "dwmapi.lib")


namespace
{

std::string ToUtf8(
    const std::wstring& wide)
{
    if (wide.empty())
        return std::string();

    int size =
        WideCharToMultiByte(
            CP_UTF8,
            0,
            wide.c_str(),
            static_cast<int>(wide.size()),
            nullptr,
            0,
            nullptr,
            nullptr
        );

    std::string result(
        size > 0 ? static_cast<size_t>(size) : 0,
        '\0'
    );

    if (size > 0)
    {
        WideCharToMultiByte(
            CP_UTF8,
            0,
            wide.c_str(),
            static_cast<int>(wide.size()),
            result.data(),
            size,
            nullptr,
            nullptr
        );
    }

    return result;
}


// "C:\...\chrome.exe" -> "chrome.exe"
std::wstring ProcessFileName(
    DWORD processId)
{
    std::wstring result;

    HANDLE process =
        OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processId
        );

    if (!process)
        return result;

    wchar_t path[MAX_PATH]{};

    DWORD length = MAX_PATH;

    if (QueryFullProcessImageNameW(
            process,
            0,
            path,
            &length))
    {
        std::wstring full(path, length);

        size_t slash = full.find_last_of(L"\\/");

        result =
            slash == std::wstring::npos
            ? full
            : full.substr(slash + 1);
    }

    CloseHandle(process);

    return result;
}


// "chrome.exe" -> "chrome"
std::wstring WithoutExtension(
    const std::wstring& name)
{
    size_t dot = name.find_last_of(L'.');

    return
        dot == std::wstring::npos
        ? name
        : name.substr(0, dot);
}


// true = o processo roda como administrador (ou nao deixa nem
// ler o token, o que da no mesmo pra nos). Um programa comum nao
// consegue capturar janelas desses processos: o Windows recusa
// com "You do not have permission to capture the given window".
bool IsCurrentProcessElevated()
{
    static int cached = -1;

    if (cached >= 0)
        return cached == 1;

    cached = 0;

    HANDLE token = nullptr;

    if (OpenProcessToken(
            GetCurrentProcess(),
            TOKEN_QUERY,
            &token))
    {
        TOKEN_ELEVATION elevation{};

        DWORD size = 0;

        if (GetTokenInformation(
                token,
                TokenElevation,
                &elevation,
                sizeof(elevation),
                &size) &&
            elevation.TokenIsElevated)
        {
            cached = 1;
        }

        CloseHandle(token);
    }

    return cached == 1;
}


bool NeedsAdminToCapture(
    DWORD processId)
{
    // Rodando como administrador, capturamos tudo.
    if (IsCurrentProcessElevated())
        return false;

    HANDLE process =
        OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processId
        );

    if (!process)
        return GetLastError() == ERROR_ACCESS_DENIED;

    bool result = false;

    HANDLE token = nullptr;

    if (OpenProcessToken(
            process,
            TOKEN_QUERY,
            &token))
    {
        TOKEN_ELEVATION elevation{};

        DWORD size = 0;

        if (GetTokenInformation(
                token,
                TokenElevation,
                &elevation,
                sizeof(elevation),
                &size))
        {
            result = elevation.TokenIsElevated != 0;
        }

        CloseHandle(token);
    }
    else
    {
        result = GetLastError() == ERROR_ACCESS_DENIED;
    }

    CloseHandle(process);

    return result;
}


bool IsCloaked(
    HWND window)
{
    // Janelas "escondidas" pelo DWM: de outras areas de
    // trabalho virtuais, apps suspensos da Store etc. Estao
    // visiveis pro Windows, mas nao na tela.
    DWORD cloaked = 0;

    return
        SUCCEEDED(
            DwmGetWindowAttribute(
                window,
                DWMWA_CLOAKED,
                &cloaked,
                sizeof(cloaked))) &&
        cloaked != 0;
}


// Mesmas regras que o Alt+Tab usa (aproximadas): visivel, com
// titulo, sem ser janela de ferramenta e sendo a "dona" do
// proprio grupo.
bool IsShareableWindow(
    HWND window)
{
    if (!IsWindowVisible(window))
        return false;

    if (GetWindowTextLengthW(window) == 0)
        return false;

    LONG_PTR exStyle =
        GetWindowLongPtrW(window, GWL_EXSTYLE);

    if (exStyle & WS_EX_TOOLWINDOW)
        return false;

    if (GetAncestor(window, GA_ROOTOWNER) != window &&
        !(exStyle & WS_EX_APPWINDOW))
    {
        return false;
    }

    if (IsCloaked(window))
        return false;

    RECT rect{};

    if (!GetWindowRect(window, &rect) ||
        rect.right - rect.left <= 0 ||
        rect.bottom - rect.top <= 0)
    {
        return false;
    }

    return true;
}


// Apps da Microsoft Store: a janela pertence ao
// ApplicationFrameHost.exe, e o app de verdade (o que toca o
// som) e uma janela-filha de outro processo.
struct StoreAppSearch
{
    DWORD hostProcessId;

    DWORD appProcessId;
};


BOOL CALLBACK FindStoreAppChild(
    HWND child,
    LPARAM param)
{
    StoreAppSearch* search =
        reinterpret_cast<StoreAppSearch*>(param);

    DWORD processId = 0;

    GetWindowThreadProcessId(child, &processId);

    if (processId != 0 &&
        processId != search->hostProcessId)
    {
        search->appProcessId = processId;

        return FALSE;
    }

    return TRUE;
}


struct WindowListContext
{
    HWND exclude;

    std::vector<CaptureTarget>* targets;
};


BOOL CALLBACK CollectWindow(
    HWND window,
    LPARAM param)
{
    WindowListContext* context =
        reinterpret_cast<WindowListContext*>(param);

    if (window == context->exclude ||
        !IsShareableWindow(window))
    {
        return TRUE;
    }

    int titleLength =
        GetWindowTextLengthW(window);

    std::wstring title(
        static_cast<size_t>(titleLength) + 1,
        L'\0');

    title.resize(
        static_cast<size_t>(
            GetWindowTextW(
                window,
                title.data(),
                titleLength + 1)));

    DWORD processId = 0;

    GetWindowThreadProcessId(window, &processId);

    // A propria janela do programa ja foi excluida; outras do
    // mesmo processo (nenhuma hoje) tambem nao fazem sentido.
    if (processId == GetCurrentProcessId())
        return TRUE;

    std::wstring processName =
        ProcessFileName(processId);

    // O dono da janela (nao o app da Store dentro dela) e quem
    // decide se o Windows deixa capturar.
    bool needsAdmin =
        NeedsAdminToCapture(processId);

    if (_wcsicmp(
            processName.c_str(),
            L"ApplicationFrameHost.exe") == 0)
    {
        StoreAppSearch search{ processId, 0 };

        EnumChildWindows(
            window,
            FindStoreAppChild,
            reinterpret_cast<LPARAM>(&search)
        );

        if (search.appProcessId != 0)
        {
            processId = search.appProcessId;

            std::wstring appName =
                ProcessFileName(processId);

            if (!appName.empty())
            {
                processName = appName;
            }
        }
    }

    CaptureTarget target;

    target.kind = CaptureTargetKind::Window;
    target.window = window;
    target.title = ToUtf8(title);
    target.processId = processId;
    target.minimized = IsIconic(window) != FALSE;

    target.detail =
        processName.empty()
        ? std::string("programa desconhecido")
        : ToUtf8(WithoutExtension(processName));

    target.needsAdmin = needsAdmin;

    // A partida do LoL costuma chegar preta capturando so a
    // janela (a tela inteira funciona).
    // O titulo das duas janelas do LoL engana: a partida se chama
    // "League of Legends (TM) Client" e o cliente (lobby), so
    // "League of Legends".
    if (_wcsicmp(processName.c_str(), L"League of Legends.exe") == 0)
    {
        target.detail = "partida do LoL · melhor pela tela inteira";
    }
    else if (_wcsicmp(processName.c_str(), L"LeagueClientUx.exe") == 0)
    {
        target.detail = "cliente do LoL (lobby)";
    }

    if (needsAdmin)
    {
        target.detail +=
            " · aberto como administrador";
    }

    context->targets->push_back(std::move(target));

    return TRUE;
}


BOOL CALLBACK CollectMonitor(
    HMONITOR monitor,
    HDC,
    LPRECT,
    LPARAM param)
{
    std::vector<CaptureTarget>* targets =
        reinterpret_cast<std::vector<CaptureTarget>*>(param);

    MONITORINFOEXW info{};

    info.cbSize = sizeof(info);

    if (!GetMonitorInfoW(monitor, &info))
        return TRUE;

    // Resolucao real (em pixels fisicos), que e o que vai ser
    // capturado - o retangulo do GetMonitorInfo pode vir
    // reduzido pela escala do Windows.
    DEVMODEW mode{};

    mode.dmSize = sizeof(mode);

    int width =
        info.rcMonitor.right - info.rcMonitor.left;

    int height =
        info.rcMonitor.bottom - info.rcMonitor.top;

    if (EnumDisplaySettingsW(
            info.szDevice,
            ENUM_CURRENT_SETTINGS,
            &mode))
    {
        width = static_cast<int>(mode.dmPelsWidth);
        height = static_cast<int>(mode.dmPelsHeight);
    }

    CaptureTarget target;

    target.kind = CaptureTargetKind::Monitor;
    target.monitor = monitor;

    target.detail =
        std::to_string(width) +
        " x " +
        std::to_string(height);

    if (info.dwFlags & MONITORINFOF_PRIMARY)
    {
        target.detail += " · principal";
    }

    targets->push_back(std::move(target));

    return TRUE;
}

} // namespace


bool ClientIsElevated()
{
    return IsCurrentProcessElevated();
}


// ============================================================
// Listagens
// ============================================================

std::vector<CaptureTarget> ListCaptureMonitors()
{
    std::vector<CaptureTarget> monitors;

    EnumDisplayMonitors(
        nullptr,
        nullptr,
        CollectMonitor,
        reinterpret_cast<LPARAM>(&monitors)
    );

    // Principal primeiro.
    std::stable_sort(
        monitors.begin(),
        monitors.end(),
        [](const CaptureTarget& a, const CaptureTarget& b)
        {
            bool aPrimary =
                a.detail.find("principal") != std::string::npos;

            bool bPrimary =
                b.detail.find("principal") != std::string::npos;

            return aPrimary && !bPrimary;
        }
    );

    for (size_t i = 0; i < monitors.size(); ++i)
    {
        monitors[i].title =
            "Tela " + std::to_string(i + 1);
    }

    return monitors;
}


std::vector<CaptureTarget> ListCaptureWindows(
    HWND exclude)
{
    std::vector<CaptureTarget> windows;

    WindowListContext context{ exclude, &windows };

    // EnumWindows percorre da janela da frente pra de tras -
    // a mais usada recentemente vem primeiro.
    EnumWindows(
        CollectWindow,
        reinterpret_cast<LPARAM>(&context)
    );

    return windows;
}


bool CaptureTargetStillValid(
    const CaptureTarget& target)
{
    if (target.kind == CaptureTargetKind::Window)
    {
        return
            target.window != nullptr &&
            IsWindow(target.window) != FALSE;
    }

    MONITORINFO info{};

    info.cbSize = sizeof(info);

    return
        target.monitor != nullptr &&
        GetMonitorInfoW(target.monitor, &info) != FALSE;
}


// ============================================================
// SupportsPerWindowAudio
// ============================================================
//
// A captura de som por processo existe a partir do build 20348
// do Windows (Windows 11 e Windows Server 2022). GetVersionEx
// "mente" a versao pra programas sem manifesto, entao a versao
// real vem do RtlGetVersion, direto do ntdll.
// ============================================================

bool SupportsPerWindowAudio()
{
    using RtlGetVersionFn =
        LONG (WINAPI*)(OSVERSIONINFOW*);

    HMODULE ntdll =
        GetModuleHandleW(L"ntdll.dll");

    if (!ntdll)
        return false;

    RtlGetVersionFn rtlGetVersion =
        reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(
                GetProcAddress(ntdll, "RtlGetVersion")));

    if (!rtlGetVersion)
        return false;

    OSVERSIONINFOW version{};

    version.dwOSVersionInfoSize = sizeof(version);

    if (rtlGetVersion(&version) != 0)
        return false;

    return
        version.dwMajorVersion > 10 ||
        (version.dwMajorVersion == 10 &&
         version.dwBuildNumber >= 20348);
}
