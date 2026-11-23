#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Claude Buddy - ponte tra Claude Code e la scheda (USB seriale o BLE).

Modi d'uso (tutti silenziosi: un errore qui non deve mai disturbare Claude Code):
  buddy.py hook         legge dallo stdin il JSON di un hook e aggiorna lo stato
  buddy.py statusline   legge il JSON dello statusLine (usage, contesto) e stampa
                        una riga di stato per Claude Code
  buddy.py send <riga>  manda una riga grezza alla scheda (prove)
  buddy.py show         stampa lo snapshot che verrebbe inviato
  buddy.py clear        dimentica tutte le sessioni
  buddy.py unpair                 la scheda dimentica i PC associati via BLE (usa l'USB)
  buddy.py standby|wake|off       mette la scheda in standby, la risveglia, la spegne
  buddy.py autostandby <minuti>   standby automatico a batteria (0 = mai)
  buddy.py ble start|stop|status  gestisce il demone BLE (vedi ble_bridge.py)

Trasporto: file host/transport.txt con "serial" (default), "ble" oppure "auto" (BLE se il
demone e' connesso alla scheda, altrimenti seriale).

Ogni sessione di Claude Code ha un file in host/state/<session_id>.json. A ogni
evento si ricalcola lo stato complessivo e si manda alla scheda uno snapshot:
  time HH:MM
  <stato> [dettaglio]       stato complessivo (idle|thinking|working|attention|done|sleep)
  sessions N
  sess <i> <S> <ctx> <cache_s> <modello> <effort> <fast> <nome>|<tool>
                            S = I/T/W/A/D, ctx = % contesto (-1 ignoto), cache_s = secondi
                            di cache prompt rimasti (0 = fredda, -1 ignoto), "-" = dato mancante
  usage <5h%> <5h_min> <7g%> <7g_min> <ctx%>     -1 = dato non disponibile
  agents N                  sub-agent attivi (mini Clawd)
  pet <xp>                  esperienza di Clawd (livello e accessori)
  stats <centesimi$> <righe+> <righe->    costo e righe di oggi
  epoch <secondi locali>    ora a muro, per l'orologio della scheda
  todo <fatti> <totali>     avanzamento della lista attivita' (TodoWrite)
  msg <eta_s> <sessione>|<testo>   ultimo messaggio di Claude
  tick <testo>              riga scorrevole: cosa sta facendo Claude (o inizio ultimo messaggio)
  today <prompt> <tool> <minuti>   contatori di oggi
  fx <seq> <eta_s> <nome>   evento una tantum: commit|push|testok|testfail|compact|pushfail|conflict|commitfail
"""
import json
import os
import re
import socket
import subprocess
import sys
import time
import unicodedata

TEST_CMD_RE = re.compile(r"\b(pytest|py\.test|jest|vitest|mocha|phpunit|rspec|go test|cargo test|dotnet test|ctest|"
                         r"npm (run )?test|yarn test|pnpm test|make test|tox|unittest)\b", re.I)
FAIL_RE = re.compile(r"\b[1-9]\d* (failed|failing|errors?)\b|\bFAILED\b|\bfailures?: ?[1-9]|AssertionError|\bFAIL\b", re.I)
PASS_RE = re.compile(r"\bpass(ed|ing)?\b|\bOK\b|test result: ok|\b0 failed\b", re.I)

HERE = os.path.dirname(os.path.abspath(__file__))
STATE_DIR = os.path.join(HERE, "state")
USAGE_FILE = os.path.join(STATE_DIR, "usage.json")
PET_FILE = os.path.join(STATE_DIR, "pet.json")
LAST_FILE = os.path.join(STATE_DIR, "last.json")
FX_FILE = os.path.join(STATE_DIR, "fx.json")
FX_SHOW_S = 15
MSG_MAX = 380
LOCK_FILE = os.path.join(STATE_DIR, ".lock")
BLE_PORT = 47633          # il demone BLE ascolta solo su 127.0.0.1
BLE_SPAWN_FILE = os.path.join(STATE_DIR, "ble_spawn.txt")
BLE_SPAWN_EVERY_S = 30    # al massimo un tentativo di avvio del demone ogni 30 s

DONE_SHOW_S = 4          # "fatto" vale solo per pochi secondi (< 5 s della scheda, poi la sessione e' a riposo)
ERROR_SHOW_S = 3.5       # durata della faccia perplessa dopo un tool fallito
AGENT_EXPIRE_S = 900     # un sub-agent senza SubagentStop dopo 15 min si considera finito
BUSY_STALE_S = 180       # thinking/working senza novita' -> a riposo (come sulla scheda)
SESSION_EXPIRE_S = 4 * 3600  # sessione senza alcun segnale: la dimentico (crash?)
MAX_ROWS = 5
PRIORITY = {"A": 0, "W": 1, "E": 1.5, "T": 2, "D": 3, "I": 4}
STATE_WORD = {"A": "attention", "W": "working", "E": "error", "T": "thinking", "D": "done", "I": "idle"}
XP_PER_EVENT = {"UserPromptSubmit": 3, "PreToolUse": 1, "Stop": 10}


# ---------------------------------------------------------------- utilita'
def clean(s, n):
    s = "".join(c if 32 <= ord(c) < 127 and c != "|" else "?" for c in str(s or ""))
    return s.strip()[:n]


def _active_ports_windows():
    """Porte COM attualmente presenti (non quelle ricordate da dispositivi scollegati)."""
    import winreg
    ports = set()
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM") as k:
            for i in range(winreg.QueryInfoKey(k)[1]):
                ports.add(str(winreg.EnumValue(k, i)[1]).upper())
    except OSError:
        pass
    return ports


def _espressif_ports_windows():
    """Porte COM dei dispositivi USB Espressif (VID 303A: la USB nativa dell'ESP32-S3) collegati ora."""
    import winreg
    active = _active_ports_windows()
    found = []
    try:
        usb = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SYSTEM\CurrentControlSet\Enum\USB")
    except OSError:
        return found
    with usb:
        for i in range(winreg.QueryInfoKey(usb)[0]):
            dev = winreg.EnumKey(usb, i)
            if not dev.upper().startswith("VID_303A"):
                continue
            try:
                with winreg.OpenKey(usb, dev) as dk:
                    for j in range(winreg.QueryInfoKey(dk)[0]):
                        with winreg.OpenKey(dk, winreg.EnumKey(dk, j) + r"\Device Parameters") as pk:
                            name = str(winreg.QueryValueEx(pk, "PortName")[0]).upper()
                            if name in active and name not in found:
                                found.append(name)
            except OSError:
                continue
    return found


def detect_port():
    """Porta seriale del buddy rilevata da sola ("" se non c'e')."""
    try:
        if os.name == "nt":
            ports = _espressif_ports_windows()
        else:
            import glob
            ports = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/cu.usbmodem*"))
        return ports[0] if ports else ""
    except Exception:
        return ""


def port_exists(port):
    if os.name == "nt":
        return port.upper() in _active_ports_windows()
    return os.path.exists(port)


def read_port():
    """host/port.txt: una porta (COM7, /dev/ttyACM0) oppure vuoto / "auto" per il rilevamento automatico.
    Se la porta scritta non esiste piu' (la scheda e' su un'altra porta) si ripiega sul rilevamento."""
    try:
        with open(os.path.join(HERE, "port.txt")) as f:
            p = f.read().strip()
    except OSError:
        p = ""
    if p and p.lower() != "auto" and port_exists(p):
        return p
    return detect_port() or (p if p.lower() != "auto" else "")


def read_transport():
    try:
        with open(os.path.join(HERE, "transport.txt")) as f:
            t = f.read().strip().lower()
    except OSError:
        return "serial"
    return t if t in ("serial", "ble", "auto") else "serial"


def load_json(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def save_json(path, data):
    tmp = path + ".tmp%d" % os.getpid()
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f)
    os.replace(tmp, path)


def session_path(sid):
    return os.path.join(STATE_DIR, clean(sid, 64).replace("?", "_") + ".json")


class Lock:
    """Un solo processo alla volta parla con la scheda / scrive lo stato."""

    def __enter__(self):
        os.makedirs(STATE_DIR, exist_ok=True)
        self.f = open(LOCK_FILE, "a+")
        try:
            import msvcrt
            for _ in range(40):
                try:
                    msvcrt.locking(self.f.fileno(), msvcrt.LK_NBLCK, 1)
                    self.kind = "msvcrt"
                    return self
                except OSError:
                    time.sleep(0.05)
        except ImportError:
            import fcntl
            fcntl.flock(self.f, fcntl.LOCK_EX)
            self.kind = "fcntl"
            return self
        self.kind = None  # timeout: si procede comunque
        return self

    def __exit__(self, *a):
        try:
            if self.kind == "msvcrt":
                import msvcrt
                msvcrt.locking(self.f.fileno(), msvcrt.LK_UNLCK, 1)
        except Exception:
            pass
        self.f.close()


# ---------------------------------------------------------------- sessioni
def tool_label(d):
    name = d.get("tool_name") or ""
    ti = d.get("tool_input") or {}
    if not isinstance(ti, dict):
        ti = {}
    if name == "Skill":
        return "Skill:" + str(ti.get("skill") or ti.get("name") or "")
    if name in ("Agent", "Task"):
        return "Agent:" + str(ti.get("subagent_type") or "")
    if name.startswith("mcp__"):
        parts = name.split("__")
        return "MCP:%s/%s" % (parts[1] if len(parts) > 1 else "?", "__".join(parts[2:]))
    return name


def effective_state(rec, now):
    st, age = rec.get("st", "I"), now - rec.get("ts", 0)
    if st == "D" and age > DONE_SHOW_S:
        return "I"
    if st in ("T", "W") and age > BUSY_STALE_S:
        return "I"
    if st == "T" and now - rec.get("err", 0) < ERROR_SHOW_S:
        return "E"
    return st


def all_sessions(now):
    out = []
    try:
        names = os.listdir(STATE_DIR)
    except OSError:
        return out
    for fn in names:
        if not fn.endswith(".json") or fn in ("usage.json", "pet.json", "last.json", "fx.json"):
            continue
        path = os.path.join(STATE_DIR, fn)
        rec = load_json(path)
        if not isinstance(rec, dict):
            continue
        if now - rec.get("ts", 0) > SESSION_EXPIRE_S:
            try:
                os.remove(path)
            except OSError:
                pass
            continue
        rec["eff"] = effective_state(rec, now)
        out.append(rec)
    # prima le sessioni che hanno piu' bisogno di te, a parita' la piu' recente
    out.sort(key=lambda r: (PRIORITY[r["eff"]], -r.get("ts", 0)))
    return out


def update_session(sid, **fields):
    path = session_path(sid)
    rec = load_json(path) or {"st": "I"}
    rec.update(fields)
    rec["ts"] = time.time()
    save_json(path, rec)


def clean_msg(text, markdown=True):
    """Testo del messaggio in ASCII su una riga (accenti -> lettere semplici, niente markdown)."""
    t = unicodedata.normalize("NFKD", str(text or ""))
    t = "".join(c for c in t if not unicodedata.combining(c))
    t = "".join(c if 32 <= ord(c) < 127 else " " for c in t)  # gli a-capo diventano spazi
    if markdown:
        t = re.sub(r"[`*#>_~]", "", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t[:MSG_MAX - 3] + "..." if len(t) > MSG_MAX else t


def tool_info(d):
    """Dettaglio breve di cosa fa il tool (comando, file, pattern...) per la riga scorrevole."""
    name = d.get("tool_name") or ""
    ti = d.get("tool_input")
    if not isinstance(ti, dict):
        ti = {}
    pick = lambda *keys: next((str(ti[k]) for k in keys if ti.get(k)), "")
    if name == "Bash":
        info = pick("command", "description")
    elif name in ("Read", "Edit", "Write", "MultiEdit", "NotebookEdit"):
        info = os.path.basename(pick("file_path", "notebook_path").replace("\\", "/"))
    elif name in ("Grep", "Glob"):
        info = pick("pattern")
    elif name in ("WebFetch",):
        info = pick("url")
    elif name == "WebSearch":
        info = pick("query")
    elif name == "Skill":
        info = pick("skill", "name")
    elif name in ("Agent", "Task"):
        info = pick("description", "subagent_type")
    elif name == "TodoWrite":
        info = "aggiorno la lista attivita"
    else:
        info = pick("description", "command", "file_path", "path", "query", "url", "pattern")
    return clean_msg(info, markdown=False)[:120]


def todo_progress(d):
    ti = d.get("tool_input") or {}
    todos = ti.get("todos") if isinstance(ti, dict) else None
    if not isinstance(todos, list):
        return None
    done = sum(1 for t in todos if isinstance(t, dict) and t.get("status") == "completed")
    return [done, len(todos)]


def today():
    return time.strftime("%Y-%m-%d")


def load_pet():
    p = load_json(PET_FILE)
    if not isinstance(p, dict):
        p = {}
    if p.get("day") != today():  # nuovo giorno: azzera i contatori giornalieri
        p.update(day=today(), cost=0.0, add=0, rm=0, prompts=0, tools=0, active_s=0)
    for k in ("prompts", "tools", "active_s"):
        p.setdefault(k, 0)
    p.setdefault("xp", 0)
    return p


def live_agents(rec, now):
    ag = rec.get("agents") or {}
    return {k: v for k, v in ag.items() if now - v < AGENT_EXPIRE_S}


def pretty_message(msg):
    msg = str(msg or "")
    marker = "permission to use "
    if marker in msg:
        return clean("@perm " + msg.split(marker, 1)[1].rstrip("."), 26)  # il firmware lo traduce
    return clean(msg, 26)


def add_fx(name):
    f = load_json(FX_FILE)
    seq = (f.get("seq", 0) if isinstance(f, dict) else 0) + 1
    save_json(FX_FILE, {"seq": seq, "name": name, "ts": time.time()})


def text_of(resp):
    if isinstance(resp, str):
        return resp[:6000]
    try:
        return json.dumps(resp, ensure_ascii=False)[:6000]
    except (TypeError, ValueError):
        return ""


def bash_fx(d):
    """Riconosce dall'esito di un comando Bash: commit, push, test passati o falliti."""
    ti = d.get("tool_input")
    cmd = str(ti.get("command", "")) if isinstance(ti, dict) else ""
    out = text_of(d.get("tool_response"))
    # un comando concatenato (git add && git commit && git push) vale come l'evento piu' importante: push > commit
    if re.search(r"\bgit\s+(-\S+\s+)*push\b", cmd) and not re.search(r"rejected|error:|fatal:", out, re.I):
        return "push"
    if re.search(r"\bgit\s+(-\S+\s+)*commit\b", cmd):
        if not re.search(r"nothing (added )?to commit|error:|fatal:", out, re.I):
            return "commit"
    elif re.search(r"\bgit\s+(-\S+\s+)*push\b", cmd):
        return None
    elif TEST_CMD_RE.search(cmd):
        if FAIL_RE.search(out):
            return "testfail"
        if PASS_RE.search(out):
            return "testok"
    return None


PUSH_FAIL_RE = re.compile(r"rejected|non-fast-forward|failed to push|permission denied|authentication failed|could not read (username|password)|fatal:", re.I)
CONFLICT_RE = re.compile(r"CONFLICT \(|automatic merge failed|could not apply|fix conflicts|unmerged paths|merge conflict", re.I)
COMMIT_FAIL_RE = re.compile(r"hook .*failed|pre-commit|commit-msg|husky|please tell me who you are|empty (commit )?message|error:|fatal:", re.I)
GIT_CONFLICT_CMD_RE = re.compile(r"\bgit\s+(-\S+\s+)*(merge|pull|rebase|cherry-pick|stash|checkout|switch|revert)\b")


def git_fail_fx(d):
    """Riconosce dal fallimento di un comando Bash un errore git: push rifiutato, conflitto, commit fallito."""
    ti = d.get("tool_input")
    cmd = str(ti.get("command", "")) if isinstance(ti, dict) else ""
    if "git" not in cmd:
        return None
    out = (str(d.get("error") or "") + " " + text_of(d.get("tool_response")))[:6000]
    if GIT_CONFLICT_CMD_RE.search(cmd) and CONFLICT_RE.search(out):
        return "conflict"
    if re.search(r"\bgit\s+(-\S+\s+)*push\b", cmd) and PUSH_FAIL_RE.search(out):
        return "pushfail"
    if re.search(r"\bgit\s+(-\S+\s+)*commit\b", cmd) and COMMIT_FAIL_RE.search(out):
        return "commitfail"
    return None


def handle_hook(d):
    ev = d.get("hook_event_name", "")
    sid = d.get("session_id")
    if not sid:
        return
    cwd = d.get("cwd") or ""
    name = os.path.basename(cwd.rstrip("/\\")) or cwd
    with Lock():
        gain = XP_PER_EVENT.get(ev, 0)
        if gain:
            pet = load_pet()
            pet["xp"] += gain
            if ev == "UserPromptSubmit":
                pet["prompts"] += 1
            elif ev == "PreToolUse":
                pet["tools"] += 1
            save_json(PET_FILE, pet)
        if ev == "SessionEnd":
            old = load_json(session_path(sid)) or {}
            pet = load_pet()  # il costo della sessione finita entra nel totale di oggi
            pet["cost"] += float(old.get("cost") or 0)
            pet["add"] += int(old.get("add") or 0)
            pet["rm"] += int(old.get("rm") or 0)
            save_json(PET_FILE, pet)
            try:
                os.remove(session_path(sid))
            except OSError:
                pass
        elif ev == "SessionStart":
            update_session(sid, st="I", tool="", name=name)
        elif ev == "UserPromptSubmit":
            update_session(sid, st="T", tool="", name=name, t0=time.time())
        elif ev == "PreToolUse":
            extra = {}
            if d.get("tool_name") == "TodoWrite":
                prog = todo_progress(d)
                if prog is not None:
                    extra["todo"] = prog
            extra["info"] = tool_info(d)
            update_session(sid, st="W", tool=tool_label(d), name=name, **extra)
        elif ev == "PostToolUse":
            fx = bash_fx(d) if d.get("tool_name") == "Bash" else None
            if fx == "testfail":
                update_session(sid, st="T", tool="", name=name, err=time.time())
            else:
                update_session(sid, st="T", tool="", name=name)
            if fx:
                add_fx(fx)
        elif ev == "Notification":
            nt = d.get("notification_type", "")
            if nt == "idle_prompt":
                update_session(sid, st="I", tool="", name=name)
            elif nt != "auth_success":
                update_session(sid, st="A", tool=pretty_message(d.get("message")), name=name)
        elif ev == "PreCompact":
            update_session(sid, st="T", tool="", name=name)
            add_fx("compact")
        elif ev == "Stop":
            old = load_json(session_path(sid)) or {}
            t0 = old.get("t0")
            if isinstance(t0, (int, float)) and 0 < time.time() - t0 < 7200:  # tempo di lavoro attivo
                pet = load_pet()
                pet["active_s"] += int(time.time() - t0)
                save_json(PET_FILE, pet)
            update_session(sid, st="D", tool="", name=name, t0=None)
            msg = clean_msg(d.get("last_assistant_message"))
            if msg:
                save_json(LAST_FILE, {"text": msg, "name": clean(name, 16) or "?", "ts": time.time()})
        elif ev == "PostToolUseFailure":
            if not d.get("is_interrupt"):
                update_session(sid, st="T", tool="", name=name, err=time.time())
                ti = d.get("tool_input")
                if d.get("tool_name") == "Bash" and isinstance(ti, dict) and TEST_CMD_RE.search(str(ti.get("command", ""))):
                    add_fx("testfail")
                elif d.get("tool_name") == "Bash":
                    gfx = git_fail_fx(d)
                    if gfx:
                        add_fx(gfx)
        elif ev in ("SubagentStart", "SubagentStop"):
            rec = load_json(session_path(sid)) or {"st": "I"}
            ag = live_agents(rec, time.time())
            aid = str(d.get("agent_id") or "")
            if ev == "SubagentStart":
                ag[aid] = time.time()
            else:
                ag.pop(aid, None)
            rec["agents"] = ag
            rec["name"] = rec.get("name") or name
            rec["ts"] = rec.get("ts", time.time())
            save_json(session_path(sid), rec)
        else:
            return
        send_snapshot()


def handle_statusline(d):
    sid = d.get("session_id")
    cw = d.get("context_window") or {}
    rl = d.get("rate_limits") or {}
    cwd = d.get("cwd") or (d.get("workspace") or {}).get("current_dir") or ""
    name = os.path.basename(cwd.rstrip("/\\")) or cwd
    ctx = cw.get("used_percentage")
    with Lock():
        if sid:
            # non cambia lo stato: lo decidono gli hook. Se la sessione non c'e' ancora
            # (era gia' aperta prima di installare gli hook) la creo a riposo.
            path = session_path(sid)
            rec = load_json(path) or {"st": "I", "ts": time.time()}
            rec["ctx"] = int(ctx) if isinstance(ctx, (int, float)) else -1
            rec["name"] = rec.get("name") or name
            cost = d.get("cost") or {}
            rec["model"] = clean((d.get("model") or {}).get("display_name") or "", 10).replace(" ", "_")
            rec["effort"] = clean((d.get("effort") or {}).get("level") or "", 8)
            rec["fast"] = bool(d.get("fast_mode"))
            pc = d.get("prompt_cache")
            if isinstance(pc, dict) and isinstance(pc.get("expires_at"), (int, float)):
                rec["cache_exp"] = pc["expires_at"]
            rec["cost"] = float(cost.get("total_cost_usd") or 0)
            rec["add"] = int(cost.get("total_lines_added") or 0)
            rec["rm"] = int(cost.get("total_lines_removed") or 0)
            save_json(path, rec)
        if rl:
            five, seven = rl.get("five_hour") or {}, rl.get("seven_day") or {}
            save_json(USAGE_FILE, {
                "five": five.get("used_percentage"), "five_reset": five.get("resets_at"),
                "seven": seven.get("used_percentage"), "seven_reset": seven.get("resets_at"),
                "ts": time.time()})
        send_snapshot()
    model = (d.get("model") or {}).get("display_name") or ""
    bits = [clean(model, 20)]
    if isinstance(ctx, (int, float)):
        bits.append("ctx %d%%" % ctx)
    five = (rl.get("five_hour") or {}).get("used_percentage")
    if isinstance(five, (int, float)):
        bits.append("5h %d%%" % round(five))
    print(" | ".join(b for b in bits if b))


# ---------------------------------------------------------------- invio
def pct(v):
    return int(round(v)) if isinstance(v, (int, float)) else -1


def minutes_left(epoch, now):
    if not isinstance(epoch, (int, float)) or epoch <= now:
        return -1
    return int((epoch - now) // 60)


def build_snapshot():
    now = time.time()
    sess = all_sessions(now)
    lines = ["time " + time.strftime("%H:%M")]
    if not sess:
        lines.append("sleep")
    else:
        top = sess[0]
        word = STATE_WORD[top["eff"]]
        extra = clean(top.get("tool"), 26) if top["eff"] in ("W", "A") else ""
        lines.append(word + (" " + extra if extra else ""))
    lines.append("sessions %d" % min(len(sess), MAX_ROWS))
    for i, r in enumerate(sess[:MAX_ROWS]):
        exp = r.get("cache_exp")
        cache = -1 if not isinstance(exp, (int, float)) else max(0, int(exp - now))
        lines.append("sess %d %s %d %d %s %s %s %s|%s" % (
            i, "T" if r["eff"] == "E" else r["eff"], pct(r.get("ctx")), cache,
            r.get("model") or "-", r.get("effort") or "-", "F" if r.get("fast") else "-", clean(r.get("name"), 16) or "?",
            clean(r.get("tool"), 26) if r["eff"] in ("W", "A") else ""))
    u = load_json(USAGE_FILE) or {}
    ctx = pct(sess[0].get("ctx")) if sess else -1
    lines.append("usage %d %d %d %d %d" % (
        pct(u.get("five")), minutes_left(u.get("five_reset"), now),
        pct(u.get("seven")), minutes_left(u.get("seven_reset"), now), ctx))
    pet = load_pet()
    agents = sum(len(live_agents(r, now)) for r in sess)
    cost = pet["cost"] + sum(float(r.get("cost") or 0) for r in sess)
    add = pet["add"] + sum(int(r.get("add") or 0) for r in sess)
    rm = pet["rm"] + sum(int(r.get("rm") or 0) for r in sess)
    todo = next((r["todo"] for r in sess if isinstance(r.get("todo"), list)
                 and len(r["todo"]) == 2 and r["todo"][1] > 0 and r["todo"][0] < r["todo"][1]), [0, 0])
    lines.append("todo %d %d" % (todo[0], todo[1]))
    last = load_json(LAST_FILE)
    if isinstance(last, dict) and last.get("text"):
        lines.append("msg %d %s|%s" % (int(now - last.get("ts", now)), last.get("name", "?"), last["text"]))
    # riga scorrevole: se lavora, il dettaglio del tool; altrimenti l'inizio dell'ultimo messaggio
    tick = ""
    if sess and sess[0]["eff"] == "W":
        tick = sess[0].get("info") or clean(sess[0].get("tool"), 26)
    elif isinstance(last, dict):
        tick = str(last.get("text") or "")[:110]
    lines.append("tick " + tick)
    lines.append("agents %d" % min(agents, 4))
    lines.append("pet %d" % pet["xp"])
    lines.append("today %d %d %d" % (pet["prompts"], pet["tools"], pet["active_s"] // 60))
    fx = load_json(FX_FILE)
    if isinstance(fx, dict) and now - fx.get("ts", 0) < FX_SHOW_S:
        lines.append("fx %d %d %s" % (fx.get("seq", 0), int(now - fx["ts"]), fx.get("name", "")))
    lines.append("stats %d %d %d" % (int(round(cost * 100)), add, rm))
    lt = time.localtime()
    lines.append("epoch %d" % (int(time.time()) + (lt.tm_gmtoff or 0)))
    return lines


def send_serial(lines):
    port = read_port()
    if not port:
        return
    try:
        with open("\\\\.\\" + port if os.name == "nt" else port, "wb", buffering=0) as f:
            f.write(("\n".join(lines) + "\n").encode("ascii", "replace"))
    except OSError:
        pass  # scheda scollegata o porta occupata (Serial Monitor aperto)


def ble_request(payload, timeout=0.6):
    """Parla col demone BLE. Ritorna la risposta ("OK" = scheda connessa e dati inoltrati,
    "NO" = demone attivo ma scheda non connessa) oppure None se il demone non c'e'."""
    try:
        with socket.create_connection(("127.0.0.1", BLE_PORT), timeout=timeout) as c:
            c.settimeout(timeout)
            c.sendall(payload)
            c.shutdown(socket.SHUT_WR)
            return c.recv(16).decode("ascii", "replace").strip()
    except OSError:
        return None


def start_ble_daemon(force=False):
    """Avvia ble_bridge.py staccato dal processo corrente (senza finestra)."""
    try:
        os.makedirs(STATE_DIR, exist_ok=True)
        last = load_json(BLE_SPAWN_FILE)
        if not force and isinstance(last, (int, float)) and time.time() - last < BLE_SPAWN_EVERY_S:
            return
        save_json(BLE_SPAWN_FILE, time.time())
        exe = sys.executable
        if os.name == "nt":
            w = os.path.join(os.path.dirname(exe), "pythonw.exe")
            if os.path.exists(w):
                exe = w
            flags = 0x00000008 | 0x00000200 | 0x08000000  # DETACHED | NEW_PROCESS_GROUP | NO_WINDOW
        else:
            flags = 0
        subprocess.Popen([exe, os.path.join(HERE, "ble_bridge.py")], stdin=subprocess.DEVNULL,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=flags,
                         close_fds=True, **({"start_new_session": True} if os.name != "nt" else {}))
    except Exception:
        pass


def send_lines(lines):
    mode = read_transport()
    if mode != "serial":
        reply = ble_request(("\n".join(lines) + "\n").encode("ascii", "replace"))
        if reply is None:
            start_ble_daemon()  # la prossima volta sara' pronto
        elif reply == "OK":
            return
        if mode == "ble":
            return
    send_serial(lines)


def send_snapshot():
    send_lines(build_snapshot())


# ---------------------------------------------------------------- main
def read_stdin_json():
    """Lo stdin di Windows non e' UTF-8 di default: si leggono i byte e si decodificano a mano."""
    return json.loads(sys.stdin.buffer.read().decode("utf-8", "replace"))


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "hook":
        handle_hook(read_stdin_json())
    elif mode == "statusline":
        handle_statusline(read_stdin_json())
    elif mode == "send":
        send_lines([" ".join(sys.argv[2:])])
    elif mode in ("standby", "wake", "off", "unpair"):
        send_lines([mode])
    elif mode == "autostandby":
        send_lines(["autostandby " + (sys.argv[2] if len(sys.argv) > 2 else "10")])
    elif mode == "ble":
        sub = sys.argv[2] if len(sys.argv) > 2 else "status"
        if sub == "start":
            start_ble_daemon(force=True)
        elif sub == "stop":
            ble_request(b"__quit__\n")
        else:
            r = ble_request(b"__ping__\n")
            print({"OK": "demone attivo, scheda connessa", "NO": "demone attivo, scheda non connessa"}.get(r, "demone non attivo"))
    elif mode == "show":
        print("\n".join(build_snapshot()))
    elif mode == "clear":
        with Lock():
            for fn in os.listdir(STATE_DIR):
                if fn.endswith(".json") and fn != "pet.json":
                    os.remove(os.path.join(STATE_DIR, fn))
        send_snapshot()


if __name__ == "__main__":
    try:
        main()
    except Exception:
        pass
    sys.exit(0)
