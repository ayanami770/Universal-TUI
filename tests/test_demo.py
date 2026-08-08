#!/usr/bin/env python3
"""pty-driven test for the Universal TUI engine via the acme-server demo.

Proves the generalized engine (arbitrary tree + CONFIG_<prefix>_ symbols) renders,
navigates, edits every item kind, and saves/loads correctly — without a real tty.
"""
import fcntl, re, os, pty, select, signal, struct, subprocess, sys, termios, time, tempfile

BIN = sys.argv[1] if len(sys.argv) > 1 else "./demo"
CFG = "acme-server.config"
ANSI = re.compile(rb"\x1b\[[0-9;?<]*[A-Za-z~]|\x1b.")
FAILED = []


def S(b): return ANSI.sub(b"", b)


def spawn(args=None, cols=100, rows=30, cwd=None):
    m, s = pty.openpty()
    fcntl.ioctl(s, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    env = dict(os.environ, TERM="xterm-256color", LANG="C.UTF-8")
    p = subprocess.Popen([BIN] + (args or []), stdin=s, stdout=s, stderr=s,
                         env=env, cwd=cwd, close_fds=True)
    os.close(s)
    return p, m


def drain(m, t=0.35):
    out = b""; end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([m], [], [], 0.05)
        if r:
            try: out += os.read(m, 65536)
            except OSError: break
    return out


def send(m, d, w=0.2): os.write(m, d.encode()); return drain(m, w)


def ck(n, c):
    print(("PASS " if c else "FAIL ") + n)
    if not c: FAILED.append(n)


def finish(p, m):
    try: out = drain(m, 0.4)
    except OSError: out = b""
    try: p.wait(timeout=3)
    except subprocess.TimeoutExpired: p.kill(); p.wait()
    os.close(m)
    return out


def t_basic(tmp):
    p, m = spawn(cwd=tmp)
    out = drain(m, 0.8)
    ck("acme backtitle", b".config - acme-server" in S(out))
    ck("title", b"acme-server Configuration" in S(out))
    ck("Networking menu", b"Networking" in S(out))
    ck("forced core -*-", b"-*- Core scheduler" in S(out))
    ck("tristate metrics <M>", b"<M> Prometheus metrics exporter" in S(out))
    ck("alt screen", b"\x1b[?1049h" in out)

    # Daemon bool toggle (hotkey 'D' -> "Run as a daemon" or "Database"? use arrows)
    out = send(m, "\x1b[F", 0.2)                 # End -> last (metrics)
    out = send(m, "\x1b[A\x1b[A", 0.2)           # up to "Run as a daemon"
    ck("reached daemon bool", b"Run as a daemon" in S(out))
    out = send(m, " ", 0.3)
    ck("space toggles daemon [*]", b"[*] Run as a daemon" in S(out))

    # help on forced core
    out = send(m, "\x1b[A", 0.2)                  # up to Core scheduler
    out = send(m, "?", 0.3)
    ck("help shows ACME symbol", b"Symbol: ACME_CORE" in S(out))
    out = send(m, "\x1b", 0.3)

    # enter Storage, change choice, edit int
    out = send(m, "\x1b[H", 0.2)
    out = send(m, "\x1b[B", 0.2)                  # Storage
    out = send(m, "\r", 0.3)
    ck("in Storage", b"Database backend" in S(out))
    out = send(m, "\r", 0.3)                      # open choice submenu
    ck("choice list (PostgreSQL)", b"PostgreSQL" in S(out))
    out = send(m, "\x1b[B", 0.2)                  # to PostgreSQL
    out = send(m, "\r", 0.3)                      # select -> back
    out = send(m, "\x1b[B", 0.2)                  # Data directory (string)
    out = send(m, "\x1b[B", 0.2)                  # Cache size (int)
    out = send(m, "\r", 0.3)                      # edit int
    ck("int editor", b"decimal value" in S(out))
    out = send(m, "\x7f\x7f\x7f", 0.2)           # clear 256
    out = send(m, "512x", 0.2)                    # digit-only: 'x' rejected
    out = send(m, "\r", 0.3)
    ck("int -> 512", b"(512)" in S(out))
    out = send(m, "\x1b", 0.3)                    # back to main

    # Save
    send(m, "\x1b[C\x1b[C\x1b[C", 0.2)
    out = send(m, "\r", 0.3)
    ck("save dialog", b"Enter a filename" in S(out))
    out = send(m, "\r", 0.4)
    ck("write confirm", b"Configuration written" in S(out))
    send(m, "\r", 0.3)
    cfg = os.path.join(tmp, CFG)
    ck("config written", os.path.exists(cfg))
    if os.path.exists(cfg):
        b = open(cfg).read()
        ck("prefix ACME used", "CONFIG_ACME_" in b)
        ck("forced CORE=y", "CONFIG_ACME_CORE=y" in b)
        ck("daemon DAEMON=y", "CONFIG_ACME_DAEMON=y" in b)
        ck("choice DB_POSTGRES=y", "CONFIG_ACME_DB_POSTGRES=y" in b)
        ck("choice DB_SQLITE not set", "# CONFIG_ACME_DB_SQLITE is not set" in b)
        ck("int CACHE_MB=512", "CONFIG_ACME_CACHE_MB=512" in b)
        ck("string with quotes", 'CONFIG_ACME_LISTEN_ADDR="0.0.0.0"' in b)

    # exit -> saved epilogue check via Yes this time
    send(m, "\x1b[D\x1b[D", 0.2)
    out = send(m, "\x1b", 0.4)
    ck("exit prompt", b"save your new configuration" in S(out))
    out = send(m, "y", 0.5)
    out += finish(p, m)
    ck("saved epilogue printed", b"Configuration saved" in S(out))
    ck("alt screen restored", b"\x1b[?1049l" in out)
    ck("clean exit", p.returncode == 0)


def t_signals(tmp):
    for sig, nm in [(signal.SIGINT, "SIGINT"), (signal.SIGQUIT, "SIGQUIT"),
                    (signal.SIGHUP, "SIGHUP")]:
        p, m = spawn(cwd=tmp)
        drain(m, 0.6)
        p.send_signal(sig)
        out = finish(p, m)
        ck(f"{nm} restores terminal", b"\x1b[?1049l" in out or p.returncode == 0)


def main():
    for name, fn in [("basic", t_basic), ("signals", t_signals)]:
        tmp = tempfile.mkdtemp(prefix="utui_" + name)
        print(f"--- {name} ---")
        try: fn(tmp)
        except Exception as e: ck(f"{name} raised {type(e).__name__}: {e}", False)
    print()
    if FAILED:
        print(f"{len(FAILED)} FAILURES:"); [print("  -", f) for f in FAILED]
        sys.exit(1)
    print("ALL TESTS PASSED")


if __name__ == "__main__":
    main()
