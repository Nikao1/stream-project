#pragma once

// ============================================================
// ENERGIA
// ============================================================
//
// Notebooks economizam energia "freando" programas: o Windows
// pode baixar o clock da CPU pro processo (EcoQoS / modo de
// eficiencia), apagar a tela ou suspender o PC no meio da
// transmissao - ele nao sabe que tem video rolando, porque o
// usuario nao mexe no mouse nem no teclado.
// ============================================================

// Pede ao Windows pra nao frear este processo (desempenho
// total mesmo na bateria/economia). Chamar uma vez no inicio.
void PowerDisableThrottling();

// true = transmitindo/assistindo: nao apagar a tela nem
// suspender. false = volta ao normal. Chamar sempre da MESMA
// thread (a da janela) - o Windows guarda isso por thread.
// Pode ser chamada a cada frame: so age quando muda.
void PowerSetActive(
    bool active
);
