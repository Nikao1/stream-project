#!/usr/bin/env python3
"""
Chegae - servidor de sinalizacao

Servidor HTTP minimo que so faz UMA coisa: apresentar as duas
pontas de uma sessao. Ele NUNCA ve video nem audio - guarda
apenas "codigo da sessao -> endereco de quem transmite".

Sem dependencias: usa so a biblioteca padrao do Python 3.8+.

Uso:
    python signaling_server.py            (porta 8080)
    python signaling_server.py 9000       (outra porta)
    (ou variavel de ambiente PORT, usada por hospedagens gratuitas)

Protocolo (requisicoes em formato de formulario "a=1&b=2",
respostas em texto "chave=valor", uma por linha):

    GET  /health
    POST /sessions                    local_ip, video_port, audio_port
                                      [stun_ip, public_video_port,
                                       public_audio_port, host_key,
                                       app_version]
                                      -> code, token, ttl
    POST /sessions/<code>/keepalive   token
                                      -> ok, viewers
                                         [viewer_video, viewer_audio]
                                         (listas "ip:porta,ip:porta")
    POST /sessions/<code>/join                     -> endereco do host
    POST /sessions/<code>/announce    channel (video|audio), ip, port
                                      -> ok
    POST /sessions/<code>/close       token        -> ok

Salas (varias pessoas; cada uma pode transmitir, e quem assiste
escolhe quem ver). A sala so guarda quem esta nela e o codigo de
SESSAO de cada um que transmite - quem assiste entra nessa
sessao pelo fluxo normal acima ("join"/"announce"):

    POST /rooms                       name, [e2e=1, salt, title]
                                      -> code, member_id, token,
                                         ttl + estado da sala
    POST /rooms/<code>/info           -> e2e (0|1), [salt]
    GET  /j                           pagina do link de convite
                                      (/j#RWRSAP-K7F9QM2X abre o
                                      programa; o codigo depois do
                                      # nao chega aqui)
    POST /rooms/<code>/join           name, [e2e=1]
                                      -> member_id, token, ttl +
                                         estado da sala

    Criptografia de ponta a ponta (e2e=1): o programa tranca
    nomes, descricoes, chat e previas com uma chave que vem de
    um segredo que NUNCA chega aqui (vai so no codigo que as
    pessoas passam entre si). Este servidor guarda esses campos
    como texto opaco ("e1.<base64>") e nao consegue le-los. So o
    "sal" da chave fica aqui (nao e segredo). Sala e2e recusa
    quem nao sabe criptografar (versoes antigas): HTTP 426.
    POST /rooms/<code>/heartbeat      member_id, token
                                      -> estado da sala
    POST /rooms/<code>/present        member_id, token,
                                      session_code, session_token,
                                      [label]      -> ok
    POST /rooms/<code>/stop           member_id, token -> ok
    POST /rooms/<code>/leave          member_id, token -> ok
    POST /rooms/<code>/chat           member_id, token, text
                                      -> ok, id
    POST /rooms/<code>/title          member_id, token, [title]
                                      -> ok
        Nome da sala (opcional; trancado nas salas e2e). Sem
        title = sala sem nome.
    POST /rooms/<code>/voice          member_id, token,
                                      [session_code, session_token]
                                      -> ok
        Canal de voz: cada pessoa na voz abre uma sessao (so de
        audio) e publica aqui; as outras entram nela pelo fluxo
        normal de sessao. Sem session_code = saiu da voz.
    POST /rooms/<code>/thumb          member_id, token, [image]
                                      -> ok
        Previa (JPEG pequeno, base64 "urlsafe") do que esta pessoa
        transmite, pra lista da sala. Sem image = tira a previa.
        So em memoria; some quando a transmissao para.
    POST /rooms/<code>/thumb_get      member_id, token, target
                                      -> image, version
    POST /rooms/<code>/avatar         member_id, token, [image]
                                      -> ok
        Foto de perfil (JPEG pequeno, criptografado nas salas
        e2e). Sem image = tira. So em memoria; some quando a
        pessoa sai da sala.
    POST /rooms/<code>/avatar_get     member_id, token, target
                                      -> image, version

    O heartbeat aceita chat_after=<id> e devolve tambem as
    mensagens de chat mais novas que esse id:
        chat_count=N
        chat_<i>=<id>|<member_id>|<nome>|<texto>

    Estado da sala:
        room_title=<nome da sala, ou vazio>
        member_count=N
        member_<i>=<member_id>|<nome>|<sessao de voz, ou vazio>|
                   <versao da foto, ou vazio>
        presenter_count=N
        presenter_<i>=<member_id>|<codigo da sessao>|<nome>|<o que
                      esta compartilhando>|<versao da previa,
                      ou vazio>
    (nome e descricao com %XX, como em URL)

Hole punching: o host registra seu endereco publico (descoberto
por STUN) ao criar a sessao; o receiver pega esse endereco no
"join" e anuncia o proprio com "announce"; o host recebe o
endereco do receiver na resposta do "keepalive".
"""

import base64
import binascii
import ipaddress
import os
import secrets
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, quote

# ------------------------------------------------------------
# Configuracao
# ------------------------------------------------------------

SESSION_TTL_SECONDS = 60        # sessao some se o host parar de dar keepalive
MAX_SESSIONS = 5000
MAX_BODY_BYTES = 4096
CODE_LENGTH = 6

# Anuncio de espectador (endereco publico pro hole punching):
# o host so precisa dele por alguns segundos, ate o HELLO do
# espectador passar. Depois disso o anuncio some sozinho.
ANNOUNCE_TTL_SECONDS = 60
MAX_ANNOUNCES_PER_SESSION = 16

# Sem 0/O, 1/I/L: evita confusao ao ditar o codigo pra alguem.
CODE_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"

# Limite de requisicoes por IP (dificulta adivinhar codigos).
# Varias pessoas da mesma casa saem pelo mesmo IP, e cada uma
# numa sala manda ~2 requisicoes a cada 2 s (sala + sessao).
# 600/min ainda deixa adivinhar um codigo (31^6 possibilidades)
# fora de questao.
RATE_LIMIT_WINDOW_SECONDS = 60
RATE_LIMIT_MAX_REQUESTS = 600

# Salas
ROOM_MEMBER_TTL_SECONDS = 20    # some quem parar de mandar heartbeat
MAX_ROOMS = 2000
MAX_ROOM_MEMBERS = 10         # voz: 9 conexoes por pessoa; transmissao: ate 8 assistindo
MAX_NAME_LENGTH = 32
MAX_LABEL_LENGTH = 80
MAX_CHAT_LENGTH = 300
CHAT_HISTORY = 50               # mensagens guardadas por sala (memoria)
MAX_THUMB_BYTES = 30000         # previa da transmissao (JPEG pequeno)

# Campos criptografados (salas e2e): "e1." + base64url. O texto
# original continua limitado pelo programa; aqui so o tamanho
# do embrulho.
MAX_SEALED_NAME = 400
MAX_SEALED_LABEL = 800
MAX_SEALED_CHAT = 2400
MAX_THUMB_BODY_BYTES = 44000    # o JPEG em base64 + campos
MAX_AVATAR_BYTES = 12000        # foto de perfil (96x96)

# So confie em X-Forwarded-For quando o servidor estiver atras
# de um proxy da hospedagem (Render, Fly etc.): TRUST_PROXY=1
TRUST_PROXY = os.environ.get("TRUST_PROXY", "0") == "1"

# ------------------------------------------------------------
# Estado (em memoria - sessoes sao temporarias por natureza)
# ------------------------------------------------------------

_lock = threading.Lock()
_sessions = {}      # code -> dict
_rooms = {}         # code -> {"members": {member_id: dict}}
_rate = {}          # ip -> [inicio_da_janela, contagem]


def _now():
    return time.monotonic()


def _purge_expired():
    """Chamar com _lock adquirido."""
    now = _now()
    expired = [c for c, s in _sessions.items() if s["expires"] <= now]
    for code in expired:
        del _sessions[code]

    # Salas: sai quem parou de dar sinal de vida; sala vazia
    # some. Quem transmitia e a sessao dele expirou volta a so
    # assistir.
    for code in list(_rooms):
        members = _rooms[code]["members"]
        for member_id in list(members):
            member = members[member_id]
            if now - member["last_seen"] > ROOM_MEMBER_TTL_SECONDS:
                del members[member_id]
            else:
                if (member["presenting"] is not None and
                        member["presenting"]["session_code"]
                        not in _sessions):
                    member["presenting"] = None
                if (member.get("voice") and
                        member["voice"] not in _sessions):
                    member["voice"] = None
        if not members:
            del _rooms[code]

    stale = [ip for ip, (start, _) in _rate.items()
             if now - start > RATE_LIMIT_WINDOW_SECONDS]
    for ip in stale:
        del _rate[ip]


def _rate_limited(ip):
    """Chamar com _lock adquirido."""
    now = _now()
    entry = _rate.get(ip)
    if entry is None or now - entry[0] > RATE_LIMIT_WINDOW_SECONDS:
        _rate[ip] = [now, 1]
        return False
    entry[1] += 1
    return entry[1] > RATE_LIMIT_MAX_REQUESTS


def _valid_ipv4(text):
    try:
        return isinstance(ipaddress.ip_address(text), ipaddress.IPv4Address)
    except ValueError:
        return False


def _same_network(ip_a, ip_b):
    """As duas requisicoes vieram de tras do mesmo roteador?

    IPv4: mesmo IP publico. IPv6: cada maquina da casa tem um
    endereco proprio, mas todas dividem o mesmo prefixo /64.
    """
    try:
        a = ipaddress.ip_address(ip_a)
        b = ipaddress.ip_address(ip_b)
    except ValueError:
        return ip_a == ip_b
    if a.version != b.version:
        return False
    if a.version == 6:
        return (int(a) >> 64) == (int(b) >> 64)
    return a == b


def _valid_port(text):
    return text.isdigit() and 1 <= int(text) <= 65535


def _clean_text(text, limit):
    """Nome/descricao vindos do programa: sem caracteres de
    controle, sem espacos sobrando, com tamanho limitado."""
    text = "".join(c for c in text if c.isprintable())
    return " ".join(text.split())[:limit]


_SEALED_CHARS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
                    "0123456789-_=")


def _sealed(text, limit):
    """Campo criptografado (salas e2e) valido, ou None."""
    if (text.startswith("e1.") and 3 < len(text) <= limit and
            all(c in _SEALED_CHARS for c in text[3:])):
        return text
    return None


def _field(room, text, plain_limit, sealed_limit):
    """Nome/descricao/chat: sala e2e so aceita o campo trancado;
    sala comum, texto limpo."""
    if room.get("e2e"):
        return _sealed(text, sealed_limit)
    return _clean_text(text, plain_limit)


def _new_code():
    """Chamar com _lock adquirido. Salas e sessoes dividem os
    codigos: um codigo digitado e de uma coisa so."""
    while True:
        code = "".join(secrets.choice(CODE_ALPHABET)
                       for _ in range(CODE_LENGTH))
        if code not in _sessions and code not in _rooms:
            return code


def _room_chat(room, after):
    """Chamar com _lock adquirido. Mensagens com id > after."""
    fields = {}
    messages = [m for m in room["chat"] if m["id"] > after]
    fields["chat_count"] = len(messages)
    for i, m in enumerate(messages):
        fields[f"chat_{i}"] = "|".join([
            str(m["id"]), m["member_id"],
            quote(m["name"]), quote(m["text"])])
    return fields


def _room_state(room):
    """Chamar com _lock adquirido. Campos do estado da sala."""
    fields = {"room_title": quote(room.get("title") or "")}
    members = sorted(room["members"].items(),
                     key=lambda item: item[1]["joined"])
    fields["member_count"] = len(members)
    presenters = []
    for i, (member_id, member) in enumerate(members):
        fields[f"member_{i}"] = "|".join([
            member_id, quote(member["name"]), member.get("voice") or "",
            str(member.get("avatar_version") or "")])
        presenting = member["presenting"]
        if presenting is not None:
            presenters.append((presenting["since"], member_id,
                               member["name"], presenting))
    presenters.sort(key=lambda p: p[0])
    fields["presenter_count"] = len(presenters)
    for i, (_, member_id, name, presenting) in enumerate(presenters):
        fields[f"presenter_{i}"] = "|".join([
            member_id,
            presenting["session_code"],
            quote(name),
            quote(presenting["label"]),
            str(presenting.get("thumb_version") or ""),
        ])
    return fields


# Pagina do link de convite (GET /j#RWRSAP-K7F9QM2X). O codigo vai
# depois do "#": o navegador NUNCA manda essa parte pro servidor -
# a pagina (no navegador) le e abre o programa por
# chegae://sala/<codigo>.
INVITE_PAGE = """<!doctype html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="robots" content="noindex">
<title>Convite - Chega\u00ea</title>
<style>
  body { margin: 0; min-height: 100vh; display: flex;
         align-items: center; justify-content: center;
         background: #0E1116; color: #E8EAED;
         font-family: "Segoe UI", system-ui, sans-serif; }
  main { max-width: 420px; margin: 24px; padding: 28px;
         background: #14171C; border: 1px solid #262A32;
         border-radius: 14px; }
  h1 { font-size: 22px; margin: 0 0 8px; }
  p { line-height: 1.45; margin: 12px 0; }
  .muted { color: #9aa0ab; font-size: 14px; }
  .btn { display: block; text-align: center; padding: 11px 14px;
         background: #FF7A45; color: #1A0E08; border-radius: 9px;
         text-decoration: none; font-weight: 600; }
  .row { display: flex; gap: 8px; align-items: center; }
  code { flex: 1; padding: 9px 12px; background: #0f1115;
         border: 1px solid #2a2d36; border-radius: 8px;
         font-size: 17px; letter-spacing: 1px; }
  button { padding: 9px 14px; background: #262933; color: #e8e9ec;
           border: 1px solid #343846; border-radius: 8px;
           cursor: pointer; font: inherit; }
</style>
</head>
<body>
<main>
  <h1>Convite para uma sala</h1>
  <p id="status">Abrindo o Chega\u00ea...</p>
  <p><a id="open" class="btn" href="#">Abrir no Chega\u00ea</a></p>
  <p class="muted">N\u00e3o abriu? Abra o programa e cole este c\u00f3digo
     na tela inicial:</p>
  <div class="row"><code id="code">-</code>
    <button id="copy" type="button">Copiar</button></div>
  <p class="muted">A parte depois do # fica s\u00f3 no seu navegador:
     ela n\u00e3o \u00e9 enviada a este servidor.</p>
</main>
<script>
(function () {
  var raw = (location.hash || "").slice(1).toUpperCase();
  var code = raw.replace(/[^A-Z0-9-]/g, "");
  var ok = /^[A-Z0-9]{6}-?[A-Z0-9]{8}$/.test(code) ||
           /^[A-Z0-9]{6}$/.test(code);
  var status = document.getElementById("status");
  var open = document.getElementById("open");
  var copy = document.getElementById("copy");
  if (!ok) {
    status.textContent = "Link de convite incompleto. Pe\u00e7a o link de novo.";
    open.style.display = "none";
    copy.disabled = true;
    return;
  }
  var url = "chegae://sala/" + code;
  open.href = url;
  document.getElementById("code").textContent = code;
  copy.onclick = function () {
    if (navigator.clipboard) {
      navigator.clipboard.writeText(code).then(function () {
        copy.textContent = "Copiado!";
      });
    }
  };
  setTimeout(function () { location.href = url; }, 300);
})();
</script>
</body>
</html>
"""


class Handler(BaseHTTPRequestHandler):
    server_version = "StreamSignaling/1.0"
    protocol_version = "HTTP/1.1"

    # -------------------------- util --------------------------

    def _client_ip(self):
        if TRUST_PROXY:
            forwarded = self.headers.get("X-Forwarded-For", "")
            first = forwarded.split(",")[0].strip()
            if first:
                return first
        return self.client_address[0]

    def _reply(self, status, fields):
        body = "".join(f"{k}={v}\n" for k, v in fields.items()).encode()
        self.send_response(status)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _error(self, status, message):
        self._reply(status, {"error": message})

    def _read_form(self):
        path = self.path.split("?")[0]
        limit = (MAX_THUMB_BODY_BYTES
                 if path.endswith("/thumb") or path.endswith("/avatar")
                 else MAX_BODY_BYTES)
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return None
        if length < 0 or length > limit:
            return None
        raw = self.rfile.read(length).decode("utf-8", "replace") if length else ""
        parsed = parse_qs(raw, keep_blank_values=True)
        return {k: v[0] for k, v in parsed.items()}

    def log_message(self, fmt, *args):
        sys.stderr.write("[%s] %s %s\n" % (
            time.strftime("%H:%M:%S"), self._client_ip(), fmt % args))

    # ------------------------- rotas --------------------------

    def do_GET(self):
        path = self.path.split("?")[0]
        if path in ("/j", "/j/"):
            return self._invite_page()
        if self.path == "/health":
            with _lock:
                _purge_expired()
                count = len(_sessions)
                rooms = len(_rooms)
            self._reply(200, {"ok": 1, "sessions": count,
                              "rooms": rooms})
        else:
            self._error(404, "not_found")

    def do_POST(self):
        ip = self._client_ip()
        form = self._read_form()
        if form is None:
            self.close_connection = True
            return self._error(400, "bad_request")

        with _lock:
            _purge_expired()
            limited = _rate_limited(ip)
        if limited:
            return self._error(429, "rate_limited")

        parts = [p for p in self.path.split("?")[0].split("/") if p]

        if parts == ["sessions"]:
            return self._create(ip, form)

        if len(parts) == 3 and parts[0] == "sessions":
            code = parts[1].upper()
            action = parts[2]
            if action == "join":
                return self._join(ip, code)
            if action == "announce":
                return self._announce(code, form)
            if action == "keepalive":
                return self._keepalive(code, form)
            if action == "close":
                return self._close(code, form)

        if parts == ["rooms"]:
            return self._room_create(form)

        if len(parts) == 3 and parts[0] == "rooms":
            code = parts[1].upper()
            action = parts[2]
            if action == "join":
                return self._room_join(code, form)
            if action == "info":
                return self._room_info(code)
            if action == "heartbeat":
                return self._room_heartbeat(code, form)
            if action == "present":
                return self._room_present(code, form)
            if action == "stop":
                return self._room_stop(code, form)
            if action == "leave":
                return self._room_leave(code, form)
            if action == "chat":
                return self._room_chat_send(code, form)
            if action == "title":
                return self._room_title(code, form)
            if action == "voice":
                return self._room_voice(code, form)
            if action == "thumb":
                return self._room_thumb(code, form)
            if action == "thumb_get":
                return self._room_thumb_get(code, form)
            if action == "avatar":
                return self._room_avatar(code, form)
            if action == "avatar_get":
                return self._room_avatar_get(code, form)

        self._error(404, "not_found")

    def _invite_page(self):
        body = INVITE_PAGE.encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "public, max-age=3600")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'none'; style-src 'unsafe-inline'; "
            "script-src 'unsafe-inline'; base-uri 'none'; "
            "form-action 'none'; frame-ancestors 'none'")
        self.end_headers()
        self.wfile.write(body)

    # ----------------------- operacoes ------------------------

    def _create(self, ip, form):
        local_ip = form.get("local_ip", "")
        video_port = form.get("video_port", "")
        audio_port = form.get("audio_port", "")

        if not (_valid_ipv4(local_ip) and _valid_port(video_port)
                and _valid_port(audio_port)):
            return self._error(400, "invalid_fields")

        # Endereco publico descoberto por STUN (opcional). Sem
        # ele, vale o IP de onde veio esta requisicao e as
        # portas locais.
        stun_ip = form.get("stun_ip", "")
        public_video_port = form.get("public_video_port", video_port)
        public_audio_port = form.get("public_audio_port", audio_port)

        if stun_ip and not _valid_ipv4(stun_ip):
            return self._error(400, "invalid_fields")
        if not (_valid_port(public_video_port)
                and _valid_port(public_audio_port)):
            return self._error(400, "invalid_fields")

        # Chave publica de quem transmite (64 digitos hex). O
        # servidor so repassa - nao consegue decifrar nada com
        # ela.
        host_key = form.get("host_key", "").lower()
        if host_key and not (len(host_key) == 64 and
                             all(c in "0123456789abcdef" for c in host_key)):
            return self._error(400, "invalid_fields")

        # Versao do programa de quem transmite (ex.: "0.2.0"),
        # repassada a quem entra pra avisar de versoes
        # diferentes. Opcional (versoes antigas nao mandam).
        app_version = form.get("app_version", "")
        if app_version and not (
                len(app_version) <= 20 and
                all(c.isalnum() or c in ".-" for c in app_version)):
            return self._error(400, "invalid_fields")

        with _lock:
            if len(_sessions) >= MAX_SESSIONS:
                return self._error(503, "server_full")

            code = _new_code()

            token = secrets.token_hex(16)
            _sessions[code] = {
                "token": token,
                "http_ip": ip,
                "public_ip": stun_ip or ip,
                "local_ip": local_ip,
                "video_port": int(video_port),
                "audio_port": int(audio_port),
                "public_video_port": int(public_video_port),
                "public_audio_port": int(public_audio_port),
                "host_key": host_key,
                "app_version": app_version,
                "viewers": 0,
                # anuncios: [canal, "ip:porta", momento]
                "announces": [],
                "expires": _now() + SESSION_TTL_SECONDS,
            }

        self._reply(200, {"code": code, "token": token,
                          "ttl": SESSION_TTL_SECONDS})

    def _join(self, ip, code):
        with _lock:
            session = _sessions.get(code)
            if session is None:
                return self._error(404, "session_not_found")
            session["viewers"] += 1
            info = dict(session)

        # Mesmo IP publico = as duas pontas estao atras do mesmo
        # roteador (ou na mesma maquina): o caminho certo e o IP
        # local do host, nao o publico.
        same_network = 1 if _same_network(ip, info["http_ip"]) else 0

        self._reply(200, {
            "host_public_ip": info["public_ip"],
            "host_local_ip": info["local_ip"],
            "video_port": info["video_port"],
            "audio_port": info["audio_port"],
            "public_video_port": info["public_video_port"],
            "public_audio_port": info["public_audio_port"],
            "same_network": same_network,
            "host_key": info["host_key"],
            "host_version": info["app_version"],
        })

    def _announce(self, code, form):
        channel = form.get("channel", "")
        viewer_ip = form.get("ip", "")
        viewer_port = form.get("port", "")

        if channel not in ("video", "audio"):
            return self._error(400, "invalid_fields")
        if not (_valid_ipv4(viewer_ip) and _valid_port(viewer_port)):
            return self._error(400, "invalid_fields")

        # Nada de multicast/broadcast/0.0.0.0: o host vai mandar
        # pacotes pra esse endereco.
        address = ipaddress.ip_address(viewer_ip)
        if (address.is_multicast or address.is_unspecified
                or address.is_reserved):
            return self._error(400, "invalid_fields")

        with _lock:
            session = _sessions.get(code)
            if session is None:
                return self._error(404, "session_not_found")
            endpoint = f"{viewer_ip}:{viewer_port}"
            announces = [a for a in session["announces"]
                         if not (a[0] == channel and a[1] == endpoint)]
            announces.append([channel, endpoint, _now()])
            session["announces"] = announces[-MAX_ANNOUNCES_PER_SESSION:]

        self._reply(200, {"ok": 1})

    def _owned_session(self, code, form):
        """Chamar com _lock adquirido. Devolve a sessao se o token confere."""
        session = _sessions.get(code)
        if session is None:
            return None
        if not secrets.compare_digest(session["token"], form.get("token", "")):
            return None
        return session

    def _keepalive(self, code, form):
        with _lock:
            session = self._owned_session(code, form)
            if session is None:
                return self._error(404, "session_not_found")
            session["expires"] = _now() + SESSION_TTL_SECONDS
            fields = {"ok": 1, "viewers": session["viewers"]}
            # Varios espectadores: lista separada por virgula.
            now = _now()
            session["announces"] = [
                a for a in session["announces"]
                if now - a[2] <= ANNOUNCE_TTL_SECONDS]
            for channel in ("video", "audio"):
                endpoints = [a[1] for a in session["announces"]
                             if a[0] == channel]
                if endpoints:
                    fields["viewer_" + channel] = ",".join(endpoints)
        self._reply(200, fields)

    def _close(self, code, form):
        with _lock:
            session = self._owned_session(code, form)
            if session is None:
                return self._error(404, "session_not_found")
            del _sessions[code]
        self._reply(200, {"ok": 1})


    # -------------------------- salas -------------------------

    def _room_name(self, form, e2e=False):
        if e2e:
            return _sealed(form.get("name", ""), MAX_SEALED_NAME)
        name = _clean_text(form.get("name", ""), MAX_NAME_LENGTH)
        return name or "Sem nome"

    def _room_title_field(self, form, e2e):
        """Nome da sala: vazio (sem nome) ou valido; None =
        invalido."""
        text = form.get("title", "")
        if not text:
            return ""
        if e2e:
            return _sealed(text, MAX_SEALED_NAME)
        return _clean_text(text, MAX_NAME_LENGTH)

    def _room_title(self, code, form):
        with _lock:
            room, member = self._room_member(code, form)
            if room is None:
                return self._error(404, "not_in_room")
            title = self._room_title_field(form, bool(room.get("e2e")))
            if title is None:
                return self._error(400, "invalid_fields")
            room["title"] = title
        self._reply(200, {"ok": 1})

    def _add_member(self, room, name):
        """Chamar com _lock adquirido."""
        member_id = secrets.token_hex(4)
        while member_id in room["members"]:
            member_id = secrets.token_hex(4)
        token = secrets.token_hex(16)
        now = _now()
        room["members"][member_id] = {
            "token": token,
            "name": name,
            "joined": now,
            "last_seen": now,
            "presenting": None,
            "voice": None,
        }
        return member_id, token

    def _room_member(self, code, form):
        """Chamar com _lock adquirido. (sala, membro) se o token
        confere; (None, None) se nao."""
        room = _rooms.get(code)
        if room is None:
            return None, None
        member = room["members"].get(form.get("member_id", ""))
        if member is None or not secrets.compare_digest(
                member["token"], form.get("token", "")):
            return None, None
        member["last_seen"] = _now()
        return room, member

    def _room_create(self, form):
        e2e = form.get("e2e") == "1"
        salt = form.get("salt", "")
        if e2e and (len(salt) != 32 or
                    any(c not in "0123456789abcdefABCDEF" for c in salt)):
            return self._error(400, "invalid_fields")
        name = self._room_name(form, e2e)
        if name is None:
            return self._error(400, "invalid_fields")
        title = self._room_title_field(form, e2e)
        if title is None:
            return self._error(400, "invalid_fields")
        with _lock:
            if len(_rooms) >= MAX_ROOMS:
                return self._error(503, "server_full")
            code = _new_code()
            room = {"members": {}, "chat": [], "next_chat_id": 1,
                    "next_thumb": 1, "e2e": e2e, "title": title,
                    "salt": salt.lower() if e2e else ""}
            _rooms[code] = room
            member_id, token = self._add_member(room, name)
            fields = {"code": code, "member_id": member_id,
                      "token": token, "ttl": ROOM_MEMBER_TTL_SECONDS,
                      "e2e": 1 if e2e else 0}
            fields.update(_room_state(room))
        self._reply(200, fields)

    def _room_info(self, code):
        with _lock:
            room = _rooms.get(code)
            if room is None:
                return self._error(404, "room_not_found")
            fields = {"e2e": 1 if room.get("e2e") else 0}
            if room.get("e2e"):
                fields["salt"] = room["salt"]
        self._reply(200, fields)

    def _room_join(self, code, form):
        with _lock:
            room = _rooms.get(code)
            if room is None:
                # Pode ser um codigo de SESSAO (versoes sem sala):
                # quem chama tenta o "join" de sessao em seguida.
                return self._error(404, "room_not_found")
            e2e = bool(room.get("e2e"))
            # Versao antiga numa sala criptografada: nao entende
            # os nomes/chat - recusa com um codigo proprio.
            if e2e and form.get("e2e") != "1":
                return self._error(426, "upgrade_required")
            name = self._room_name(form, e2e)
            if name is None:
                return self._error(400, "invalid_fields")
            if len(room["members"]) >= MAX_ROOM_MEMBERS:
                return self._error(409, "room_full")
            member_id, token = self._add_member(room, name)
            fields = {"member_id": member_id, "token": token,
                      "ttl": ROOM_MEMBER_TTL_SECONDS}
            fields.update(_room_state(room))
        self._reply(200, fields)

    def _room_heartbeat(self, code, form):
        after_text = form.get("chat_after", "")
        after = int(after_text) if after_text.isdigit() else None
        with _lock:
            room, member = self._room_member(code, form)
            if room is None:
                return self._error(404, "not_in_room")
            fields = {"ok": 1}
            fields.update(_room_state(room))
            # Programa sem chat (0.5.0) nao manda chat_after:
            # a resposta continua igual pra ele.
            if after is not None:
                fields.update(_room_chat(room, after))
        self._reply(200, fields)

    def _room_chat_send(self, code, form):
        with _lock:
            room, member = self._room_member(code, form)
            if room is None:
                return self._error(404, "not_in_room")
            text = _field(room, form.get("text", ""),
                          MAX_CHAT_LENGTH, MAX_SEALED_CHAT)
            if not text:
                return self._error(400, "invalid_fields")
            message_id = room["next_chat_id"]
            room["next_chat_id"] += 1
            room["chat"].append({
                "id": message_id,
                "member_id": form["member_id"],
                "name": member["name"],
                "text": text,
            })
            room["chat"] = room["chat"][-CHAT_HISTORY:]
        self._reply(200, {"ok": 1, "id": message_id})

    def _room_present(self, code, form):
        session_code = form.get("session_code", "").upper()
        session_token = form.get("session_token", "")
        with _lock:
            room, member = self._room_member(code, form)
            if room is None:
                return self._error(404, "not_in_room")
            label = _field(room, form.get("label", ""),
                           MAX_LABEL_LENGTH, MAX_SEALED_LABEL)
            if label is None:
                # Descricao vazia/estranha numa sala e2e: sem
                # descricao.
                label = ""
            # So publica sessao que e DELE (prova: o token da
            # sessao) - ninguem anuncia a transmissao de outro.
            session = _sessions.get(session_code)
            if session is None or not secrets.compare_digest(
                    session["token"], session_token):
                return self._error(404, "session_not_found")
            previous = member["presenting"]
            member["presenting"] = {
                "session_code": session_code,
                "label": label,
                # Mudar so a descricao nao muda a ordem da lista.
                "since": (previous["since"]
                          if previous is not None and
                          previous["session_code"] == session_code
                          else _now()),
            }
            # Mesma transmissao (ex.: trocou so a descricao): a
            # previa continua.
            if (previous is not None and
                    previous["session_code"] == session_code and
                    "thumb" in previous):
                member["presenting"]["thumb"] = previous["thumb"]
                member["presenting"]["thumb_version"] = \
                    previous["thumb_version"]
        self._reply(200, {"ok": 1})

    def _room_voice(self, code, form):
        session_code = form.get("session_code", "").upper()
        session_token = form.get("session_token", "")
        with _lock:
            room, member = self._room_member(code, form)
            if room is None:
                return self._error(404, "not_in_room")
            if not session_code:
                member["voice"] = None
            else:
                # Mesma prova do "present": so publica sessao sua.
                session = _sessions.get(session_code)
                if session is None or not secrets.compare_digest(
                        session["token"], session_token):
                    return self._error(404, "session_not_found")
                member["voice"] = session_code
        self._reply(200, {"ok": 1})

    def _room_thumb(self, code, form):
        text = form.get("image", "")
        image = None
        if text:
            try:
                image = base64.urlsafe_b64decode(text.encode("ascii"))
            except (binascii.Error, ValueError, UnicodeEncodeError):
                return self._error(400, "invalid_fields")
            # So JPEG (ou JPEG criptografado: "SPE1"), pequeno.
            if (len(image) > MAX_THUMB_BYTES or
                    not (image.startswith(b"\xff\xd8") or
                         image.startswith(b"SPE1"))):
                return self._error(400, "invalid_fields")
        with _lock:
            room, member = self._room_member(code, form)
            if room is None:
                return self._error(404, "not_in_room")
            presenting = member["presenting"]
            if presenting is None:
                return self._error(409, "not_presenting")
            if image is None:
                presenting.pop("thumb", None)
                presenting.pop("thumb_version", None)
            else:
                version = room.get("next_thumb", 1)
                room["next_thumb"] = version + 1
                presenting["thumb"] = image
                presenting["thumb_version"] = version
        self._reply(200, {"ok": 1})

    def _room_thumb_get(self, code, form):
        with _lock:
            room, member = self._room_member(code, form)
            if room is None:
                return self._error(404, "not_in_room")
            target = room["members"].get(form.get("target", ""))
            presenting = target["presenting"] if target else None
            if presenting is None or "thumb" not in presenting:
                return self._error(404, "no_thumb")
            image = presenting["thumb"]
            version = presenting["thumb_version"]
        self._reply(200, {
            "image": base64.urlsafe_b64encode(image).decode("ascii"),
            "version": version,
        })

    def _room_avatar(self, code, form):
        text = form.get("image", "")
        image = None
        if text:
            try:
                image = base64.urlsafe_b64decode(text.encode("ascii"))
            except (binascii.Error, ValueError, UnicodeEncodeError):
                return self._error(400, "invalid_fields")
            if (len(image) > MAX_AVATAR_BYTES or
                    not (image.startswith(b"\xff\xd8") or
                         image.startswith(b"SPE1"))):
                return self._error(400, "invalid_fields")
        with _lock:
            room, member = self._room_member(code, form)
            if room is None:
                return self._error(404, "not_in_room")
            # Sala e2e: so foto criptografada.
            if (image is not None and room.get("e2e") and
                    not image.startswith(b"SPE1")):
                return self._error(400, "invalid_fields")
            if image is None:
                member.pop("avatar", None)
                member.pop("avatar_version", None)
            else:
                version = room.get("next_thumb", 1)
                room["next_thumb"] = version + 1
                member["avatar"] = image
                member["avatar_version"] = version
        self._reply(200, {"ok": 1})

    def _room_avatar_get(self, code, form):
        with _lock:
            room, member = self._room_member(code, form)
            if room is None:
                return self._error(404, "not_in_room")
            target = room["members"].get(form.get("target", ""))
            if target is None or "avatar" not in target:
                return self._error(404, "no_avatar")
            image = target["avatar"]
            version = target["avatar_version"]
        self._reply(200, {
            "image": base64.urlsafe_b64encode(image).decode("ascii"),
            "version": version,
        })

    def _room_stop(self, code, form):
        with _lock:
            room, member = self._room_member(code, form)
            if room is None:
                return self._error(404, "not_in_room")
            member["presenting"] = None
        self._reply(200, {"ok": 1})

    def _room_leave(self, code, form):
        with _lock:
            room, member = self._room_member(code, form)
            if room is None:
                return self._error(404, "not_in_room")
            del room["members"][form["member_id"]]
            if not room["members"]:
                del _rooms[code]
        self._reply(200, {"ok": 1})


def main():
    port = int(os.environ.get("PORT", "8080"))
    if len(sys.argv) > 1:
        port = int(sys.argv[1])

    server = ThreadingHTTPServer(("0.0.0.0", port), Handler)
    server.daemon_threads = True

    print(f"Servidor de sinalizacao rodando na porta {port}")
    print("Ctrl+C para encerrar.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nEncerrando.")
        server.server_close()


if __name__ == "__main__":
    main()
