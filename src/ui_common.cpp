#include "ui_common.h"

#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <string>


// Declarada no backend do ImGui (de proposito fora do header).
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
);


namespace
{

bool g_uiReady = false;

float g_fontScale = 1.0f;


// Segoe UI vem em todo Windows 10/11. Se por algum motivo nao
// estiver la, o ImGui usa a fonte embutida dele.
std::string SystemFontPath(
    const char* fileName)
{
    char windowsDir[MAX_PATH]{};

    UINT length =
        GetWindowsDirectoryA(
            windowsDir,
            MAX_PATH
        );

    if (length == 0 || length >= MAX_PATH)
        return "";

    return
        std::string(windowsDir) +
        "\\Fonts\\" +
        fileName;
}


bool FileExists(
    const std::string& path)
{
    DWORD attributes =
        GetFileAttributesA(path.c_str());

    return
        attributes != INVALID_FILE_ATTRIBUTES &&
        !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}


void LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();

    std::string regular = SystemFontPath("segoeui.ttf");
    std::string semibold = SystemFontPath("seguisb.ttf");

    if (!semibold.empty() && !FileExists(semibold))
    {
        semibold = regular;
    }

    if (!regular.empty() && FileExists(regular))
    {
        // A primeira fonte carregada e a padrao.
        io.Fonts->AddFontFromFileTTF(
            regular.c_str(),
            18.0f * g_fontScale,
            nullptr,
            UiGlyphRanges()
        );

        UiSetTitleFont(
            io.Fonts->AddFontFromFileTTF(
                semibold.c_str(),
                30.0f * g_fontScale,
                nullptr,
                UiGlyphRanges()
            ));
    }
    else
    {
        io.Fonts->AddFontDefault();
    }
}

} // namespace


// ============================================================
// Ciclo de vida
// ============================================================

bool UiInit(
    HWND hwnd,
    ID3D11Device* device,
    ID3D11DeviceContext* context)
{
    if (g_uiReady)
        return true;

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();

    // Nada de arquivo imgui.ini ao lado do executavel.
    io.IniFilename = nullptr;

    io.ConfigFlags |=
        ImGuiConfigFlags_NavEnableKeyboard;

    g_fontScale =
        ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);

    if (g_fontScale < 1.0f)
    {
        g_fontScale = 1.0f;
    }

    LoadFonts();

    UiApplyTheme(g_fontScale);

    if (!ImGui_ImplWin32_Init(hwnd))
        return false;

    if (!ImGui_ImplDX11_Init(device, context))
    {
        ImGui_ImplWin32_Shutdown();

        return false;
    }

    g_uiReady = true;

    return true;
}


void UiShutdown()
{
    if (!g_uiReady)
        return;

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();

    ImGui::DestroyContext();

    g_uiReady = false;
}


bool UiHandleWindowMessage(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (!g_uiReady)
        return false;

    return
        ImGui_ImplWin32_WndProcHandler(
            hwnd,
            message,
            wParam,
            lParam) != 0;
}


void UiBeginFrame()
{
    if (!g_uiReady)
        return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    ImGui::NewFrame();
}


void UiEndFrame()
{
    if (!g_uiReady)
        return;

    ImGui::Render();

    ImGui_ImplDX11_RenderDrawData(
        ImGui::GetDrawData()
    );
}


bool UiWantsKeyboard()
{
    return
        g_uiReady &&
        ImGui::GetIO().WantCaptureKeyboard;
}


bool UiWantsMouse()
{
    return
        g_uiReady &&
        ImGui::GetIO().WantCaptureMouse;
}
