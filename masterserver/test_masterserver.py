#!/usr/bin/env python3
"""Test suite for the CSS Enhanced Master Server."""

import json
import http.client
import os
import signal
import socket
import ssl
import struct
import subprocess
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.realpath(__file__))
SERVER_SCRIPT = os.path.join(SCRIPT_DIR, "masterserver.py")
DB_FILE = os.path.join(SCRIPT_DIR, "masterserver.db")
LOG_FILE = os.path.join(SCRIPT_DIR, "masterserver.log")
CERT_FILE = os.path.join(SCRIPT_DIR, "masterserver.crt")
KEY_FILE = os.path.join(SCRIPT_DIR, "masterserver.key")

HTTPS_PORT = 27011
UDP_PORT = 27010

passed = 0
failed = 0


def check(desc, expected, actual):
    global passed, failed
    if expected == actual:
        print(f"  PASS: {desc}")
        passed += 1
    else:
        print(f"  FAIL: {desc}")
        print(f"    expected: {expected}")
        print(f"    got:      {actual}")
        failed += 1


def https_request(method, path, body=None):
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    conn = http.client.HTTPSConnection("localhost", HTTPS_PORT, context=ctx, timeout=5)
    headers = {"Content-Type": "application/json"} if body else {}
    conn.request(method, path, body=json.dumps(body) if body else None, headers=headers)
    resp = conn.getresponse()
    data = resp.read().decode()
    conn.close()
    return json.loads(data) if data else None


def udp_query():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3)
    sock.sendto(bytes([0x31]), ("127.0.0.1", UDP_PORT))
    data = sock.recvfrom(2048)[0]
    sock.close()

    header = struct.unpack("<I", data[0:4])[0]
    msg_type = data[4]
    servers = []
    offset = 5
    while offset + 6 <= len(data):
        ip_int = struct.unpack("<I", data[offset : offset + 4])[0]
        port = struct.unpack("<H", data[offset + 4 : offset + 6])[0]
        if ip_int == 0 and port == 0:
            break
        ip_str = socket.inet_ntoa(struct.pack(">I", ip_int))
        servers.append(f"{ip_str}:{port}")
        offset += 6
    return header, msg_type, sorted(servers)


def cleanup_files():
    for f in [DB_FILE, LOG_FILE, CERT_FILE, KEY_FILE]:
        if os.path.exists(f):
            os.remove(f)


def main():
    global passed, failed

    # Clean state
    cleanup_files()

    # Start the server
    proc = subprocess.Popen(
        [sys.executable, SERVER_SCRIPT],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    # Wait for it to come up
    for _ in range(20):
        time.sleep(0.25)
        try:
            https_request("GET", "/clan/list")
            break
        except Exception:
            continue
    else:
        print("ERROR: Server did not start in time")
        proc.kill()
        sys.exit(1)

    # Read API key from the DB
    import sqlite3

    api_key = sqlite3.connect(DB_FILE).execute(
        "SELECT value FROM config WHERE key = 'api_key'"
    ).fetchone()[0]

    try:
        run_tests(api_key)
    finally:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
        cleanup_files()

    print()
    print(f"{'=' * 40}")
    print(f"PASSED: {passed}  FAILED: {failed}")
    print(f"{'=' * 40}")
    sys.exit(1 if failed else 0)


def run_tests(api_key):
    def post(path, data=None):
        body = {**(data or {}), "api_key": api_key}
        return https_request("POST", path, body)

    def get(path):
        return https_request("GET", path)

    # ── CRUD ──────────────────────────────────────────────
    print("CRUD")

    check("GET /clan/list (empty)", [], get("/clan/list"))

    r = post("/clan/create", {"tag": "Alpha"})
    check("POST /clan/create Alpha", {"id": 1, "tag": "Alpha"}, r)

    r = post("/clan/create", {"tag": "Bravo"})
    check("POST /clan/create Bravo", {"id": 2, "tag": "Bravo"}, r)

    r = get("/clan/list")
    check("GET /clan/list (2 clans)", 2, len(r))

    check("GET /user/list (empty)", [], get("/user/list"))

    r = post("/user/create", {"name": "Alice"})
    check("POST /user/create Alice (name)", "Alice", r["name"])
    check("POST /user/create Alice (id)", 1, r["id"])
    check("POST /user/create Alice (token len)", 48, len(r["token"]))
    alice_token = r["token"]

    r = post("/user/create", {"name": "Bob"})
    check("POST /user/create Bob", "Bob", r["name"])
    bob_token = r["token"]

    r = get("/user/list")
    check("GET /user/list (2 users)", 2, len(r))

    # ── Clan membership ───────────────────────────────────
    print("\nClan Membership")

    r = post("/user/clans/add", {"user_name": "Alice", "clan_tag": "Alpha"})
    check("Add Alice -> Alpha", "ok", r["status"])

    r = post("/user/clans/add", {"user_name": "Alice", "clan_tag": "Bravo"})
    check("Add Alice -> Bravo", "ok", r["status"])

    r = post("/user/clans/add", {"user_name": "Bob", "clan_tag": "Alpha"})
    check("Add Bob -> Alpha", "ok", r["status"])

    r = post("/user/clans/default", {"user_name": "Alice", "clan_tag": "Alpha"})
    check("Set Alice default -> Alpha", "ok", r["status"])

    r = get(f"/clan/default/{alice_token}")
    check("GET Alice default clan", "Alpha", r["tag"])

    r = post("/user/clans/default", {"user_name": "Bob", "clan_tag": "Alpha"})
    check("Set Bob default -> Alpha", "ok", r["status"])

    r = get(f"/clan/default/{bob_token}")
    check("GET Bob default clan", "Alpha", r["tag"])

    r = post("/user/clans/remove", {"user_name": "Alice", "clan_tag": "Bravo"})
    check("Remove Alice <- Bravo", "ok", r["status"])

    # ── Servers ───────────────────────────────────────────
    print("\nServers")

    r = post("/server/add", {"ip": "192.168.1.10", "port": 27015})
    check("Add server 192.168.1.10:27015", "ok", r["status"])

    r = post("/server/add", {"ip": "10.0.0.5", "port": 27016})
    check("Add server 10.0.0.5:27016", "ok", r["status"])

    r = post("/server/add", {"ip": "172.16.0.1"})
    check("Add server 172.16.0.1 (default port)", "ok", r["status"])

    # ── UDP ───────────────────────────────────────────────
    print("\nUDP Server")

    header, msg_type, servers = udp_query()
    check("UDP header", 0xFFFFFFFF, header)
    check("UDP msg type", 0x4A, msg_type)
    check(
        "UDP 3 servers",
        ["10.0.0.5:27016", "172.16.0.1:27015", "192.168.1.10:27015"],
        servers,
    )

    r = post("/server/delete", {"ip": "10.0.0.5", "port": 27016})
    check("Delete server 10.0.0.5:27016", "ok", r["status"])

    _, _, servers = udp_query()
    check(
        "UDP reflects deletion (2 servers)",
        ["172.16.0.1:27015", "192.168.1.10:27015"],
        servers,
    )

    # ── Error cases ───────────────────────────────────────
    print("\nError Cases")

    r = https_request("POST", "/clan/create", {"api_key": "wrong", "tag": "X"})
    check("Bad API key", "Invalid API key", r["error"])

    r = post("/clan/create", {})
    check("Missing clan tag", "Clan tag required", r["error"])

    r = post("/user/create", {})
    check("Missing user name", "User name required", r["error"])

    r = post("/server/add", {})
    check("Missing IP", "IP required", r["error"])

    r = post("/server/add", {"ip": "nope"})
    check("Invalid IP", "Invalid IP", r["error"])

    r = get("/does/not/exist")
    check("Unknown GET path", "Not found", r["error"])

    r = get("/clan/default/fake_token")
    check("Default clan bad token", "No default clan", r["error"])

    r = post("/user/delete", {"name": "Nobody"})
    check("Delete unknown user", "User not found", r["error"])

    r = post("/clan/delete", {"tag": "NoClan"})
    check("Delete unknown clan", "Clan not found", r["error"])

    r = post("/user/clans/add", {"user_name": "Ghost", "clan_tag": "Alpha"})
    check("Add clan to unknown user", "User not found", r["error"])

    r = post("/user/clans/add", {"user_name": "Alice", "clan_tag": "NoClan"})
    check("Add unknown clan to user", "Clan not found", r["error"])

    # ── Deletions ─────────────────────────────────────────
    print("\nDeletions")

    r = post("/clan/delete", {"tag": "Bravo"})
    check("Delete clan Bravo", "ok", r["status"])

    r = get("/clan/list")
    check("1 clan remaining", [{"id": 1, "tag": "Alpha"}], r)

    r = post("/user/delete", {"name": "Bob"})
    check("Delete user Bob", "ok", r["status"])

    r = get("/user/list")
    check("1 user remaining", 1, len(r))
    check("Remaining user is Alice", "Alice", r[0]["name"])


if __name__ == "__main__":
    main()
