#include "ui_widgets.h"

#include <algorithm>
#include <cstring>
#include <string>


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


const ImWchar* UiGlyphRanges()
{
    static const ImWchar ranges[] =
    {
        0x0020, 0x00FF,     // latim basico + acentos
        0x0100, 0x017F,     // latim estendido A
        0x2000, 0x206F,     // pontuacao: — – " " … •
        0x20AC, 0x20AC,     // €
        0x2122, 0x2122,     // ™
        0x2190, 0x2199,     // setas
        0,
    };

    return ranges;
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


void UiBeginCornerPanel(
    const char* id,
    float width)
{
    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    float margin = 16.0f * g_uiScale;

    float scaledWidth = width * g_uiScale;

    float maxWidth =
        viewport->WorkSize.x - margin * 2.0f;

    if (scaledWidth > maxWidth)
    {
        scaledWidth = maxWidth;
    }

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + margin,
            viewport->WorkPos.y + margin),
        ImGuiCond_Always
    );

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


bool UiPrimaryButtonSized(
    const char* label,
    const ImVec2& size)
{
    const ImVec4 accent = UiColorAccent();

    ImGui::PushStyleColor(ImGuiCol_Button, accent);

    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered,
        ImVec4(accent.x + 0.08f, accent.y + 0.08f, accent.z, 1.0f));

    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        ImVec4(accent.x - 0.05f, accent.y - 0.05f, accent.z - 0.05f, 1.0f));

    bool clicked = ImGui::Button(label, size);

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


void UiFieldLabel(
    const char* text)
{
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        UiColorMuted());

    ImGui::TextUnformatted(text);

    ImGui::PopStyleColor();

    // Cola o rotulo no campo logo abaixo.
    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() -
        ImGui::GetStyle().ItemSpacing.y * 0.6f);
}


void UiDrawBackgroundImage(
    ImTextureID texture,
    float imageWidth,
    float imageHeight,
    float dim)
{
    if (imageWidth <= 0.0f || imageHeight <= 0.0f)
        return;

    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    ImVec2 area = viewport->WorkSize;

    if (area.x <= 0.0f || area.y <= 0.0f)
        return;

    // Maior tamanho que cabe inteiro na janela.
    float scale = area.x / imageWidth;

    if (imageHeight * scale > area.y)
    {
        scale = area.y / imageHeight;
    }

    ImVec2 size(imageWidth * scale, imageHeight * scale);

    ImVec2 topLeft(
        viewport->WorkPos.x + (area.x - size.x) * 0.5f,
        viewport->WorkPos.y + (area.y - size.y) * 0.5f);

    ImVec2 bottomRight(
        topLeft.x + size.x,
        topLeft.y + size.y);

    ImDrawList* background =
        ImGui::GetBackgroundDrawList();

    background->AddImage(
        texture,
        topLeft,
        bottomRight
    );

    if (dim > 0.0f)
    {
        background->AddRectFilled(
            topLeft,
            bottomRight,
            ImGui::GetColorU32(
                ImVec4(0.0f, 0.0f, 0.0f, dim))
        );
    }
}


// ============================================================
// Tela "falta o FFmpeg"
// ============================================================

void UiFfmpegProblemPanel(
    const char* problem,
    const char* purpose,
    bool& outOpenDownloadPage,
    bool& outRecheck)
{
    UiBeginCenteredPanel("##ffmpeg", 520.0f);

    UiTitle("Falta o FFmpeg");

    UiWrappedColored(
        UiColorError(),
        (std::string("Motivo: ") + problem + ".").c_str()
    );

    UiMutedText(purpose);

    ImGui::Spacing();

    ImGui::PushTextWrapPos(0.0f);

    ImGui::TextUnformatted(
        "1. Baixe o arquivo \"ffmpeg-release-essentials.zip\".\n"
        "2. Extraia e copie o ffmpeg.exe da pasta \"bin\" para "
        "a mesma pasta deste programa.\n"
        "3. Clique em \"Verificar de novo\"."
    );

    ImGui::PopTextWrapPos();

    UiMutedText(
        "O arquivo certo tem dezenas de MB. Um ffmpeg.exe de "
        "poucos KB é só um atalho e não funciona fora do "
        "computador de origem."
    );

    ImGui::Spacing();

    if (UiPrimaryButton("Abrir a página de download"))
    {
        outOpenDownloadPage = true;
    }

    if (ImGui::Button(
            "Verificar de novo",
            ImVec2(
                ImGui::GetContentRegionAvail().x,
                0.0f)))
    {
        outRecheck = true;
    }

    UiEndPanel();
}


// ============================================================
// UiSpinner
// ============================================================

void UiSpinner(
    float radius)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::Dummy(ImVec2(radius * 2.0f, radius * 2.0f));

    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImVec2 center(pos.x + radius, pos.y + radius);

    float thickness = (std::max)(2.0f, radius * 0.22f);

    // Trilho apagado + arco girando.
    draw->AddCircle(
        center,
        radius - thickness * 0.5f,
        ImGui::GetColorU32(ImVec4(1, 1, 1, 0.10f)),
        32,
        thickness);

    float start =
        static_cast<float>(ImGui::GetTime()) * 5.0f;

    draw->PathArcTo(
        center,
        radius - thickness * 0.5f,
        start,
        start + 3.14159f * 0.65f,
        24);

    draw->PathStroke(
        ImGui::GetColorU32(UiColorAccent()),
        ImDrawFlags_None,
        thickness);
}


// ============================================================
// UiAvatar
// ============================================================

namespace
{

// Primeiro caractere (UTF-8) do nome, em maiuscula quando for
// letra simples.
std::string Initial(
    const char* name)
{
    if (!name || !*name)
        return "?";

    unsigned char first =
        static_cast<unsigned char>(name[0]);

    size_t length =
        first < 0x80 ? 1 :
        (first & 0xE0) == 0xC0 ? 2 :
        (first & 0xF0) == 0xE0 ? 3 :
        4;

    std::string initial(name, strnlen(name, length));

    if (initial.size() == 1 &&
        initial[0] >= 'a' && initial[0] <= 'z')
    {
        initial[0] = static_cast<char>(initial[0] - 'a' + 'A');
    }

    // a-z acentuados mais comuns (UTF-8 de 2 bytes, 0xC3 0xA0..0xBF):
    // maiuscula = segundo byte - 0x20.
    if (initial.size() == 2 &&
        static_cast<unsigned char>(initial[0]) == 0xC3 &&
        static_cast<unsigned char>(initial[1]) >= 0xA0 &&
        static_cast<unsigned char>(initial[1]) <= 0xBE)
    {
        initial[1] = static_cast<char>(initial[1] - 0x20);
    }

    return initial;
}

} // namespace


void UiAvatar(
    const char* name,
    float diameter,
    bool live,
    float speaking)
{
    // Paleta: cores medias, legiveis com texto branco.
    static const ImU32 palette[] =
    {
        IM_COL32(59, 130, 246, 255),   // azul
        IM_COL32(16, 185, 129, 255),   // verde
        IM_COL32(245, 158, 11, 255),   // ambar
        IM_COL32(236, 72, 153, 255),   // rosa
        IM_COL32(139, 92, 246, 255),   // roxo
        IM_COL32(20, 184, 166, 255),   // turquesa
        IM_COL32(239, 68, 68, 255),    // vermelho
        IM_COL32(132, 204, 22, 255),   // lima
    };

    // Hash simples (FNV-1a) do nome -> cor.
    unsigned int hash = 2166136261u;

    for (const char* c = name; c && *c; ++c)
    {
        hash ^= static_cast<unsigned char>(*c);
        hash *= 16777619u;
    }

    ImU32 color =
        palette[hash % (sizeof(palette) / sizeof(palette[0]))];

    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::Dummy(ImVec2(diameter, diameter));

    ImDrawList* draw = ImGui::GetWindowDrawList();

    float radius = diameter * 0.5f;

    ImVec2 center(pos.x + radius, pos.y + radius);

    draw->AddCircleFilled(center, radius, color, 32);

    if (live)
    {
        draw->AddCircle(
            center,
            radius + 1.5f * g_uiScale,
            IM_COL32(239, 68, 68, 255),
            32,
            2.0f * g_uiScale);
    }

    // Anel verde com intensidade (acende e apaga suave).
    if (speaking > 0.01f)
    {
        int alpha =
            static_cast<int>((speaking > 1.0f ? 1.0f : speaking) * 255.0f);

        draw->AddCircle(
            center,
            radius + (live ? 4.5f : 1.5f) * g_uiScale,
            IM_COL32(34, 197, 94, alpha),
            32,
            2.0f * g_uiScale);
    }

    std::string initial = Initial(name);

    ImVec2 textSize =
        ImGui::CalcTextSize(initial.c_str());

    draw->AddText(
        ImVec2(
            center.x - textSize.x * 0.5f,
            center.y - textSize.y * 0.5f),
        IM_COL32(255, 255, 255, 255),
        initial.c_str());
}


// ============================================================
// UiMicIcon
// ============================================================

void UiMicIcon(
    float size,
    ImU32 color,
    bool slashed)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::Dummy(ImVec2(size, size));

    UiMicIconAt(pos, size, color, slashed);
}


void UiMicIconAt(
    ImVec2 pos,
    float size,
    ImU32 color,
    bool slashed)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();

    float cx = pos.x + size * 0.5f;

    float thick = (std::max)(1.0f, size * 0.09f);

    // Capsula
    float capW = size * 0.36f;

    draw->AddRectFilled(
        ImVec2(cx - capW * 0.5f, pos.y + size * 0.08f),
        ImVec2(cx + capW * 0.5f, pos.y + size * 0.58f),
        color,
        capW * 0.5f);

    // Suporte em U
    draw->PathArcTo(
        ImVec2(cx, pos.y + size * 0.42f),
        size * 0.30f,
        0.0f,
        3.14159265f,
        12);

    draw->PathStroke(color, 0, thick);

    // Haste + base
    draw->AddLine(
        ImVec2(cx, pos.y + size * 0.72f),
        ImVec2(cx, pos.y + size * 0.88f),
        color,
        thick);

    draw->AddLine(
        ImVec2(cx - size * 0.18f, pos.y + size * 0.90f),
        ImVec2(cx + size * 0.18f, pos.y + size * 0.90f),
        color,
        thick);

    if (slashed)
    {
        draw->AddLine(
            ImVec2(pos.x + size * 0.12f, pos.y + size * 0.10f),
            ImVec2(pos.x + size * 0.88f, pos.y + size * 0.90f),
            IM_COL32(239, 68, 68, 255),
            thick * 1.3f);
    }
}


// ============================================================
// UiLogo
// ============================================================

void UiLogo(
    float size)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::Dummy(ImVec2(size, size));

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Fundo: quadrado arredondado (anil), com um brilho leve na
    // metade de cima.
    draw->AddRectFilled(
        pos,
        ImVec2(pos.x + size, pos.y + size),
        IM_COL32(99, 102, 241, 255),
        size * 0.24f);

    draw->AddRectFilled(
        pos,
        ImVec2(pos.x + size, pos.y + size * 0.5f),
        IM_COL32(255, 255, 255, 22),
        size * 0.24f,
        ImDrawFlags_RoundCornersTop);

    // Monitor (contorno branco) + pe.
    ImU32 white = IM_COL32(255, 255, 255, 255);

    float s = size;

    ImVec2 screenMin(pos.x + s * 0.20f, pos.y + s * 0.24f);
    ImVec2 screenMax(pos.x + s * 0.80f, pos.y + s * 0.64f);

    draw->AddRect(
        screenMin,
        screenMax,
        white,
        s * 0.05f,
        0,
        (std::max)(1.5f, s * 0.06f));

    draw->AddLine(
        ImVec2(pos.x + s * 0.50f, pos.y + s * 0.64f),
        ImVec2(pos.x + s * 0.50f, pos.y + s * 0.74f),
        white,
        (std::max)(1.5f, s * 0.06f));

    draw->AddLine(
        ImVec2(pos.x + s * 0.36f, pos.y + s * 0.76f),
        ImVec2(pos.x + s * 0.64f, pos.y + s * 0.76f),
        white,
        (std::max)(1.5f, s * 0.06f));

    // "Play" no meio da tela.
    draw->AddTriangleFilled(
        ImVec2(pos.x + s * 0.44f, pos.y + s * 0.34f),
        ImVec2(pos.x + s * 0.44f, pos.y + s * 0.54f),
        ImVec2(pos.x + s * 0.60f, pos.y + s * 0.44f),
        white);
}
