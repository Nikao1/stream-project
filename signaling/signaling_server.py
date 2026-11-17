#!/usr/bin/env python3
"""
Stream Project - servidor de sinalizacao

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

Hole punching: o host registra seu endereco publico (descoberto
por STUN) ao criar a sessao; o receiver pega esse endereco no
"join" e anuncia o proprio com "announce"; o host recebe o
endereco do receiver na resposta do "keepalive".
"""

import ipaddress
import os
import secrets
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs

# ------------------------------------------------------------
# Configuracao
# ------------------------------------------------------------

SESSION_TTL_SECONDS = 60        # sessao some se o host parar de dar keepalive
MAX_SESSIONS = 5000
MAX_BODY_BYTES = 2048
CODE_LENGTH = 6

# Anuncio de espectador (endereco publico pro hole punching):
# o host so precisa dele por alguns segundos, ate o HELLO do
# espectador passar. Depois disso o anuncio some sozinho.
ANNOUNCE_TTL_SECONDS = 60
MAX_ANNOUNCES_PER_SESSION = 16

# Sem 0/O, 1/I/L: evita confusao ao ditar o codigo pra alguem.
CODE_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"

# Limite de requisicoes por IP (dificulta adivinhar codigos).
RATE_LIMIT_WINDOW_SECONDS = 60
RATE_LIMIT_MAX_REQUESTS = 120

# So confie em X-Forwarded-For quando o servidor estiver atras
# de um proxy da hospedagem (Render, Fly etc.): TRUST_PROXY=1
TRUST_PROXY = os.environ.get("TRUST_PROXY", "0") == "1"

# ------------------------------------------------------------
# Estado (em memoria - sessoes sao temporarias por natureza)
# ------------------------------------------------------------

_lock = threading.Lock()
_sessions = {}      # code -> dict
_rate = {}          # ip -> [inicio_da_janela, contagem]


def _now():
    return time.monotonic()


def _purge_expired():
    """Chamar com _lock adquirido."""
    now = _now()
    expired = [c for c, s in _sessions.items() if s["expires"] <= now]
    for code in expired:
        del _sessions[code]

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
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            return None
        if length < 0 or length > MAX_BODY_BYTES:
            return None
        raw = self.rfile.read(length).decode("utf-8", "replace") if length else ""
        parsed = parse_qs(raw, keep_blank_values=True)
        return {k: v[0] for k, v in parsed.items()}

    def log_message(self, fmt, *args):
        sys.stderr.write("[%s] %s %s\n" % (
            time.strftime("%H:%M:%S"), self._client_ip(), fmt % args))

    # ------------------------- rotas --------------------------

    def do_GET(self):
        if self.path == "/health":
            with _lock:
                _purge_expired()
                count = len(_sessions)
            self._reply(200, {"ok": 1, "sessions": count})
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

        self._error(404, "not_found")

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

            while True:
                code = "".join(secrets.choice(CODE_ALPHABET)
                               for _ in range(CODE_LENGTH))
                if code not in _sessions:
                    break

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
