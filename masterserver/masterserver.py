#!/usr/bin/env python3
import json
import os
import secrets
import socket
import sqlite3
import ssl
import struct
import threading
import time

from http.server import HTTPServer, BaseHTTPRequestHandler
from socketserver import ThreadingMixIn
from subprocess import run, DEVNULL
from urllib.parse import urlparse

# ---------------------------------------------------------------------------
# Paths & constants
# ---------------------------------------------------------------------------

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
LOG_FILE = os.path.join(SCRIPT_DIR, "masterserver.log")
DB_FILE = os.path.join(SCRIPT_DIR, "masterserver.db")
CERT_FILE = os.path.join(SCRIPT_DIR, "masterserver.crt")
KEY_FILE = os.path.join(SCRIPT_DIR, "masterserver.key")

UDP_PORT = 27010
HTTPS_PORT = 27011

CONNECTIONLESS_HEADER = 0xFFFFFFFF
C2M_CLIENTQUERY = 0x31
M2C_QUERY = 0x4A

SESSION_TIMEOUT = 30  # 30 secs
SESSION_CLEANUP_INTERVAL = 60  # cleanup every 60 secs
MAX_SESSIONS = 1000  # max sessions to prevent memory/abuse

# ---------------------------------------------------------------------------
# Session store  (session_id -> {user_id, client_ip, timestamp})
# ---------------------------------------------------------------------------

_sessions = {}  # session_id -> {user_id, client_ip, timestamp}


def cleanup_expired_sessions():
    """Remove all expired sessions"""
    now = time.time()
    expired = [
        sid
        for sid, sess in _sessions.items()
        if now - sess["timestamp"] > SESSION_TIMEOUT
    ]
    for sid in expired:
        del _sessions[sid]
    if expired:
        log(f"[CLEANUP] Removed {len(expired)} expired sessions")


def cleanup_thread():
    """Background thread that periodically cleans up expired sessions"""
    while True:
        time.sleep(SESSION_CLEANUP_INTERVAL)
        cleanup_expired_sessions()


# Start cleanup thread
_cleanup_thread = threading.Thread(target=cleanup_thread, daemon=True)
_cleanup_thread.start()

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

_log_fd = open(LOG_FILE, "a")


def log(message):
    print(message)
    _log_fd.write(message + "\n")
    _log_fd.flush()


# ---------------------------------------------------------------------------
# Database  (thread-local connections, reused across requests)
# ---------------------------------------------------------------------------

_db_local = threading.local()


def get_db():
    conn = getattr(_db_local, "conn", None)
    if conn is None:
        conn = sqlite3.connect(DB_FILE)
        conn.execute("PRAGMA journal_mode=WAL")
        _db_local.conn = conn
    return conn


def init_database():
    conn = get_db()
    conn.executescript("""
        CREATE TABLE IF NOT EXISTS config     (key TEXT PRIMARY KEY, value TEXT);
        CREATE TABLE IF NOT EXISTS clans      (id INTEGER PRIMARY KEY, tag TEXT UNIQUE);
        CREATE TABLE IF NOT EXISTS users      (id INTEGER PRIMARY KEY, token TEXT UNIQUE,
                                               name TEXT, default_clan INTEGER);
        CREATE TABLE IF NOT EXISTS user_clans (user_id INTEGER, clan_id INTEGER,
                                               PRIMARY KEY (user_id, clan_id));
        CREATE TABLE IF NOT EXISTS servers    (ip INTEGER PRIMARY KEY, port INTEGER,
                                               priority INTEGER DEFAULT 0);
    """)


# ---------------------------------------------------------------------------
# Config & API key  (cached in memory after first load)
# ---------------------------------------------------------------------------

_api_key = None


def load_api_key():
    global _api_key
    conn = get_db()
    row = conn.execute("SELECT value FROM config WHERE key = 'api_key'").fetchone()
    if row:
        _api_key = row[0]
    else:
        _api_key = secrets.token_hex(32)
        conn.execute("INSERT INTO config VALUES ('api_key', ?)", (_api_key,))
        conn.commit()
        log(f"[INFO] Generated API key: {_api_key}")


# ---------------------------------------------------------------------------
# Lookup helpers
# ---------------------------------------------------------------------------


def find_user_id(conn, name=None, user_id=None):
    if user_id:
        return user_id
    if not name:
        return None
    row = conn.execute("SELECT id FROM users WHERE name = ?", (name,)).fetchone()
    return row[0] if row else None


def find_clan_id(conn, tag=None, clan_id=None):
    if clan_id:
        return clan_id
    if not tag:
        return None
    row = conn.execute("SELECT id FROM clans WHERE tag = ?", (tag,)).fetchone()
    return row[0] if row else None


def parse_ip(ip_string):
    try:
        return struct.unpack(">I", socket.inet_aton(ip_string))[0]
    except Exception:
        return None


def check_server_authorized(handler, conn):
    server_ip = handler.client_address[0]
    server_ip_int = parse_ip(server_ip)
    if server_ip_int is None:
        return False
    row = conn.execute(
        "SELECT ip FROM servers WHERE ip = ?", (server_ip_int,)
    ).fetchone()
    return row is not None


# ---------------------------------------------------------------------------
# TLS certificate
# ---------------------------------------------------------------------------


def generate_certificate():
    if os.path.exists(CERT_FILE):
        return
    run(
        [
            "openssl",
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-keyout",
            KEY_FILE,
            "-out",
            CERT_FILE,
            "-days",
            "365",
            "-nodes",
            "-subj",
            "/CN=localhost",
        ],
        stdout=DEVNULL,
        stderr=DEVNULL,
    )


# ---------------------------------------------------------------------------
# UDP game-server browser  (payload is pre-built, only rebuilt on changes)
# ---------------------------------------------------------------------------


class UDPServer:
    def __init__(self):
        self._payload = b""
        self.reload()

    def reload(self):
        rows = get_db().execute("SELECT ip, port FROM servers").fetchall()
        buf = bytearray(struct.pack("<I", CONNECTIONLESS_HEADER))
        buf.append(M2C_QUERY)
        for ip, port in rows:
            buf.extend(struct.pack("<IH", ip, port))
        buf.extend(b"\x00" * 6)
        self._payload = bytes(buf)
        self._last_count = len(rows)

    def run(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("0.0.0.0", UDP_PORT))
        last_reload = time.time()
        while True:
            now = time.time()
            if now - last_reload > 30:
                current = get_db().execute("SELECT COUNT(*) FROM servers").fetchone()[0]
                if current != self._last_count:
                    self.reload()
                    last_reload = now
            data, addr = sock.recvfrom(2048)
            if data and data[0] == C2M_CLIENTQUERY:
                sock.sendto(self._payload, addr)


udp_server = None

# ---------------------------------------------------------------------------
# HTTPS route handlers  —  each endpoint is one function
# ---------------------------------------------------------------------------

# ---- GET -----------------------------------------------------------------


def get_clan_list(handler, conn, path, data):
    rows = conn.execute("SELECT id, tag FROM clans").fetchall()
    handler.json_response(200, [{"id": r[0], "tag": r[1]} for r in rows])


def get_user_list(handler, conn, path, data):
    rows = conn.execute("SELECT id, token, name, default_clan FROM users").fetchall()
    handler.json_response(
        200,
        [{"id": r[0], "token": r[1], "name": r[2], "default_clan": r[3]} for r in rows],
    )


def get_clan_default(handler, conn, path, data):
    parts = path.rstrip("/").split("/")
    if len(parts) != 4:
        return handler.json_error(
            400, "Invalid path format, expected /clan/default/{session_id}"
        )

    session_id = parts[-1]

    now = time.time()

    session = _sessions.get(session_id)
    if not session:
        return handler.json_error(404, "Session not found")

    if now - session["timestamp"] > SESSION_TIMEOUT:
        del _sessions[session_id]
        return handler.json_error(401, "Session expired")

    if not check_server_authorized(handler, conn):
        return handler.json_response(200, {"error": "Server not authorized"})

    user_id = session["user_id"]
    row = conn.execute(
        "SELECT c.tag FROM users u LEFT JOIN clans c ON u.default_clan = c.id "
        "WHERE u.id = ?",
        (user_id,),
    ).fetchone()
    if row and row[0]:
        handler.json_response(200, {"tag": row[0]})
    else:
        handler.json_error(404, "No default clan")


# ---- POST ----------------------------------------------------------------


def post_clan_create(handler, conn, path, data):
    tag = data.get("tag")
    if not tag:
        return handler.json_error(400, "Clan tag required")
    conn.execute("INSERT OR REPLACE INTO clans (tag) VALUES (?)", (tag,))
    conn.commit()
    clan_id = conn.execute("SELECT last_insert_rowid()").fetchone()[0]
    handler.json_response(200, {"id": clan_id, "tag": tag})


def post_clan_delete(handler, conn, path, data):
    key = data.get("tag") or data.get("clan_tag") or data.get("id")
    if not key:
        return handler.json_error(400, "Clan id or tag required")
    row = conn.execute(
        "SELECT id FROM clans WHERE tag = ? OR id = ?", (key, key)
    ).fetchone()
    if not row:
        return handler.json_error(404, "Clan not found")
    conn.execute("DELETE FROM user_clans WHERE clan_id = ?", (row[0],))
    conn.execute("DELETE FROM clans WHERE id = ?", (row[0],))
    conn.commit()
    handler.json_response(200, {"status": "ok"})


def post_user_create(handler, conn, path, data):
    name = data.get("name")
    if not name:
        return handler.json_error(400, "User name required")
    token = secrets.token_hex(24)
    conn.execute("INSERT INTO users (token, name) VALUES (?, ?)", (token, name))
    conn.commit()
    user_id = conn.execute("SELECT last_insert_rowid()").fetchone()[0]
    handler.json_response(200, {"id": user_id, "token": token, "name": name})


def post_user_delete(handler, conn, path, data):
    key = data.get("name") or data.get("user_name") or data.get("id")
    if not key:
        return handler.json_error(400, "User id or name required")
    row = conn.execute(
        "SELECT id FROM users WHERE name = ? OR id = ?", (key, key)
    ).fetchone()
    if not row:
        return handler.json_error(404, "User not found")
    conn.execute("DELETE FROM user_clans WHERE user_id = ?", (row[0],))
    conn.execute("DELETE FROM users WHERE id = ?", (row[0],))
    conn.commit()
    handler.json_response(200, {"status": "ok"})


def post_user_clans_add(handler, conn, path, data):
    user_id = find_user_id(conn, data.get("user_name")) or data.get("user_id")
    clan_id = find_clan_id(conn, data.get("clan_tag")) or data.get("clan_id")
    if not user_id:
        return handler.json_error(404, "User not found")
    if not clan_id:
        return handler.json_error(404, "Clan not found")
    conn.execute("INSERT OR IGNORE INTO user_clans VALUES (?, ?)", (user_id, clan_id))
    conn.commit()
    handler.json_response(200, {"status": "ok"})


def post_user_clans_remove(handler, conn, path, data):
    user_id = find_user_id(conn, data.get("user_name")) or data.get("user_id")
    clan_id = find_clan_id(conn, data.get("clan_tag")) or data.get("clan_id")
    if user_id and clan_id:
        conn.execute(
            "DELETE FROM user_clans WHERE user_id = ? AND clan_id = ?",
            (user_id, clan_id),
        )
        conn.commit()
    handler.json_response(200, {"status": "ok"})


def post_user_clans_default(handler, conn, path, data):
    user_id = find_user_id(conn, data.get("user_name")) or data.get("user_id")
    clan_id = find_clan_id(conn, data.get("clan_tag")) or data.get("clan_id")
    if user_id and clan_id:
        conn.execute(
            "UPDATE users SET default_clan = ? WHERE id = ?", (clan_id, user_id)
        )
        conn.commit()
    handler.json_response(200, {"status": "ok"})


def post_server_add(handler, conn, path, data):
    ip_str = data.get("ip")
    port = data.get("port", 27015)
    if not ip_str:
        return handler.json_error(400, "IP required")
    ip_int = parse_ip(ip_str)
    if ip_int is None:
        return handler.json_error(400, "Invalid IP")
    conn.execute("INSERT OR REPLACE INTO servers VALUES (?, ?, 0)", (ip_int, port))
    conn.commit()
    if udp_server:
        udp_server.reload()
    handler.json_response(200, {"status": "ok"})


def post_server_delete(handler, conn, path, data):
    ip_str = data.get("ip")
    port = data.get("port", 27015)
    if not ip_str:
        return handler.json_error(400, "IP required")
    ip_int = parse_ip(ip_str)
    if ip_int is None:
        return handler.json_error(400, "Invalid IP")
    conn.execute("DELETE FROM servers WHERE ip = ? AND port = ?", (ip_int, port))
    conn.commit()
    if udp_server:
        udp_server.reload()
    handler.json_response(200, {"status": "ok"})


def post_auth(handler, conn, path, data):
    if path.startswith("/auth/"):
        return post_auth_verify(handler, conn, path)

    token = data.get("token")
    if not token:
        return handler.json_error(400, "Token required")

    row = conn.execute("SELECT id FROM users WHERE token = ?", (token,)).fetchone()
    if not row:
        return handler.json_error(401, "Invalid token")

    user_id = row[0]
    client_ip = handler.client_address[0]

    # Check session limit
    if len(_sessions) >= MAX_SESSIONS:
        log(f"[WARN] Too many sessions, rejecting new auth")
        return handler.json_error(503, "Server busy, try again later")

    session_id = secrets.token_hex(16)

    _sessions[session_id] = {
        "user_id": user_id,
        "client_ip": client_ip,
        "timestamp": time.time(),
    }

    log(f"[INFO] Created session {session_id} for user {user_id} from {client_ip}")

    handler.json_response(200, {"session_id": session_id})


def post_auth_verify(handler, conn, path):
    if not check_server_authorized(handler, conn):
        return handler.json_response(200, {"error": "Server not authorized"})

    parts = path.rstrip("/").split("/")
    if len(parts) != 3:
        return handler.json_error(
            400, "Invalid path format, expected /auth/{session_id}"
        )

    session_id = parts[-1]

    log(f"[AUTH] Verify: session={session_id}")

    now = time.time()

    session = _sessions.get(session_id)
    if not session:
        log(f"[AUTH] Session not found: {session_id}")
        return handler.json_response(
            200, {"accepted": "false", "error": "Session not found"}
        )

    if now - session["timestamp"] > SESSION_TIMEOUT:
        log(f"[AUTH] Session expired: {session_id}")
        del _sessions[session_id]
        return handler.json_response(
            200, {"accepted": "false", "error": "Session expired"}
        )

    log(f"[AUTH] Accepted: session={session_id}")
    handler.json_response(200, {"accepted": "true"})


# ---------------------------------------------------------------------------
# Route tables  —  add new endpoints here
# ---------------------------------------------------------------------------

GET_ROUTES = {
    "/clan/list": get_clan_list,
    "/user/list": get_user_list,
}

GET_PREFIX_ROUTES = {
    "/clan/default/": get_clan_default,
}

POST_ROUTES = {
    "/clan/create": post_clan_create,
    "/clan/delete": post_clan_delete,
    "/user/create": post_user_create,
    "/user/delete": post_user_delete,
    "/user/clans/add": post_user_clans_add,
    "/user/clans/remove": post_user_clans_remove,
    "/user/clans/default": post_user_clans_default,
    "/server/add": post_server_add,
    "/server/delete": post_server_delete,
    "/auth": post_auth,
}

POST_PREFIX_ROUTES = {
    "/auth/": post_auth,
}

# ---------------------------------------------------------------------------
# HTTP request handler & threaded server
# ---------------------------------------------------------------------------


class ThreadingHTTPServer(ThreadingMixIn, HTTPServer):
    daemon_threads = True


class RequestHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        log(f"[HTTP] {args[0]}")

    def json_response(self, code, data):
        body = json.dumps(data).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", len(body))
        self.end_headers()
        self.wfile.write(body)

    def json_error(self, code, message):
        self.json_response(code, {"error": message})

    def do_GET(self):
        path = urlparse(self.path).path
        conn = get_db()

        handler = GET_ROUTES.get(path)
        if handler:
            return handler(self, conn, path, None)

        for prefix, handler in GET_PREFIX_ROUTES.items():
            if path.startswith(prefix):
                return handler(self, conn, path, None)

        self.json_error(404, "Not found")

    def do_POST(self):
        path = urlparse(self.path).path
        length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(length) if length > 0 else b""
        data = json.loads(body.decode()) if body else {}

        if not path.startswith("/auth"):
            if data.get("api_key") != _api_key:
                return self.json_error(401, "Invalid API key")

        handler = POST_ROUTES.get(path)
        if handler:
            return handler(self, get_db(), path, data)

        for prefix, handler in POST_PREFIX_ROUTES.items():
            if path.startswith(prefix):
                return handler(self, get_db(), path, data)

        self.json_error(404, "Not found")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def main():
    global udp_server

    init_database()
    generate_certificate()
    load_api_key()

    udp_server = UDPServer()
    threading.Thread(target=udp_server.run, daemon=True).start()
    log(f"[INFO] UDP port: {UDP_PORT}")

    server = ThreadingHTTPServer(("0.0.0.0", HTTPS_PORT), RequestHandler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(CERT_FILE, KEY_FILE)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)

    log(f"[INFO] HTTPS port: {HTTPS_PORT}")
    server.serve_forever()


if __name__ == "__main__":
    log("[INFO] CSS Enhanced Master Server")
    main()
