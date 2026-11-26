// Colaê - tela, som e voz direto entre amigos
// Copyright (c) 2026 Nikao1 (github.com/Nikao1) - licença MIT (ver LICENSE)

#pragma once

#include <string>

// ============================================================
// ATUALIZACOES
// ============================================================
//
// 1. Pergunta ao servidor configurado qual a ultima versao
//    (GET /versao). So isso sai do computador: o servidor ve o
//    IP (como ja ve ao entrar numa sala) - nada do que a pessoa
//    faz no programa.
// 2. Versao nova + o programa instalado pelo instalador + chave
//    publica embutida (update_key.h): "Atualizar" baixa o
//    instalador do GitHub, CONFERE a assinatura (update_signature)
//    e roda ele em silencio; o programa fecha e o instalador abre
//    a versao nova.
// 3. Sem chave, ou rodando do .zip: so avisa e abre a pagina de
//    downloads.
//
// Tudo em threads proprias; a interface so consulta o estado.
// ============================================================

namespace updater
{

enum class Status
{
    Idle,        // nunca procurou
    Checking,
    UpToDate,
    Available,   // ha versao nova (ver GetLatestVersion)
    Downloading,
    Ready,       // baixado e conferido: chamar LaunchInstaller
    Failed       // ver ErrorText (procurar de novo limpa)
};

// Procura versao nova (ignorado se ja estiver procurando ou
// baixando).
void CheckAsync();

Status GetStatus();

// Versao anunciada (vazia se nao houver).
std::string GetLatestVersion();

// Notas da versao (texto curto, pode ser vazio).
std::string GetNotes();

std::string ErrorText();

// 0..1 durante o download.
float DownloadProgress();

// true = da pra atualizar sozinho (instalado pelo instalador, com
// chave e link valido). false = so "Baixar" (abre a pagina).
bool CanAutoInstall();

// Baixa e confere o instalador (Status::Downloading -> Ready ou
// Failed).
void StartDownload();

// Roda o instalador conferido em silencio. true = rodou: o
// programa deve fechar logo (o instalador espera ele sair e abre
// a versao nova).
bool LaunchInstaller();

// Abre a pagina da versao (ou a de downloads) no navegador.
void OpenReleasePage();

// a > b ("0.8.12" > "0.8.9").
bool IsNewerVersion(
    const std::string& a,
    const std::string& b
);

// Encerrar (espera as threads).
void Shutdown();

} // namespace updater
