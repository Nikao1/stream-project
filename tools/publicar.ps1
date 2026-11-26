# Colae - Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licenca MIT
<#
.SYNOPSIS
    Publica uma versao nova do Colae (com a atualizacao automatica).

.DESCRIPTION
    Com -Lancar, faz TUDO sozinho - so pede a senha da chave:

      1. confere se o git esta limpo e atualizado;
      2. sobe o numero da versao no CMakeLists.txt (a proxima, ou a
         de -Versao);
      3. compila e monta o .zip e o instalador (empacotar.ps1);
      4. assina o instalador (pede a senha da chave);
      5. commit + push da versao nova;
      6. cria o release no GitHub com o instalador e o .zip;
      7. so entao escreve, commita e sobe o signaling\versao.txt;
      8. espera o servidor (Render) passar a anunciar a versao.

    Sem -Lancar, so prepara (passos 3, 4 e escreve o versao.txt) e
    mostra o que fazer a mao. Veja PUBLICAR.md.

    Precisa do GitHub CLI logado (uma vez):
      winget install GitHub.cli
      gh auth login

.PARAMETER Chave
    Arquivo da chave privada. Padrao: variavel COLAE_CHAVE.

.PARAMETER Notas
    Texto curto das novidades (vai no release e na confirmacao da
    atualizacao).

.PARAMETER Versao
    Numero da versao nova (ex.: 0.9.0). Padrao com -Lancar: sobe o
    ultimo numero (0.8.12 -> 0.8.13).

.PARAMETER Lancar
    Faz tudo, inclusive o release no GitHub e o push.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\publicar.ps1 -Lancar -Chave C:\Users\Nikolas\chaves\colae.chave -Notas "Transmissao encerra 1 minuto depois de a janela fechar"
#>

param(
    [string] $Chave = $env:COLAE_CHAVE,
    [string] $Notas = "",
    [string] $Versao = "",
    [switch] $Lancar,
    [switch] $SemCompilar,
    [string] $PastaBuild = "build"
)

$ErrorActionPreference = "Stop"

# Mensagens sem acento de proposito (PowerShell 5 le scripts sem
# BOM como ANSI).

function Falhar([string] $mensagem)
{
    Write-Host ""
    Write-Host "ERRO: $mensagem" -ForegroundColor Red
    exit 1
}

function Passo([string] $mensagem)
{
    Write-Host ""
    Write-Host "== $mensagem" -ForegroundColor Cyan
}

function Rodar([string] $descricao, [scriptblock] $comando)
{
    & $comando

    if ($LASTEXITCODE -ne 0) { Falhar "$descricao falhou (veja acima)." }
}

$projeto = Split-Path -Parent $PSScriptRoot

Set-Location $projeto

$repositorio = "https://github.com/Nikao1/stream-project"
$servidor = "https://colae.onrender.com"

$arquivoCmake = Join-Path $projeto "CMakeLists.txt"
$utf8 = New-Object System.Text.UTF8Encoding($false)

$padraoVersao = "(project\s*\(\s*stream-[a-z]+\s+VERSION\s+)([0-9]+\.[0-9]+\.[0-9]+)"

function VersaoAtual
{
    $texto = [IO.File]::ReadAllText($arquivoCmake)

    $achou = [regex]::Match($texto, $padraoVersao)

    if (-not $achou.Success) { Falhar "Nao achei 'VERSION x.y.z' no CMakeLists.txt." }

    return $achou.Groups[2].Value
}

function MudarVersao([string] $nova)
{
    $texto = [IO.File]::ReadAllText($arquivoCmake)

    $texto = ([regex] $padraoVersao).Replace($texto, "`${1}$nova", 1)

    [IO.File]::WriteAllText($arquivoCmake, $texto, $utf8)
}

function DesfazerVersao
{
    & git checkout -- CMakeLists.txt | Out-Null
}

# Roda um comando "quieto" (sem que mensagens no stderr virem erro
# no PowerShell 5) e devolve o codigo de saida.
function Quieto([scriptblock] $comando)
{
    $antes = $ErrorActionPreference

    $ErrorActionPreference = "Continue"

    try { & $comando *> $null } catch { }

    $codigo = $LASTEXITCODE

    $ErrorActionPreference = $antes

    return $codigo
}

if (-not $Chave -or -not (Test-Path $Chave))
{
    Falhar "Informe a chave privada: -Chave C:\caminho\colae.chave (ou a variavel COLAE_CHAVE). Para criar: veja PUBLICAR.md."
}

# ------------------------------------------------------------
# 0. Conferencias antes de mexer em qualquer coisa (-Lancar)
# ------------------------------------------------------------

$ramo = ""

if ($Lancar)
{
    Passo "Conferindo git e GitHub"

    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { Falhar "git nao encontrado." }

    if (-not (Get-Command gh -ErrorAction SilentlyContinue))
    {
        Falhar "GitHub CLI nao encontrado. Instale com: winget install GitHub.cli  e depois: gh auth login"
    }

    if ((Quieto { gh auth status }) -ne 0) { Falhar "GitHub CLI sem login. Rode: gh auth login" }

    $pendente = & git status --porcelain

    if ($pendente)
    {
        Write-Host ($pendente -join "`n")
        Falhar "Ha mudancas sem commit (acima). Faca o commit (ou descarte) antes de lancar."
    }

    $ramo = (& git rev-parse --abbrev-ref HEAD).Trim()

    Rodar "git pull" { git pull --ff-only }

    $atual = VersaoAtual

    if (-not $Versao)
    {
        $partes = $atual.Split(".")

        $Versao = "{0}.{1}.{2}" -f $partes[0], $partes[1], ([int] $partes[2] + 1)
    }

    if ($Versao -notmatch "^[0-9]+\.[0-9]+\.[0-9]+$") { Falhar "Versao invalida: $Versao (use x.y.z)." }

    $existe = & git ls-remote --tags origin "refs/tags/v$Versao"

    if ($existe) { Falhar "A tag v$Versao ja existe no GitHub. Use -Versao com outro numero." }

    Write-Host "Versao atual: $atual  ->  nova: $Versao (ramo $ramo)"

    if (-not $Notas) { $Notas = "Colae $Versao" }

    MudarVersao $Versao
}
elseif ($Versao)
{
    MudarVersao $Versao
}

# ------------------------------------------------------------
# 1. Empacotar (compila tambem o colae-assinar)
# ------------------------------------------------------------

$argumentos = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "empacotar.ps1"), "-PastaBuild", $PastaBuild)

if ($SemCompilar) { $argumentos += "-SemCompilar" }

& powershell @argumentos

if ($LASTEXITCODE -ne 0)
{
    if ($Lancar) { DesfazerVersao }

    Falhar "O empacotar.ps1 falhou."
}

$versao = VersaoAtual

$instalador = Join-Path $projeto "dist\Instalar-Colae-$versao.exe"
$zip = Join-Path $projeto "dist\Colae-$versao-windows.zip"

if (-not (Test-Path $instalador))
{
    if ($Lancar) { DesfazerVersao }

    Falhar "Nao achei $instalador. A atualizacao automatica precisa do instalador (Inno Setup)."
}

# ------------------------------------------------------------
# 2. Assinar
# ------------------------------------------------------------

Passo "Assinando o instalador (digite a senha da chave quando pedir)"

$assinar = Get-ChildItem -Path (Join-Path $projeto $PastaBuild) -Recurse -Filter "colae-assinar.exe" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1

if (-not $assinar)
{
    if ($Lancar) { DesfazerVersao }

    Falhar "Nao achei o colae-assinar.exe na pasta $PastaBuild (compile o projeto)."
}

$saida = & $assinar.FullName assinar $Chave $instalador $versao

if ($LASTEXITCODE -ne 0)
{
    if ($Lancar) { DesfazerVersao }

    Falhar "O colae-assinar falhou (senha errada?). Nada foi publicado."
}

$campos = @{}

foreach ($linha in $saida)
{
    $i = $linha.IndexOf("=")

    if ($i -gt 0) { $campos[$linha.Substring(0, $i)] = $linha.Substring($i + 1) }
}

# A chave publica embutida no programa precisa ser a mesma.
$chaveNoCodigo = Select-String -Path (Join-Path $projeto "src\update_key.h") -Pattern 'COLAE_UPDATE_PUBLIC_KEY = "([0-9a-f]*)"' |
    ForEach-Object { $_.Matches[0].Groups[1].Value }

if ($chaveNoCodigo -ne $campos["publica"])
{
    if ($Lancar) { DesfazerVersao }

    Falhar "A chave publica em src\update_key.h nao e a desta chave privada. Cole la: $($campos['publica']) e compile de novo."
}

$nomeInstalador = Split-Path -Leaf $instalador

$textoVersao =
    "# Ultima versao do Colae (gerado pelo tools\publicar.ps1)`n" +
    "versao=$versao`n" +
    "url=$repositorio/releases/download/v$versao/$nomeInstalador`n" +
    "pagina=$repositorio/releases/tag/v$versao`n" +
    "hash=$($campos['hash'])`n" +
    "assinatura=$($campos['assinatura'])`n"

if ($Notas) { $textoVersao += "notas=$($Notas -replace "[`r`n]+", ' ')`n" }

$arquivoVersao = Join-Path $projeto "signaling\versao.txt"

# ------------------------------------------------------------
# Sem -Lancar: so prepara e explica o resto
# ------------------------------------------------------------

if (-not $Lancar)
{
    [IO.File]::WriteAllText($arquivoVersao, $textoVersao, $utf8)

    Write-Host ""
    Write-Host "Pronto para publicar a $versao!" -ForegroundColor Green
    Write-Host ""
    Write-Host "1) Crie o release v$versao no GitHub com estes arquivos:"
    Write-Host "     $instalador"
    if (Test-Path $zip) { Write-Host "     $zip" }
    Write-Host "   Pelo site: $repositorio/releases/new (tag: v$versao)"
    Write-Host ""
    Write-Host "2) SO DEPOIS do release publicado, suba o signaling\versao.txt:"
    Write-Host "     git add signaling/versao.txt"
    Write-Host "     git commit -m `"Release $versao`""
    Write-Host "     git push"
    Write-Host ""
    Write-Host "(Da proxima vez, use -Lancar para fazer tudo sozinho.)"

    exit 0
}

# ------------------------------------------------------------
# 3. Commit + push da versao nova
# ------------------------------------------------------------

Passo "Subindo a versao $versao"

Rodar "git add" { git add CMakeLists.txt }
Rodar "git commit" { git commit -m "Version $versao" }
Rodar "git push" { git push }

# ------------------------------------------------------------
# 4. Release no GitHub
# ------------------------------------------------------------

Passo "Criando o release v$versao no GitHub"

$arquivosRelease = @($instalador)

if (Test-Path $zip) { $arquivosRelease += $zip }

& gh release create "v$versao" @arquivosRelease --title "Colae $versao" --notes $Notas --target $ramo

if ($LASTEXITCODE -ne 0)
{
    Falhar ("O release nao foi criado. A versao $versao ja esta no git; tente de novo pelo site " +
            "($repositorio/releases/new, tag v$versao) e depois suba o versao.txt rodando este " +
            "script sem -Lancar e com -SemCompilar.")
}

# ------------------------------------------------------------
# 5. versao.txt (so agora: o download ja existe)
# ------------------------------------------------------------

Passo "Anunciando a versao (signaling\versao.txt)"

[IO.File]::WriteAllText($arquivoVersao, $textoVersao, $utf8)

Rodar "git add" { git add signaling/versao.txt }
Rodar "git commit" { git commit -m "Publish $versao update" }
Rodar "git push" { git push }

# ------------------------------------------------------------
# 6. Esperar o servidor anunciar
# ------------------------------------------------------------

Passo "Esperando o servidor ($servidor) anunciar a $versao (ate 6 minutos)"

$anunciou = $false

for ($i = 0; $i -lt 36; $i++)
{
    try
    {
        $resposta = Invoke-WebRequest -Uri "$servidor/versao" -UseBasicParsing -TimeoutSec 20

        if ($resposta.Content -match "versao=$([regex]::Escape($versao))") { $anunciou = $true; break }
    }
    catch { }

    Start-Sleep -Seconds 10
}

Write-Host ""

if ($anunciou)
{
    Write-Host "Colae $versao publicado!" -ForegroundColor Green
    Write-Host "Quem tem o Colae instalado ve o botao Atualizar ao abrir (ou em ate 6 horas)."
}
else
{
    Write-Host "Release publicado, mas o servidor ainda nao anuncia a $versao." -ForegroundColor Yellow
    Write-Host "Confira o deploy no painel do Render e depois: $servidor/versao"
}

Write-Host "Release: $repositorio/releases/tag/v$versao"
