; ============================================================
; Instalador do Colaê (Inno Setup 6.3 ou mais novo)
; ============================================================
;
; Normalmente quem chama isto e o tools\empacotar.ps1, depois de
; montar a pasta do pacote:
;
;   ISCC.exe /DAppVersion=0.8.7 /DPacote=..\dist\Colae-0.8.7
;            /DSaida=..\dist installer\Colae.iss
;
; Como o Discord: instala so para o usuario atual, em
; %LOCALAPPDATA%\Programs\Colae, sem pedir administrador. Cria
; atalho no Menu Iniciar (e na area de trabalho, se a pessoa
; quiser), registra o link de convite colae:// e aparece em
; "Aplicativos instalados", com desinstalador.
; ============================================================

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

#ifndef Pacote
  #define Pacote "..\dist\Colae-" + AppVersion
#endif

#ifndef Saida
  #define Saida "..\dist"
#endif

[Setup]
; Identificador fixo do app: NAO MUDAR (e por ele que o Windows
; reconhece uma atualizacao instalando por cima).
AppId={{6E2B7C4A-9D31-4F8E-A5C2-3B7D1E0F9A64}
AppName=Colaê
AppVersion={#AppVersion}
AppVerName=Colaê {#AppVersion}
AppPublisher=Nikolas (Nikao1)
AppPublisherURL=https://github.com/Nikao1/stream-project
AppSupportURL=https://github.com/Nikao1/stream-project/issues
AppUpdatesURL=https://github.com/Nikao1/stream-project/releases

; So para este usuario, sem administrador.
PrivilegesRequired=lowest
DefaultDirName={localappdata}\Programs\Colae
DisableDirPage=yes
DisableProgramGroupPage=yes
DefaultGroupName=Colaê
DisableReadyPage=yes
DisableWelcomePage=no

ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0

; O Colaê aberto segura os arquivos: o instalador pede pra fechar
; (as instancias abertas criam estes nomes - ver receiver.cpp).
AppMutex=ColaeInstance1,ColaeInstance2,ColaeInstance3,ColaeInstance4,ColaeInstance5,ColaeInstance6,ColaeInstance7,ColaeInstance8
CloseApplications=yes
RestartApplications=no

SetupIconFile=..\resources\colae.ico
UninstallDisplayIcon={app}\Colae.exe
UninstallDisplayName=Colaê
WizardStyle=modern

OutputDir={#Saida}
OutputBaseFilename=Instalar-Colae-{#AppVersion}
Compression=lzma2/max
SolidCompression=yes

VersionInfoVersion={#AppVersion}
VersionInfoProductName=Colaê
VersionInfoDescription=Instalador do Colaê
VersionInfoCompany=Nikolas (Nikao1)

[Languages]
Name: "ptbr"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Messages]
ptbr.WelcomeLabel2=Isto vai instalar o [name/ver] no seu computador.%n%nTela, som e voz direto entre amigos — leve, gratuito e criptografado.

[Tasks]
Name: "desktopicon"; Description: "Criar um atalho na área de trabalho"; GroupDescription: "Atalhos:"

[Files]
Source: "{#Pacote}\Colae.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Pacote}\ffmpeg.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Pacote}\LEIA-ME.txt"; DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "{#Pacote}\LICENSE.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Pacote}\AVISOS-DE-TERCEIROS.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#Pacote}\PRIVACIDADE.txt"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Colaê"; Filename: "{app}\Colae.exe"; Comment: "Tela, som e voz direto entre amigos"
Name: "{autodesktop}\Colaê"; Filename: "{app}\Colae.exe"; Tasks: desktopicon

[Registry]
; Link de convite colae:// (o programa tambem refaz isto ao abrir;
; aqui e pra ja funcionar antes da primeira abertura e pra sair
; junto na desinstalacao).
Root: HKCU; Subkey: "Software\Classes\colae"; ValueType: string; ValueName: ""; ValueData: "URL:Colaê"; Flags: uninsdeletekey
Root: HKCU; Subkey: "Software\Classes\colae"; ValueType: string; ValueName: "URL Protocol"; ValueData: ""
Root: HKCU; Subkey: "Software\Classes\colae\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: """{app}\Colae.exe"",0"
Root: HKCU; Subkey: "Software\Classes\colae\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Colae.exe"" --convite ""%1"""

[Run]
Filename: "{app}\Colae.exe"; Description: "Abrir o Colaê"; Flags: nowait postinstall skipifsilent

[Code]
// Na desinstalacao: perguntar se apaga tambem as configuracoes
// (nome, foto, atalhos, salas recentes). Desinstalando em
// silencio (ex.: atualizacao), nunca apaga.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  Pasta: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    Pasta := ExpandConstant('{userappdata}\Colae');

    if (not UninstallSilent) and DirExists(Pasta) then
    begin
      if MsgBox('Apagar também suas configurações do Colaê (nome, foto, atalhos e salas recentes)?',
                mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
      begin
        DelTree(Pasta, True, True, True);
      end;
    end;
  end;
end;
