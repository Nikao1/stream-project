#include "ui_widgets.h"


namespace
{

float g_uiScale = 1.0f;

ImFont* g_titleFont = nullptr;

} // namespace


// ============================================================
// Tema
// ============================================================

void UiApplyTheme(
    float scale)
{
    g_uiScale = scale;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.PopupRounding = 8.0f;

    style.WindowPadding = ImVec2(28.0f, 26.0f);
    style.FramePadding = ImVec2(12.0f, 9.0f);
    style.ItemSpacing = ImVec2(10.0f, 12.0f);

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;

    ImVec4* colors = style.Colors;

    const ImVec4 accent = UiColorAccent();

    colors[ImGuiCol_WindowBg] =
        ImVec4(0.09f, 0.10f, 0.12f, 0.97f);

    colors[ImGuiCol_Border] =
        ImVec4(1.0f, 1.0f, 1.0f, 0.08f);

    colors[ImGuiCol_FrameBg] =
        ImVec4(1.0f, 1.0f, 1.0f, 0.06f);

    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(1.0f, 1.0f, 1.0f, 0.09f);

    colors[ImGuiCol_FrameBgActive] =
        ImVec4(1.0f, 1.0f, 1.0f, 0.12f);

    colors[ImGuiCol_Button] =
        ImVec4(1.0f, 1.0f, 1.0f, 0.08f);

    colors[ImGuiCol_ButtonHovered] =
        ImVec4(1.0f, 1.0f, 1.0f, 0.14f);

    colors[ImGuiCol_ButtonActive] =
        ImVec4(1.0f, 1.0f, 1.0f, 0.20f);

    colors[ImGuiCol_Text] =
        ImVec4(0.94f, 0.95f, 0.96f, 1.0f);

    colors[ImGuiCol_TextSelectedBg] =
        ImVec4(accent.x, accent.y, accent.z, 0.45f);

    colors[ImGuiCol_NavHighlight] =
        accent;

    style.ScaleAllSizes(g_uiScale);
}


void UiSetTitleFont(
    ImFont* font)
{
    g_titleFont = font;
}


float UiScale()
{
    return g_uiScale;
}


// ============================================================
// Cores
// ============================================================

ImVec4 UiColorMuted()
{
    return ImVec4(0.62f, 0.65f, 0.70f, 1.0f);
}


ImVec4 UiColorError()
{
    return ImVec4(1.00f, 0.45f, 0.42f, 1.0f);
}


ImVec4 UiColorSuccess()
{
    return ImVec4(0.42f, 0.86f, 0.58f, 1.0f);
}


ImVec4 UiColorAccent()
{
    return ImVec4(0.25f, 0.52f, 0.96f, 1.0f);
}


// ============================================================
// Componentes
// ============================================================

void UiBeginCenteredPanel(
    const char* id,
    float width)
{
    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    float scaledWidth = width * g_uiScale;

    // Em janelas estreitas o painel encolhe pra caber.
    float maxWidth =
        viewport->WorkSize.x - 24.0f * g_uiScale;

    if (scaledWidth > maxWidth)
    {
        scaledWidth = maxWidth;
    }

    ImGui::SetNextWindowPos(
        viewport->GetWorkCenter(),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f)
    );

    // Largura fixa; altura 0 = o ImGui ajusta ao conteudo.
    ImGui::SetNextWindowSize(
        ImVec2(scaledWidth, 0.0f),
        ImGuiCond_Always
    );

    ImGui::Begin(
        id,
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings
    );
}


void UiEndPanel()
{
    ImGui::End();
}


void UiTitle(
    const char* text)
{
    // Sem fonte de titulo definida, usa a padrao.
    if (g_titleFont)
    {
        ImGui::PushFont(g_titleFont);
    }

    ImGui::TextUnformatted(text);

    if (g_titleFont)
    {
        ImGui::PopFont();
    }
}


void UiWrappedColored(
    const ImVec4& color,
    const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);

    ImGui::PushTextWrapPos(0.0f);

    ImGui::TextUnformatted(text);

    ImGui::PopTextWrapPos();

    ImGui::PopStyleColor();
}


void UiMutedText(
    const char* text)
{
    UiWrappedColored(UiColorMuted(), text);
}


bool UiPrimaryButton(
    const char* label,
    bool enabled)
{
    const ImVec4 accent = UiColorAccent();

    ImGui::PushStyleColor(
        ImGuiCol_Button,
        accent);

    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(accent.x + 0.08f, accent.y + 0.08f, accent.z, 1.0f));

    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        ImVec4(accent.x - 0.05f, accent.y - 0.05f, accent.z - 0.05f, 1.0f));

    ImGui::BeginDisabled(!enabled);

    bool clicked =
        ImGui::Button(
            label,
            ImVec2(
                ImGui::GetContentRegionAvail().x,
                44.0f * g_uiScale));

    ImGui::EndDisabled();

    ImGui::PopStyleColor(3);

    return clicked;
}


const char* UiBusyDots()
{
    static const char* frames[] =
        { "", ".", "..", "..." };

    int index =
        static_cast<int>(ImGui::GetTime() * 2.5) % 4;

    return frames[index];
}
