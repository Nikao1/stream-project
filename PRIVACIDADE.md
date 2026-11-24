# Privacidade

O Colaê foi feito para o seu conteúdo **não passar por ninguém além de quem
está na sala**. Este texto explica, sem juridiquês, o que fica com quem.

## O que vai direto entre vocês (nunca passa pelo servidor)

- **A tela e o som transmitidos.** Vão de um computador para o outro,
  criptografados.
- **A voz.** Também vai direto entre as pessoas da sala, criptografada.

## O que passa pelo servidor, mas criptografado (ele não consegue ler)

O código da sala tem uma parte secreta (depois do traço) que **nunca é enviada
ao servidor**. A chave que tranca os dados sai dessa parte. Por isso o servidor
guarda, sem conseguir ler:

- os nomes das pessoas e o nome da sala;
- as mensagens do chat;
- as fotos de perfil e as prévias pequenas das transmissões;
- a descrição do que cada um compartilha.

No link de convite, o código vai depois do `#` — essa parte fica só no
navegador e também não chega ao servidor.

## O que o servidor vê

Para conseguir apresentar as pessoas umas às outras, o servidor precisa ver:

- o código curto da sala (a parte antes do traço);
- quantas pessoas estão na sala e quem está transmitindo ou na voz (sem saber
  os nomes);
- os **endereços de internet (IP e porta)** de cada um, para que os
  computadores consigam se conectar direto.

## Quanto tempo fica guardado

Nada vai para banco de dados nem para arquivo. Tudo fica só na memória do
servidor e some quando a pessoa sai ou a sala fecha (a sala fecha quando todos
saem). O servidor não guarda registro de uso.

## O que fica no seu computador

Na pasta `%APPDATA%\Colae`: seu nome, sua foto, suas configurações e a lista
das últimas salas em que você entrou (com o código completo, para você voltar
com um clique). Nada disso é enviado a ninguém. Para apagar, é só apagar a
pasta.

## Servidor

O servidor de sinalização é o arquivo `signaling/signaling_server.py` deste
repositório — dá para conferir o que ele faz linha por linha. Por padrão ele
não registra IPs nem códigos de sala nem no log da hospedagem.

Prefere não usar o servidor público? Hospede o seu (veja `HOSPEDAR.md`) e
escolha-o em Configurações → Geral → Servidor de sinalização.

## Contato

Dúvidas sobre privacidade: abra uma issue em
https://github.com/Nikao1/stream-project/issues
