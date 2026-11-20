# ESP32-S3 Claude Desk Buddy

Un compagno da scrivania per **Claude Code**: una scheda ESP32-S3 con display AMOLED rotondo che
mostra **Clawd**, la mascotte di Claude Code, e ne cambia il comportamento secondo cio' che Claude
sta facendo. Non e' solo una notifica: e' un piccolo pet che lavora, pensa, si annoia, si stanca,
sale di livello e ti dice a colpo d'occhio cosa succede nelle tue sessioni.

> *English, in short:* a desk pet for Claude Code. An ESP32-S3 AMOLED board shows Clawd reacting to
> Claude Code hooks (thinking, working, needs you, done, errors, commits...), plus session list,
> usage limits, timer and more. Software is MIT-licensed. Documentation below is in Italian.

```
Claude Code ──hook / statusLine──▶ host/buddy.py ──USB seriale──▶ ESP32-S3 ──▶ schermo + audio
```

Progetto non ufficiale, non affiliato ad Anthropic (vedi [Marchi](#marchi-e-licenza)).

## Cosa fa

### Clawd reagisce a Claude Code

| Situazione | Cosa vedi |
|---|---|
| In attesa | passeggia e saluta; dopo 1 minuto si annoia e sbadiglia; di notte "Buonanotte" e dorme presto |
| Penso | mano sul mento e nuvoletta di pensiero |
| Lavoro | batte sulla tastiera, scintille; sotto una **riga scorrevole** col dettaglio (comando, file, pattern...) |
| Ops! | un tool e' fallito: scuote la testa con un "?" |
| Serve te! | salta, sfondo giallo pulsante e messaggio ("Permesso: Bash") |
| Fatto! | salta di gioia tra i coriandoli |
| Commit / Push / Test ok | festa con badge verde / razzetto che parte / pollice in su |
| Compattazione | si schiaccia e si riprende mentre Claude compatta il contesto |
| Memoria piena | si gonfia quando il contesto supera il 90% |
| Cache fredda | trema dal freddo quando la cache dei prompt e' scaduta |
| Stanco | occhi pesanti e sudore se l'usage e' vicino al limite; **allarme rosso** al 90% e "Puoi ripartire!" quando si azzera |
| Sub-agent | un mini Clawd sopra la testa per ogni agente attivo |
| Lista attivita' | "Task 3/7" con barra, dal `TodoWrite` |

Clawd **sale di livello** con l'uso (cappellino al lv 3, occhiali al 6, corona al 10, colori da
sbloccare) e reagisce anche a te: carezza con un tap, ride dopo 3 tocchi, si infastidisce dopo 6,
si stordisce se lo scuoti, festeggia a mezzanotte. Ci sono suoni per gli eventi principali.

### Pagine (swipe orizzontale)

Clawd · **Sessioni** · **Usage** · **Oggi** · **Timer** · **Messaggio** · **Stile** · **Info**

- **Sessioni**: una riga per sessione di Claude Code, con cartella (icona colorata per progetto),
  stato, tool in uso, % contesto, modello/effort e conto alla rovescia della cache.
- **Usage**: limiti a 5 ore e settimanale, contesto, costo e righe di oggi, batteria.
- **Oggi**: prompt, tool, ore di lavoro, costo, righe, livello.
- **Timer**: pomodoro 25/5 (tocca = avvia/pausa, tieni premuto = reset).
- **Messaggio**: ultima risposta di Claude, con scorrimento verticale.
- **Stile**: colore, cappello, occhiali, suono (tocca una riga). Si apre anche tenendo premuto su Clawd.
- **Info**: stato del PC e dei chip (touch, audio, movimento, RTC, PMU), memoria, versione.

Il tasto **BOOT** scorre gli stati di Clawd (utile per provare senza PC).

## Cosa serve

**Hardware:** [Waveshare ESP32-S3-Touch-AMOLED-1.75](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.75)
(provato sulla variante 1.75-B: display CO5300 466x466, touch CST9217, codec ES8311, IMU QMI8658,
PMU AXP2101, RTC PCF85063). Cavo USB-C verso il PC.

**Software:**
- Arduino IDE o `arduino-cli`, con il core **esp32 by Espressif 3.3.x** (provato con 3.3.12)
- Librerie: *GFX Library for Arduino* (con il driver CO5300; Waveshare la fornisce in
  `examples/arduino/libraries` del suo repository), *SensorLib*, *XPowersLib*
- **Python 3** nel `PATH` (nessuna dipendenza esterna)
- Claude Code con gli **hook** abilitati (testato su Windows; su Linux/macOS vedi le note sotto)

## Installazione

### 1. Firmware

Apri `claude_buddy/claude_buddy.ino` (insieme a `hw.h` e `hw.cpp`) e imposta in *Strumenti*:

| Impostazione | Valore |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | **Enabled** |
| PSRAM | **OPI PSRAM** (serve al canvas da ~430 KB) |
| Flash Size | 16MB |
| Partition Scheme | `app3M_fat9M_16MB` |

Oppure da riga di comando:

```
arduino-cli compile --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB" claude_buddy
arduino-cli upload  -p COM7 --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,PSRAM=opi,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB" claude_buddy
```

Se la scheda non entra in download mode: tieni premuto BOOT, premi RESET, rilascia BOOT.
Se lo swipe va nel verso sbagliato imposta `SWIPE_INVERT` a 1 in `claude_buddy.ino`.

### 2. Porta seriale

```
copy host\port.example.txt host\port.txt      (Windows)
cp host/port.example.txt host/port.txt        (Linux/macOS)
```

Scrivi in `host/port.txt` la porta della scheda (es. `COM7`; su Linux/macOS il percorso completo,
es. `/dev/ttyACM0`). Il numero puo' cambiare se la scheda si ri-enumera. **Chiudi il Serial Monitor**
dell'IDE: la porta puo' essere aperta da una sola applicazione alla volta.

### 3. Hook e statusLine di Claude Code

In `host/hooks.json` sostituisci `/PERCORSO/DEL/REPO` con il percorso reale del repository
(con `/` anche su Windows, es. `C:/Users/nome/Documents/ESP32-S3_Claude-Desk-Buddy`), poi unisci le
sezioni `hooks` e `statusLine` nel tuo `~/.claude/settings.json` (senza sovrascrivere quelle gia'
presenti) e **riavvia Claude Code**.

Gli hook usati sono `SessionStart`, `SessionEnd`, `UserPromptSubmit`, `PreToolUse`, `PostToolUse`,
`PostToolUseFailure`, `Notification`, `Stop`, `SubagentStart`, `SubagentStop` e `PreCompact`; tutti
chiamano `python -I host/buddy.py hook`. Lo statusLine chiama `buddy.py statusline`: ricava i dati
di usage, contesto, costo, modello e cache, e stampa anche una riga di stato dentro Claude Code.

Un hook che fallisce non disturba Claude Code: lo script esce sempre senza errori.

### Provare senza Claude Code

```
python host/buddy.py show              # stampa lo snapshot che verrebbe inviato alla scheda
python host/buddy.py send "attention"  # manda una riga grezza alla scheda
python host/buddy.py clear             # dimentica le sessioni
```

## Come funziona

Ogni sessione di Claude Code ha un file in `host/state/` (non versionato). A ogni evento `buddy.py`
aggiorna lo stato, ricalcola quello complessivo (priorita': *serve te* > *lavora* > *errore* >
*pensa* > *fatto* > *riposo*) e manda alla scheda uno **snapshot** di righe di testo:

| Riga | Significato |
|---|---|
| `idle`, `thinking`, `working [tool]`, `attention [msg]`, `done`, `error`, `sleep` | stato complessivo |
| `sessions N` · `sess i S ctx cache modello effort fast nome\|tool` | sessioni aperte |
| `usage 5h% 5h_min 7g% 7g_min ctx%` | limiti di usage (`-1` = non disponibile) |
| `agents N` · `todo fatti totali` · `pet xp` | sub-agent, lista attivita', esperienza |
| `stats centesimi righe+ righe-` · `today prompt tool minuti` | costi e contatori di oggi |
| `msg eta sessione\|testo` · `tick testo` | ultimo messaggio e riga scorrevole |
| `fx seq eta nome` | evento una tantum (commit, push, testok, testfail, compact) |
| `epoch secondi` · `time HH:MM` · `bright 0-255` · `sound 0\|1` | orologio, luminosita', suono |

Il protocollo e' solo testo su USB seriale (115200, una riga per comando), quindi si puo' provare
anche con un normale monitor seriale. I dettagli sono nel commento in testa a `host/buddy.py`.

## Struttura del repository

```
claude_buddy/
  claude_buddy.ino   firmware: display, Clawd, pagine, touch, protocollo
  hw.h / hw.cpp      audio (ES8311), movimento (QMI8658), RTC (PCF85063), batteria (AXP2101)
host/
  buddy.py           ponte hook/statusLine -> seriale
  hooks.json         hook e statusLine da unire in ~/.claude/settings.json
  port.example.txt   esempio di porta seriale
LICENSE              MIT
THIRD-PARTY-NOTICES.md   crediti e licenze di terze parti
```

## Limiti noti e problemi comuni

- **Il PC non invia nulla / la porta e' occupata**: chiudi il Serial Monitor e controlla `host/port.txt`.
- **Usage e limiti**: i limiti a 5 ore e settimanale arrivano dallo statusLine e solo per gli
  abbonati; compaiono dopo la prima risposta di Claude. Il costo e' una stima.
- **Lista attivita'**: legge `TodoWrite`; se la tua versione di Claude Code usa altri strumenti per
  le attivita', "Task N/M" non comparira'.
- **Eventi commit/test**: dedotti dal comando e dal testo di output di Bash, quindi possono sfuggire
  comandi insoliti.
- **Messaggi**: convertiti in ASCII (niente accenti ne' markdown) e limitati a ~380 caratteri.
- **Touch**: orientato come nel progetto TamaPoke (XY specchiati); se i tap risultano specchiati va ritoccato.
- **Linux/macOS**: lo script scrive direttamente sul device seriale; il progetto e' stato provato solo su Windows.
- **Cavo**: oggi serve il collegamento USB. Un collegamento Wi-Fi e l'approvazione dei permessi
  dalla scheda sono idee non ancora realizzate.

## Marchi e licenza

Questo e' un progetto non ufficiale e indipendente, **non affiliato ne' approvato da Anthropic**.
"Claude", "Claude Code" e la mascotte "Clawd" appartengono ad Anthropic; il disegno di Clawd nel
firmware e' una semplice riproduzione a blocchi.

Il codice di questo repository e' rilasciato con licenza **MIT** (vedi [`LICENSE`](LICENSE)).
Crediti e licenze delle componenti di terze parti sono in
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md).
