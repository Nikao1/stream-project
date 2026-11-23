#include "receiver_screens.h"

#include <algorithm>

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


void DrawHomeScreen(
    HomeScreenModel& model,
    HomeScreenActions& actions)
{
    UiBeginCenteredPanel("##inicio", 440.0f);

    UiLogo(ImGui::GetFrameHeight() * 1.7f);

    ImGui::SameLine();

    ImGui::SetCursorPosY(
        ImGui::GetCursorPosY() + 6.0f * UiScale());

    UiTitle("Stream Project");

    // Configuracoes (canto direito do titulo).
    ImGui::SameLine(
        ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());

    if (GearButton("##inicio_config", "Configurações"))
    {
        actions.openSettings = true;
    }

    UiMutedText(
        "Tela e som direto de um computador para outro, sem "
        "passar por servidor."
    );

    ImGui::Spacing();

    ImGui::BeginDisabled(model.busy);

    UiFieldLabel("Seu nome");

    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x);

    ImGui::InputTextWithHint(
        "##nome",
        "Como os outros vão te ver na sala",
        model.name,
        model.nameSize
    );

    ImGui::Spacing();

    if (UiPrimaryButton(
            model.busy ? "Aguarde..." : "Criar uma sala",
            !model.busy))
    {
        actions.createRoom = true;
    }

    ImGui::Spacing();

    UiFieldLabel("Ou entre com um código");

    const char* enterLabel = "Entrar";

    float enterWidth =
        ImGui::CalcTextSize(enterLabel).x +
        ImGui::GetStyle().FramePadding.x * 4.0f;

    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x -
        enterWidth -
        ImGui::GetStyle().ItemSpacing.x
    );

    bool submitted =
        ImGui::InputTextWithHint(
            "##codigo",
            "Código da sala",
            model.code,
            model.codeSize,
            ImGuiInputTextFlags_EnterReturnsTrue |
            ImGuiInputTextFlags_AutoSelectAll
        );

    ImGui::SameLine();

    if (ImGui::Button(
            enterLabel,
            ImVec2(enterWidth, 0.0f)) ||
        submitted)
    {
        actions.enter = true;
    }

    ImGui::EndDisabled();

    if (model.busy)
    {
        UiSpinner(8.0f * UiScale());

        ImGui::SameLine();

        UiMutedText(
            (model.status + UiBusyDots()).c_str()
        );

        UiMutedText(
            "Na primeira vez depois de um tempo parado, o "
            "servidor pode levar até 1 minuto para responder."
        );
    }
    else if (!model.error.empty())
    {
        UiWrappedColored(
            UiColorError(),
            model.error.c_str()
        );
    }
    else
    {
        UiMutedText(
            "Funciona também com o código de quem transmite "
            "sem sala."
        );
    }

    ImGui::Spacing();

    ImGui::Separator();

    ImGui::BeginDisabled(model.busy);

    if (ImGui::Button(
            "Transmitir sem sala",
            ImVec2(
                ImGui::GetContentRegionAvail().x,
                0.0f)))
    {
        actions.shareWithoutRoom = true;
    }

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

    ImGui::BeginChild(
        "##mensagens",
        ImVec2(0.0f, height - inputHeight - style.ItemSpacing.y),
        ImGuiChildFlags_Borders);

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
    void* image = nullptr)
{
    float avatar = ImGui::GetFrameHeight() * 1.15f;

    UiAvatar(name.c_str(), avatar, live, speaking, image);

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
    const char* detail = "";

    switch (voice)
    {
    case MemberVoice::Connecting: detail = "conectando a voz..."; break;
    case MemberVoice::In:         detail = "na voz"; break;
    case MemberVoice::Speaking:   detail = "falando"; break;
    default: break;
    }

    std::string shown = name;

    std::string detailText = detail;

    // Silenciada por esta pessoa.
    if (!isMe && voice != MemberVoice::Out && volume <= 0.001f)
    {
        detailText = "silenciado por você";
    }

    ImGui::BeginGroup();

    PersonRow(
        shown,
        detailText,
        live,
        0.0f,
        speakAmount,
        image);

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

    windowBg.w = 1.0f;

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
void DrawVoiceSection(
    const RoomScreenModel& model,
    RoomScreenActions& actions)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    UiFieldLabel("Voz");

    ImGui::Spacing();

    float gear = ImGui::GetFrameHeight();

    if (model.voiceJoining)
    {
        UiSpinner(8.0f * UiScale());

        ImGui::SameLine();

        UiMutedText((std::string("Entrando na voz") + UiBusyDots()).c_str());
    }
    else if (!model.voiceJoined)
    {
        if (ImGui::Button(
                "Entrar na voz",
                ImVec2(
                    ImGui::GetContentRegionAvail().x - gear -
                        style.ItemSpacing.x,
                    0.0f)))
        {
            actions.voiceJoin = true;
        }

        ImGui::SameLine();

        if (GearButton("##voz_config", "Configurações de voz e atalhos"))
        {
            actions.openVoiceSettings = true;
        }

        UiMutedText(
            "Converse com a sala por aqui mesmo. A voz vai direto "
            "entre vocês, criptografada.");
    }
    else
    {
        // [microfone] [sair da voz] [engrenagem]
        float half =
            (ImGui::GetContentRegionAvail().x - gear -
             style.ItemSpacing.x * 2.0f) * 0.5f;

        float icon = ImGui::GetTextLineHeight();

        ImVec4 micColor =
            model.voiceMuted
            ? UiColorError()
            : ImGui::GetStyleColorVec4(ImGuiCol_Text);

        ImGui::PushStyleColor(ImGuiCol_Text, micColor);

        if (ImGui::Button(
                model.voiceMuted
                ? "      Ligar microfone##mic"
                : "      Silenciar##mic",
                ImVec2(half, 0.0f)))
        {
            actions.voiceToggleMute = true;
        }

        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered() && !model.voiceMuteKey.empty())
        {
            ImGui::SetTooltip(
                "Atalho: %s (funciona com o jogo aberto)",
                model.voiceMuteKey.c_str());
        }

        ImVec2 buttonMin = ImGui::GetItemRectMin();

        UiMicIconAt(
            ImVec2(
                buttonMin.x + style.FramePadding.x * 1.5f,
                buttonMin.y + style.FramePadding.y),
            icon,
            ImGui::GetColorU32(micColor),
            model.voiceMuted);

        ImGui::SameLine();

        if (ImGui::Button("Sair da voz", ImVec2(half, 0.0f)))
        {
            actions.voiceLeave = true;
        }

        ImGui::SameLine();

        if (GearButton("##voz_config", "Configurações de voz e atalhos"))
        {
            actions.openVoiceSettings = true;
        }

        // Nivel do microfone (verde quando a voz esta saindo).
        ImGui::PushStyleColor(
            ImGuiCol_PlotHistogram,
            model.voiceTransmitting
            ? UiColorSuccess()
            : UiColorMuted());

        ImGui::ProgressBar(
            (std::min)(1.0f, model.voiceMicLevel * 4.0f),
            ImVec2(-1.0f, 4.0f * UiScale()),
            "");

        ImGui::PopStyleColor();

        // Uma linha de situacao, so quando ajuda.
        if (model.voiceMuted)
        {
            UiMutedText("Seu microfone está desligado.");
        }
        else if (!model.voicePttKey.empty() && !model.voiceTransmitting)
        {
            UiMutedText(
                ("Segure " + model.voicePttKey + " para falar.").c_str());
        }

        if (!model.voiceEchoCancel)
        {
            UiWrappedColored(
                UiColorMuted(),
                "Sem cancelamento de eco neste Windows: use fone de "
                "ouvido.");
        }
    }

    if (!model.voiceError.empty())
    {
        UiWrappedColored(UiColorError(), model.voiceError.c_str());
    }
}

} // namespace


void DrawRoomScreen(
    RoomScreenModel& model,
    RoomScreenActions& actions)
{
    static double codeCopiedAt = 0.0;

    const ImGuiStyle& style = ImGui::GetStyle();

    UiBeginCenteredPanel("##sala", 860.0f);

    // Duas colunas em janelas largas; uma so nas estreitas.
    bool twoColumns =
        ImGui::GetContentRegionAvail().x >= 640.0f * UiScale();


    // --------------------------------------------------------
    // Cabecalho: nome da sala + copiar codigo
    // --------------------------------------------------------

    UiTitle(("Sala " + model.code).c_str());

    {
        static double linkCopiedAt = 0.0;

        auto recently =
            [](double at)
            {
                return at > 0.0 && ImGui::GetTime() - at < 2.0;
            };

        float codeWidth =
            ImGui::CalcTextSize("Copiar código").x +
            style.FramePadding.x * 4.0f;

        float linkWidth =
            model.inviteLink.empty()
            ? 0.0f
            : ImGui::CalcTextSize("Copiar convite").x +
                  style.FramePadding.x * 4.0f;

        float total =
            codeWidth +
            (linkWidth > 0.0f ? linkWidth + style.ItemSpacing.x : 0.0f);

        // Ao lado do titulo se couber; senao, na linha de baixo.
        float titleWidth = ImGui::GetItemRectSize().x;

        if (titleWidth + total + style.ItemSpacing.x * 2.0f <=
            ImGui::GetContentRegionAvail().x)
        {
            ImGui::SameLine(ImGui::GetContentRegionMax().x - total);

            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() + 4.0f * UiScale());
        }

        float buttonsY = ImGui::GetCursorPosY();

        if (linkWidth > 0.0f)
        {
            if (ImGui::Button(
                    recently(linkCopiedAt)
                    ? "Copiado!##copiarlink"
                    : "Copiar convite##copiarlink",
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

        if (ImGui::Button(
                recently(codeCopiedAt)
                ? "Copiado!##copiarsala"
                : "Copiar código##copiarsala",
                ImVec2(codeWidth, 0.0f)))
        {
            ImGui::SetClipboardText(model.code.c_str());

            codeCopiedAt = ImGui::GetTime();
        }
    }

    UiMutedText(
        (std::string(model.inviteLink.empty()
                         ? "Passe o código inteiro para quem vai entrar"
                         : "Mande o convite (ou o código inteiro) para quem vai entrar") +
         "  ·  cabem até " + std::to_string(model.maxMembers) +
         " pessoas").c_str());

    if (model.offline)
    {
        UiWrappedColored(
            UiColorError(),
            "Sem resposta do servidor da sala. Verifique sua "
            "internet - quem você já está assistindo continua."
        );
    }

    ImGui::Spacing();

    bool table =
        twoColumns &&
        ImGui::BeginTable(
            "##salacolunas",
            2,
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_BordersInnerV);

    if (table)
    {
        ImGui::TableSetupColumn("a", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("b", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextColumn();
    }


    // --------------------------------------------------------
    // Quem esta transmitindo
    // --------------------------------------------------------

    UiFieldLabel("Transmitindo agora");

    ImGui::Spacing();

    if (model.presenters.empty())
    {
        UiMutedText(
            "Ninguém está transmitindo ainda. Quando alguém "
            "começar, aparece aqui.");
    }

    const float buttonWidth =
        ImGui::CalcTextSize("Assistindo").x +
        style.FramePadding.x * 4.0f;

    for (size_t i = 0; i < model.presenters.size(); ++i)
    {
        const RoomPresenterEntry& entry =
            model.presenters[i];

        ImGui::PushID(static_cast<int>(i));

        float rowTop = ImGui::GetCursorPosY();

        if (entry.thumbnail &&
            entry.thumbWidth > 0.0f &&
            entry.thumbHeight > 0.0f)
        {
            // Previa no lugar da bolinha (clicar = assistir).
            float maxW = 136.0f * UiScale();
            float maxH = 77.0f * UiScale();

            float scale =
                (std::min)(maxW / entry.thumbWidth,
                           maxH / entry.thumbHeight);

            ImVec2 size(entry.thumbWidth * scale,
                        entry.thumbHeight * scale);

            ImVec2 pos = ImGui::GetCursorScreenPos();

            // Centraliza numa caixa fixa (linhas alinhadas).
            ImGui::InvisibleButton("##previa", ImVec2(maxW, maxH));

            bool hovered = ImGui::IsItemHovered();

            if (ImGui::IsItemClicked() &&
                !entry.isMe && !entry.watching && !model.connecting)
            {
                actions.watch = static_cast<int>(i);
            }

            ImDrawList* draw = ImGui::GetWindowDrawList();

            ImVec2 min(pos.x + (maxW - size.x) * 0.5f,
                       pos.y + (maxH - size.y) * 0.5f);

            ImVec2 max(min.x + size.x, min.y + size.y);

            draw->AddRectFilled(
                pos, ImVec2(pos.x + maxW, pos.y + maxH),
                IM_COL32(0, 0, 0, 255), 6.0f * UiScale());

            draw->AddImageRounded(
                reinterpret_cast<ImTextureID>(entry.thumbnail),
                min, max, ImVec2(0, 0), ImVec2(1, 1),
                IM_COL32(255, 255, 255, 255),
                6.0f * UiScale());

            draw->AddRect(
                pos, ImVec2(pos.x + maxW, pos.y + maxH),
                hovered
                    ? ImGui::GetColorU32(UiColorAccent())
                    : IM_COL32(239, 68, 68, 200),
                6.0f * UiScale(), 0, 1.5f * UiScale());

            if (hovered)
            {
                // Maior ao passar o mouse.
                ImGui::BeginTooltip();

                float big =
                    (std::min)(360.0f * UiScale() / entry.thumbWidth,
                               203.0f * UiScale() / entry.thumbHeight);

                ImGui::Image(
                    reinterpret_cast<ImTextureID>(entry.thumbnail),
                    ImVec2(entry.thumbWidth * big,
                           entry.thumbHeight * big));

                ImGui::EndTooltip();
            }

            ImGui::SameLine();

            ImGui::BeginGroup();

            float textWidth =
                ImGui::GetContentRegionAvail().x -
                buttonWidth - style.ItemSpacing.x;

            ImGui::PushTextWrapPos(
                ImGui::GetCursorPosX() + (std::max)(40.0f, textWidth));

            ImGui::SetCursorPosY(
                ImGui::GetCursorPosY() + maxH * 0.18f);

            ImGui::TextUnformatted(
                (entry.isMe ? entry.name + " (você)" : entry.name).c_str());

            if (!entry.label.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, UiColorMuted());

                ImGui::TextUnformatted(entry.label.c_str());

                ImGui::PopStyleColor();
            }

            ImGui::PopTextWrapPos();

            ImGui::EndGroup();

            // A linha tem pelo menos a altura da previa.
            float bottom = rowTop + maxH + style.ItemSpacing.y;

            if (ImGui::GetCursorPosY() < bottom)
            {
                ImGui::SetCursorPosY(bottom);
            }
        }
        else
        {
            PersonRow(
                entry.isMe ? entry.name + " (você)" : entry.name,
                entry.label,
                true,
                buttonWidth + style.ItemSpacing.x,
                0.0f,
                entry.avatar);
        }

        float rowBottom = ImGui::GetCursorPosY();

        // Botao alinhado a direita, no meio da linha.
        ImGui::SameLine(
            ImGui::GetContentRegionMax().x - buttonWidth);

        ImGui::SetCursorPosY(
            rowTop +
            ((rowBottom - rowTop) - style.ItemSpacing.y -
             ImGui::GetFrameHeight()) * 0.5f);

        if (entry.isMe)
        {
            ImGui::BeginDisabled();

            ImGui::Button("Você", ImVec2(buttonWidth, 0.0f));

            ImGui::EndDisabled();
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

            // (a acao principal da sala e "Transmitir"; aqui um
            // botao comum, com o texto em destaque)
            ImGui::PushStyleColor(ImGuiCol_Text, UiColorAccent());

            bool watch =
                ImGui::Button(
                    "Assistir",
                    ImVec2(buttonWidth, 0.0f));

            ImGui::PopStyleColor();

            if (watch)
            {
                actions.watch = static_cast<int>(i);
            }

            ImGui::EndDisabled();
        }

        ImGui::SetCursorPosY(rowBottom);

        ImGui::PopID();

        ImGui::Spacing();
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
        UiWrappedColored(
            UiColorError(),
            model.error.c_str());
    }

    ImGui::Spacing();

    ImGui::Separator();

    ImGui::Spacing();


    // --------------------------------------------------------
    // A transmissao desta pessoa
    // --------------------------------------------------------

    if (model.sharing)
    {
        std::string text =
            "Você está transmitindo: " +
            model.sharingLabel +
            "  ·  " +
            std::to_string(model.sharingViewers) +
            " assistindo";

        UiWrappedColored(UiColorSuccess(), text.c_str());

        float half =
            (ImGui::GetContentRegionAvail().x -
             style.ItemSpacing.x) * 0.5f;

        // Coluna estreita: textos curtos (nada cortado).
        bool narrow =
            ImGui::CalcTextSize("Opções da transmissão").x +
                style.FramePadding.x * 2.0f > half;

        if (ImGui::Button(
                narrow ? "Opções##opcoestransm"
                       : "Opções da transmissão",
                ImVec2(half, 0.0f)))
        {
            actions.openShare = true;
        }

        ImGui::SameLine();

        if (ImGui::Button(
                narrow ? "Parar##parartransm"
                       : "Parar de transmitir",
                ImVec2(half, 0.0f)))
        {
            actions.stopShare = true;
        }
    }
    else if (UiPrimaryButton("Transmitir minha tela"))
    {
        actions.openShare = true;
    }

    ImGui::Spacing();

    if (ImGui::Button(
            "Sair da sala",
            ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
    {
        actions.leave = true;
    }


    // --------------------------------------------------------
    // Pessoas + chat (segunda coluna, ou embaixo)
    // --------------------------------------------------------

    if (table)
    {
        ImGui::TableNextColumn();
    }
    else
    {
        ImGui::Spacing();
        ImGui::Separator();
    }

    UiFieldLabel(
        ("Na sala (" + std::to_string(model.members.size()) +
         ")").c_str());

    ImGui::Spacing();

    for (size_t i = 0; i < model.members.size(); ++i)
    {
        bool live =
            i < model.memberLive.size() &&
            model.memberLive[i];

        MemberVoice voice =
            i < model.memberVoice.size()
            ? model.memberVoice[i]
            : MemberVoice::Out;

        bool isMe =
            i < model.memberIsMe.size() &&
            model.memberIsMe[i];

        ImGui::PushID(static_cast<int>(1000 + i));

        float volume =
            i < model.memberVolume.size()
            ? model.memberVolume[i]
            : 1.0f;

        float newVolume = volume;

        if (MemberRow(
                model.members[i],
                live,
                voice,
                isMe,
                model.voiceJoined,
                volume,
                newVolume,
                i < model.memberSpeak.size() ? model.memberSpeak[i] : 0.0f,
                i < model.memberAvatar.size() ? model.memberAvatar[i] : nullptr))
        {
            actions.volumeMember = static_cast<int>(i);
            actions.volumeValue = newVolume;
        }

        ImGui::PopID();
    }

    ImGui::Spacing();

    DrawVoiceSection(model, actions);

    ImGui::Spacing();

    UiFieldLabel("Chat");

    if (model.chatInput &&
        DrawChatBox(
            "salachat",
            model.chat,
            model.chatInput,
            model.chatInputSize,
            (twoColumns ? 230.0f : 180.0f) * UiScale(),
            false))
    {
        actions.sendChat = true;
    }

    if (table)
    {
        ImGui::EndTable();
    }

    UiEndPanel();
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
    }

    bool clicked =
        ImGui::Button(
            label,
            ImVec2(width, 0.0f));

    if (selected)
    {
        ImGui::PopStyleColor(3);
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

    if (ImGui::Button(
            "×##fechar",
            ImVec2(closeSize, closeSize)))
    {
        collapsed = true;
    }

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

    for (size_t i = 0; i < model.people.size(); ++i)
    {
        const PeopleEntry& person = model.people[i];

        ImGui::PushID(static_cast<int>(i));

        float newVolume = person.volume;

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

    ImGui::EndChild();
}


// Rodape do painel: entrar / silenciar / sair da voz.
void SideVoiceControls(
    const PeoplePanelModel& model,
    PeoplePanelActions& actions)
{
    const ImGuiStyle& style = ImGui::GetStyle();

    if (model.voiceJoining)
    {
        UiSpinner(7.0f * UiScale());

        ImGui::SameLine();

        UiMutedText((std::string("Entrando na voz") + UiBusyDots()).c_str());
    }
    else if (!model.voiceJoined)
    {
        if (ImGui::Button(
                "Entrar na voz",
                ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
        {
            actions.voiceJoin = true;
        }
    }
    else
    {
        float half =
            (ImGui::GetContentRegionAvail().x -
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
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        selected
        ? ImGui::GetStyleColorVec4(ImGuiCol_Text)
        : UiColorMuted());

    bool clicked = ImGui::Button(label);

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


float RoomSidePanelWidth()
{
    return 300.0f * UiScale();
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

    ImGui::SetNextWindowBgAlpha(0.97f);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, eased);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(
        ImGuiStyleVar_WindowPadding,
        ImVec2(14.0f * UiScale(), 10.0f * UiScale()));

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

    ImGui::SameLine(
        ImGui::GetContentRegionMax().x -
        button * 2.0f - style.ItemSpacing.x);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    if (UiIconButton(
            "##fixar",
            model.pinned ? UiIcon::Pin : UiIcon::PinOff,
            model.pinned
            ? "Fixado: fica aberto ao clicar no vídeo"
            : "Fixar o painel (não fecha ao clicar no vídeo)",
            model.pinned))
    {
        actions.togglePin = true;
    }

    ImGui::SameLine();

    if (UiIconButton("##fecharpainel", UiIcon::Close, "Fechar"))
    {
        actions.close = true;
    }

    ImGui::PopStyleColor();

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

    SideVoiceControls(model.people, actions.people);

    if (ImGui::SmallButton("Atalhos"))
    {
        actions.people.openShortcuts = true;
    }

    storage->SetFloat(footerKey, ImGui::GetCursorPosY() - footerStart);

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

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));

    // Popovers acima do botao que os abriu.
    auto anchorPopup =
        []()
        {
            ImVec2 min = ImGui::GetItemRectMin();
            ImVec2 max = ImGui::GetItemRectMax();

            ImGui::GetStateStorage()->SetFloat(
                ImGui::GetID("##ancorax"), (min.x + max.x) * 0.5f);
            ImGui::GetStateStorage()->SetFloat(
                ImGui::GetID("##ancoray"), min.y);
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

        if (UiIconButton("##bartrocar", UiIcon::Switch, "Trocar de tela"))
        {
            anchorPopup();

            ImGui::OpenPopup("##trocartela");
        }
    }

    // ---------------- Video, config., sair ----------------

    BarDivider();

    if (model.watching)
    {
        if (UiIconButton("##baropcoes", UiIcon::Sliders, "Opções do vídeo"))
        {
            anchorPopup();

            ImGui::OpenPopup("##opcoesvideo");
        }

        ImGui::SameLine();
    }

    if (GearButton("##barconfig", "Configurações"))
    {
        actions.openSettings = true;
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
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
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

    ImGui::Begin(
        "##configuracoes",
        nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings);

    // Fica na frente das outras telas - mas sem roubar o foco de
    // uma lista aberta (antes, os dropdowns fechavam na hora).
    if (model.openTab >= 0 ||
        (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
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
