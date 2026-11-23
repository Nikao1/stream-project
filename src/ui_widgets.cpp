#include "ui_widgets.h"

#include <algorithm>
#include <cstdio>
#include <cmath>
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

// Tema claro (false = escuro). So muda cores: nenhum custo de
// desempenho.
bool g_lightTheme = false;


void UiSetLightTheme(
    bool light)
{
    if (light == g_lightTheme)
        return;

    g_lightTheme = light;

    UiApplyTheme(g_uiScale);
}


bool UiIsLightTheme()
{
    return g_lightTheme;
}


void UiApplyTheme(
    float scale)
{
    g_uiScale = scale;

    // Recomeca do estilo padrao (reaplicar nao acumula a escala).
    ImGui::GetStyle() = ImGuiStyle();

    if (g_lightTheme)
    {
        ImGui::StyleColorsLight();
    }
    else
    {
        ImGui::StyleColorsDark();
    }

    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 12.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 8.0f;
    style.PopupRounding = 8.0f;

    style.WindowPadding = ImVec2(28.0f, 26.0f);
    style.FramePadding = ImVec2(12.0f, 9.0f);
    style.ItemSpacing = ImVec2(10.0f, 12.0f);

    style.WindowBorderSize = 1.0f;

    // Claro: contornos nos botoes, campos e abas (branco sobre
    // branco nao separa nada). Escuro: sem (o contraste ja separa).
    style.FrameBorderSize = g_lightTheme ? 1.0f : 0.0f;
    style.TabBorderSize = g_lightTheme ? 1.0f : 0.0f;

    ImVec4* colors = style.Colors;

    const ImVec4 accent = UiColorAccent();

    // Escuro: camadas de branco transparente sobre o fundo.
    // Claro: camadas de preto transparente sobre o fundo.
    const float ink = g_lightTheme ? 0.0f : 1.0f;

    colors[ImGuiCol_WindowBg] =
        g_lightTheme
        ? ImVec4(0.97f, 0.97f, 0.98f, 0.98f)
        : ImVec4(0.09f, 0.10f, 0.12f, 0.97f);

    colors[ImGuiCol_PopupBg] =
        g_lightTheme
        ? ImVec4(1.0f, 1.0f, 1.0f, 0.99f)
        : ImVec4(0.11f, 0.12f, 0.14f, 0.99f);

    colors[ImGuiCol_Border] =
        ImVec4(ink, ink, ink, g_lightTheme ? 0.16f : 0.08f);

    colors[ImGuiCol_FrameBg] =
        ImVec4(ink, ink, ink, g_lightTheme ? 0.05f : 0.06f);

    colors[ImGuiCol_FrameBgHovered] =
        ImVec4(ink, ink, ink, g_lightTheme ? 0.08f : 0.09f);

    colors[ImGuiCol_FrameBgActive] =
        ImVec4(ink, ink, ink, g_lightTheme ? 0.11f : 0.12f);

    colors[ImGuiCol_Button] =
        ImVec4(ink, ink, ink, g_lightTheme ? 0.06f : 0.08f);

    colors[ImGuiCol_ButtonHovered] =
        ImVec4(ink, ink, ink, g_lightTheme ? 0.10f : 0.14f);

    colors[ImGuiCol_ButtonActive] =
        ImVec4(ink, ink, ink, g_lightTheme ? 0.15f : 0.20f);

    colors[ImGuiCol_Header] =
        ImVec4(accent.x, accent.y, accent.z, 0.20f);

    colors[ImGuiCol_HeaderHovered] =
        ImVec4(accent.x, accent.y, accent.z, 0.30f);

    colors[ImGuiCol_HeaderActive] =
        ImVec4(accent.x, accent.y, accent.z, 0.40f);

    colors[ImGuiCol_Text] =
        g_lightTheme
        ? ImVec4(0.10f, 0.11f, 0.13f, 1.0f)
        : ImVec4(0.94f, 0.95f, 0.96f, 1.0f);

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
    return g_lightTheme
        ? ImVec4(0.38f, 0.40f, 0.45f, 1.0f)
        : ImVec4(0.62f, 0.65f, 0.70f, 1.0f);
}


ImVec4 UiColorError()
{
    return g_lightTheme
        ? ImVec4(0.82f, 0.18f, 0.16f, 1.0f)
        : ImVec4(1.00f, 0.45f, 0.42f, 1.0f);
}


ImVec4 UiColorSuccess()
{
    return g_lightTheme
        ? ImVec4(0.09f, 0.55f, 0.27f, 1.0f)
        : ImVec4(0.42f, 0.86f, 0.58f, 1.0f);
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

    // Texto branco sobre o azul (no tema claro o texto padrao e
    // escuro).
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    ImGui::BeginDisabled(!enabled);

    bool clicked =
        ImGui::Button(
            label,
            ImVec2(
                ImGui::GetContentRegionAvail().x,
                44.0f * g_uiScale));

    ImGui::EndDisabled();

    ImGui::PopStyleColor(4);

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

    // Texto branco sobre o azul (no tema claro o texto padrao e
    // escuro).
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

    bool clicked = ImGui::Button(label, size);

    ImGui::PopStyleColor(4);

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
    float speaking,
    void* image)
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

    // Tudo some junto com a janela (fade).
    const float fade = ImGui::GetStyle().Alpha;

    auto faded = [fade](ImU32 c)
    {
        ImU32 a = static_cast<ImU32>(((c >> IM_COL32_A_SHIFT) & 0xFF) * fade);

        return (c & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
    };

    ImU32 color =
        faded(palette[hash % (sizeof(palette) / sizeof(palette[0]))]);

    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::Dummy(ImVec2(diameter, diameter));

    ImDrawList* draw = ImGui::GetWindowDrawList();

    float radius = diameter * 0.5f;

    ImVec2 center(pos.x + radius, pos.y + radius);

    if (image)
    {
        // Foto redonda.
        draw->AddImageRounded(
            reinterpret_cast<ImTextureID>(image),
            pos,
            ImVec2(pos.x + diameter, pos.y + diameter),
            ImVec2(0, 0),
            ImVec2(1, 1),
            faded(IM_COL32(255, 255, 255, 255)),
            radius);
    }
    else
    {
        draw->AddCircleFilled(center, radius, color, 32);
    }

    if (live)
    {
        draw->AddCircle(
            center,
            radius + 1.5f * g_uiScale,
            faded(IM_COL32(239, 68, 68, 255)),
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
            faded(IM_COL32(34, 197, 94, alpha)),
            32,
            2.0f * g_uiScale);
    }

    if (!image)
    {
        std::string initial = Initial(name);

        ImVec2 textSize =
            ImGui::CalcTextSize(initial.c_str());

        draw->AddText(
            ImVec2(
                center.x - textSize.x * 0.5f,
                center.y - textSize.y * 0.5f),
            faded(IM_COL32(255, 255, 255, 255)),
            initial.c_str());
    }
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
            IM_COL32(239, 68, 68, static_cast<int>(255 * ImGui::GetStyle().Alpha)),
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


// ============================================================
// UiToggle
// ============================================================

bool UiToggle(
    const char* label,
    bool* v)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    float height = ImGui::GetFrameHeight() * 0.72f;
    float width = height * 1.8f;

    // Texto visivel (sem o "##id").
    const char* labelEnd = std::strstr(label, "##");

    ImVec2 textSize =
        ImGui::CalcTextSize(label, labelEnd);

    ImVec2 pos = ImGui::GetCursorScreenPos();

    float rowHeight = ImGui::GetFrameHeight();

    ImVec2 total(
        width + (textSize.x > 0.0f ? style.ItemInnerSpacing.x * 2.0f + textSize.x : 0.0f),
        rowHeight);

    bool clicked = ImGui::InvisibleButton(label, total);

    if (clicked)
    {
        *v = !*v;
    }

    bool hovered = ImGui::IsItemHovered();

    // Animacao da bolinha (0 = desligado, 1 = ligado).
    ImGuiStorage* storage = ImGui::GetStateStorage();

    ImGuiID id = ImGui::GetItemID();

    float t = storage->GetFloat(id, *v ? 1.0f : 0.0f);

    float target = *v ? 1.0f : 0.0f;

    float speed = ImGui::GetIO().DeltaTime / 0.12f;

    t += (std::max)(-speed, (std::min)(speed, target - t));

    storage->SetFloat(id, t);

    ImDrawList* draw = ImGui::GetWindowDrawList();

    float alpha = ImGui::GetStyle().Alpha;

    ImVec2 trackMin(pos.x, pos.y + (rowHeight - height) * 0.5f);
    ImVec2 trackMax(trackMin.x + width, trackMin.y + height);

    ImVec4 off =
        g_lightTheme
        ? ImVec4(0.78f, 0.79f, 0.82f, alpha)
        : ImVec4(0.24f, 0.25f, 0.29f, alpha);
    ImVec4 on = UiColorAccent();

    on.w = alpha;

    ImVec4 track(
        off.x + (on.x - off.x) * t,
        off.y + (on.y - off.y) * t,
        off.z + (on.z - off.z) * t,
        alpha);

    if (hovered)
    {
        track.x = (std::min)(1.0f, track.x + 0.06f);
        track.y = (std::min)(1.0f, track.y + 0.06f);
        track.z = (std::min)(1.0f, track.z + 0.06f);
    }

    draw->AddRectFilled(
        trackMin, trackMax,
        ImGui::ColorConvertFloat4ToU32(track),
        height * 0.5f);

    float knobRadius = height * 0.5f - 2.5f * g_uiScale;

    float knobX =
        trackMin.x + height * 0.5f + (width - height) * t;

    draw->AddCircleFilled(
        ImVec2(knobX, trackMin.y + height * 0.5f),
        knobRadius,
        ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)),
        24);

    if (textSize.x > 0.0f)
    {
        draw->AddText(
            ImVec2(
                trackMax.x + style.ItemInnerSpacing.x * 2.0f,
                pos.y + (rowHeight - textSize.y) * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text),
            label,
            labelEnd);
    }

    return clicked;
}


// ============================================================
// UiGearIconAt
// ============================================================

void UiGearIconAt(
    ImVec2 center,
    float size,
    ImU32 color,
    ImU32 hole)
{
    (void)color;

    ImDrawList* draw = ImGui::GetWindowDrawList();

    // Engrenagem de 8 dentes: contorno azul forte, miolo azul
    // claro e furo no meio (cor do fundo).
    const int teeth = 8;
    const float pi = 3.14159265f;

    float outer = size * 0.5f;
    float inner = outer * 0.74f;
    float thickness = (std::max)(1.0f, size * 0.085f);

    // Desconta metade da linha (o contorno cabe no tamanho).
    outer -= thickness * 0.5f;
    inner -= thickness * 0.5f;

    ImVec2 points[teeth * 8];
    int count = 0;

    for (int i = 0; i < teeth; ++i)
    {
        float base = i * 2.0f * pi / teeth;

        // dente: sobe, topo, desce, vale (cantos levemente
        // chanfrados pra parecer arredondado)
        const float shape[8][2] =
        {
            { -0.30f, inner / outer },
            { -0.20f, 0.97f },
            { -0.15f, 1.00f },
            {  0.15f, 1.00f },
            {  0.20f, 0.97f },
            {  0.30f, inner / outer },
            {  0.40f, inner / outer * 0.99f },
            {  0.50f, inner / outer },
        };

        for (const auto& p : shape)
        {
            float angle = base + p[0] * 2.0f * pi / teeth;
            float radius = outer * p[1];

            points[count++] =
                ImVec2(
                    center.x + std::cos(angle) * radius,
                    center.y + std::sin(angle) * radius);
        }
    }

    // Some junto com a janela (fade): respeita o alpha do estilo.
    ImVec4 accent = UiColorAccent();

    accent.w *= ImGui::GetStyle().Alpha;

    ImU32 strong = ImGui::ColorConvertFloat4ToU32(accent);

    ImU32 light =
        ImGui::ColorConvertFloat4ToU32(
            ImVec4(
                accent.x + (1.0f - accent.x) * 0.45f,
                accent.y + (1.0f - accent.y) * 0.45f,
                accent.z + (1.0f - accent.z) * 0.45f,
                accent.w));

    draw->AddConcavePolyFilled(points, count, light);

    draw->AddPolyline(
        points, count, strong, ImDrawFlags_Closed, thickness);

    float holeRadius = outer * 0.36f;

    draw->AddCircleFilled(center, holeRadius, hole, 20);

    draw->AddCircle(center, holeRadius, strong, 20, thickness);
}


// ============================================================
// UiIconAt / UiIconButton
// ============================================================

void UiIconAt(
    UiIcon icon,
    ImVec2 c,
    float size,
    ImU32 color)
{
    ImDrawList* draw = ImGui::GetWindowDrawList();

    float h = size * 0.5f;
    float t = (std::max)(1.0f, size * 0.09f);

    // Vermelho do "desligado", sumindo junto com a janela.
    const ImU32 redOff =
        IM_COL32(239, 68, 68, static_cast<int>(255 * ImGui::GetStyle().Alpha));

    auto P = [&](float x, float y) { return ImVec2(c.x + x * h, c.y + y * h); };

    switch (icon)
    {
    case UiIcon::Mic:
    case UiIcon::MicOff:
    {
        draw->AddRect(P(-0.32f, -0.95f), P(0.32f, 0.25f), color, h * 0.32f, 0, t);
        draw->PathArcTo(P(0.0f, -0.05f), h * 0.62f, 0.0f, 3.14159265f, 14);
        draw->PathStroke(color, 0, t);
        draw->AddLine(P(0.0f, 0.57f), P(0.0f, 0.88f), color, t);
        draw->AddLine(P(-0.38f, 0.92f), P(0.38f, 0.92f), color, t);

        if (icon == UiIcon::MicOff)
        {
            draw->AddLine(P(-0.85f, -0.85f), P(0.85f, 0.85f), redOff, t * 1.3f);
        }

        break;
    }

    case UiIcon::Volume:
    case UiIcon::VolumeOff:
    {
        ImVec2 body[6] =
        {
            P(-0.85f, -0.3f), P(-0.45f, -0.3f), P(0.0f, -0.75f),
            P(0.0f, 0.75f), P(-0.45f, 0.3f), P(-0.85f, 0.3f)
        };

        draw->AddPolyline(body, 6, color, ImDrawFlags_Closed, t);

        if (icon == UiIcon::Volume)
        {
            draw->PathArcTo(P(0.05f, 0.0f), h * 0.42f, -0.9f, 0.9f, 10);
            draw->PathStroke(color, 0, t);
            draw->PathArcTo(P(0.05f, 0.0f), h * 0.78f, -0.9f, 0.9f, 12);
            draw->PathStroke(color, 0, t);
        }
        else
        {
            draw->AddLine(P(0.3f, -0.35f), P(0.9f, 0.35f), redOff, t);
            draw->AddLine(P(0.3f, 0.35f), P(0.9f, -0.35f), redOff, t);
        }

        break;
    }

    case UiIcon::People:
    {
        draw->AddCircle(P(-0.25f, -0.35f), h * 0.32f, color, 16, t);
        draw->PathArcTo(P(-0.25f, 0.85f), h * 0.62f, 3.14159265f * 1.12f, 3.14159265f * 1.88f, 14);
        draw->PathStroke(color, 0, t);
        draw->AddCircle(P(0.45f, -0.25f), h * 0.25f, color, 14, t);
        draw->PathArcTo(P(0.55f, 0.75f), h * 0.45f, 3.14159265f * 1.25f, 3.14159265f * 1.9f, 12);
        draw->PathStroke(color, 0, t);
        break;
    }

    case UiIcon::Chat:
    {
        draw->AddRect(P(-0.85f, -0.7f), P(0.85f, 0.45f), color, h * 0.3f, 0, t);

        ImVec2 tail[3] = { P(-0.45f, 0.45f), P(-0.55f, 0.88f), P(-0.05f, 0.45f) };

        draw->AddPolyline(tail, 3, color, 0, t);
        break;
    }

    case UiIcon::Switch:
    {
        draw->AddLine(P(-0.8f, -0.4f), P(0.8f, -0.4f), color, t);
        draw->AddLine(P(0.8f, -0.4f), P(0.45f, -0.75f), color, t);
        draw->AddLine(P(0.8f, 0.4f), P(-0.8f, 0.4f), color, t);
        draw->AddLine(P(-0.8f, 0.4f), P(-0.45f, 0.75f), color, t);
        break;
    }

    case UiIcon::Sliders:
    {
        const float ys[3] = { -0.6f, 0.0f, 0.6f };
        const float knobs[3] = { 0.35f, -0.35f, 0.15f };

        for (int i = 0; i < 3; ++i)
        {
            draw->AddLine(P(-0.85f, ys[i]), P(0.85f, ys[i]), color, t);
            draw->AddCircleFilled(P(knobs[i], ys[i]), h * 0.17f, color, 10);
        }

        break;
    }

    case UiIcon::Fullscreen:
    case UiIcon::ExitFullscreen:
    {
        // Quatro cantos (pra fora = entrar; pra dentro = sair).
        float a = icon == UiIcon::Fullscreen ? 0.85f : 0.3f;
        float b = icon == UiIcon::Fullscreen ? 0.35f : 0.85f;

        const float sx[4] = { -1, 1, 1, -1 };
        const float sy[4] = { -1, -1, 1, 1 };

        for (int i = 0; i < 4; ++i)
        {
            ImVec2 corner = P(sx[i] * a, sy[i] * a);

            draw->AddLine(corner, P(sx[i] * (icon == UiIcon::Fullscreen ? b : b), sy[i] * a), color, t);
            draw->AddLine(corner, P(sx[i] * a, sy[i] * (icon == UiIcon::Fullscreen ? b : b)), color, t);
        }

        break;
    }

    case UiIcon::Exit:
    {
        ImVec2 door[4] = { P(0.1f, -0.85f), P(-0.75f, -0.85f), P(-0.75f, 0.85f), P(0.1f, 0.85f) };

        draw->AddPolyline(door, 4, color, 0, t);
        draw->AddLine(P(-0.25f, 0.0f), P(0.9f, 0.0f), color, t);
        draw->AddLine(P(0.9f, 0.0f), P(0.55f, -0.35f), color, t);
        draw->AddLine(P(0.9f, 0.0f), P(0.55f, 0.35f), color, t);
        break;
    }

    case UiIcon::Pin:
    case UiIcon::PinOff:
    {
        draw->AddCircle(P(0.0f, -0.4f), h * 0.42f, color, 16, t);
        draw->AddLine(P(0.0f, 0.02f), P(0.0f, 0.95f), color, t);

        if (icon == UiIcon::Pin)
        {
            draw->AddCircleFilled(P(0.0f, -0.4f), h * 0.42f, color, 16);
        }

        break;
    }

    case UiIcon::Screen:
    {
        // Monitor com "ondas" (transmitindo).
        draw->AddRect(P(-0.85f, -0.7f), P(0.85f, 0.45f), color, h * 0.12f, 0, t);
        draw->AddLine(P(0.0f, 0.45f), P(0.0f, 0.8f), color, t);
        draw->AddLine(P(-0.4f, 0.85f), P(0.4f, 0.85f), color, t);
        draw->AddCircleFilled(P(0.0f, -0.12f), h * 0.14f, color, 10);
        draw->PathArcTo(P(0.0f, -0.12f), h * 0.36f, -0.8f, 0.8f, 8);
        draw->PathStroke(color, 0, t);
        draw->PathArcTo(P(0.0f, -0.12f), h * 0.36f, 3.14159265f - 0.8f, 3.14159265f + 0.8f, 8);
        draw->PathStroke(color, 0, t);
        break;
    }

    case UiIcon::Edit:
    {
        // Lapis inclinado.
        ImVec2 body[4] = { P(0.55f, -0.85f), P(0.85f, -0.55f), P(-0.45f, 0.75f), P(-0.75f, 0.45f) };

        draw->AddPolyline(body, 4, color, ImDrawFlags_Closed, t);
        draw->AddLine(P(-0.75f, 0.45f), P(-0.85f, 0.85f), color, t);
        draw->AddLine(P(-0.85f, 0.85f), P(-0.45f, 0.75f), color, t);
        break;
    }

    case UiIcon::Stop:
    {
        draw->AddRectFilled(P(-0.55f, -0.55f), P(0.55f, 0.55f), color, h * 0.12f);
        break;
    }

    case UiIcon::Close:
    {
        draw->AddLine(P(-0.6f, -0.6f), P(0.6f, 0.6f), color, t);
        draw->AddLine(P(-0.6f, 0.6f), P(0.6f, -0.6f), color, t);
        break;
    }
    }
}


bool UiIconButton(
    const char* id,
    UiIcon icon,
    const char* tooltip,
    bool active,
    bool danger,
    int badge)
{
    float size = ImGui::GetFrameHeight();

    if (active)
    {
        ImVec4 accent = UiColorAccent();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x, accent.y, accent.z, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x, accent.y, accent.z, 0.30f));
    }

    bool clicked = ImGui::Button(id, ImVec2(size, size));

    if (active)
    {
        ImGui::PopStyleColor(2);
    }

    ImVec2 min = ImGui::GetItemRectMin();

    ImVec4 color =
        danger ? UiColorError()
        : active ? UiColorAccent()
        : ImGui::GetStyleColorVec4(ImGuiCol_Text);

    color.w *= ImGui::GetStyle().Alpha;

    UiIconAt(
        icon,
        ImVec2(min.x + size * 0.5f, min.y + size * 0.5f),
        size * 0.5f,
        ImGui::ColorConvertFloat4ToU32(color));

    if (badge > 0)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();

        float r = size * 0.2f;

        ImVec2 center(min.x + size - r * 0.8f, min.y + r * 0.8f);

        draw->AddCircleFilled(center, r, IM_COL32(239, 68, 68, static_cast<int>(255 * ImGui::GetStyle().Alpha)), 14);

        char text[4];

        snprintf(text, sizeof(text), "%d", badge > 9 ? 9 : badge);

        ImVec2 ts = ImGui::CalcTextSize(text);

        float scale = (r * 1.6f) / ts.y;

        ImGui::GetWindowDrawList()->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize() * scale,
            ImVec2(center.x - ts.x * scale * 0.5f, center.y - ts.y * scale * 0.5f),
            IM_COL32(255, 255, 255, static_cast<int>(255 * ImGui::GetStyle().Alpha)),
            text);
    }

    if (ImGui::IsItemHovered() && tooltip && *tooltip)
    {
        ImGui::SetTooltip("%s", tooltip);
    }

    return clicked;
}
