# Hospedar o seu próprio servidor de sinalização

O servidor de sinalização só **apresenta as pessoas umas às outras**: troca os
endereços de internet para os computadores se conectarem direto. Tela, som e
voz nunca passam por ele, e nomes, chat, fotos e prévias chegam criptografados
(ele não consegue ler). Mesmo assim, se você prefere não depender do servidor
público, dá para rodar o seu — é um único arquivo Python, sem dependências.

**Só uma pessoa do grupo precisa hospedar.** Quem entra pelo convite (link ou
código) vai automaticamente para o servidor de quem criou a sala.

## 1. Escolher onde rodar

| Opção | Bom para | Custo |
|---|---|---|
| [Render](#render-gratuito) | Não ter nada ligado em casa | Grátis (o servidor "dorme" e leva ~1 min para acordar) |
| [PC ou Raspberry Pi em casa](#em-casa-pc-ou-raspberry-pi) | Controle total | Grátis (precisa ficar ligado e abrir uma porta no roteador) |
| [Docker / VPS](#docker) | Quem já tem um servidor | Depende da hospedagem |

Requisito em todos os casos: **Python 3.8 ou mais novo** (o Docker já traz).

### Render (gratuito)

1. Faça um *fork* deste repositório no GitHub.
2. No [Render](https://render.com): **New → Blueprint** e escolha o seu fork. O
   arquivo `render.yaml` já configura tudo.
3. Quando terminar, o Render mostra o endereço, algo como
   `https://seu-servico.onrender.com`.

### Em casa (PC ou Raspberry Pi)

1. Baixe o arquivo `signaling/signaling_server.py`.
2. Rode:

   ```bash
   python signaling_server.py          # porta 8080
   python signaling_server.py 9000     # outra porta
   ```

3. **Só a sua rede (LAN party, mesma casa):** já funciona. Use
   `http://IP-DO-PC:8080` (ex.: `http://192.168.0.10:8080`). Libere a porta no
   Firewall do Windows se ele perguntar.
4. **Amigos de fora:** no roteador, encaminhe a porta (ex.: 8080/TCP) para o PC
   que roda o servidor (*port forwarding*). O endereço passa a ser
   `http://SEU-IP-PUBLICO:8080`.
   - Seu IP público muda de vez em quando? Use um DNS dinâmico gratuito
     (DuckDNS, No-IP...) para ter um nome fixo, como `http://meucolae.duckdns.org:8080`.
   - Algumas operadoras usam CGNAT e não deixam abrir porta. Nesse caso, use o
     Render ou um VPS.

Para deixar sempre ligado no Windows, crie uma tarefa no Agendador de Tarefas
que rode o comando acima ao iniciar. No Linux/Raspberry Pi, um serviço do
systemd faz o mesmo.

### Docker

```bash
docker build -t colae-sinalizacao signaling
docker run -d --restart unless-stopped -p 8080:8080 colae-sinalizacao
```

Atrás de um proxy reverso (nginx, Caddy, Traefik...), defina `TRUST_PROXY=1`
para o servidor enxergar o IP real de quem conecta.

## 2. Usar no Colaê

1. Abra o Colaê → engrenagem → **Geral → Servidor de sinalização**.
2. Cole o endereço (ex.: `https://seu-servico.onrender.com` ou
   `http://192.168.0.10:8080`), clique em **Testar** e depois em **Usar**.
3. Crie a sala e mande o convite normalmente. O convite leva o servidor junto:
   quem entra não precisa configurar nada.

Para voltar ao servidor público, clique em **Voltar ao padrão**.

## Privacidade e segurança

- O servidor guarda tudo **só em memória** e apaga quando a sala fecha. Por
  padrão ele **não registra** IPs nem códigos de sala no log (para depurar,
  `LOG_REQUESTS=1` liga o registro).
- **HTTPS ou HTTP?** Num servidor em casa sem HTTPS, alguém observando a rede
  conseguiria ver o código curto da sala e os IPs — nunca o conteúdo, que
  continua criptografado de ponta a ponta. Para HTTPS em casa, coloque o
  servidor atrás de um proxy como o Caddy, que gera o certificado sozinho.
- Variáveis de ambiente úteis: `PORT` (porta), `TRUST_PROXY=1` (atrás de
  proxy), `LOG_REQUESTS=1` (log de depuração).
