# Colae - Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licenca MIT
<#
.SYNOPSIS
    Prepara uma versao nova do Colae para a atualizacao automatica.

.DESCRIPTION
    1. Roda o tools\empacotar.ps1 (compila, monta o .zip e o
       instalador).
    2. Assina o instalador com a sua chave privada
       (colae-assinar, compilado junto).
    3. Escreve signaling\versao.txt - o que o servidor responde em
       /versao (versao, link, resumo e assinatura).
    4. Mostra o que falta: publicar o release no GitHub e, DEPOIS,
       subir o versao.txt (o Render atualiza o servidor sozinho).

    Veja PUBLICAR.md para o passo a passo completo.

.PARAMETER Chave
    Arquivo da chave privada (criado com
    "colae-assinar gerar-chave"). Padrao: variavel de ambiente
    COLAE_CHAVE.

.PARAMETER Notas
    Texto curto das novidades (aparece na confirmacao da
    atualizacao). Opcional.

.PARAMETER SemCompilar
    Repassado ao empacotar.ps1 (usa o que ja esta compilado).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\publicar.ps1 -Chave D:\chaves\colae.chave -Notas "Botao de atualizar e tela Sobre"
#>

param(
    [string] $Chave = $env:COLAE_CHAVE,
    [string] $Notas = "",
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

$projeto = Split-Path -Parent $PSScriptRoot

Set-Location $projeto

if (-not $Chave -or -not (Test-Path $Chave))
{
    Falhar "Informe a chave privada: -Chave C:\caminho\colae.chave (ou a variavel COLAE_CHAVE). Para criar: veja PUBLICAR.md."
}

$repositorio = "https://github.com/Nikao1/stream-project"

# ------------------------------------------------------------
# 1. Empacotar (compila tambem o colae-assinar)
# ------------------------------------------------------------

$argumentos = @("-ExecutionPolicy", "Bypass", "-File", (Join-Path $PSScriptRoot "empacotar.ps1"), "-PastaBuild", $PastaBuild)

if ($SemCompilar) { $argumentos += "-SemCompilar" }

& powershell @argumentos

if ($LASTEXITCODE -ne 0) { Falhar "O empacotar.ps1 falhou." }

$cmakeTexto = Get-Content (Join-Path $projeto "CMakeLists.txt") -Raw

$versao = [regex]::Match(
    $cmakeTexto,
    "project\s*\(\s*stream-[a-z]+\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)").Groups[1].Value

$instalador = Join-Path $projeto "dist\Instalar-Colae-$versao.exe"
$zip = Join-Path $projeto "dist\Colae-$versao-windows.zip"

if (-not (Test-Path $instalador))
{
    Falhar "Nao achei $instalador. A atualizacao automatica precisa do instalador (Inno Setup)."
}

# ------------------------------------------------------------
# 2. Assinar
# ------------------------------------------------------------

Passo "Assinando o instalador"

$assinar = Get-ChildItem -Path (Join-Path $projeto $PastaBuild) -Recurse -Filter "colae-assinar.exe" -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1

if (-not $assinar) { Falhar "Nao achei o colae-assinar.exe na pasta $PastaBuild (compile o projeto)." }

$saida = & $assinar.FullName assinar $Chave $instalador $versao

if ($LASTEXITCODE -ne 0) { Falhar "O colae-assinar falhou." }

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
    Falhar "A chave publica em src\update_key.h nao e a desta chave privada. Cole la: $($campos['publica']) e compile de novo."
}

# ------------------------------------------------------------
# 3. signaling\versao.txt
# ------------------------------------------------------------

Passo "Escrevendo signaling\versao.txt"

$nomeInstalador = Split-Path -Leaf $instalador

$texto =
    "# Ultima versao do Colae (gerado pelo tools\publicar.ps1)`n" +
    "versao=$versao`n" +
    "url=$repositorio/releases/download/v$versao/$nomeInstalador`n" +
    "pagina=$repositorio/releases/tag/v$versao`n" +
    "hash=$($campos['hash'])`n" +
    "assinatura=$($campos['assinatura'])`n"

if ($Notas) { $texto += "notas=$($Notas -replace "[`r`n]+", ' ')`n" }

$arquivoVersao = Join-Path $projeto "signaling\versao.txt"

[IO.File]::WriteAllText($arquivoVersao, $texto, (New-Object System.Text.UTF8Encoding($false)))

# ------------------------------------------------------------
# 4. O que falta
# ------------------------------------------------------------

Write-Host ""
Write-Host "Pronto para publicar a $versao!" -ForegroundColor Green
Write-Host ""
Write-Host "1) Crie o release v$versao no GitHub com estes arquivos:"
Write-Host "     $instalador"
if (Test-Path $zip) { Write-Host "     $zip" }
Write-Host "   Pelo site: $repositorio/releases/new (tag: v$versao)"

if (Get-Command gh -ErrorAction SilentlyContinue)
{
    Write-Host "   Ou pelo GitHub CLI:"
    Write-Host "     gh release create v$versao `"$instalador`" `"$zip`" --title `"Colae $versao`" --notes `"$Notas`""
}

Write-Host ""
Write-Host "2) SO DEPOIS do release publicado, suba o signaling\versao.txt:"
Write-Host "     git add signaling/versao.txt"
Write-Host "     git commit -m `"Release $versao`""
Write-Host "     git push"
Write-Host "   O Render atualiza o servidor e os Colae abertos passam a ver a versao nova."
