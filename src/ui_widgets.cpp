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


    ImVec4* colors = style.Colors;

    const ImVec4 accent = UiColorAccent();

    // Paleta "Brasa". Escuro: grafite azulado em camadas. Claro:
    // minimalista - fundo #F8F9FA, cartoes brancos, bordas
    // #E5E7EB, texto quase preto.
    auto hex =
        [](unsigned rgb, float a = 1.0f)
        {
            return ImVec4(
                ((rgb >> 16) & 0xFF) / 255.0f,
                ((rgb >> 8) & 0xFF) / 255.0f,
                (rgb & 0xFF) / 255.0f,
                a);
        };

    const bool light = g_lightTheme;

    colors[ImGuiCol_WindowBg] = light ? hex(0xF4F3F0, 1.0f) : hex(0x14171C, 0.98f);
    colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg] = light ? hex(0xF7F6F3, 0.99f) : hex(0x181B20, 0.99f);

    colors[ImGuiCol_Border] = light ? hex(0xD9D6D0) : hex(0x262A32);
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    colors[ImGuiCol_FrameBg] = light ? hex(0xECEAE6) : hex(0x0E1116);
    colors[ImGuiCol_FrameBgHovered] = light ? hex(0xE7E5E0) : hex(0x11141A);
    colors[ImGuiCol_FrameBgActive] = light ? hex(0xE1DED8) : hex(0x13171E);

    colors[ImGuiCol_Button] = light ? hex(0xE8E6E1) : hex(0x1E2229);
    colors[ImGuiCol_ButtonHovered] = light ? hex(0xDFDCD6) : hex(0x262A32);
    colors[ImGuiCol_ButtonActive] = light ? hex(0xD4D0C9) : hex(0x2D323B);

    colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.16f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.24f);
    colors[ImGuiCol_HeaderActive] = ImVec4(accent.x, accent.y, accent.z, 0.32f);

    colors[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TabHovered] = ImVec4(accent.x, accent.y, accent.z, 0.16f);
    colors[ImGuiCol_TabSelected] = ImVec4(accent.x, accent.y, accent.z, 0.20f);
    colors[ImGuiCol_TabSelectedOverline] = accent;

    // Claro: divisorias (e contornos de paineis/cartoes) em
    // laranja suave, delimitando os espacos.
    colors[ImGuiCol_Separator] = UiColorDivider();
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accent;

    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_PlotHistogram] = accent;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab] = light ? hex(0xD1D5DB) : hex(0x2D323B);
    colors[ImGuiCol_ScrollbarGrabHovered] = light ? hex(0x9CA3AF) : hex(0x363C48);
    colors[ImGuiCol_ScrollbarGrabActive] = light ? hex(0x6B7280) : hex(0x414856);

    colors[ImGuiCol_Text] = light ? hex(0x111827) : hex(0xE8EAED);
    colors[ImGuiCol_TextDisabled] = light ? hex(0x9CA3AF) : hex(0x5C6370);

    colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);

    colors[ImGuiCol_NavHighlight] = accent;

    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, light ? 0.25f : 0.55f);

    style.WindowRounding = 14.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.GrabRounding = 8.0f;
    style.TabRounding = 6.0f;
    style.ScrollbarRounding = 8.0f;

    // Contornos finos nos dois temas (camadas parecidas precisam
    // da borda pra se separar).
    style.FrameBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;

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
    // #4B5563 (alto contraste no claro) / #9DA7B8
    return g_lightTheme
        ? ImVec4(0.294f, 0.333f, 0.388f, 1.0f)
        : ImVec4(0.62f, 0.65f, 0.72f, 1.0f);
}


ImVec4 UiColorError()
{
    // #D93D42 / #E5484D (vermelho levemente rosado: nao se
    // confunde com o laranja)
    return g_lightTheme
        ? ImVec4(0.85f, 0.24f, 0.26f, 1.0f)
        : ImVec4(0.90f, 0.28f, 0.30f, 1.0f);
}


ImVec4 UiColorSuccess()
{
    // #16A34A / #22C55E
    return g_lightTheme
        ? ImVec4(0.09f, 0.64f, 0.29f, 1.0f)
        : ImVec4(0.13f, 0.77f, 0.37f, 1.0f);
}


ImVec4 UiColorAccent()
{
    // Brasa: #E8622C (claro; o #FF7A45 fica lavado no branco) /
    // #FF7A45 (escuro)
    return g_lightTheme
        ? ImVec4(0.91f, 0.38f, 0.17f, 1.0f)
        : ImVec4(1.00f, 0.48f, 0.27f, 1.0f);
}


ImVec4 UiColorOnAccent()
{
    // Escuro: texto quase preto sobre o laranja claro (mais
    // contraste que branco). Claro: branco.
    return g_lightTheme
        ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
        : ImVec4(0.10f, 0.055f, 0.03f, 1.0f);
}


ImVec4 UiColorDivider()
{
    // Laranja suave nos dois temas: #E8622C a ~45% (claro),
    // #FF7A45 a ~48% (escuro).
    return g_lightTheme
        ? ImVec4(0.91f, 0.38f, 0.17f, 0.45f)
        : ImVec4(1.00f, 0.48f, 0.27f, 0.48f);
}


ImVec4 UiColorBase()
{
    // #E9E8E4 (off-white mais baixo: sem clarao) / #0E1116
    return g_lightTheme
        ? ImVec4(0.914f, 0.910f, 0.894f, 1.0f)
        : ImVec4(0.055f, 0.067f, 0.086f, 1.0f);
}


ImVec4 UiColorSurface()
{
    // #F4F3F0 / #14171C
    return g_lightTheme
        ? ImVec4(0.957f, 0.953f, 0.941f, 1.0f)
        : ImVec4(0.078f, 0.090f, 0.110f, 1.0f);
}


void UiDrawShadow(
    ImVec2 min,
    ImVec2 max,
    float rounding)
{
    ImDrawList* draw = ImGui::GetBackgroundDrawList();

    float alpha = ImGui::GetStyle().Alpha * (g_lightTheme ? 0.018f : 0.10f);

    // De dentro pra fora, cada vez mais largo e mais fraco; um
    // pouco pra baixo (luz de cima).
    for (int i = 1; i <= 4; ++i)
    {
        float grow = 3.0f * g_uiScale * i;
        float down = 2.0f * g_uiScale * i;

        draw->AddRectFilled(
            ImVec2(min.x - grow, min.y - grow + down),
            ImVec2(max.x + grow, max.y + grow + down),
            IM_COL32(0, 0, 0, static_cast<int>(255 * alpha)),
            rounding + grow);
    }
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

    // Contorno do painel na cor das divisorias (desenhado no
    // Begin).
    ImGui::PushStyleColor(ImGuiCol_Border, UiColorDivider());

    ImGui::Begin(
        id,
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings
    );

    ImGui::PopStyleColor();

    ImVec2 pos = ImGui::GetWindowPos();
    ImVec2 size = ImGui::GetWindowSize();

    UiDrawShadow(
        pos,
        ImVec2(pos.x + size.x, pos.y + size.y),
        ImGui::GetStyle().WindowRounding);
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
        ImVec4((std::min)(1.0f, accent.x + 0.04f), (std::min)(1.0f, accent.y + 0.06f), (std::min)(1.0f, accent.z + 0.06f), 1.0f));

    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        ImVec4(accent.x - 0.06f, accent.y - 0.06f, accent.z - 0.04f, 1.0f));

    ImGui::PushStyleColor(ImGuiCol_Text, UiColorOnAccent());

    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    ImGui::BeginDisabled(!enabled);

    bool clicked =
        ImGui::Button(
            label,
            ImVec2(
                ImGui::GetContentRegionAvail().x,
                44.0f * g_uiScale));

    ImGui::EndDisabled();

    ImGui::PopStyleColor(5);

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
        ImVec4((std::min)(1.0f, accent.x + 0.04f), (std::min)(1.0f, accent.y + 0.06f), (std::min)(1.0f, accent.z + 0.06f), 1.0f));

    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive,
        ImVec4(accent.x - 0.06f, accent.y - 0.06f, accent.z - 0.04f, 1.0f));

    ImGui::PushStyleColor(ImGuiCol_Text, UiColorOnAccent());

    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));

    bool clicked = ImGui::Button(label, size);

    ImGui::PopStyleColor(5);

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
    float dim,
    float rightInset)
{
    if (imageWidth <= 0.0f || imageHeight <= 0.0f)
        return;

    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    ImVec2 area = viewport->WorkSize;

    area.x -= rightInset;

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

    // Quadrado arredondado na cor Brasa; dentro, um "e" com o
    // circunflexo virando telhado ("chega aqui em casa").
    draw->AddRectFilled(
        pos,
        ImVec2(pos.x + size, pos.y + size),
        ImGui::GetColorU32(UiColorAccent()),
        size * 0.26f);

    ImU32 ink = ImGui::GetColorU32(UiColorOnAccent());

    float s = size;

    float thick = (std::max)(1.5f, s * 0.08f);

    ImVec2 roof[3] =
    {
        ImVec2(pos.x + s * 0.30f, pos.y + s * 0.35f),
        ImVec2(pos.x + s * 0.50f, pos.y + s * 0.18f),
        ImVec2(pos.x + s * 0.70f, pos.y + s * 0.35f)
    };

    draw->AddPolyline(roof, 3, ink, 0, thick);

    // "e" desenhado (nao depende da fonte): a barra do meio e o
    // arco que sobe pela direita, contorna e para embaixo.
    ImVec2 center(pos.x + s * 0.50f, pos.y + s * 0.63f);

    float r = s * 0.20f;

    draw->PathArcTo(center, r, 0.0f, -(2.0f * 3.14159265f - 0.80f), 28);
    draw->PathStroke(ink, 0, thick);

    draw->AddLine(
        ImVec2(center.x - r, center.y),
        ImVec2(center.x + r, center.y),
        ink,
        thick);
}


void UiWordmark()
{
    if (g_titleFont)
    {
        ImGui::PushFont(g_titleFont);
    }

    // Um texto so (alinhado como os outros); o "ê" e redesenhado
    // por cima na cor de destaque.
    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImGui::TextUnformatted("Chegaê");

    float offset = ImGui::CalcTextSize("Chega").x;

    ImVec4 accent = UiColorAccent();

    ImVec4 background = UiColorSurface();

    ImVec2 glyph = ImGui::CalcTextSize("ê");

    ImDrawList* draw = ImGui::GetWindowDrawList();

    draw->AddRectFilled(
        ImVec2(pos.x + offset, pos.y),
        ImVec2(pos.x + offset + glyph.x, pos.y + glyph.y),
        ImGui::GetColorU32(background));

    draw->AddText(
        ImVec2(pos.x + offset, pos.y),
        ImGui::GetColorU32(accent),
        "ê");

    if (g_titleFont)
    {
        ImGui::PopFont();
    }
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
        ? ImVec4(0.82f, 0.84f, 0.86f, alpha)
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

    case UiIcon::Link:
    {
        // Dois elos inclinados.
        draw->AddLine(P(-0.25f, 0.25f), P(0.25f, -0.25f), color, t);

        ImVec2 a[4] = { P(-0.05f, 0.45f), P(-0.35f, 0.75f), P(-0.75f, 0.35f), P(-0.45f, 0.05f) };
        ImVec2 b[4] = { P(0.05f, -0.45f), P(0.35f, -0.75f), P(0.75f, -0.35f), P(0.45f, -0.05f) };

        draw->AddPolyline(a, 4, color, 0, t);
        draw->AddPolyline(b, 4, color, 0, t);
        break;
    }

    case UiIcon::Copy:
    {
        draw->AddRect(P(-0.35f, -0.35f), P(0.75f, 0.8f), color, h * 0.15f, 0, t);

        ImVec2 back[4] = { P(-0.55f, 0.55f), P(-0.75f, 0.55f), P(-0.75f, -0.8f), P(0.45f, -0.8f) };

        draw->AddPolyline(back, 4, color, 0, t);
        draw->AddLine(P(0.45f, -0.8f), P(0.45f, -0.55f), color, t);
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


bool UiIconLabelButton(
    const char* label,
    UiIcon icon,
    ImVec2 size,
    ImVec4 iconColor)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    // Texto ate o "##" (o resto e so id).
    const char* end = strstr(label, "##");

    std::string text = end ? std::string(label, end) : std::string(label);

    float line = ImGui::GetTextLineHeight();

    float iconSize = line * 0.95f;

    float gap = 7.0f * g_uiScale;

    ImVec2 textSize = ImGui::CalcTextSize(text.c_str());

    float contentWidth = iconSize + gap + textSize.x;

    if (size.x <= 0.0f)
    {
        size.x = contentWidth + style.FramePadding.x * 2.0f;
    }

    if (size.y <= 0.0f)
    {
        size.y = ImGui::GetFrameHeight();
    }

    std::string id = std::string("##") + label;

    bool clicked = ImGui::Button(id.c_str(), size);

    ImVec2 min = ImGui::GetItemRectMin();

    float startX = min.x + (size.x - contentWidth) * 0.5f;
    float midY = min.y + size.y * 0.5f;

    ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);

    ImVec4 icon4 = iconColor.w > 0.0f ? iconColor : textColor;

    icon4.w *= style.Alpha;
    textColor.w *= style.Alpha;

    UiIconAt(
        icon,
        ImVec2(startX + iconSize * 0.5f, midY),
        iconSize,
        ImGui::ColorConvertFloat4ToU32(icon4));

    ImGui::GetWindowDrawList()->AddText(
        ImVec2(startX + iconSize + gap, midY - textSize.y * 0.5f),
        ImGui::ColorConvertFloat4ToU32(textColor),
        text.c_str());

    return clicked;
}


void UiSectionHeader(
    const char* title,
    const char* right)
{
    ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

    ImGui::TextUnformatted(title);

    if (right && *right)
    {
        float width = ImGui::CalcTextSize(right).x;

        ImGui::SameLine(ImGui::GetContentRegionMax().x - width);

        ImGui::TextUnformatted(right);
    }

    ImGui::PopStyleColor();
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
