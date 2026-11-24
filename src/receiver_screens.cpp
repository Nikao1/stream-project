#include "receiver_screens.h"

#include <algorithm>
#include <cstring>

#include "imgui.h"
#include "ui_widgets.h"


// ============================================================
// ffmpeg ausente ou quebrado
// ============================================================


void DrawVoiceSpeakers(
    const std::vector<std::string>& names,
    bool myMicMuted)
{
    if (names.empty() && !myMicMuted)
        return;

    ImGui::SetNextWindowPos(
        ImVec2(12.0f * UiScale(), 12.0f * UiScale()));

    ImGui::SetNextWindowBgAlpha(0.55f);

    ImGui::Begin(
        "##falando",
        nullptr,
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoInputs);

    float icon = ImGui::GetTextLineHeight();

    if (myMicMuted)
    {
        float top = ImGui::GetCursorPosY();

        ImGui::SetCursorPosY(top + icon * 0.1f);

        UiMicIcon(icon, IM_COL32(239, 68, 68, 255), true);

        if (!names.empty())
        {
            ImGui::SameLine();
        }
    }

    for (size_t i = 0; i < names.size(); ++i)
    {
        if (i > 0)
        {
            ImGui::SameLine();
        }

        float avatar = icon * 1.2f;

        float top = ImGui::GetCursorPosY();

        UiAvatar(names[i].c_str(), avatar, false, true);

        ImGui::SameLine();

        ImGui::SetCursorPosY(
            top + (avatar - ImGui::GetTextLineHeight()) * 0.5f);

        ImGui::TextUnformatted(names[i].c_str());
    }

    ImGui::End();
}

void DrawFfmpegProblemScreen(
    const std::string& problem,
    ReceiverScreenActions& actions)
{
    UiFfmpegProblemPanel(
        problem.c_str(),
        "O FFmpeg é o programa que decodifica o vídeo. Sem ele "
        "não há imagem.",
        actions.openDownloadPage,
        actions.recheckFfmpeg
    );
}


// ============================================================
// Tela de conexao
// ============================================================

void DrawConnectScreen(
    char* input,
    size_t inputSize,
    bool busy,
    const std::string& status,
    const std::string& error,
    ReceiverScreenActions& actions)
{
    UiBeginCenteredPanel("##conectar", 440.0f);

    UiTitle("Assistir");

    UiMutedText(
        "Digite o código da sessão que a pessoa que está "
        "transmitindo te passou."
    );

    ImGui::Spacing();

    ImGui::BeginDisabled(busy);

    const char* pasteLabel = "Colar";

    float pasteWidth =
        ImGui::CalcTextSize(pasteLabel).x +
        ImGui::GetStyle().FramePadding.x * 2.0f;

    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x -
        pasteWidth -
        ImGui::GetStyle().ItemSpacing.x
    );

    if (ImGui::IsWindowAppearing())
    {
        ImGui::SetKeyboardFocusHere();
    }

    bool submitted =
        ImGui::InputTextWithHint(
            "##alvo",
            "Código da sessão",
            input,
            inputSize,
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll
        );

    ImGui::SameLine();

    if (ImGui::Button(pasteLabel))
    {
        // Cola tirando espacos e quebras de linha (um
        // convite longo as vezes vem quebrado em duas).
        const char* clipboard =
            ImGui::GetClipboardText();

        size_t length = 0;

        for (const char* c = clipboard;
             c && *c && length + 1 < inputSize;
             ++c)
        {
            if (*c != ' ' && *c != '\t' &&
                *c != '\r' && *c != '\n')
            {
                input[length++] = *c;
            }
        }

        input[length] = '\0';
    }

    ImGui::EndDisabled();

    if (UiPrimaryButton(
            busy ? "Conectando..." : "Conectar",
            !busy) ||
        (submitted && !busy))
    {
        actions.connect = true;
    }

    if (busy)
    {
        UiSpinner(8.0f * UiScale());

        ImGui::SameLine();

        UiMutedText(
            (status + UiBusyDots()).c_str()
        );

        UiMutedText(
            "Na primeira conexão depois de um tempo parado, "
            "o servidor pode levar até 1 minuto para "
            "responder."
        );
    }
    else if (!error.empty())
    {
        UiWrappedColored(
            UiColorError(),
            error.c_str()
        );
    }
    else
    {
        UiMutedText(
            "Também aceita um convite (texto que começa com "
            "SPH1.) ou o endereço IP de quem transmite."
        );
    }

    if (!busy)
    {
        ImGui::Spacing();

        if (ImGui::Button(
                "Voltar",
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    0.0f)))
        {
            actions.goHome = true;
        }
    }

    UiEndPanel();
}


// ============================================================
// Tela inicial do programa
// ============================================================

namespace
{
bool GearButton(const char* id, const char* tooltip);
}

std::string FitText(const std::string& text, float maxWidth);


void DrawHomeScreen(
    HomeScreenModel& model,
    HomeScreenActions& actions)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    // Largura: duas colunas (criar | entrar) se a janela deixar.
    float panelWidth =
        (std::min)(600.0f, ImGui::GetIO().DisplaySize.x / UiScale() - 40.0f);

    UiBeginCenteredPanel("##inicio", model.firstRun ? 420.0f : panelWidth);

    // ---------------- Titulo + engrenagem ----------------

    UiLogo(ImGui::GetFrameHeight() * 1.7f);

    ImGui::SameLine();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6.0f * UiScale());

    UiWordmark();

    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());

    if (GearButton("##inicio_config", "Configurações"))
    {
        actions.openSettings = true;
    }

    UiMutedText(
        "Tela e som direto de um computador para outro, sem "
        "passar por servidor.");

    ImGui::Spacing();


    // ---------------- Primeira vez: so nome e foto ----------------

    if (model.firstRun)
    {
        UiFieldLabel("Como os outros vão te ver?");

        ImGui::Spacing();

        float avatar = ImGui::GetFrameHeight() * 2.2f;

        ImVec2 avatarPos = ImGui::GetCursorScreenPos();

        UiAvatar(
            model.name && model.name[0] ? model.name : "?",
            avatar, false, 0.0f, model.avatar);

        ImGui::SetCursorScreenPos(avatarPos);

        if (ImGui::InvisibleButton("##fotoinicio", ImVec2(avatar, avatar)))
        {
            actions.chooseAvatar = true;
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Escolher uma foto (opcional)");
        }

        ImGui::SameLine();

        ImGui::BeginGroup();

        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() + (avatar - ImGui::GetFrameHeight()) * 0.5f);

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }

        bool submitted =
            ImGui::InputTextWithHint(
                "##nomeinicio",
                "Seu nome",
                model.name,
                model.nameSize,
                ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::EndGroup();

        if (!model.avatarError.empty())
        {
            UiWrappedColored(UiColorError(), model.avatarError.c_str());
        }

        ImGui::Spacing();

        bool hasName = false;

        for (const char* c = model.name; c && *c; ++c)
        {
            hasName |= *c != ' ' && *c != '\t';
        }

        if (UiPrimaryButton("Continuar", hasName) || (submitted && hasName))
        {
            actions.finishProfile = true;
        }

        UiMutedText("Clique na bolinha para escolher uma foto (opcional).");

        UiEndPanel();

        return;
    }


    // ---------------- Convite copiado ----------------

    if (!model.clipboardRoom.empty() && !model.busy)
    {
        ImVec4 accent = UiColorAccent();

        ImVec2 start = ImGui::GetCursorScreenPos();

        float width = ImGui::GetContentRegionAvail().x;

        float height = ImGui::GetFrameHeight() + style.FramePadding.y * 4.0f;

        ImGui::GetWindowDrawList()->AddRectFilled(
            start, ImVec2(start.x + width, start.y + height),
            ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, 0.14f)),
            8.0f * UiScale());

        float pad = style.FramePadding.y * 2.0f;

        ImGui::SetCursorScreenPos(ImVec2(start.x + pad * 1.5f, start.y + pad));

        ImGui::AlignTextToFramePadding();

        ImGui::TextUnformatted(
            ("Convite copiado: sala " + model.clipboardRoom).c_str());

        float close = ImGui::GetFrameHeight();

        float enterWidth =
            ImGui::CalcTextSize("Entrar").x + style.FramePadding.x * 4.0f;

        ImGui::SameLine(
            ImGui::GetContentRegionMax().x - enterWidth - close -
            style.ItemSpacing.x - pad);

        if (UiPrimaryButtonSized("Entrar##colado", ImVec2(enterWidth, 0.0f)))
        {
            actions.joinClipboard = true;
        }

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

        if (UiIconButton("##ignorarcolado", UiIcon::Close, "Ignorar"))
        {
            actions.dismissClipboard = true;
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + height));

        ImGui::Dummy(ImVec2(width, style.ItemSpacing.y));
    }


    // ---------------- Perfil ----------------

    {
        float avatar = ImGui::GetFrameHeight() * 1.5f;

        UiAvatar(model.name, avatar, false, 0.0f, model.avatar);

        ImGui::SameLine();

        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() + (avatar - ImGui::GetFrameHeight()) * 0.5f);

        ImGui::AlignTextToFramePadding();

        ImGui::TextUnformatted(model.name);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

        static char editName[64] = "";

        if (UiIconButton("##editarperfil", UiIcon::Edit, "Mudar nome ou foto"))
        {
            strncpy(editName, model.name, sizeof(editName) - 1);
            editName[sizeof(editName) - 1] = '\0';

            ImGui::OpenPopup("##perfil");
        }

        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // Testar microfone (canto direito).
        const char* testLabel = "      Testar microfone";

        float testWidth =
            ImGui::CalcTextSize(testLabel).x + style.FramePadding.x * 2.0f;

        ImGui::SameLine(ImGui::GetContentRegionMax().x - testWidth);

        if (ImGui::Button(testLabel, ImVec2(testWidth, 0.0f)))
        {
            actions.testMicrophone = true;
        }

        ImVec2 testMin = ImGui::GetItemRectMin();

        UiMicIconAt(
            ImVec2(testMin.x + style.FramePadding.x * 1.2f, testMin.y + style.FramePadding.y),
            ImGui::GetTextLineHeight(),
            ImGui::GetColorU32(ImGuiCol_Text),
            false);

        if (ImGui::BeginPopup("##perfil"))
        {
            UiMutedText("Seu nome");

            ImGui::SetNextItemWidth(260.0f * UiScale());

            if (ImGui::IsWindowAppearing())
            {
                ImGui::SetKeyboardFocusHere();
            }

            bool enter =
                ImGui::InputText(
                    "##nomeperfil", editName, sizeof(editName),
                    ImGuiInputTextFlags_EnterReturnsTrue);

            float half = (260.0f * UiScale() - style.ItemSpacing.x) * 0.5f;

            if (ImGui::Button("Trocar foto", ImVec2(half, 0.0f)))
            {
                actions.chooseAvatar = true;
            }

            ImGui::SameLine();

            ImGui::BeginDisabled(model.avatar == nullptr);

            if (ImGui::Button("Remover foto", ImVec2(half, 0.0f)))
            {
                actions.removeAvatar = true;
            }

            ImGui::EndDisabled();

            if (!model.avatarError.empty())
            {
                UiWrappedColored(UiColorError(), model.avatarError.c_str());
            }

            bool hasName = false;

            for (const char* c = editName; *c; ++c)
            {
                hasName |= *c != ' ' && *c != '\t';
            }

            if ((UiPrimaryButton("Salvar", hasName) || enter) && hasName)
            {
                strncpy(model.name, editName, model.nameSize - 1);
                model.name[model.nameSize - 1] = '\0';

                actions.nameChanged = true;

                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();


    // ---------------- Criar | Entrar ----------------

    ImGui::BeginDisabled(model.busy);

    bool twoColumns = ImGui::GetContentRegionAvail().x >= 480.0f * UiScale();

    float column =
        twoColumns
        ? (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x * 3.0f) * 0.5f
        : ImGui::GetContentRegionAvail().x;

    float columnsTop = ImGui::GetCursorPosY();

    ImGui::BeginGroup();

    UiFieldLabel("Criar uma sala");

    if (model.roomTitle)
    {
        ImGui::SetNextItemWidth(column);

        ImGui::InputTextWithHint(
            "##nomesalainicio",
            "Nome da sala (opcional)",
            model.roomTitle,
            model.roomTitleSize);
    }

    if (UiPrimaryButtonSized(
            model.busy ? "Aguarde..." : "Criar sala",
            ImVec2(column, 0.0f)))
    {
        actions.createRoom = true;
    }

    ImGui::EndGroup();

    if (twoColumns)
    {
        // (SetCursorPos em vez de SameLine: o rotulo fica na mesma
        // altura do outro, sem o ajuste de linha dos botoes.)
        ImGui::SetCursorPos(
            ImVec2(
                ImGui::GetCursorPosX() + column + style.ItemSpacing.x * 3.0f,
                columnsTop));
    }
    else
    {
        ImGui::Spacing();
    }

    ImGui::BeginGroup();

    UiFieldLabel("Entrar com um código");

    ImGui::SetNextItemWidth(column);

    bool submitted =
        ImGui::InputTextWithHint(
            "##codigo",
            "Código ou convite",
            model.code,
            model.codeSize,
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll);

    if (ImGui::Button("Entrar", ImVec2(column, 0.0f)) || submitted)
    {
        actions.enter = true;
    }

    ImGui::EndGroup();

    ImGui::EndDisabled();

    if (model.busy)
    {
        UiSpinner(8.0f * UiScale());

        ImGui::SameLine();

        UiMutedText((model.status + UiBusyDots()).c_str());

        UiMutedText(
            "Na primeira vez depois de um tempo parado, o "
            "servidor pode levar até 1 minuto para responder.");
    }
    else if (!model.error.empty())
    {
        UiWrappedColored(UiColorError(), model.error.c_str());
    }


    // ---------------- Salas recentes ----------------

    if (!model.recent.empty())
    {
        ImGui::Spacing();

        UiFieldLabel("Salas recentes");

        float enterWidth =
            ImGui::CalcTextSize("Entrar").x + style.FramePadding.x * 4.0f;

        float forget = ImGui::GetFrameHeight();

        for (size_t i = 0; i < model.recent.size(); ++i)
        {
            const RecentRoomEntry& room = model.recent[i];

            ImGui::PushID(static_cast<int>(i));

            // Uma linha: nome  codigo · quando  [Entrar] [x]
            float textWidth =
                ImGui::GetContentRegionAvail().x - enterWidth - forget -
                style.ItemSpacing.x * 3.0f;

            std::string detail = room.shortCode + "  ·  " + room.when;

            float detailWidth = ImGui::CalcTextSize(detail.c_str()).x;

            ImGui::AlignTextToFramePadding();

            ImGui::TextUnformatted(
                FitText(room.title.empty() ? "Sala sem nome" : room.title,
                        (std::max)(60.0f, textWidth - detailWidth - style.ItemSpacing.x * 2.0f)).c_str());

            ImGui::SameLine(0.0f, style.ItemSpacing.x * 2.0f);

            ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());
            ImGui::TextUnformatted(detail.c_str());
            ImGui::PopStyleColor();

            ImGui::SameLine(
                ImGui::GetContentRegionMax().x - enterWidth - forget -
                style.ItemSpacing.x);

            ImGui::BeginDisabled(model.busy);

            if (ImGui::Button("Entrar", ImVec2(enterWidth, 0.0f)))
            {
                actions.joinRecent = static_cast<int>(i);
            }

            ImGui::EndDisabled();

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

            if (UiIconButton("##esquecer", UiIcon::Close, "Tirar da lista"))
            {
                actions.forgetRecent = static_cast<int>(i);
            }

            ImGui::PopStyleVar();
            ImGui::PopStyleColor();

            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::BeginDisabled(model.busy);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

    if (ImGui::Button("Transmitir sem sala (código direto)"))
    {
        actions.shareWithoutRoom = true;
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImGui::EndDisabled();

    UiEndPanel();
}


// ============================================================
// Chat
// ============================================================

bool DrawChatBox(
    const char* id,
    const std::vector<ChatLine>& lines,
    char* input,
    size_t inputSize,
    float height,
    bool focusInput)
{
    ImGui::PushID(id);

    const ImGuiStyle& style = ImGui::GetStyle();

    float inputHeight = ImGui::GetFrameHeight();

    ImGui::PushStyleColor(ImGuiCol_Border, UiColorDivider());

    ImGui::BeginChild(
        "##mensagens",
        ImVec2(0.0f, height - inputHeight - style.ItemSpacing.y),
        ImGuiChildFlags_Borders);

    ImGui::PopStyleColor();

    if (lines.empty())
    {
        UiMutedText("Nenhuma mensagem ainda.");
    }

    for (const ChatLine& line : lines)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            line.mine ? UiColorAccent() : UiColorMuted());

        ImGui::TextUnformatted(line.name.c_str());

        ImGui::PopStyleColor();

        ImGui::SameLine();

        ImGui::PushTextWrapPos(0.0f);

        ImGui::TextUnformatted(line.text.c_str());

        ImGui::PopTextWrapPos();
    }

    // Mensagem nova: rola pro fim (se a pessoa nao estiver
    // lendo algo mais antigo la em cima).
    // (contagem guardada por caixa - a da sala e a do video sao
    // separadas)
    ImGuiStorage* storage = ImGui::GetStateStorage();

    ImGuiID countKey = ImGui::GetID("##quantas");

    int lastCount = storage->GetInt(countKey, -1);

    if (static_cast<int>(lines.size()) != lastCount &&
        (lastCount < 0 ||
         ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40.0f))
    {
        ImGui::SetScrollHereY(1.0f);
    }

    storage->SetInt(countKey, static_cast<int>(lines.size()));

    ImGui::EndChild();

    const char* sendLabel = "Enviar";

    float sendWidth =
        ImGui::CalcTextSize(sendLabel).x +
        style.FramePadding.x * 2.0f;

    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x -
        sendWidth -
        style.ItemSpacing.x);

    if (focusInput)
    {
        ImGui::SetKeyboardFocusHere();
    }

    bool submitted =
        ImGui::InputTextWithHint(
            "##escrever",
            "Mensagem",
            input,
            inputSize,
            ImGuiInputTextFlags_EnterReturnsTrue);

    ImGui::SameLine();

    bool clicked = ImGui::Button(sendLabel);

    bool send =
        (submitted || clicked) &&
        input[0] != '\0';

    // Depois de enviar com Enter, o cursor continua no campo.
    if (submitted)
    {
        ImGui::SetKeyboardFocusHere(-1);
    }

    ImGui::PopID();

    return send;
}


// ============================================================
// Avisos
// ============================================================

void DrawToasts(
    const std::vector<Toast>& toasts)
{
    const double lifetime = 5.0;

    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    float margin = 16.0f * UiScale();

    float y =
        viewport->WorkPos.y + viewport->WorkSize.y - margin;

    double now = ImGui::GetTime();

    int index = 0;

    // Os mais novos embaixo.
    for (auto it = toasts.rbegin(); it != toasts.rend(); ++it)
    {
        double age = now - it->shownAt;

        if (age > lifetime || age < 0.0)
            continue;

        float alpha =
            age < 4.3
            ? 1.0f
            : static_cast<float>((lifetime - age) / 0.7);

        std::string id =
            "##aviso" + std::to_string(index++);

        ImGui::SetNextWindowPos(
            ImVec2(
                viewport->WorkPos.x + viewport->WorkSize.x - margin,
                y),
            ImGuiCond_Always,
            ImVec2(1.0f, 1.0f));

        ImGui::SetNextWindowBgAlpha(0.95f);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

        ImGui::Begin(
            id.c_str(),
            nullptr,
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs);

        ImGui::PushTextWrapPos(320.0f * UiScale());

        ImGui::TextUnformatted(it->text.c_str());

        ImGui::PopTextWrapPos();

        y -= ImGui::GetWindowHeight() + 8.0f * UiScale();

        ImGui::End();

        ImGui::PopStyleVar();

        // No maximo 4 de uma vez.
        if (index >= 4)
            break;
    }
}


// Paineis flutuantes (chat, pessoas, barra da sala): margem
// interna menor que a das telas - o X fica perto da borda e nao
// sobra espaco vazio.
// Largura do painel de pessoas (o do video fica ao lado dele).
constexpr float PEOPLE_PANEL_WIDTH = 290.0f;


void PushFloatingPanelStyle()
{
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(14.0f * UiScale(), 12.0f * UiScale()));
}


void PopFloatingPanelStyle()
{
    ImGui::PopStyleVar();
}


// ============================================================
// Sala
// ============================================================

namespace
{

// Pessoa: bolinha + nome (+ linha cinza).
void PersonRow(
    const std::string& name,
    const std::string& detail,
    bool live,
    float rightReserve,
    float speaking = 0.0f,
    void* image = nullptr,
    int indicator = 0)
{
    // indicator: 0 = nada, 1 = na voz (bolinha verde), 2 =
    // microfone desligado, 3 = silenciado por voce.
    float avatar = ImGui::GetFrameHeight() * 1.15f;

    ImVec2 avatarPos = ImGui::GetCursorScreenPos();

    UiAvatar(name.c_str(), avatar, live, speaking, image);

    // Na voz: bolinha verde no canto da foto (com um aro da cor
    // do fundo, pra descolar da foto).
    if (indicator != 0)
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();

        ImVec2 dot(avatarPos.x + avatar * 0.86f, avatarPos.y + avatar * 0.86f);

        float r = avatar * 0.14f;

        ImVec4 ring = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);

        ring.w = ImGui::GetStyle().Alpha;

        ImVec4 green = UiColorSuccess();

        green.w *= ImGui::GetStyle().Alpha;

        draw->AddCircleFilled(dot, r + 2.0f * UiScale(), ImGui::ColorConvertFloat4ToU32(ring), 14);
        draw->AddCircleFilled(dot, r, ImGui::ColorConvertFloat4ToU32(green), 14);

        if (ImGui::IsMouseHoveringRect(
                ImVec2(dot.x - r * 1.5f, dot.y - r * 1.5f),
                ImVec2(dot.x + r * 1.5f, dot.y + r * 1.5f)))
        {
            ImGui::SetTooltip("Na voz");
        }
    }

    ImGui::SameLine();

    ImGui::BeginGroup();

    float textWidth =
        ImGui::GetContentRegionAvail().x - rightReserve;

    ImGui::PushTextWrapPos(
        ImGui::GetCursorPosX() + (std::max)(40.0f, textWidth));

    if (detail.empty())
    {
        // Centraliza o nome na altura da bolinha.
        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() +
            (avatar - ImGui::GetTextLineHeight()) * 0.5f);
    }

    ImGui::TextUnformatted(name.c_str());

    // Microfone desligado / silenciado por voce: ao lado do nome.
    if (indicator >= 2)
    {
        ImVec2 nameMax = ImGui::GetItemRectMax();
        ImVec2 nameMin = ImGui::GetItemRectMin();

        float line = ImGui::GetTextLineHeight();

        ImVec2 center(
            nameMax.x + 6.0f * UiScale() + line * 0.45f,
            (nameMin.y + nameMax.y) * 0.5f);

        int alpha = static_cast<int>(255 * ImGui::GetStyle().Alpha);

        ImVec4 red4 = UiColorError();

        red4.w = alpha / 255.0f;

        ImU32 red = ImGui::ColorConvertFloat4ToU32(red4);

        const char* tip = "";

        if (indicator == 2)
        {
            UiMicIconAt(
                ImVec2(center.x - line * 0.45f, center.y - line * 0.45f),
                line * 0.9f, red, true);

            tip = "Microfone desligado";
        }
        else
        {
            UiIconAt(UiIcon::VolumeOff, center, line * 0.9f, ImGui::GetColorU32(UiColorMuted()));

            tip = "Silenciado por você";
        }

        if (ImGui::IsMouseHoveringRect(
                ImVec2(center.x - line * 0.5f, nameMin.y),
                ImVec2(center.x + line * 0.5f, nameMax.y)))
        {
            ImGui::SetTooltip("%s", tip);
        }
    }

    if (!detail.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

        ImGui::TextUnformatted(detail.c_str());

        ImGui::PopStyleColor();
    }

    ImGui::PopTextWrapPos();

    ImGui::EndGroup();
}

// Pessoa na lista da sala / painel: bolinha (anel vermelho =
// transmitindo, verde = falando), nome, situacao na voz. Com a
// voz ligada, clicar abre o volume dessa pessoa. Devolve true
// quando o volume mudou (em outVolume).
bool MemberRow(
    const std::string& name,
    bool live,
    MemberVoice voice,
    bool isMe,
    bool voiceJoined,
    float volume,
    float& outVolume,
    float speakAmount,
    void* image)
{
    // Uma linha so: nome + um sinal (bolinha verde = na voz;
    // microfone cortado; som cortado = silenciado por voce). Falar
    // ja acende o anel verde da foto. Texto so enquanto conecta.
    std::string shown = name;

    std::string detailText =
        voice == MemberVoice::Connecting ? "conectando..." : "";

    int indicator = 0;

    if (voice == MemberVoice::Muted)
        indicator = 2;
    else if (voice == MemberVoice::In || voice == MemberVoice::Speaking)
        indicator = 1;

    if (!isMe && voice != MemberVoice::Out && volume <= 0.001f)
    {
        indicator = 3;
    }

    ImGui::BeginGroup();

    PersonRow(
        shown,
        detailText,
        live,
        0.0f,
        speakAmount,
        image,
        indicator);

    ImGui::EndGroup();

    bool adjustable =
        !isMe &&
        voiceJoined &&
        voice != MemberVoice::Out;

    if (!adjustable)
        return false;

    bool changed = false;

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Clique para ajustar o volume");
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
        ImGui::IsItemClicked(ImGuiMouseButton_Right))
    {
        ImGui::OpenPopup("##volume");
    }

    if (ImGui::BeginPopup("##volume"))
    {
        ImGui::TextUnformatted(name.c_str());

        float percent = volume * 100.0f;

        ImGui::SetNextItemWidth(220.0f * UiScale());

        if (ImGui::SliderFloat(
                "##vol", &percent, 0.0f, 200.0f, "%.0f%%"))
        {
            outVolume = percent / 100.0f;
            changed = true;
        }

        bool silenced = volume <= 0.001f;

        float half =
            (220.0f * UiScale() -
             ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        if (ImGui::Button(
                silenced ? "Ativar som" : "Silenciar",
                ImVec2(half, 0.0f)))
        {
            outVolume = silenced ? 1.0f : 0.0f;
            changed = true;
        }

        ImGui::SameLine();

        if (ImGui::Button("Volume normal", ImVec2(half, 0.0f)))
        {
            outVolume = 1.0f;
            changed = true;
        }

        ImGui::EndPopup();
    }

    return changed;
}


// Botao com a engrenagem (abre as configuracoes de voz).
bool GearButton(
    const char* id,
    const char* tooltip)
{
    float size = ImGui::GetFrameHeight();

    bool clicked = ImGui::Button(id, ImVec2(size, size));

    bool hovered = ImGui::IsItemHovered();

    ImVec2 min = ImGui::GetItemRectMin();

    // Furo da engrenagem: cor do fundo (opaca).
    ImVec4 windowBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);

    windowBg.w = ImGui::GetStyle().Alpha;

    (void)hovered;

    UiGearIconAt(
        ImVec2(min.x + size * 0.5f, min.y + size * 0.5f),
        size * 0.66f,
        ImGui::GetColorU32(ImGuiCol_Text),
        ImGui::ColorConvertFloat4ToU32(windowBg));

    if (hovered)
    {
        ImGui::SetTooltip("%s", tooltip);
    }

    return clicked;
}


// Lista de dispositivos (microfone / saida): rotulo a esquerda
// em fieldX. outChoice: 0 = padrao, i + 1 = names[i].
void DeviceCombo(
    const char* label,
    const char* id,
    const std::vector<std::string>& names,
    int selected,
    float fieldX,
    int& outChoice,
    bool& outRefresh)
{
    ImGui::AlignTextToFramePadding();

    UiMutedText(label);

    ImGui::SameLine();

    ImGui::SetCursorPosX(fieldX);

    ImGui::SetNextItemWidth(-1.0f);

    if (selected < 0 ||
        selected > static_cast<int>(names.size()))
    {
        selected = 0;
    }

    const char* preview =
        selected == 0
        ? "Padrão do Windows"
        : names[selected - 1].c_str();

    if (ImGui::BeginCombo(id, preview))
    {
        // Acabou de abrir: atualiza a lista (alguem pode ter
        // ligado um fone agora).
        if (ImGui::IsWindowAppearing())
        {
            outRefresh = true;
        }

        if (ImGui::Selectable("Padrão do Windows", selected == 0))
        {
            outChoice = 0;
        }

        for (size_t i = 0; i < names.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));

            if (ImGui::Selectable(
                    names[i].c_str(),
                    selected == static_cast<int>(i) + 1))
            {
                outChoice = static_cast<int>(i) + 1;
            }

            ImGui::PopID();
        }

        ImGui::EndCombo();
    }
}


// Secao "Voz" da sala: so o essencial (entrar/sair, microfone).
// O resto fica nas configuracoes (engrenagem).
} // namespace


void DrawRoomScreen(
    RoomScreenModel& model,
    RoomScreenActions& actions)
{
    static double codeCopiedAt = 0.0;
    static double linkCopiedAt = 0.0;

    const ImGuiStyle& style = ImGui::GetStyle();

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    // A janela inteira, menos o painel Pessoas/Chat a direita.
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);

    ImGui::SetNextWindowSize(
        ImVec2(
            (std::max)(200.0f, viewport->WorkSize.x - model.rightInset),
            viewport->WorkSize.y),
        ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(24.0f * UiScale(), 18.0f * UiScale()));

    // Fundo da sala: o tom de base (os cartoes ficam por cima, na
    // cor das superficies).
    ImGui::PushStyleColor(ImGuiCol_WindowBg, UiColorBase());

    ImGui::Begin(
        "##sala",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    auto recently =
        [](double at)
        {
            return at > 0.0 && ImGui::GetTime() - at < 2.0;
        };


    // --------------------------------------------------------
    // Cabecalho: nome (lapis renomeia) · pessoas | convite,
    // codigo, sair (e, em janela estreita, Pessoas/Chat)
    // --------------------------------------------------------

    bool hasTitle = !model.title.empty();

    if (hasTitle)
    {
        UiTitle(model.title.c_str());
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

        UiTitle("Sala sem nome");

        ImGui::PopStyleColor();
    }

    float titleBottom = ImGui::GetItemRectMax().y;
    float titleTop = ImGui::GetItemRectMin().y;

    ImGui::SameLine();

    ImGui::SetCursorScreenPos(
        ImVec2(
            ImGui::GetCursorScreenPos().x,
            (titleTop + titleBottom - ImGui::GetFrameHeight()) * 0.5f));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    static char titleBuffer[64] = "";

    if (UiIconButton(
            "##renomear",
            UiIcon::Edit,
            hasTitle ? "Renomear a sala" : "Dar um nome à sala"))
    {
        strncpy(titleBuffer, model.title.c_str(), sizeof(titleBuffer) - 1);

        titleBuffer[sizeof(titleBuffer) - 1] = '\0';

        ImGui::OpenPopup("##nomesala");
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (ImGui::BeginPopup("##nomesala"))
    {
        UiMutedText("Nome da sala (todos veem; opcional)");

        ImGui::SetNextItemWidth(260.0f * UiScale());

        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
        }

        bool enter =
            ImGui::InputTextWithHint(
                "##nome", "Ex.: Jogatina de sexta",
                titleBuffer, 33,
                ImGuiInputTextFlags_EnterReturnsTrue);

        float half = (260.0f * UiScale() - style.ItemSpacing.x) * 0.5f;

        if (ImGui::Button("Salvar", ImVec2(half, 0.0f)) || enter)
        {
            actions.setTitle = true;
            actions.titleText = titleBuffer;

            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(!hasTitle);

        if (ImGui::Button("Tirar o nome", ImVec2(half, 0.0f)))
        {
            actions.setTitle = true;
            actions.titleText.clear();

            ImGui::CloseCurrentPopup();
        }

        ImGui::EndDisabled();

        ImGui::EndPopup();
    }

    // Botoes a direita (na mesma linha se couber; senao embaixo).
    // (icone + espaco + texto + margens)
    float iconExtra = ImGui::GetTextLineHeight() * 0.95f + 7.0f * UiScale();

    float codeWidth =
        ImGui::CalcTextSize("Copiar código").x + iconExtra + style.FramePadding.x * 2.0f;

    float linkWidth =
        model.inviteLink.empty()
        ? 0.0f
        : ImGui::CalcTextSize("Copiar convite").x + iconExtra + style.FramePadding.x * 2.0f;

    float leaveWidth =
        ImGui::CalcTextSize("Sair").x + iconExtra + style.FramePadding.x * 2.0f;

    float panelWidth = model.narrow ? ImGui::GetFrameHeight() + style.ItemSpacing.x : 0.0f;

    float total =
        codeWidth + leaveWidth + style.ItemSpacing.x +
        (linkWidth > 0.0f ? linkWidth + style.ItemSpacing.x : 0.0f) +
        panelWidth;

    float lineEnd = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

    float buttonsY =
        (titleTop + titleBottom - ImGui::GetFrameHeight()) * 0.5f -
        ImGui::GetWindowPos().y;

    if (lineEnd + total + style.ItemSpacing.x * 2.0f <=
        ImGui::GetContentRegionMax().x)
    {
        ImGui::SameLine(ImGui::GetContentRegionMax().x - total);

        ImGui::SetCursorPosY(buttonsY);
    }
    else
    {
        buttonsY = ImGui::GetCursorPosY();
    }

    if (linkWidth > 0.0f)
    {
        if (UiIconLabelButton(
                recently(linkCopiedAt)
                ? "Copiado!##copiarlink"
                : "Copiar convite##copiarlink",
                UiIcon::Link,
                ImVec2(linkWidth, 0.0f)))
        {
            ImGui::SetClipboardText(model.inviteLink.c_str());

            linkCopiedAt = ImGui::GetTime();
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                "Link que abre o programa direto nesta sala.\n"
                "A chave vai depois do # e não passa pelo servidor.");
        }

        ImGui::SameLine();

        ImGui::SetCursorPosY(buttonsY);
    }

    if (UiIconLabelButton(
            recently(codeCopiedAt)
            ? "Copiado!##copiarsala"
            : "Copiar código##copiarsala",
            UiIcon::Copy,
            ImVec2(codeWidth, 0.0f)))
    {
        ImGui::SetClipboardText(model.code.c_str());

        codeCopiedAt = ImGui::GetTime();
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", model.code.c_str());
    }

    ImGui::SameLine();

    ImGui::SetCursorPosY(buttonsY);

    {
        ImVec4 red = UiColorError();

        ImGui::PushStyleColor(ImGuiCol_Text, red);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(red.x, red.y, red.z, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(red.x, red.y, red.z, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(red.x, red.y, red.z, 0.16f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(red.x, red.y, red.z, 0.24f));

        if (UiIconLabelButton("Sair##sairsala", UiIcon::Exit, ImVec2(leaveWidth, 0.0f)))
        {
            actions.leave = true;
        }

        ImGui::PopStyleColor(5);
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Sair da sala");
    }

    if (model.narrow)
    {
        ImGui::SameLine();

        ImGui::SetCursorPosY(buttonsY);

        if (UiIconButton(
                "##abrirpainel",
                UiIcon::People,
                "Pessoas e chat",
                model.panelOpen,
                false,
                model.panelOpen ? 0 : model.unreadChat))
        {
            actions.togglePanel = true;
        }
    }

    UiMutedText(
        (std::to_string(model.memberCount) + " de " +
         std::to_string(model.maxMembers) + " pessoas  ·  " +
         (model.inviteLink.empty()
          ? "passe o código para quem vai entrar"
          : "mande o convite para quem vai entrar")).c_str());

    if (model.offline)
    {
        UiWrappedColored(
            UiColorError(),
            "Sem resposta do servidor da sala. Verifique sua "
            "internet - quem você já está assistindo continua."
        );
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();


    // --------------------------------------------------------
    // Transmissoes: cartoes com previa, e o de transmitir
    // --------------------------------------------------------

    UiFieldLabel("Transmitindo agora");

    ImGui::Spacing();

    float avail = ImGui::GetContentRegionAvail().x;

    float gap = 14.0f * UiScale();

    int columns =
        (std::max)(1, (std::min)(4, static_cast<int>((avail + gap) / (250.0f * UiScale() + gap))));

    float cardWidth = (avail - gap * (columns - 1)) / columns;

    float thumbHeight = cardWidth * 9.0f / 16.0f;

    float footer = ImGui::GetFrameHeight() + style.FramePadding.y * 2.0f + 6.0f * UiScale();

    float cardHeight = thumbHeight + footer;

    float rounding = 10.0f * UiScale();

    ImDrawList* draw = ImGui::GetWindowDrawList();

    ImU32 border = ImGui::GetColorU32(UiColorDivider());

    int slot = 0;

    // Posiciona o proximo cartao na grade.
    auto nextCard =
        [&]()
        {
            if (slot % columns != 0)
            {
                ImGui::SameLine(0.0f, gap);
            }

            slot++;
        };

    size_t count = model.presenters.size();

    for (size_t i = 0; i < count; ++i)
    {
        const RoomPresenterEntry& entry = model.presenters[i];

        ImGui::PushID(static_cast<int>(i));

        nextCard();

        ImGui::BeginGroup();

        ImVec2 pos = ImGui::GetCursorScreenPos();

        bool canWatch = !entry.isMe && !entry.watching && !model.connecting;

        ImGui::InvisibleButton("##previa", ImVec2(cardWidth, thumbHeight));

        bool hovered = ImGui::IsItemHovered();

        if (ImGui::IsItemClicked() && canWatch)
        {
            actions.watch = static_cast<int>(i);
        }

        if (hovered && canWatch)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }

        ImVec2 thumbMax(pos.x + cardWidth, pos.y + thumbHeight);

        draw->AddRectFilled(
            pos, thumbMax, IM_COL32(0, 0, 0, 255), rounding,
            ImDrawFlags_RoundCornersTop);

        if (entry.thumbnail && entry.thumbWidth > 0.0f && entry.thumbHeight > 0.0f)
        {
            float scale =
                (std::min)(cardWidth / entry.thumbWidth,
                           thumbHeight / entry.thumbHeight);

            ImVec2 size(entry.thumbWidth * scale, entry.thumbHeight * scale);

            ImVec2 min(pos.x + (cardWidth - size.x) * 0.5f,
                       pos.y + (thumbHeight - size.y) * 0.5f);

            draw->AddImageRounded(
                reinterpret_cast<ImTextureID>(entry.thumbnail),
                min, ImVec2(min.x + size.x, min.y + size.y),
                ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(255, 255, 255, 255),
                rounding, ImDrawFlags_RoundCornersTop);
        }
        else
        {
            // Sem previa: foto ou inicial no meio.
            float diameter = thumbHeight * 0.38f;

            ImVec2 back = ImGui::GetCursorScreenPos();

            ImGui::SetCursorScreenPos(
                ImVec2(pos.x + (cardWidth - diameter) * 0.5f,
                       pos.y + (thumbHeight - diameter) * 0.5f));

            UiAvatar(entry.name.c_str(), diameter, false, 0.0f, entry.avatar);

            ImGui::SetCursorScreenPos(back);
        }

        // Rodape do cartao: bolinha vermelha, nome · o que mostra,
        // botao.
        ImVec2 footerMin(pos.x, thumbMax.y);
        ImVec2 cardMax(pos.x + cardWidth, thumbMax.y + footer);

        draw->AddRectFilled(
            footerMin, cardMax,
            ImGui::GetColorU32(UiColorSurface()), rounding,
            ImDrawFlags_RoundCornersBottom);

        draw->AddRect(
            pos, cardMax,
            hovered && canWatch ? ImGui::GetColorU32(UiColorAccent()) : border,
            rounding, 0, 1.0f * UiScale());

        // Sua transmissao: dois icones (opcoes, parar) no lugar
        // do botao.
        float iconButton = ImGui::GetFrameHeight();

        float buttonWidth =
            entry.isMe
            ? iconButton * 2.0f + style.ItemSpacing.x
            : ImGui::CalcTextSize("Assistindo").x + style.FramePadding.x * 2.0f;

        float midY = footerMin.y + footer * 0.5f;

        if (entry.isMe)
        {
            // "N assistindo" no canto da previa.
            std::string viewers =
                std::to_string(model.sharingViewers) +
                " assistindo";

            ImVec2 textSize = ImGui::CalcTextSize(viewers.c_str());

            float padX = 8.0f * UiScale();
            float padY = 3.0f * UiScale();

            ImVec2 pillMin(pos.x + 8.0f * UiScale(), pos.y + 8.0f * UiScale());
            ImVec2 pillMax(pillMin.x + textSize.x + padX * 2.0f,
                           pillMin.y + textSize.y + padY * 2.0f);

            draw->AddRectFilled(pillMin, pillMax, IM_COL32(0, 0, 0, 170), 20.0f * UiScale());

            draw->AddText(
                ImVec2(pillMin.x + padX, pillMin.y + padY),
                IM_COL32(255, 255, 255, 235),
                viewers.c_str());
        }

        float pad = 10.0f * UiScale();

        draw->AddCircleFilled(
            ImVec2(footerMin.x + pad + 4.0f * UiScale(), midY),
            4.0f * UiScale(), IM_COL32(239, 68, 68, 255), 12);

        std::string text =
            (entry.isMe ? std::string("Você") : entry.name) +
            (entry.label.empty() ? std::string() : "  ·  " + entry.label);

        float textX = footerMin.x + pad + 14.0f * UiScale();

        float textMax = cardMax.x - pad - buttonWidth - style.ItemSpacing.x - textX;

        std::string fitted = FitText(text, textMax);

        draw->AddText(
            ImVec2(textX, midY - ImGui::GetTextLineHeight() * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text),
            fitted.c_str());

        if (fitted != text &&
            ImGui::IsMouseHoveringRect(footerMin, cardMax))
        {
            ImGui::SetTooltip("%s", text.c_str());
        }

        ImGui::SetCursorScreenPos(
            ImVec2(cardMax.x - pad - buttonWidth,
                   midY - ImGui::GetFrameHeight() * 0.5f));

        if (entry.isMe)
        {
            if (UiIconButton("##opcoesminha", UiIcon::Sliders, "Opções da transmissão"))
            {
                actions.toggleShareOptions = true;
            }

            ImGui::SameLine();

            if (UiIconButton("##pararminha", UiIcon::Stop, "Parar de transmitir", false, true))
            {
                actions.stopShare = true;
            }
        }
        else if (entry.watching)
        {
            ImGui::BeginDisabled();
            ImGui::Button(
                model.connecting ? "Conectando" : "Assistindo",
                ImVec2(buttonWidth, 0.0f));
            ImGui::EndDisabled();
        }
        else
        {
            ImGui::BeginDisabled(model.connecting);

            ImGui::PushStyleColor(ImGuiCol_Text, UiColorAccent());

            if (ImGui::Button("Assistir", ImVec2(buttonWidth, 0.0f)))
            {
                actions.watch = static_cast<int>(i);
            }

            ImGui::PopStyleColor();

            ImGui::EndDisabled();
        }

        ImGui::SetCursorScreenPos(pos);

        ImGui::Dummy(ImVec2(cardWidth, cardHeight));

        ImGui::EndGroup();

        ImGui::PopID();
    }

    // Cartao de transmitir (ou da sua transmissao, enquanto ela
    // ainda nao apareceu na lista).
    bool myCard = false;

    for (const RoomPresenterEntry& entry : model.presenters)
    {
        myCard |= entry.isMe;
    }

    // Ninguem transmitindo: um cartao so, grande, no meio.
    bool emptyState = model.presenters.empty() && !model.sharing;

    if (emptyState)
    {
        cardWidth = (std::min)(380.0f * UiScale(), avail);
        cardHeight = cardWidth * 0.52f;

        float below = ImGui::GetTextLineHeightWithSpacing() * 2.0f;

        float free = ImGui::GetContentRegionAvail().y - cardHeight - below;

        ImGui::SetCursorPos(
            ImVec2(
                ImGui::GetCursorPosX() + (avail - cardWidth) * 0.5f,
                ImGui::GetCursorPosY() + (std::max)(0.0f, free * 0.45f)));
    }

    if (!myCard)
    {
        if (!emptyState)
        {
            nextCard();
        }

        ImGui::BeginGroup();

        ImVec2 pos = ImGui::GetCursorScreenPos();

        ImVec2 cardMax(pos.x + cardWidth, pos.y + cardHeight);

        if (!model.sharing)
        {
            ImGui::InvisibleButton("##transmitir", ImVec2(cardWidth, cardHeight));

            bool hovered = ImGui::IsItemHovered();

            if (ImGui::IsItemClicked())
            {
                actions.openShare = true;
            }

            if (hovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }

            ImVec4 accent = UiColorAccent();

            draw->AddRectFilled(
                pos, cardMax,
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(accent.x, accent.y, accent.z, hovered ? 0.16f : 0.08f)),
                rounding);

            draw->AddRect(
                pos, cardMax,
                ImGui::ColorConvertFloat4ToU32(
                    ImVec4(accent.x, accent.y, accent.z, hovered ? 0.9f : 0.5f)),
                rounding, 0, 1.0f * UiScale());

            float icon = (std::min)(cardHeight * 0.22f, 44.0f * UiScale());

            const char* label = "Transmitir minha tela";

            ImVec2 labelSize = ImGui::CalcTextSize(label);

            float blockTop =
                pos.y + (cardHeight - icon - labelSize.y - 10.0f * UiScale()) * 0.5f;

            ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y));

            UiIconAt(
                UiIcon::Screen,
                ImVec2(pos.x + cardWidth * 0.5f, blockTop + icon * 0.5f),
                icon,
                ImGui::GetColorU32(accent));

            draw->AddText(
                ImVec2(pos.x + (cardWidth - labelSize.x) * 0.5f,
                       blockTop + icon + 10.0f * UiScale()),
                ImGui::GetColorU32(accent),
                label);
        }
        else
        {
            draw->AddRectFilled(
                pos, cardMax,
                ImGui::GetColorU32(UiColorSurface()), rounding);

            draw->AddRect(pos, cardMax, border, rounding, 0, 1.0f * UiScale());

            float pad = 14.0f * UiScale();

            float inner = cardWidth - pad * 2.0f;

            ImGui::SetCursorScreenPos(ImVec2(pos.x + pad, pos.y + pad));

            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + inner);

            UiWrappedColored(UiColorSuccess(), "Você está transmitindo");

            std::string detail =
                model.sharingLabel + "  ·  " +
                std::to_string(model.sharingViewers) + " assistindo";

            UiMutedText(detail.c_str());

            ImGui::PopTextWrapPos();

            ImGui::SetCursorScreenPos(
                ImVec2(pos.x + pad,
                       cardMax.y - pad - ImGui::GetFrameHeight()));

            float half = (inner - style.ItemSpacing.x) * 0.5f;

            if (ImGui::Button("Opções##opcoestransm", ImVec2(half, 0.0f)))
            {
                actions.toggleShareOptions = true;
            }

            ImGui::SameLine();

            ImGui::PushStyleColor(ImGuiCol_Text, UiColorError());

            if (ImGui::Button("Parar##parartransm", ImVec2(half, 0.0f)))
            {
                actions.stopShare = true;
            }

            ImGui::PopStyleColor();

            ImGui::SetCursorScreenPos(pos);

            ImGui::Dummy(ImVec2(cardWidth, cardHeight));
        }

        ImGui::EndGroup();
    }

    ImGui::Spacing();

    if (emptyState)
    {
        const char* hint =
            "Ninguém está transmitindo ainda. Quando alguém começar, aparece aqui.";

        float width = ImGui::CalcTextSize(hint).x;

        if (width < avail)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - width) * 0.5f);
        }

        UiMutedText(hint);
    }

    if (model.connecting)
    {
        UiSpinner(8.0f * UiScale());

        ImGui::SameLine();

        UiMutedText(
            ("Conectando a " + model.connectingTo +
             UiBusyDots()).c_str());
    }

    if (!model.notice.empty())
    {
        UiMutedText(model.notice.c_str());
    }

    if (!model.error.empty())
    {
        UiWrappedColored(UiColorError(), model.error.c_str());
    }

    ImGui::End();

    ImGui::PopStyleColor();

    ImGui::PopStyleVar(3);
}


void DrawRoomWaitingScreen(
    const std::string& presenterName,
    const std::string& status,
    const std::string& versionWarning,
    bool& outBackToRoom)
{
    UiBeginCenteredPanel("##salaaguardando", 440.0f);

    UiAvatar(
        presenterName.c_str(),
        ImGui::GetFrameHeight() * 1.6f,
        true);

    ImGui::SameLine();

    ImGui::BeginGroup();

    UiTitle(presenterName.c_str());

    UiMutedText("está transmitindo");

    ImGui::EndGroup();

    ImGui::Spacing();

    UiSpinner(10.0f * UiScale());

    ImGui::SameLine();

    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + 2.0f * UiScale());

    UiMutedText(
        (status.empty()
            ? std::string("Aguardando a imagem chegar")
            : status).c_str());

    if (!versionWarning.empty())
    {
        UiWrappedColored(
            UiColorError(),
            versionWarning.c_str());
    }

    ImGui::Spacing();

    if (ImGui::Button(
            "Voltar para a sala",
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
    {
        outBackToRoom = true;
    }

    UiEndPanel();
}


// ============================================================
// Conectado, esperando o primeiro frame
// ============================================================

void DrawWaitingScreen(
    const std::string& reply,
    const std::string& versionWarning,
    bool hostFull)
{
    UiBeginCenteredPanel("##aguardando", 440.0f);

    if (hostFull)
    {
        UiTitle("Transmissão cheia");

        UiWrappedColored(
            UiColorError(),
            "Já tem 8 pessoas assistindo, o máximo por "
            "transmissão.");

        UiMutedText(
            (std::string("Esperando uma vaga") +
             UiBusyDots()).c_str());

        UiMutedText(
            "Você entra sozinho assim que alguém sair. Pode deixar "
            "esta tela aberta.");
    }
    else
    {
        UiTitle("Conectando");

        UiMutedText(
            (std::string("Aguardando a transmissão começar") +
             UiBusyDots()).c_str()
        );

        UiMutedText(
            "Se demorar, confira se quem transmite já escolheu "
            "a tela que vai compartilhar."
        );
    }

    if (!versionWarning.empty())
    {
        UiWrappedColored(
            UiColorError(),
            versionWarning.c_str()
        );
    }

    // Convite manual: a resposta que precisa voltar pra
    // quem transmite (so entre redes diferentes).
    if (!reply.empty())
    {
        ImGui::Separator();

        ImGui::PushTextWrapPos(0.0f);

        ImGui::TextUnformatted(
            "Se vocês estão em redes diferentes, envie esta "
            "resposta para quem transmite:"
        );

        ImGui::PopTextWrapPos();

        UiWrappedColored(
            UiColorSuccess(),
            reply.c_str()
        );

        if (ImGui::Button(
                "Copiar resposta",
                ImVec2(
                    ImGui::GetContentRegionAvail().x,
                    0.0f)))
        {
            ImGui::SetClipboardText(reply.c_str());
        }
    }

    UiEndPanel();
}


// ============================================================
// Painel sobre o video
// ============================================================

namespace
{

// Comeca uma janela sem moldura encostada no canto superior
// direito, com a transparencia dada.
void BeginTopRightWindow(
    const char* id,
    float alpha,
    float topOffset = 0.0f,
    float rightOffset = 0.0f)
{
    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    float margin = 16.0f * UiScale();

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x - margin -
                rightOffset,
            viewport->WorkPos.y + margin + topOffset),
        ImGuiCond_Always,
        ImVec2(1.0f, 0.0f)
    );

    ImGui::SetNextWindowBgAlpha(0.93f);

    ImGui::PushStyleVar(
        ImGuiStyleVar_Alpha,
        alpha
    );

    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(14.0f * UiScale(), 10.0f * UiScale())
    );

    ImGui::Begin(
        id,
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoFocusOnAppearing
    );
}


bool EndTopRightWindow()
{
    bool hovered =
        ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows) ||
        ImGui::IsAnyItemActive();

    ImGui::End();

    ImGui::PopStyleVar(2);

    return hovered;
}


// Dois botoes lado a lado que funcionam como uma escolha
// (o escolhido fica destacado).
bool ToggleButton(
    const char* label,
    bool selected,
    float width)
{
    if (selected)
    {
        const ImVec4 accent = UiColorAccent();

        ImGui::PushStyleColor(ImGuiCol_Button, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, accent);
        ImGui::PushStyleColor(ImGuiCol_Text, UiColorOnAccent());
    }

    bool clicked =
        ImGui::Button(
            label,
            ImVec2(width, 0.0f));

    if (selected)
    {
        ImGui::PopStyleColor(4);
    }

    return clicked;
}

} // namespace


// Corta o texto com "..." pra caber na largura (sem quebrar um
// caractere UTF-8 no meio).
std::string FitText(
    const std::string& text,
    float maxWidth)
{
    if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth)
        return text;

    std::string cut = text;

    while (!cut.empty())
    {
        do
        {
            cut.pop_back();
        }
        while (!cut.empty() &&
               (static_cast<unsigned char>(cut.back()) & 0xC0) ==
                   0x80);

        std::string candidate = cut + "...";

        if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth)
            return candidate;
    }

    return "...";
}


bool DrawStreamOverlay(
    StreamOverlayModel& model,
    StreamOverlayActions& actions)
{
    // Ao lado do painel de pessoas so se couber; senao, embaixo.
    float sideOffset = 0.0f;
    float topOffset = model.topOffset;

    if (model.besidePeoplePanel)
    {
        float needed =
            (PEOPLE_PANEL_WIDTH + 8.0f + 330.0f + 32.0f) * UiScale();

        if (ImGui::GetMainViewport()->WorkSize.x >= needed)
        {
            sideOffset = (PEOPLE_PANEL_WIDTH + 8.0f) * UiScale();
        }
        else
        {
            topOffset += model.peoplePanelHeight;
        }
    }

    if (model.alpha <= 0.0f)
        return false;

    bool& collapsed = *model.collapsed;


    // --------------------------------------------------------
    // Painel fechado: so um botao pequeno pra reabrir
    // --------------------------------------------------------

    if (collapsed)
    {
        BeginTopRightWindow(
            "##opcoesfechado", model.alpha, topOffset, sideOffset);

        if (ImGui::Button("Opções"))
        {
            collapsed = false;
        }

        return EndTopRightWindow();
    }


    BeginTopRightWindow(
        "##sobrevideo", model.alpha, topOffset, sideOffset);

    const ImGuiStyle& style = ImGui::GetStyle();

    float rowWidth = 300.0f * UiScale();


    // --------------------------------------------------------
    // Cabecalho com o botao de fechar
    // --------------------------------------------------------

    ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

    // Alinha o texto com o centro do botao de fechar.
    ImGui::AlignTextToFramePadding();

    ImGui::TextUnformatted(
        model.inRoom && !model.presenterName.empty()
            ? model.presenterName.c_str()
            : "Opções");

    ImGui::PopStyleColor();

    float closeSize =
        ImGui::GetFrameHeight();

    ImGui::SameLine(
        rowWidth - closeSize + style.WindowPadding.x
    );

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    if (ImGui::Button(
            "×##fechar",
            ImVec2(closeSize, closeSize)))
    {
        collapsed = true;
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Fechar (as teclas continuam funcionando)");
    }


    // --------------------------------------------------------
    // Volume
    // --------------------------------------------------------

    bool& muted = *model.muted;

    float& volume = *model.volume;

    float muteWidth =
        ImGui::CalcTextSize("Som desligado").x +
        style.FramePadding.x * 2.0f;

    if (muted)
    {
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            UiColorError());
    }

    if (ImGui::Button(
            muted ? "Som desligado" : "Som ligado",
            ImVec2(muteWidth, 0.0f)))
    {
        muted = !muted;
    }

    if (muted)
    {
        ImGui::PopStyleColor();
    }

    ImGui::SameLine();

    ImGui::SetNextItemWidth(
        rowWidth - muteWidth - style.ItemSpacing.x);

    int percent =
        static_cast<int>(volume * 100.0f + 0.5f);

    ImGui::BeginDisabled(muted);

    if (ImGui::SliderInt(
            "##volume",
            &percent,
            0,
            100,
            "%d%%"))
    {
        volume = percent / 100.0f;
    }

    ImGui::EndDisabled();


    // --------------------------------------------------------
    // Exibicao
    // --------------------------------------------------------

    bool& fill = *model.fillWindow;

    float half =
        (rowWidth - style.ItemSpacing.x) * 0.5f;

    if (ToggleButton("Imagem inteira", !fill, half))
    {
        fill = false;
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Mostra tudo; pode sobrar borda.");
    }

    ImGui::SameLine();

    if (ToggleButton("Preencher", fill, half))
    {
        fill = true;
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Sem bordas; corta um pouco das beiradas.");
    }

    if (ImGui::Button(
            model.fullscreen
                ? "Sair da tela cheia"
                : "Tela cheia",
            ImVec2(rowWidth, 0.0f)))
    {
        actions.toggleFullscreen = true;
    }

    // Na sala: chat, trocar de tela e voltar pra lista
    if (model.inRoom)
    {
        // (Pessoas, chat e microfone ficam na barra da sala,
        // acima deste painel.)
        // (configuracoes: engrenagem na barra da sala)
        if (ImGui::Button(
                "Voltar à sala",
                ImVec2(rowWidth, 0.0f)))
        {
            actions.backToRoom = true;
        }

        if (!model.otherPresenters.empty())
        {
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

            ImGui::TextUnformatted("Trocar de tela");

            ImGui::PopStyleColor();

            for (size_t i = 0;
                 i < model.otherPresenters.size();
                 ++i)
            {
                ImGui::PushID(static_cast<int>(i));

                const std::string& full =
                    model.otherPresenters[i];

                std::string label =
                    FitText(
                        full,
                        rowWidth - style.FramePadding.x * 2.0f);

                if (ImGui::Button(
                        (label + "##troca").c_str(),
                        ImVec2(rowWidth, 0.0f)))
                {
                    actions.switchTo = static_cast<int>(i);
                }

                if (label != full &&
                    ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", full.c_str());
                }

                ImGui::PopID();
            }
        }
    }


    // --------------------------------------------------------
    // Estatisticas e atalhos
    // --------------------------------------------------------

    std::string stats =
        std::to_string(model.width) +
        " x " +
        std::to_string(model.height) +
        "  ·  " +
        std::to_string(model.fps) +
        " FPS  ·  perdidos: " +
        std::to_string(model.lostNetwork);

    if (model.recovered > 0)
    {
        stats +=
            "  ·  recuperados: " +
            std::to_string(model.recovered);
    }

    if (model.late > 0)
    {
        stats +=
            "  ·  atrasados: " +
            std::to_string(model.late);
    }

    ImGui::PushStyleColor(
        ImGuiCol_Text,
        UiColorMuted());

    ImGui::TextUnformatted(stats.c_str());

    ImGui::TextUnformatted(
        model.keyHint.empty()
        ? "setas: volume"
        : model.keyHint.c_str());

    ImGui::PopStyleColor();

    return EndTopRightWindow();
}


// ============================================================
// Painel "Pessoas" (sobre o video ou a transmissao)
// ============================================================

// ============================================================
// Sala sobre o video: barra de icones e painel lateral
// ============================================================

namespace
{

// Lista de pessoas (rola se nao couber).
void SidePeopleList(
    const PeoplePanelModel& model,
    PeoplePanelActions& actions,
    float height)
{
    ImGui::BeginChild("##listapessoas", ImVec2(0.0f, height), false);

    // Folga pros aneis da foto (falando/transmitindo) nao
    // ficarem cortados na borda.
    float ring = 6.0f * UiScale();

    ImGui::Dummy(ImVec2(0.0f, ring * 0.5f));

    ImGui::Indent(ring);

    for (size_t i = 0; i < model.people.size(); ++i)
    {
        const PeopleEntry& person = model.people[i];

        ImGui::PushID(static_cast<int>(i));

        float newVolume = person.volume;

        // A sua linha: fundo leve.
        if (person.isMe)
        {
            ImVec2 rowPos = ImGui::GetCursorScreenPos();

            float rowHeight = ImGui::GetFrameHeight() * 1.15f;

            float pad = 5.0f * UiScale();

            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(rowPos.x - pad, rowPos.y - pad),
                ImVec2(rowPos.x + ImGui::GetContentRegionAvail().x - 2.0f, rowPos.y + rowHeight + pad),
                UiIsLightTheme()
                ? ImGui::GetColorU32(ImGuiCol_Button)
                : ImGui::GetColorU32(ImGuiCol_PopupBg),
                8.0f * UiScale());
        }

        if (MemberRow(
                person.isMe ? person.name + " (você)" : person.name,
                person.live,
                person.voice,
                person.isMe,
                model.voiceJoined,
                person.volume,
                newVolume,
                person.speakAmount,
                person.avatar))
        {
            actions.volumeMember = static_cast<int>(i);
            actions.volumeValue = newVolume;
        }

        ImGui::PopID();
    }

    ImGui::Unindent(ring);

    ImGui::EndChild();
}


// Rodape do painel: entrar / silenciar / sair da voz.
void SideVoiceControls(
    const PeoplePanelModel& model,
    PeoplePanelActions& actions,
    bool showGear,
    bool& outOpenSettings)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    // Engrenagem a direita da linha de botoes (lista da sala).
    float gear = showGear ? ImGui::GetFrameHeight() + style.ItemSpacing.x : 0.0f;

    auto gearButton =
        [&]()
        {
            if (!showGear)
                return;

            ImGui::SameLine();

            if (GearButton("##painel_config", "Configurações de voz e atalhos"))
            {
                outOpenSettings = true;
            }
        };

    if (model.voiceJoining)
    {
        UiSpinner(7.0f * UiScale());

        ImGui::SameLine();

        UiMutedText((std::string("Entrando na voz") + UiBusyDots()).c_str());
    }
    else if (!model.voiceJoined)
    {
        if (UiIconLabelButton(
                "Entrar na voz",
                UiIcon::Mic,
                ImVec2(ImGui::GetContentRegionAvail().x - gear, 0.0f),
                UiColorSuccess()))
        {
            actions.voiceJoin = true;
        }

        gearButton();
    }
    else
    {
        float half =
            (ImGui::GetContentRegionAvail().x - gear -
             style.ItemSpacing.x) * 0.5f;

        ImVec4 micColor =
            model.voiceMuted
            ? UiColorError()
            : ImGui::GetStyleColorVec4(ImGuiCol_Text);

        ImGui::PushStyleColor(ImGuiCol_Text, micColor);

        if (ImGui::Button(
                model.voiceMuted
                ? "      Ligar mic.##micp"
                : "      Silenciar##micp",
                ImVec2(half, 0.0f)))
        {
            actions.toggleMute = true;
        }

        ImGui::PopStyleColor();

        ImVec2 buttonMin = ImGui::GetItemRectMin();

        UiMicIconAt(
            ImVec2(
                buttonMin.x + style.FramePadding.x * 1.5f,
                buttonMin.y + style.FramePadding.y),
            ImGui::GetTextLineHeight(),
            ImGui::GetColorU32(micColor),
            model.voiceMuted);

        ImGui::SameLine();

        if (ImGui::Button("Sair da voz", ImVec2(half, 0.0f)))
        {
            actions.voiceLeave = true;
        }

        gearButton();

        ImGui::PushStyleColor(
            ImGuiCol_PlotHistogram,
            model.voiceTransmitting ? UiColorSuccess() : UiColorMuted());

        ImGui::ProgressBar(
            (std::min)(1.0f, model.micLevel * 4.0f),
            ImVec2(-1.0f, 4.0f * UiScale()),
            "");

        ImGui::PopStyleColor();

        std::string hint;

        if (!model.pttKey.empty())
        {
            hint = "Segure " + model.pttKey + " para falar";
        }

        if (!model.muteKey.empty())
        {
            hint +=
                (hint.empty() ? "" : "  ·  ") +
                model.muteKey + ": microfone";
        }

        if (!hint.empty())
        {
            UiMutedText(hint.c_str());
        }
    }

    if (!model.voiceError.empty())
    {
        UiWrappedColored(UiColorError(), model.voiceError.c_str());
    }
}


// Aba do painel: texto com sublinhado na cor de destaque quando
// escolhida. badge > 0 = bolinha com numero.
bool SideTab(
    const char* label,
    bool selected,
    int badge)
{
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        selected
        ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
        : UiColorMuted());

    bool clicked = ImGui::Button(label);

    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);

    ImVec2 min = ImGui::GetItemRectMin();
    ImVec2 max = ImGui::GetItemRectMax();

    ImDrawList* draw = ImGui::GetWindowDrawList();

    if (selected)
    {
        float pad = ImGui::GetStyle().FramePadding.x;

        draw->AddRectFilled(
            ImVec2(min.x + pad, max.y - 2.0f * UiScale()),
            ImVec2(max.x - pad, max.y),
            ImGui::GetColorU32(UiColorAccent()),
            1.0f * UiScale());
    }

    if (badge > 0)
    {
        float r = ImGui::GetTextLineHeight() * 0.3f;

        draw->AddCircleFilled(
            ImVec2(max.x - r * 0.6f, min.y + r * 1.2f),
            r,
            ImGui::GetColorU32(UiColorError()),
            12);
    }

    return clicked;
}


// Separador vertical entre grupos de icones da barra.
void BarDivider()
{
    float height = ImGui::GetFrameHeight();

    float gap = ImGui::GetStyle().ItemSpacing.x;

    ImGui::SameLine(0.0f, gap);

    ImVec2 pos = ImGui::GetCursorScreenPos();

    ImVec4 color = UiColorMuted();

    color.w *= 0.35f * ImGui::GetStyle().Alpha;

    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(pos.x, pos.y + height * 0.2f),
        ImVec2(pos.x, pos.y + height * 0.8f),
        ImGui::ColorConvertFloat4ToU32(color),
        1.0f * UiScale());

    ImGui::Dummy(ImVec2(1.0f, height));

    ImGui::SameLine(0.0f, gap);
}

} // namespace


namespace
{

// Largura do painel sem escala (300 = padrao).
float g_sidePanelWidth = 300.0f;

}


float RoomSidePanelSavedWidth()
{
    return g_sidePanelWidth;
}


void RoomSidePanelSetSavedWidth(
    float width)
{
    g_sidePanelWidth = (std::max)(260.0f, (std::min)(900.0f, width));
}


float RoomSidePanelWidth()
{
    float width = g_sidePanelWidth * UiScale();

    // Nunca mais que 60% da janela (o resto e do video/lista).
    float limit =
        (std::max)(260.0f * UiScale(), ImGui::GetIO().DisplaySize.x * 0.6f);

    return (std::max)(260.0f * UiScale(), (std::min)(width, limit));
}


void DrawRoomSidePanel(
    const RoomSidePanelModel& model,
    RoomSidePanelActions& actions)
{
    if (model.progress <= 0.001f)
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    const ImGuiStyle& style = ImGui::GetStyle();

    float width = RoomSidePanelWidth();

    // Entra deslizando da direita (e some junto).
    float eased =
        1.0f - (1.0f - model.progress) * (1.0f - model.progress);

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x - width * eased,
            viewport->WorkPos.y),
        ImGuiCond_Always);

    ImGui::SetNextWindowSize(
        ImVec2(width, viewport->WorkSize.y),
        ImGuiCond_Always);

    ImGui::SetNextWindowBgAlpha(1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, eased);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(14.0f * UiScale(), 10.0f * UiScale()));

    ImGui::PushStyleColor(ImGuiCol_Border, UiColorDivider());

    ImGui::Begin(
        "##painellateral",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoFocusOnAppearing |
        (model.progress < 0.5f ? ImGuiWindowFlags_NoInputs : 0));

    ImGui::PopStyleColor();

    // ---------------- Cabecalho: abas, fixar, fechar ----------

    std::string peopleLabel =
        "Pessoas (" + std::to_string(model.people.people.size()) +
        ")##abapessoas";

    if (SideTab(peopleLabel.c_str(), model.tab == 0, 0) &&
        model.tab != 0)
    {
        actions.setTab = 0;
    }

    ImGui::SameLine(0.0f, 2.0f * UiScale());

    if (SideTab(
            "Chat##abachat",
            model.tab == 1,
            model.tab == 1 ? 0 : model.unreadChat) &&
        model.tab != 1)
    {
        actions.setTab = 1;
    }

    float button = ImGui::GetFrameHeight();

    if (!model.docked)
    {
    ImGui::SameLine(
        ImGui::GetContentRegionMax().x -
        (model.showPin ? button * 2.0f + style.ItemSpacing.x : button));

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    if (model.showPin &&
        UiIconButton(
            "##fixar",
            model.pinned ? UiIcon::Pin : UiIcon::PinOff,
            model.pinned
            ? "Fixado: fica aberto ao clicar no vídeo"
            : "Fixar o painel (não fecha ao clicar no vídeo)",
            model.pinned))
    {
        actions.togglePin = true;
    }

    if (model.showPin)
    {
        ImGui::SameLine();
    }

    if (UiIconButton("##fecharpainel", UiIcon::Close, "Fechar"))
    {
        actions.close = true;
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    }

    ImGui::Separator();

    // ---------------- Corpo + rodape (voz) ---------------------
    //
    // A altura do rodape varia (entrar / silenciar + dicas):
    // usa a medida do frame anterior.

    ImGuiStorage* storage = ImGui::GetStateStorage();

    ImGuiID footerKey = ImGui::GetID("##rodape");

    float footer =
        storage->GetFloat(footerKey, ImGui::GetFrameHeight() * 2.5f);

    float body =
        (std::max)(
            ImGui::GetFrameHeight() * 3.0f,
            ImGui::GetContentRegionAvail().y - footer -
                style.ItemSpacing.y * 2.0f);

    if (model.tab == 0)
    {
        SidePeopleList(model.people, actions.people, body);
    }
    else
    {
        if (DrawChatBox(
                "painelchat",
                *model.chat,
                model.chatInput,
                model.chatInputSize,
                body,
                model.focusChat))
        {
            actions.sendChat = true;
        }
    }

    ImGui::Separator();

    float footerStart = ImGui::GetCursorPosY();

    SideVoiceControls(model.people, actions.people, model.showGear, actions.openVoiceSettings);

    if (ImGui::SmallButton("Atalhos"))
    {
        actions.people.openShortcuts = true;
    }

    storage->SetFloat(footerKey, ImGui::GetCursorPosY() - footerStart);

    // Borda esquerda: arrastar muda a largura (o video/lista do
    // lado se ajusta).
    if (model.progress >= 1.0f)
    {
        ImVec2 back = ImGui::GetCursorScreenPos();

        ImVec2 edge = ImGui::GetWindowPos();

        float grip = 6.0f * UiScale();

        ImGui::SetCursorScreenPos(edge);

        ImGui::InvisibleButton("##largura", ImVec2(grip, ImGui::GetWindowHeight()));

        bool active = ImGui::IsItemActive();

        if (ImGui::IsItemHovered() || active)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);

            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(edge.x + 1.0f, edge.y),
                ImVec2(edge.x + 1.0f, edge.y + ImGui::GetWindowHeight()),
                ImGui::GetColorU32(UiColorAccent()),
                2.0f * UiScale());
        }

        if (active && ImGui::GetIO().MouseDelta.x != 0.0f)
        {
            // Parte do que ja esta na tela (pode estar limitada).
            float current = RoomSidePanelWidth() / UiScale();

            RoomSidePanelSetSavedWidth(
                current - ImGui::GetIO().MouseDelta.x / UiScale());
        }

        if (ImGui::IsItemDeactivated())
        {
            actions.widthChanged = true;
        }

        ImGui::SetCursorScreenPos(back);

        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    actions.hovered =
        ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows |
            ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    ImGui::End();

    ImGui::PopStyleVar(3);
}


void DrawRoomBar(
    RoomBarModel& model,
    RoomBarActions& actions)
{
    if (model.alpha <= 0.001f)
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();

    const ImGuiStyle& style = ImGui::GetStyle();

    float margin = 16.0f * UiScale();

    // Centro da area livre (o painel lateral ocupa a direita).
    float centerX =
        viewport->WorkPos.x +
        (viewport->WorkSize.x - model.rightInset) * 0.5f;

    ImGui::SetNextWindowPos(
        ImVec2(centerX, viewport->WorkPos.y + viewport->WorkSize.y - margin),
        ImGuiCond_Always,
        ImVec2(0.5f, 1.0f));

    ImGui::SetNextWindowBgAlpha(0.92f);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, model.alpha);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(10.0f * UiScale(), 8.0f * UiScale()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * UiScale());

    ImGui::PushStyleColor(ImGuiCol_Border, UiColorDivider());

    ImGui::Begin(
        "##barraicones",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoFocusOnAppearing |
        (model.alpha < 0.5f ? ImGuiWindowFlags_NoInputs : 0));

    ImGui::PopStyleColor();

    {
        ImVec2 pos = ImGui::GetWindowPos();
        ImVec2 size = ImGui::GetWindowSize();

        UiDrawShadow(pos, ImVec2(pos.x + size.x, pos.y + size.y), 12.0f * UiScale());
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);


    // Botao de popover: clicar de novo com ele aberto fecha (o
    // clique ja fechou o popover ao apertar - nao reabre ao
    // soltar).
    ImGuiStorage* barStorage = ImGui::GetStateStorage();

    auto popoverButton =
        [&](bool clicked, const char* popupId)
        {
            ImGuiID wasKey = ImGui::GetID((std::string(popupId) + "#aberto").c_str());
            ImGuiID skipKey = ImGui::GetID((std::string(popupId) + "#pular").c_str());

            if (ImGui::IsItemActivated() && barStorage->GetBool(wasKey))
            {
                barStorage->SetBool(skipKey, true);
            }

            if (!clicked)
                return;

            if (barStorage->GetBool(skipKey))
            {
                barStorage->SetBool(skipKey, false);

                return;
            }

            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();

            barStorage->SetFloat(ImGui::GetID("##ancorax"), (min.x + max.x) * 0.5f);
            barStorage->SetFloat(ImGui::GetID("##ancoray"), min.y);

            ImGui::OpenPopup(popupId);
        };

    // ---------------- Voz e som ----------------

    std::string micTip;

    if (model.voiceJoining)
    {
        micTip = "Entrando na voz...";
    }
    else if (!model.voiceJoined)
    {
        micTip = "Entrar na voz";
    }
    else
    {
        micTip = model.voiceMuted ? "Ligar o microfone" : "Silenciar o microfone";

        if (!model.muteKey.empty())
        {
            micTip += " (" + model.muteKey + ")";
        }
    }

    ImGui::BeginDisabled(model.voiceJoining);

    if (UiIconButton(
            "##barmic",
            model.voiceJoined && !model.voiceMuted ? UiIcon::Mic : UiIcon::MicOff,
            micTip.c_str()))
    {
        if (model.voiceJoined)
        {
            actions.toggleMic = true;
        }
        else
        {
            actions.joinVoice = true;
        }
    }

    ImGui::EndDisabled();

    if (model.watching)
    {
        ImGui::SameLine();

        std::string soundTip =
            *model.muted ? "Ligar o som da transmissão" : "Desligar o som da transmissão";

        if (!model.soundKey.empty())
        {
            soundTip += " (" + model.soundKey + ")";
        }

        if (UiIconButton(
                "##barsom",
                *model.muted ? UiIcon::VolumeOff : UiIcon::Volume,
                soundTip.c_str()))
        {
            *model.muted = !*model.muted;
        }
    }

    // ---------------- Pessoas, chat, trocar ----------------

    BarDivider();

    if (UiIconButton(
            "##barpessoas",
            UiIcon::People,
            model.peopleKey.empty()
            ? "Pessoas"
            : ("Pessoas (" + model.peopleKey + ")").c_str(),
            model.peopleOpen))
    {
        actions.togglePeople = true;
    }

    ImGui::SameLine();

    if (UiIconButton(
            "##barchat",
            UiIcon::Chat,
            model.chatKey.empty()
            ? "Chat"
            : ("Chat (" + model.chatKey + ")").c_str(),
            model.chatOpen,
            false,
            model.chatOpen ? 0 : model.unreadChat))
    {
        actions.toggleChat = true;
    }

    if (model.watching && !model.otherPresenters.empty())
    {
        ImGui::SameLine();

        popoverButton(
            UiIconButton("##bartrocar", UiIcon::Switch, "Trocar de tela"),
            "##trocartela");
    }

    // ---------------- Video, config., sair ----------------

    BarDivider();

    if (model.watching)
    {
        popoverButton(
            UiIconButton("##baropcoes", UiIcon::Sliders, "Opções do vídeo"),
            "##opcoesvideo");

        ImGui::SameLine();
    }

    if (!model.watching && model.sharingLive)
    {
        std::string liveTip =
            "Transmissão  ·  " + std::to_string(model.viewers) +
            (model.viewers == 1 ? " pessoa assistindo" : " pessoas assistindo");

        if (UiIconButton(
                "##barvivo",
                UiIcon::Screen,
                liveTip.c_str(),
                model.livePanelOpen))
        {
            actions.toggleLivePanel = true;
        }

        ImGui::SameLine();
    }

    if (GearButton("##barconfig", "Configurações"))
    {
        actions.openSettings = true;
    }

    if (!model.watching && model.sharingLive)
    {
        ImGui::SameLine();

        if (UiIconButton(
                "##barparar",
                UiIcon::Stop,
                "Parar de transmitir",
                false,
                true))
        {
            actions.stopSharing = true;
        }

        ImGui::SameLine();

        if (UiIconButton("##barvoltarsala", UiIcon::Exit, "Voltar à sala"))
        {
            actions.backToRoom = true;
        }
    }

    if (model.watching)
    {
        ImGui::SameLine();

        std::string fullTip =
            model.fullscreen ? "Sair da tela cheia" : "Tela cheia";

        if (!model.fullscreenKey.empty())
        {
            fullTip += " (" + model.fullscreenKey + ")";
        }

        if (UiIconButton(
                "##bartelacheia",
                model.fullscreen ? UiIcon::ExitFullscreen : UiIcon::Fullscreen,
                fullTip.c_str()))
        {
            actions.toggleFullscreen = true;
        }

        ImGui::SameLine();

        if (UiIconButton(
                "##barvoltar",
                UiIcon::Exit,
                "Voltar à sala",
                false,
                true))
        {
            actions.backToRoom = true;
        }
    }

    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // ---------------- Popovers ----------------

    ImGuiStorage* storage = ImGui::GetStateStorage();

    ImVec2 anchor(
        storage->GetFloat(ImGui::GetID("##ancorax"), centerX),
        storage->GetFloat(ImGui::GetID("##ancoray"), viewport->WorkSize.y));

    float popupWidth = 300.0f * UiScale();

    // Nao deixa passar da borda da janela.
    float half = popupWidth * 0.5f + style.WindowPadding.x;

    anchor.x =
        (std::max)(
            viewport->WorkPos.x + margin + half,
            (std::min)(
                anchor.x,
                viewport->WorkPos.x + viewport->WorkSize.x - margin - half));

    ImVec2 popupPos(anchor.x, anchor.y - 18.0f * UiScale());

    bool anyPopup = false;

    ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always, ImVec2(0.5f, 1.0f));

    if (ImGui::BeginPopup("##trocartela"))
    {
        anyPopup = true;

        UiMutedText("Trocar de tela");

        for (size_t i = 0; i < model.otherPresenters.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));

            const std::string& full = model.otherPresenters[i];

            std::string label =
                FitText(full, popupWidth - style.FramePadding.x * 2.0f);

            if (ImGui::Button((label + "##troca").c_str(), ImVec2(popupWidth, 0.0f)))
            {
                actions.switchTo = static_cast<int>(i);

                ImGui::CloseCurrentPopup();
            }

            if (label != full && ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", full.c_str());
            }

            ImGui::PopID();
        }

        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always, ImVec2(0.5f, 1.0f));

    if (ImGui::BeginPopup("##opcoesvideo"))
    {
        anyPopup = true;

        if (!model.presenterName.empty())
        {
            UiMutedText(model.presenterName.c_str());
        }

        // Volume
        bool& muted = *model.muted;
        float& volume = *model.volume;

        if (UiIconButton(
                "##popsom",
                muted ? UiIcon::VolumeOff : UiIcon::Volume,
                muted ? "Ligar o som" : "Desligar o som"))
        {
            muted = !muted;
        }

        ImGui::SameLine();

        ImGui::SetNextItemWidth(
            popupWidth - ImGui::GetFrameHeight() - style.ItemSpacing.x);

        int percent = static_cast<int>(volume * 100.0f + 0.5f);

        ImGui::BeginDisabled(muted);

        if (ImGui::SliderInt("##volume", &percent, 0, 100, "%d%%"))
        {
            volume = percent / 100.0f;
        }

        ImGui::EndDisabled();

        // Exibicao
        bool& fill = *model.fillWindow;

        float halfWidth = (popupWidth - style.ItemSpacing.x) * 0.5f;

        if (ToggleButton("Imagem inteira", !fill, halfWidth))
        {
            fill = false;
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Mostra tudo; pode sobrar borda.");
        }

        ImGui::SameLine();

        if (ToggleButton("Preencher", fill, halfWidth))
        {
            fill = true;
        }

        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Sem bordas; corta um pouco das beiradas.");
        }

        // Estatisticas e atalhos
        std::string stats =
            std::to_string(model.width) + " x " +
            std::to_string(model.height) + "  ·  " +
            std::to_string(model.fps) + " FPS  ·  perdidos: " +
            std::to_string(model.lostNetwork);

        if (model.recovered > 0)
        {
            stats += "  ·  recuperados: " + std::to_string(model.recovered);
        }

        if (model.late > 0)
        {
            stats += "  ·  atrasados: " + std::to_string(model.late);
        }

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + popupWidth);

        ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

        ImGui::TextUnformatted(stats.c_str());

        if (!model.keyHint.empty())
        {
            ImGui::TextUnformatted(model.keyHint.c_str());
        }

        ImGui::PopStyleColor();

        ImGui::PopTextWrapPos();

        ImGui::EndPopup();
    }

    actions.popupOpen = anyPopup;

    barStorage->SetBool(
        ImGui::GetID("##trocartela#aberto"), ImGui::IsPopupOpen("##trocartela"));
    barStorage->SetBool(
        ImGui::GetID("##opcoesvideo#aberto"), ImGui::IsPopupOpen("##opcoesvideo"));

    actions.centerX = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() * 0.5f;
    actions.top = ImGui::GetWindowPos().y;

    actions.hovered =
        anyPopup ||
        ImGui::IsWindowHovered(
            ImGuiHoveredFlags_ChildWindows |
            ImGuiHoveredFlags_AllowWhenBlockedByPopup);

    ImGui::End();

    ImGui::PopStyleVar(3);
}


// ============================================================
// Tela de atalhos
// ============================================================

// Aba Atalhos.
void DrawShortcutsTab(
    const SettingsModel& model,
    SettingsActions& actions)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    UiMutedText(
        "Clique na tecla de um atalho e aperte a nova (vale "
        "botão lateral do mouse). Esc cancela, Backspace remove.");

    ImGui::Spacing();

    float keyWidth = 200.0f * UiScale();

    float clearWidth = ImGui::GetFrameHeight();

    for (size_t i = 0; i < model.rows.size(); ++i)
    {
        const ShortcutRow& row = model.rows[i];

        ImGui::PushID(static_cast<int>(i));

        ImGui::Separator();

        float rowTop = ImGui::GetCursorPosY();

        float textWidth =
            ImGui::GetContentRegionAvail().x -
            keyWidth - clearWidth - style.ItemSpacing.x * 3.0f;

        ImGui::BeginGroup();

        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);

        ImGui::TextUnformatted(row.label.c_str());

        ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

        ImGui::TextWrapped("%s", row.help.c_str());

        ImGui::PopStyleColor();

        if (row.global)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, UiColorAccent());

            ImGui::TextWrapped("Funciona com o jogo aberto.");

            ImGui::PopStyleColor();
        }

        ImGui::PopTextWrapPos();

        ImGui::EndGroup();

        float rowBottom = ImGui::GetCursorPosY();

        // Botao da tecla, centralizado na altura da linha
        float buttonY =
            rowTop +
            ((rowBottom - rowTop) - style.ItemSpacing.y -
             ImGui::GetFrameHeight()) * 0.5f;

        ImGui::SameLine(
            ImGui::GetContentRegionMax().x - keyWidth -
            clearWidth - style.ItemSpacing.x);

        ImGui::SetCursorPosY(buttonY);

        bool capturing = model.capturing == static_cast<int>(i);

        if (capturing)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, UiColorAccent());

            if (ImGui::Button(
                    "Aperte uma tecla...##tecla",
                    ImVec2(keyWidth, 0.0f)))
            {
                actions.cancelCapture = true;
            }

            ImGui::PopStyleColor();
        }
        else
        {
            if (!row.hasKey)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());
            }

            if (ImGui::Button(
                    (row.keyName + "##tecla").c_str(),
                    ImVec2(keyWidth, 0.0f)))
            {
                actions.capture = static_cast<int>(i);
            }

            if (!row.hasKey)
            {
                ImGui::PopStyleColor();
            }
        }

        ImGui::SameLine();

        ImGui::SetCursorPosY(buttonY);

        ImGui::BeginDisabled(!row.hasKey || capturing);

        if (ImGui::Button("×##limpar", ImVec2(clearWidth, 0.0f)))
        {
            actions.clear = static_cast<int>(i);
        }

        ImGui::EndDisabled();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
            row.hasKey)
        {
            ImGui::SetTooltip("Remover este atalho");
        }

        ImGui::SetCursorPosY(rowBottom);

        ImGui::PopID();
    }

    ImGui::Separator();

    UiMutedText(
        "Fixos: setas para cima/baixo mudam o volume da "
        "transmissão; duplo clique no vídeo é tela cheia.");

    if (ImGui::Button("Restaurar padrões"))
    {
        actions.restoreDefaults = true;
    }
}


// Aba Geral: aparencia e foto.
void DrawGeneralSettingsTab(
    const SettingsModel& model,
    SettingsActions& actions)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    ImGui::Spacing();

    // Aparencia
    if (model.lightTheme)
    {
        UiFieldLabel("Aparência");

        float half =
            (ImGui::GetContentRegionAvail().x - style.ItemSpacing.x) * 0.5f;

        auto choice =
            [&](const char* label, bool selected)
            {
                if (selected)
                {
                    ImVec4 accent = UiColorAccent();

                    ImGui::PushStyleColor(ImGuiCol_Button, accent);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, accent);
                    ImGui::PushStyleColor(ImGuiCol_Text, UiColorOnAccent());
                }

                bool clicked = ImGui::Button(label, ImVec2(half, 0.0f));

                if (selected)
                {
                    ImGui::PopStyleColor(3);
                }

                return clicked && !selected;
            };

        if (choice("Escuro", !*model.lightTheme))
        {
            *model.lightTheme = false;
            actions.themeChanged = true;
        }

        ImGui::SameLine();

        if (choice("Claro", *model.lightTheme))
        {
            *model.lightTheme = true;
            actions.themeChanged = true;
        }

        ImGui::Spacing();
    }

    // Foto de perfil
    UiFieldLabel("Sua foto");

    float size = ImGui::GetFrameHeight() * 2.2f;

    UiAvatar(
        model.myName.empty() ? "?" : model.myName.c_str(),
        size,
        false,
        0.0f,
        model.myAvatar);

    ImGui::SameLine();

    ImGui::BeginGroup();

    float buttonsY =
        ImGui::GetCursorPosY() +
        (size - ImGui::GetFrameHeight()) * 0.5f;

    ImGui::SetCursorPosY(buttonsY);

    if (ImGui::Button("Escolher imagem..."))
    {
        actions.chooseAvatar = true;
    }

    if (model.myAvatar)
    {
        ImGui::SameLine();

        ImGui::SetCursorPosY(buttonsY);

        if (ImGui::Button("Remover"))
        {
            actions.removeAvatar = true;
        }
    }

    ImGui::EndGroup();

    if (!model.avatarError.empty())
    {
        UiWrappedColored(UiColorError(), model.avatarError.c_str());
    }

    UiMutedText(
        "Aparece para quem está na sala com você. Vai "
        "criptografada, como o chat - o servidor não vê a imagem.");

    // Servidor de sinalizacao
    if (model.serverInput)
    {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        UiFieldLabel("Servidor de sinalização");

        UiMutedText(
            "Só apresenta as pessoas umas às outras: tela, som e voz "
            "vão direto entre vocês, e nomes e chat chegam "
            "criptografados. Você pode usar o seu próprio (veja "
            "HOSPEDAR.md no GitHub do projeto).");

        ImGui::Spacing();

        float testWidth =
            ImGui::CalcTextSize("Testar").x + style.FramePadding.x * 2.0f;

        float useWidth =
            ImGui::CalcTextSize("Usar").x + style.FramePadding.x * 2.0f;

        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x - testWidth - useWidth -
            style.ItemSpacing.x * 2.0f);

        bool enter =
            ImGui::InputTextWithHint(
                "##servidor",
                "https://meu-servidor.com  ou  http://192.168.0.10:8080",
                model.serverInput,
                model.serverInputSize,
                ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::SameLine();

        ImGui::BeginDisabled(model.serverTesting);

        if (ImGui::Button("Testar", ImVec2(testWidth, 0.0f)))
        {
            actions.serverTest = true;
        }

        ImGui::EndDisabled();

        ImGui::SameLine();

        if (UiPrimaryButtonSized("Usar", ImVec2(useWidth, 0.0f)) || enter)
        {
            actions.serverApply = true;
        }

        if (model.serverTesting)
        {
            UiSpinner(7.0f * UiScale());

            ImGui::SameLine();

            UiMutedText((std::string("Testando") + UiBusyDots()).c_str());
        }
        else if (!model.serverStatus.empty())
        {
            UiWrappedColored(
                model.serverStatusOk ? UiColorSuccess() : UiColorError(),
                model.serverStatus.c_str());
        }

        std::string current =
            model.serverIsDefault
            ? "Em uso: o servidor público do Colaê (padrão)."
            : "Em uso: " + model.serverCurrent;

        UiMutedText(current.c_str());

        if (!model.serverIsDefault)
        {
            if (ImGui::SmallButton("Voltar ao padrão"))
            {
                actions.serverDefault = true;
            }
        }

        UiMutedText(
            "Vale para as próximas salas. O convite leva o servidor "
            "junto: quem entra pelo link não precisa configurar nada.");
    }
}


// Aba Voz.
void DrawVoiceSettingsTab(
    const SettingsModel& model,
    SettingsActions& actions)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    ImGui::Spacing();

    float fieldX =
        ImGui::GetCursorPosX() +
        ImGui::CalcTextSize("Microfone:").x + style.ItemSpacing.x * 2.0f;

    DeviceCombo(
        "Microfone:", "##mic", model.microphones, model.microphone,
        fieldX, actions.microphone, actions.refreshDevices);

    DeviceCombo(
        "Ouvir em:", "##saida", model.speakers, model.speaker,
        fieldX, actions.speaker, actions.refreshDevices);

    // Teste do microfone
    ImGui::AlignTextToFramePadding();

    UiMutedText("Nível:");

    ImGui::SameLine();

    ImGui::SetCursorPosX(fieldX);

    bool showLevel = model.voiceJoined || model.micTestRunning;

    float level =
        model.voiceJoined ? model.micLevel : model.micTestLevel;

    if (showLevel)
    {
        // Barra centrada na altura da linha do rotulo.
        float lineY = ImGui::GetCursorPosY();
        float barHeight = 6.0f * UiScale();

        ImGui::SetCursorPosY(
            lineY + (ImGui::GetFrameHeight() - barHeight) * 0.5f);

        ImGui::PushStyleColor(
            ImGuiCol_PlotHistogram,
            !model.voiceJoined || model.voiceTransmitting
                ? UiColorSuccess() : UiColorMuted());

        ImGui::ProgressBar(
            (std::min)(1.0f, level * 4.0f),
            ImVec2(-1.0f, barHeight),
            "");

        ImGui::PopStyleColor();

        ImGui::SetCursorPosY(
            lineY + ImGui::GetFrameHeight() + style.ItemSpacing.y);
    }
    else
    {
        UiMutedText("clique em Testar microfone e fale");
    }

    // Fora da voz: testar (e ouvir a propria voz).
    if (!model.voiceJoined)
    {
        ImGui::SetCursorPosX(fieldX);

        if (ImGui::Button(
                model.micTestRunning
                ? "Parar teste##teste"
                : "Testar microfone##teste"))
        {
            actions.micTestToggle = true;
        }

        if (model.micTestLoopback)
        {
            ImGui::SameLine();

            UiToggle("Ouvir minha voz", model.micTestLoopback);

            if (*model.micTestLoopback)
            {
                ImGui::SetCursorPosX(fieldX);

                UiMutedText(
                    "Use fone: com caixa de som, o microfone pega o "
                    "próprio som e apita.");
            }
        }
    }

    if (model.voiceJoined && !model.echoCancel)
    {
        UiWrappedColored(
            UiColorMuted(),
            "Este Windows não ofereceu cancelamento de eco: use fone "
            "de ouvido para os outros não ouvirem a própria voz de "
            "volta.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (model.noiseSuppression)
    {
        if (UiToggle(
                "Reduzir ruído e eco",
                model.noiseSuppression))
        {
            actions.voiceSettingsChanged = true;
        }

        UiMutedText(
            "Tira do seu microfone teclado, mouse, ventoinha e o eco "
            "que vaza das caixas de som.");

        ImGui::Spacing();
    }

    if (model.soundsEnabled && model.soundsVolume)
    {
        if (UiToggle("Sons de aviso", model.soundsEnabled))
        {
            actions.voiceSettingsChanged = true;
        }

        if (*model.soundsEnabled)
        {
            ImGui::SameLine();

            ImGui::SetNextItemWidth(-1.0f);

            float percent = *model.soundsVolume * 100.0f;

            if (ImGui::SliderFloat(
                    "##volsons", &percent, 0.0f, 100.0f, "volume %.0f%%"))
            {
                // (ponteiro do modelo: a tela altera direto)
                *model.soundsVolume = percent / 100.0f;

                actions.voiceSettingsChanged = true;
            }

            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                actions.previewSound = true;
            }
        }

        UiMutedText(
            "Microfone ligado/mudo, alguém entrou ou saiu da voz, "
            "transmissão nova e chat.");
    }
}


void DrawSettingsWindow(
    const SettingsModel& model,
    SettingsActions& actions)
{
    const ImGuiViewport* viewport =
        ImGui::GetMainViewport();

    const ImGuiStyle& style = ImGui::GetStyle();

    float width =
        (std::min)(680.0f * UiScale(), viewport->WorkSize.x - 32.0f);

    ImGui::SetNextWindowPos(
        ImVec2(
            viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
            viewport->WorkPos.y + viewport->WorkSize.y * 0.5f),
        ImGuiCond_Always,
        ImVec2(0.5f, 0.5f));

    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);

    // Janela pequena: rola em vez de passar da tela.
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(0.0f, 0.0f),
        ImVec2(width, viewport->WorkSize.y - 24.0f * UiScale()));

    ImGui::PushStyleColor(ImGuiCol_Border, UiColorDivider());

    ImGui::Begin(
        "##configuracoes",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::PopStyleColor();

    // Fica na frente das outras telas - mas sem roubar o foco de
    // uma lista aberta (antes, os dropdowns fechavam na hora).
    // (Nem no meio de um clique em outro lugar: tomar o foco ali
    // cancelava o clique - ex.: a engrenagem que fecha esta
    // janela.)
    if (model.openTab >= 0 ||
        (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
         !ImGui::IsAnyItemActive() &&
         !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
         !ImGui::IsPopupOpen(
             "",
             ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)))
    {
        ImGui::SetWindowFocus();
    }

    UiTitle("Configurações");

    {
        const char* label = "Fechar";

        float buttonWidth =
            ImGui::CalcTextSize(label).x + style.FramePadding.x * 4.0f;

        ImGui::SameLine(ImGui::GetContentRegionMax().x - buttonWidth);

        ImGui::SetCursorPosY(
            ImGui::GetCursorPosY() + 4.0f * UiScale());

        if (UiPrimaryButtonSized(label, ImVec2(buttonWidth, 0.0f)))
        {
            actions.close = true;
        }
    }

    ImGui::Spacing();

    if (ImGui::BeginTabBar("##abas"))
    {
        if (ImGui::BeginTabItem(
                "Geral",
                nullptr,
                model.openTab == SETTINGS_TAB_GENERAL
                    ? ImGuiTabItemFlags_SetSelected : 0))
        {
            DrawGeneralSettingsTab(model, actions);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(
                "Voz",
                nullptr,
                model.openTab == SETTINGS_TAB_VOICE
                    ? ImGuiTabItemFlags_SetSelected : 0))
        {
            DrawVoiceSettingsTab(model, actions);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(
                "Atalhos",
                nullptr,
                model.openTab == SETTINGS_TAB_SHORTCUTS
                    ? ImGuiTabItemFlags_SetSelected : 0))
        {
            DrawShortcutsTab(model, actions);

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}
