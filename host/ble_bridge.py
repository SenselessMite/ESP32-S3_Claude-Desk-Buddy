#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Claude Buddy - demone BLE.

Tiene aperta la connessione BLE con la scheda (Nordic UART Service) e inoltra le righe che
gli mandano gli hook di buddy.py su un socket locale (solo 127.0.0.1). Cosi' ogni hook paga
pochi millisecondi invece dei secondi di una nuova connessione BLE.

Serve la libreria bleak:   pip install bleak

Di solito non va lanciato a mano: buddy.py lo avvia da solo quando host/transport.txt
contiene "ble" o "auto". Per gestirlo:  python host/buddy.py ble start|stop|status
Il demone si chiude da solo dopo 2 ore senza hook; il prossimo hook lo riavvia.
"""
import asyncio
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import buddy  # noqa: E402  (stesso stato e stesso snapshot degli hook)

NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
DEVICE_NAME = "Claude Buddy"
SCAN_S = 6
RETRY_S = 6
RETRY_MAX_S = 30        # scheda spenta/lontana: scansioni sempre piu' rade, mai riavvii
FAIL_RESTART = 4        # la scheda si vede ma la connessione fallisce/si blocca N volte di fila
RESTART_MIN_S = 600     # al massimo un riavvio ogni 10 minuti
MISS_RESTART = 12       # scansioni a vuoto (con hook recenti) prima di riavviare
RETRY_MAX_FAIL_S = 60   # attesa massima tra tentativi falliti con la scheda in vista
SCAN_HARD_S = SCAN_S + 10
CONNECT_HARD_S = 30
WRITE_HARD_S = 10
IDLE_EXIT_S = 2 * 3600
LOG_FILE = os.path.join(buddy.STATE_DIR, "ble.log")
# comandi "una tantum": se la scheda non e' connessa si scartano (non vanno rieseguiti dopo)
# gli altri sono snapshot, e alla connessione se ne manda uno nuovo


def log(msg):
    try:
        os.makedirs(buddy.STATE_DIR, exist_ok=True)
        if os.path.exists(LOG_FILE) and os.path.getsize(LOG_FILE) > 100_000:
            os.remove(LOG_FILE)
        with open(LOG_FILE, "a", encoding="utf-8") as f:
            f.write("%s %s\n" % (time.strftime("%H:%M:%S"), msg))
    except OSError:
        pass


try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    log("bleak non installato: pip install bleak")
    sys.exit(2)


class Bridge:
    def __init__(self):
        self.client = None
        self.queue = asyncio.Queue(maxsize=64)
        self.last_hook = time.time()
        self.stop = asyncio.Event()
        self.restart = False

    @property
    def connected(self):
        return self.client is not None and self.client.is_connected

    # ------------------------------------------------------------ lato PC (hook)
    async def on_hook(self, reader, writer):
        try:
            data = await asyncio.wait_for(reader.read(65536), 2)
            self.last_hook = time.time()
            text = data.decode("ascii", "replace")
            if text.strip() == "__quit__":
                writer.write(b"OK\n")
                await writer.drain()
                self.stop.set()
                return
            if text.strip() == "__ping__":
                reply = b"OK\n" if self.connected else b"NO\n"
            else:
                reply = b"OK\n" if self.connected else b"NO\n"
                if self.connected and data:
                    try:
                        self.queue.put_nowait(data)
                    except asyncio.QueueFull:
                        pass
            writer.write(reply)
            await writer.drain()
        except Exception as e:  # un client sbagliato non deve fermare il demone
            log("hook: %r" % e)
        finally:
            try:
                writer.close()
            except Exception:
                pass

    # ------------------------------------------------------------ lato scheda (BLE)
    async def find_device(self):
        def match(dev, adv):
            uuids = [u.lower() for u in (adv.service_uuids or [])]
            return NUS_SERVICE in uuids or (adv.local_name or dev.name) == DEVICE_NAME

        return await BleakScanner.find_device_by_filter(match, timeout=SCAN_S)

    async def write(self, client, data):
        size = max(20, min((getattr(client, "mtu_size", 23) or 23) - 3, 240))
        for i in range(0, len(data), size):
            await asyncio.wait_for(client.write_gatt_char(NUS_RX, data[i:i + size], response=True), WRITE_HARD_S)

    async def ble_loop(self):
        misses = 0   # scansioni senza trovare la scheda (spenta o lontana: solo attesa piu' lunga)
        fails = 0    # scheda vista ma connessione/scrittura fallita (stato BLE di Windows incastrato o associazione scaduta)
        last_err = ""
        while not self.stop.is_set():
            try:
                dev = await asyncio.wait_for(self.find_device(), SCAN_HARD_S)
            except Exception as e:
                log("scansione: %r" % e)
                dev = None
            if dev is None:
                misses += 1
                # scheda mai vista ma il PC sta lavorando: lo scanner di Windows puo' essere incastrato
                if misses >= MISS_RESTART and time.time() - self.last_hook < 600 and self.can_restart():
                    log("scheda non vista da %d scansioni con hook recenti: riavvio il demone" % misses)
                    self.restart = True
                    self.stop.set()
                    return
                await self.sleep(min(RETRY_S * misses, RETRY_MAX_S))
                continue
            misses = 0
            healthy = False   # almeno una scrittura riuscita
            t_conn = time.time()
            lost = asyncio.Event()
            # dopo due fallimenti si forza una nuova scoperta dei servizi (la cache di Windows puo' essere sporca)
            kwargs = {"winrt": {"use_cached_services": False}} if fails >= 2 and sys.platform == "win32" else {}
            client = BleakClient(dev, disconnected_callback=lambda _c: lost.set(), services=[NUS_SERVICE],
                                 timeout=15, **kwargs)
            try:
                await asyncio.wait_for(client.connect(), CONNECT_HARD_S)
                log("connesso a %s" % dev.address)
                self.client = client
                while not self.queue.empty():
                    self.queue.get_nowait()
                # snapshot fresco appena connessi
                await self.write(client, ("\n".join(buddy.build_snapshot()) + "\n").encode("ascii", "replace"))
                healthy = True
                fails = 0
                last_err = ""
                while client.is_connected and not lost.is_set() and not self.stop.is_set():
                    try:
                        data = await asyncio.wait_for(self.queue.get(), 2)
                    except asyncio.TimeoutError:
                        continue
                    await self.write(client, data)
            except Exception as e:
                err = repr(e)
                if err != last_err or fails % 10 == 0:  # niente righe identiche a raffica
                    log("connessione: %s" % err)
                last_err = err
            finally:
                if self.client is not None:
                    log("disconnesso")
                self.client = None
                try:
                    await asyncio.wait_for(client.disconnect(), 8)
                except Exception:
                    pass
            lived = time.time() - t_conn
            ok = healthy and lived >= 10   # una connessione che cade dopo pochi secondi non e' "riuscita"
            if ok:
                fails = 0
                await self.sleep(2)
                continue
            fails += 1
            if fails == 3:
                log("3 fallimenti di fila con la scheda in vista: probabile associazione scaduta. Rimuovi \"Claude Buddy\" "
                    "da Impostazioni > Bluetooth e riassocia dal menu della scheda (Impostazioni > Bluetooth)")
            if fails >= FAIL_RESTART and self.can_restart():
                log("%d connessioni fallite con la scheda in vista: riavvio il demone" % fails)
                self.restart = True
                self.stop.set()
                return
            # backoff: la scheda non va martellata (ogni tentativo la tiene sveglia e la scalda)
            await self.sleep(min(3 * fails, RETRY_MAX_FAIL_S))

    @staticmethod
    def can_restart():
        return time.time() - float(os.environ.get("BUDDY_LAST_RESTART", "0")) > RESTART_MIN_S

    async def sleep(self, s):
        try:
            await asyncio.wait_for(self.stop.wait(), s)
        except asyncio.TimeoutError:
            pass

    async def watchdog(self):
        while not self.stop.is_set():
            await self.sleep(60)
            if time.time() - self.last_hook > IDLE_EXIT_S:
                log("inattivo da 2 ore: esco")
                self.stop.set()

    async def run(self):
        server = None
        for _ in range(10):  # dopo un riavvio il vecchio socket puo' metterci un attimo a liberarsi
            try:
                server = await asyncio.start_server(self.on_hook, "127.0.0.1", buddy.BLE_PORT)
                break
            except OSError:
                await asyncio.sleep(0.5)
        if server is None:
            return  # un altro demone e' gia' attivo
        log("demone avviato")
        tasks = [asyncio.create_task(self.ble_loop()), asyncio.create_task(self.watchdog())]
        await self.stop.wait()
        server.close()
        for t in tasks:
            t.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        log("demone fermato")


if __name__ == "__main__":
    bridge = Bridge()
    try:
        asyncio.run(bridge.run())
    except Exception as e:
        log("errore fatale: %r" % e)
    if bridge.restart:  # rilancia se stesso da zero (azzera lo stato BLE incastrato)
        os.environ["BUDDY_LAST_RESTART"] = str(time.time())
        time.sleep(1)
        os.execv(sys.executable, [sys.executable] + sys.argv)
