<#
.SYNOPSIS
    Monta o pacote para distribuir o Chegae (um .zip pronto
    para mandar para outras pessoas).

.DESCRIPTION
    1. Compila o projeto em Release (a menos que use -SemCompilar).
    2. Junta numa pasta:
         Chegae.exe    (assistir e transmitir)
         ffmpeg.exe           (o verdadeiro - nao um atalho)
         LEIA-ME.txt
    3. Compacta em dist\Chegae-<versao>-windows.zip e mostra
       o SHA-256 do arquivo.

    O ffmpeg.exe e conferido antes de entrar no pacote: atalhos
    criados por Chocolatey/Scoop/winget (poucos KB) e versoes que
    dependem de DLLs ao lado sao recusados - foi o que deixou a
    tela preta no primeiro teste.

.PARAMETER Ffmpeg
    Caminho do ffmpeg.exe a incluir. Sem ele, o script procura no
    PATH e nas pastas de instalacao do Chocolatey, Scoop e winget.

.PARAMETER SemCompilar
    Usa os executaveis ja compilados, sem rodar o CMake.

.PARAMETER PastaBuild
    Pasta de build do CMake (padrao: build).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\empacotar.ps1

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\empacotar.ps1 -Ffmpeg C:\ffmpeg\bin\ffmpeg.exe
#>

param(
    [string] $Ffmpeg = "",
    [switch] $SemCompilar,
    [string] $PastaBuild = "build"
)

$ErrorActionPreference = "Stop"

# Mensagens sem acento de proposito: o PowerShell 5 (o que vem no
# Windows) le scripts sem BOM como ANSI e trocaria os acentos.

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


# ------------------------------------------------------------
# Pasta do projeto (a pasta acima de tools\)
# ------------------------------------------------------------

$projeto = Split-Path -Parent $PSScriptRoot

Set-Location $projeto


# ------------------------------------------------------------
# Versao: lida do CMakeLists.txt, a mesma que vai nos .exe
# ------------------------------------------------------------

$cmakeTexto = Get-Content (Join-Path $projeto "CMakeLists.txt") -Raw

$achou = [regex]::Match(
    $cmakeTexto,
    "project\s*\(\s*stream-[a-z]+\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)")

if (-not $achou.Success)
{
    Falhar "Nao achei 'VERSION x.y.z' no project() do CMakeLists.txt."
}

$versao = $achou.Groups[1].Value

Write-Host "Chegae $versao"


# ------------------------------------------------------------
# 1. Compilar
# ------------------------------------------------------------

if (-not $SemCompilar)
{
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue))
    {
        Falhar "cmake nao encontrado no PATH."
    }

    Passo "Configurando (cmake)"

    & cmake -S . -B $PastaBuild

    if ($LASTEXITCODE -ne 0) { Falhar "cmake -S . -B $PastaBuild falhou." }

    Passo "Compilando em Release"

    & cmake --build $PastaBuild --config Release

    if ($LASTEXITCODE -ne 0) { Falhar "A compilacao falhou. Veja os erros acima." }
}


# ------------------------------------------------------------
# 2. Executaveis
# ------------------------------------------------------------

function Achar-Exe([string] $nome)
{
    # Visual Studio poe em build\Release; Ninja/Makefiles, direto
    # em build\.
    foreach ($pasta in @(
        (Join-Path $PastaBuild "Release"),
        $PastaBuild))
    {
        $caminho = Join-Path $pasta $nome

        if (Test-Path $caminho) { return (Resolve-Path $caminho).Path }
    }

    Falhar "Nao achei $nome em $PastaBuild\Release nem em $PastaBuild. Compile antes (ou tire o -SemCompilar)."
}

Passo "Procurando os executaveis"

$programa = Achar-Exe "Chegae.exe"

Write-Host "  $programa"


# ------------------------------------------------------------
# 3. ffmpeg.exe verdadeiro
# ------------------------------------------------------------

$TAMANHO_MINIMO = 10MB

function Resolver-Atalho([string] $caminho)
{
    # Links simbolicos (winget) apontam para o arquivo real.
    $item = Get-Item $caminho -ErrorAction SilentlyContinue

    if ($item -and $item.LinkType -and $item.Target)
    {
        $alvo = @($item.Target)[0]

        if (-not [System.IO.Path]::IsPathRooted($alvo))
        {
            $alvo = Join-Path (Split-Path -Parent $caminho) $alvo
        }

        return $alvo
    }

    return $caminho
}

function Candidatos-Ffmpeg
{
    $lista = New-Object System.Collections.Generic.List[string]

    if ($Ffmpeg) { $lista.Add($Ffmpeg) }

    $noPath = Get-Command ffmpeg -ErrorAction SilentlyContinue

    if ($noPath) { $lista.Add((Resolver-Atalho $noPath.Source)) }

    # Onde os gerenciadores de pacote guardam o programa de verdade
    # (o que fica no PATH costuma ser so um atalho).
    if ($env:ProgramData)
    {
        $lista.Add((Join-Path $env:ProgramData "chocolatey\lib\ffmpeg\tools\ffmpeg\bin\ffmpeg.exe"))
        $lista.Add((Join-Path $env:ProgramData "chocolatey\lib\ffmpeg-full\tools\ffmpeg\bin\ffmpeg.exe"))
    }

    if ($env:USERPROFILE)
    {
        $lista.Add((Join-Path $env:USERPROFILE "scoop\apps\ffmpeg\current\bin\ffmpeg.exe"))
    }

    if ($env:LOCALAPPDATA)
    {
        $winget = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"

        if (Test-Path $winget)
        {
            Get-ChildItem $winget -Directory -Filter "Gyan.FFmpeg*" -ErrorAction SilentlyContinue |
                ForEach-Object {
                    Get-ChildItem $_.FullName -Recurse -Filter "ffmpeg.exe" -ErrorAction SilentlyContinue |
                        ForEach-Object { $lista.Add($_.FullName) }
                }
        }
    }

    return $lista
}

Passo "Procurando o ffmpeg.exe"

$ffmpegOk = $null

foreach ($candidato in (Candidatos-Ffmpeg))
{
    if (-not $candidato -or -not (Test-Path $candidato)) { continue }

    $arquivo = Get-Item $candidato

    if ($arquivo.Length -lt $TAMANHO_MINIMO)
    {
        Write-Host ("  ignorado (so {0:N0} KB - atalho ou versao com DLLs): {1}" -f ($arquivo.Length / 1KB), $candidato) -ForegroundColor Yellow
        continue
    }

    # Precisa rodar sozinho (sem DLLs ao lado).
    & $arquivo.FullName -hide_banner -version *> $null

    if ($LASTEXITCODE -ne 0)
    {
        Write-Host "  ignorado (nao executou): $candidato" -ForegroundColor Yellow
        continue
    }

    $ffmpegOk = $arquivo.FullName

    break
}

if (-not $ffmpegOk)
{
    Falhar ("Nao achei um ffmpeg.exe completo. Baixe ffmpeg-release-essentials.zip em " +
            "https://www.gyan.dev/ffmpeg/builds/ e rode de novo com " +
            "-Ffmpeg <pasta>\bin\ffmpeg.exe")
}

Write-Host ("  {0} ({1:N0} MB)" -f $ffmpegOk, ((Get-Item $ffmpegOk).Length / 1MB))


# ------------------------------------------------------------
# 4. Montar a pasta e o .zip
# ------------------------------------------------------------

$nome = "Chegae-$versao"
$dist = Join-Path $projeto "dist"
$pasta = Join-Path $dist $nome
$zip = Join-Path $dist "$nome-windows.zip"

Passo "Montando $nome"

if (Test-Path $pasta) { Remove-Item $pasta -Recurse -Force }
if (Test-Path $zip) { Remove-Item $zip -Force }

New-Item -ItemType Directory -Path $pasta -Force | Out-Null

Copy-Item $programa (Join-Path $pasta "Chegae.exe")
Copy-Item $ffmpegOk (Join-Path $pasta "ffmpeg.exe")

# LEIA-ME com a versao preenchida. Gravado em UTF-8 com BOM, pra o
# Bloco de Notas mostrar os acentos em qualquer Windows.
$leiaMe = [System.IO.File]::ReadAllText(
    (Join-Path $PSScriptRoot "LEIA-ME.txt"),
    [System.Text.Encoding]::UTF8)

$leiaMe = $leiaMe.Replace("{VERSAO}", $versao)

[System.IO.File]::WriteAllText(
    (Join-Path $pasta "LEIA-ME.txt"),
    $leiaMe,
    (New-Object System.Text.UTF8Encoding($true)))

Compress-Archive -Path $pasta -DestinationPath $zip

$hash = (Get-FileHash $zip -Algorithm SHA256).Hash

Write-Host ""
Write-Host "Pronto!" -ForegroundColor Green
Write-Host ("  {0} ({1:N0} MB)" -f $zip, ((Get-Item $zip).Length / 1MB))
Write-Host "  SHA-256: $hash"
Write-Host ""
Write-Host "Mande o .zip inteiro (nao o .exe solto)."
