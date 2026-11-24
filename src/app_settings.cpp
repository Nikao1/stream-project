#include "app_settings.h"

#include <shlobj.h>

#include <cwchar>

namespace
{

const wchar_t* const SECTION = L"Stream";

} // namespace


AppSettings::AppSettings(
    const wchar_t* fileName)
{
    PWSTR appData = nullptr;

    if (FAILED(SHGetKnownFolderPath(
            FOLDERID_RoamingAppData,
            0,
            nullptr,
            &appData)))
    {
        return;
    }

    std::wstring folder =
        std::wstring(appData) + L"\\Chegae";

    // Versoes "Stream Project" guardavam em ...\StreamProject:
    // na primeira vez, a pasta muda de nome (nada se perde).
    {
        std::wstring old = std::wstring(appData) + L"\\StreamProject";

        if (GetFileAttributesW(folder.c_str()) == INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(old.c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            MoveFileW(old.c_str(), folder.c_str());
        }
    }

    CoTaskMemFree(appData);

    if (!CreateDirectoryW(folder.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
    {
        return;
    }

    m_path =
        folder + L"\\" + fileName;
}


int AppSettings::GetInt(
    const wchar_t* key,
    int defaultValue) const
{
    if (m_path.empty())
        return defaultValue;

    return static_cast<int>(
        GetPrivateProfileIntW(
            SECTION,
            key,
            defaultValue,
            m_path.c_str()));
}


void AppSettings::SetInt(
    const wchar_t* key,
    int value)
{
    if (m_path.empty())
        return;

    wchar_t text[32];

    swprintf_s(text, L"%d", value);

    WritePrivateProfileStringW(
        SECTION,
        key,
        text,
        m_path.c_str());
}


std::wstring AppSettings::GetString(
    const wchar_t* key,
    const wchar_t* defaultValue) const
{
    if (m_path.empty())
        return defaultValue;

    wchar_t buffer[1024]{};

    GetPrivateProfileStringW(
        SECTION,
        key,
        defaultValue,
        buffer,
        static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0])),
        m_path.c_str());

    return buffer;
}


void AppSettings::SetString(
    const wchar_t* key,
    const std::wstring& value)
{
    if (m_path.empty())
        return;

    WritePrivateProfileStringW(
        SECTION,
        key,
        value.c_str(),
        m_path.c_str());
}


bool AppSettings::RestoreWindowPlacement(
    HWND window,
    int& outShowCmd) const
{
    RECT rect;

    rect.left = GetInt(L"JanelaEsquerda", 0);
    rect.top = GetInt(L"JanelaTopo", 0);
    rect.right = GetInt(L"JanelaDireita", 0);
    rect.bottom = GetInt(L"JanelaBaixo", 0);

    // Nada salvo, ou pequeno demais pra ser de verdade.
    if (rect.right - rect.left < 200 ||
        rect.bottom - rect.top < 150)
    {
        return false;
    }

    // Monitor que estava la pode nao existir mais.
    if (!MonitorFromRect(&rect, MONITOR_DEFAULTTONULL))
        return false;

    WINDOWPLACEMENT placement{};

    placement.length = sizeof(placement);
    placement.showCmd = SW_HIDE;
    placement.rcNormalPosition = rect;

    if (!SetWindowPlacement(window, &placement))
        return false;

    outShowCmd =
        GetInt(L"JanelaMaximizada", 0) != 0
        ? SW_SHOWMAXIMIZED
        : SW_SHOWNORMAL;

    return true;
}


void AppSettings::SaveWindowPlacement(
    const WINDOWPLACEMENT& placement)
{
    const RECT& rect =
        placement.rcNormalPosition;

    SetInt(L"JanelaEsquerda", rect.left);
    SetInt(L"JanelaTopo", rect.top);
    SetInt(L"JanelaDireita", rect.right);
    SetInt(L"JanelaBaixo", rect.bottom);

    SetInt(
        L"JanelaMaximizada",
        placement.showCmd == SW_SHOWMAXIMIZED ? 1 : 0);
}
