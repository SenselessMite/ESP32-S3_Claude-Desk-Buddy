// SPDX-License-Identifier: MIT
#pragma once
// Testi a schermo in cinque lingue: italiano, inglese, francese, tedesco, spagnolo.
// Il font di base e' solo ASCII: niente accenti (e/e, u/ue, n/n).
// Per aggiungere una lingua: una colonna in STR_LIST, una voce in LANG_NAMES e LANG_COUNT.
#include <stdint.h>

enum Lang : uint8_t { LANG_IT, LANG_EN, LANG_FR, LANG_DE, LANG_ES, LANG_COUNT };
static const char *const LANG_CODES[LANG_COUNT] = { "IT", "EN", "FR", "DE", "ES" };

//        ID                 italiano                 english                  francais                 deutsch                  espanol
#define STR_LIST(X) \
  X(TXT_WAITING,    "In attesa",             "Waiting",               "En attente",            "Bereit",                "En espera") \
  X(TXT_THINKING,   "Penso",                 "Thinking",              "Je reflechis",          "Ich denke",             "Pensando") \
  X(TXT_WORKING,    "Lavoro",                "Working",               "Je travaille",          "Ich arbeite",           "Trabajando") \
  X(TXT_NEEDYOU,    "Serve te!",             "Need you!",             "Besoin de toi!",        "Brauche dich!",         "Te necesito!") \
  X(TXT_DONE,       "Fatto!",                "Done!",                 "Termine!",              "Fertig!",               "Listo!") \
  X(TXT_OOPS,       "Ops!",                  "Oops!",                 "Oups!",                 "Ups!",                  "Ups!") \
  X(TXT_BATLOW,     "Batteria scarica",      "Battery low",           "Batterie faible",       "Akku schwach",          "Bateria baja") \
  X(TXT_MEMFULL,    "Memoria piena",         "Memory full",           "Memoire pleine",        "Speicher voll",         "Memoria llena") \
  X(TXT_TIRED,      "Stanco... usage %d%%",  "Tired... usage %d%%",   "Fatigue... usage %d%%", "Muede... Usage %d%%",   "Cansado... uso %d%%") \
  X(TXT_COLDCACHE,  "Cache fredda",          "Cold cache",            "Cache froid",           "Kalter Cache",          "Cache fria") \
  X(TXT_NIGHT,      "Buonanotte",            "Good night",            "Bonne nuit",            "Gute Nacht",            "Buenas noches") \
  X(TXT_BORED,      "Mi annoio...",          "I'm bored...",          "Je m'ennuie...",        "Langweilig...",         "Me aburro...") \
  X(TXT_DIZZY,      "Che giramenti!",        "So dizzy!",             "Ca tourne!",            "Mir ist schwindlig",    "Que mareo!") \
  X(TXT_BATFULL,    "Carica completa!",      "Fully charged!",        "Charge complete!",      "Voll geladen!",         "Carga completa!") \
  X(TXT_BATALERT,   "Batteria scarica!",     "Battery low!",          "Batterie faible!",      "Akku schwach!",         "Bateria baja!") \
  X(TXT_PLUGIN,     "Collegami al cavo",     "Plug me in",            "Branche-moi",           "Bitte laden",           "Conectame") \
  X(TXT_USAGEAL,    "Usage al %d%%!",        "Usage at %d%%!",        "Usage a %d%%!",         "Usage bei %d%%!",       "Uso al %d%%!") \
  X(TXT_RESETOK,    "Puoi ripartire!",       "Ready to go!",          "C'est reparti!",        "Es kann los!",          "Ya puedes seguir!") \
  X(TXT_COMPACT,    "Compatto...",           "Compacting...",         "Compactage...",         "Kompaktiere...",        "Compactando...") \
  X(TXT_ANNOYED,    "Basta!",                "Enough!",               "Ca suffit!",            "Genug!",                "Basta!") \
  X(TXT_LAUGH,      "Ahah!",                 "Haha!",                 "Haha!",                 "Haha!",                 "Jaja!") \
  X(TXT_MIDNIGHT,   "Mezzanotte!",           "Midnight!",             "Minuit!",               "Mitternacht!",          "Medianoche!") \
  X(TXT_LEVEL_UP,   "Livello %d!",           "Level %d!",             "Niveau %d!",            "Level %d!",             "Nivel %d!") \
  X(TXT_TESTFAIL,   "Test falliti",          "Tests failed",          "Tests echoues",         "Tests fehlgeschl.",     "Tests fallidos") \
  X(TXT_FX_PUSHFAIL,"Push rifiutato",        "Push rejected",         "Push refuse",           "Push abgelehnt",        "Push rechazado") \
  X(TXT_FX_CONFLICT,"Conflitto!",            "Conflict!",             "Conflit !",             "Konflikt!",             "Conflicto!") \
  X(TXT_FX_COMMITFAIL,"Commit fallito",        "Commit failed",         "Commit echoue",         "Commit fehlg.",         "Commit fallido") \
  X(TXT_FOCUS,      "Focus",                 "Focus",                 "Focus",                 "Fokus",                 "Foco") \
  X(TXT_BREAK,      "Pausa",                 "Break",                 "Pause",                 "Pause",                 "Pausa") \
  X(TXT_LV,         "Lv %d",                 "Lv %d",                 "Niv %d",                "Lv %d",                 "Nv %d") \
  X(TXT_PERM,       "Permesso: ",            "Permission: ",          "Permission: ",          "Erlaubnis: ",           "Permiso: ") \
  X(ST_W,           "Lavoro",                "Working",               "Travail",               "Arbeit",                "Trabajo") \
  X(ST_T,           "Penso",                 "Thinking",              "Reflexion",             "Denkt",                 "Piensa") \
  X(ST_A,           "Serve te!",             "Needs you!",            "Besoin!",               "Du bist dran",          "Tu turno!") \
  X(ST_D,           "Fatto",                 "Done",                  "Fini",                  "Fertig",                "Hecho") \
  X(ST_I,           "A riposo",              "Idle",                  "Repos",                 "Ruhe",                  "Reposo") \
  X(TXT_SESSIONS,   "Sessioni: %d",          "Sessions: %d",          "Sessions: %d",          "Sitzungen: %d",         "Sesiones: %d") \
  X(TXT_NOSESS1,    "Nessuna sessione",      "No Claude Code",        "Aucune session",        "Keine Sitzung",         "Ninguna sesion") \
  X(TXT_NOSESS2,    "di Claude Code",        "sessions",              "Claude Code",           "von Claude Code",       "de Claude Code") \
  X(TXT_CACHECOLD,  "cache fredda",          "cache cold",            "cache froid",           "Cache kalt",            "cache fria") \
  X(TXT_FIVEH,      "Sessione 5 ore",        "5-hour session",        "Session 5 h",           "5-Std-Sitzung",         "Sesion 5 h") \
  X(TXT_WEEK,       "Settimana",             "Week",                  "Semaine",               "Woche",                 "Semana") \
  X(TXT_RING_WEEK,  "7g",                    "7d",                    "7j",                    "7T",                    "7d") \
  X(TXT_CONTEXT,    "Contesto",              "Context",               "Contexte",              "Kontext",               "Contexto") \
  X(TXT_TODAYCOST,  "Oggi $%d.%02d  +%d -%d", "Today $%d.%02d  +%d -%d", "Auj. $%d.%02d  +%d -%d", "Heute $%d.%02d  +%d -%d", "Hoy $%d.%02d  +%d -%d") \
  X(TXT_LIMITS,     "limiti: solo abbonati", "limits: subscribers only", "limites: abonnes",    "Limits: nur Abo",       "limites: suscriptores") \
  X(TXT_NA,         "n/d",                   "n/a",                   "n/d",                   "k.A.",                  "n/d") \
  X(TXT_DAYFMT,     "%dg %dh",               "%dd %dh",               "%dj %dh",               "%dT %dh",               "%dd %dh") \
  X(TXT_CHARGING,   " carica",               " charging",             " en charge",            " laedt",                " cargando") \
  X(TXT_TODAY,      "Oggi",                  "Today",                 "Aujourd'hui",           "Heute",                 "Hoy") \
  X(TXT_L_WORK,     "Lavoro",                "Work",                  "Travail",               "Arbeit",                "Trabajo") \
  X(TXT_L_COST,     "Costo",                 "Cost",                  "Cout",                  "Kosten",                "Coste") \
  X(TXT_L_LINES,    "Righe",                 "Lines",                 "Lignes",                "Zeilen",                "Lineas") \
  X(TXT_L_LEVEL,    "Livello",               "Level",                 "Niveau",                "Level",                 "Nivel") \
  X(TXT_TAP_PAUSE,  "tocca: pausa",          "tap: pause",            "toucher: pause",        "tippen: Pause",         "toca: pausa") \
  X(TXT_TAP_START,  "tocca: avvia",          "tap: start",            "toucher: start",        "tippen: Start",         "toca: iniciar") \
  X(TXT_HOLD_RESET, "tieni premuto: reset",  "hold: reset",           "maintenir: reset",      "halten: Reset",         "mantener: reset") \
  X(TXT_LASTMSG,    "Ultimo messaggio",      "Last message",          "Dernier message",       "Letzte Nachricht",      "Ultimo mensaje") \
  X(TXT_NOMSG,      "Nessun messaggio",      "No messages",           "Aucun message",         "Keine Nachricht",       "Sin mensajes") \
  X(TXT_AGE_NOW,    "%s - ora",              "%s - now",              "%s - maint.",           "%s - jetzt",            "%s - ahora") \
  X(TXT_AGE_MIN,    "%s - %u min fa",        "%s - %u min ago",       "%s - %u min",           "%s - vor %u Min",       "%s - hace %u min") \
  X(TXT_AGE_HOUR,   "%s - %u h fa",          "%s - %u h ago",         "%s - %u h",             "%s - vor %u Std",       "%s - hace %u h") \
  X(TXT_STY_COLOR,  "COLORE",                "COLOR",                 "COULEUR",               "FARBE",                 "COLOR") \
  X(TXT_STY_HAT,    "CAPPELLO",              "HAT",                   "CHAPEAU",               "HUT",                   "SOMBRERO") \
  X(TXT_STY_GLASS,  "OCCHIALI",              "GLASSES",               "LUNETTES",              "BRILLE",                "GAFAS") \
  X(TXT_STY_SOUND,  "SUONO",                 "SOUND",                 "SON",                   "TON",                   "SONIDO") \
  X(TXT_STY_COSTUME, "COSTUME",              "COSTUME",               "COSTUME",               "KOSTUEM",               "DISFRAZ")   X(COS_0,          "Nessuno",               "None",                  "Aucun",                 "Keiner",                "Ninguno")   X(COS_1,          "Zelda",                 "Zelda",                 "Zelda",                 "Zelda",                 "Zelda")   X(COS_2,          "Dark Souls",            "Dark Souls",            "Dark Souls",            "Dark Souls",            "Dark Souls")   X(TXT_ON,         "acceso",                "on",                    "actif",                 "an",                    "activo") \
  X(TXT_OFF,        "spento",                "off",                   "coupe",                 "aus",                   "mudo") \
  X(COLOR_0,        "Arancio",               "Orange",                "Orange",                "Orange",                "Naranja") \
  X(COLOR_1,        "Blu",                   "Blue",                  "Bleu",                  "Blau",                  "Azul") \
  X(COLOR_2,        "Verde",                 "Green",                 "Vert",                  "Gruen",                 "Verde") \
  X(COLOR_3,        "Viola",                 "Purple",                "Violet",                "Lila",                  "Morado") \
  X(COLOR_4,        "Rosa",                  "Pink",                  "Rose",                  "Rosa",                  "Rosa") \
  X(COLOR_5,        "Oro",                   "Gold",                  "Or",                    "Gold",                  "Oro") \
  X(HAT_0,          "Nessuno",               "None",                  "Aucun",                 "Keiner",                "Ninguno") \
  X(HAT_1,          "Berretto",              "Cap",                   "Casquette",             "Kappe",                 "Gorra") \
  X(HAT_2,          "Corona",                "Crown",                 "Couronne",              "Krone",                 "Corona") \
  X(HAT_3,          "Auto",                  "Auto",                  "Auto",                  "Auto",                  "Auto") \
  X(HAT_4,          "Cilindro",              "Top hat",               "Haut-forme",            "Zylinder",              "Chistera") \
  X(HAT_5,          "Mago",                  "Wizard",                "Magicien",              "Zauberer",              "Mago") \
  X(HAT_6,          "Cuoco",                 "Chef",                  "Cuisinier",             "Koch",                  "Cocinero") \
  X(HAT_7,          "Elica",                 "Propeller",             "Helice",                "Propeller",             "Helice") \
  X(GLS_0,          "No",                    "No",                    "Non",                   "Nein",                  "No") \
  X(GLS_1,          "Si",                    "Yes",                   "Oui",                   "Ja",                    "Si") \
  X(GLS_2,          "Auto",                  "Auto",                  "Auto",                  "Auto",                  "Auto") \
  X(GLS_3,          "Thug Life",             "Thug Life",             "Thug Life",             "Thug Life",             "Thug Life") \
  X(GLS_4,          "Sole",                  "Shades",                "Soleil",                "Sonne",                 "Sol") \
  X(GLS_5,          "Cuori",                 "Hearts",                "Coeurs",                "Herzen",                "Corazon") \
  X(GLS_6,          "Monocolo",              "Monocle",               "Monocle",               "Monokel",               "Monoculo") \
  X(GLS_7,          "3D",                    "3D",                    "3D",                    "3D",                    "3D") \
  X(TXT_LEVEL_N,    "Livello %d",            "Level %d",              "Niveau %d",             "Level %d",              "Nivel %d") \
  X(TXT_PCDATA_S,   "PC: dati %us fa",       "PC: data %us ago",      "PC: donnees %us",       "PC: Daten %us",         "PC: datos %us") \
  X(TXT_PCDATA_M,   "PC: dati %um fa",       "PC: data %um ago",      "PC: donnees %um",       "PC: Daten %um",         "PC: datos %um") \
  X(TXT_PCNODATA,   "PC: nessun dato",       "PC: no data",           "PC: aucune donnee",     "PC: keine Daten",       "PC: sin datos") \
  X(TXT_UPTIME,     "Acceso: %uh %02um",     "Uptime: %uh %02um",     "Actif: %uh %02um",      "Laufzeit: %uh %02um",   "Activo: %uh %02um") \
  X(TXT_TIME,       "Ora: %s (%s)",          "Time: %s (%s)",         "Heure: %s (%s)",        "Zeit: %s (%s)",         "Hora: %s (%s)") \
  X(TXT_TIMEUNSET,  "Ora: non impostata",    "Time: not set",         "Heure: non reglee",     "Zeit: nicht gesetzt",   "Hora: sin ajustar") \
  X(TXT_MOTION,     "Moto",                  "Motion",                "Motion",                "Motion",                "Motion") \
  X(TXT_SETTINGS,   "Impostazioni",          "Settings",              "Reglages",              "Einstellungen",         "Ajustes") \
  X(TXT_VOLUME,     "Volume",                "Volume",                "Volume",                "Lautst.",               "Volumen") \
  X(TXT_AUTOVOL,    "Vol. auto",             "Auto vol.",             "Vol. auto",             "Auto-Vol.",             "Vol. auto") \
  X(TXT_AUTOROT,    "Rotazione",             "Rotation",              "Rotation",              "Drehung",               "Rotacion") \
  X(TXT_BRIGHT,     "Luminosita'",         "Brightness",            "Luminosite",            "Helligkeit",            "Brillo") \
  X(TXT_STANDBY,    "Standby",               "Standby",               "Veille",                "Standby",               "En espera") \
  X(TXT_LANGUAGE,   "Lingua",                "Language",              "Langue",                "Sprache",               "Idioma") \
  X(TXT_MUTE,       "muto",                  "mute",                  "muet",                  "stumm",                 "mudo") \
  X(TXT_NEVER,      "mai",                   "never",                 "jamais",                "nie",                   "nunca") \
  X(TXT_PAIR,       "Associa PC",            "Pair PC",               "Associer PC",           "PC koppeln",            "Vincular PC") \
  X(TXT_CANCEL,     "Annulla",               "Cancel",                "Annuler",               "Abbruch",               "Cancelar") \
  X(TXT_FORGET,     "Dimentica",             "Forget",                "Oublier",               "Vergessen",             "Olvidar") \
  X(TXT_POWEROFF,   "Spegni",                "Power off",             "Eteindre",              "Ausschalten",           "Apagar") \
  X(TXT_EXIT,       "Esci",                  "Exit",                  "Quitter",               "Zurueck",               "Salir") \
  X(TXT_SET_HINT,   "BOOT: scorri  POWER: ok", "BOOT: next  POWER: ok", "BOOT: suiv.  POWER: ok", "BOOT: weiter  POWER: ok", "BOOT: sig.  POWER: ok") \
  X(TXT_FORGOT,     "PC dimenticati",        "PCs forgotten",         "PC oublies",            "PCs vergessen",         "PC olvidados") \
  X(TXT_SURE_FORGET, "Dimenticare i PC?",    "Forget all PCs?",       "Oublier les PC?",       "Alle PCs vergessen?",   "Olvidar los PC?") \
  X(TXT_SURE_FORGET2, "Dovranno riassociarsi", "They must pair again",  "A reassocier ensuite",  "Neu koppeln noetig",    "Habra que vincular") \
  X(TXT_SURE_OFF,   "Spegnere la scheda?",   "Power off?",            "Eteindre?",             "Ausschalten?",          "Apagar?") \
  X(TXT_POWERING,"Spegnimento...",        "Powering off...",       "Extinction...",         "Ausschalten...",        "Apagando...") \
  X(TXT_PAIRING,    "Associazione",          "Pairing",               "Association",           "Kopplung",              "Vinculacion") \
  X(TXT_PAIR_L1,    "Sul PC apri",           "On the PC open",        "Sur le PC ouvre",       "Am PC oeffne",          "En el PC abre") \
  X(TXT_PAIR_L2,    "Bluetooth > Aggiungi",  "Bluetooth > Add",       "Bluetooth > Ajouter",   "Bluetooth > Hinzu",     "Bluetooth > Anadir") \
  X(TXT_PAIR_L3,    "dispositivo",           "device",                "un appareil",           "Geraet",                "dispositivo") \
  X(TXT_PAIR_L4,    "e scegli",              "and choose",            "et choisis",            "und waehle",            "y elige") \
  X(TXT_TYPECODE,   "Digita questo codice",  "Type this code",        "Saisis ce code",        "Code eingeben",         "Escribe este codigo") \
  X(TXT_ONPC,       "sul PC",                "on the PC",             "sur le PC",             "am PC",                 "en el PC") \
  X(TXT_PAIR_FOOT,  "Bluetooth > Aggiungi dispositivo", "Bluetooth > Add device", "Bluetooth > Ajouter appareil", "Bluetooth > Geraet hinzufuegen", "Bluetooth > Anadir dispositivo") \
  X(TXT_ACT_BALL,   "Gioco a palla",         "Playing ball",          "Je joue au ballon",     "Ich spiele Ball",       "Jugando a la pelota") \
  X(TXT_ACT_READ,   "Leggo un libro",        "Reading a book",        "Je lis un livre",       "Ich lese ein Buch",     "Leyendo un libro") \
  X(TXT_ACT_COFFEE, "Pausa caffe",           "Coffee break",          "Pause cafe",            "Kaffeepause",           "Hora del cafe") \
  X(TXT_ACT_MUSIC,  "Ascolto musica",        "Listening to music",    "J'ecoute de la musique", "Ich hoere Musik",      "Escuchando musica") \
  X(DEMO_0,  "In attesa",       "Waiting",       "En attente",    "Bereit",        "En espera") \
  X(DEMO_1,  "Penso",           "Thinking",      "Reflexion",     "Denkt",         "Pensando") \
  X(DEMO_2,  "Lavoro",          "Working",       "Travail",       "Arbeitet",      "Trabajando") \
  X(DEMO_3,  "Errore",          "Error",         "Erreur",        "Fehler",        "Error") \
  X(DEMO_4,  "Serve te",        "Needs you",     "Besoin de toi", "Braucht dich",  "Te necesita") \
  X(DEMO_5,  "Fatto",           "Done",          "Termine",       "Fertig",        "Listo") \
  X(DEMO_6,  "Dorme",           "Sleeping",      "Dort",          "Schlaeft",      "Durmiendo") \
  X(DEMO_7,  "Annoiato",        "Bored",         "Ennuye",        "Gelangweilt",   "Aburrido") \
  X(DEMO_8,  "Stanco",          "Tired",         "Fatigue",       "Muede",         "Cansado") \
  X(DEMO_9,  "Memoria piena",   "Memory full",   "Memoire pleine", "Speicher voll", "Memoria llena") \
  X(DEMO_10, "Cache fredda",    "Cold cache",    "Cache froid",   "Kalter Cache",  "Cache fria") \
  X(DEMO_11, "Batt. scarica",   "Low battery",   "Batt. faible",  "Akku schwach",  "Bateria baja") \
  X(DEMO_12, "Allarme batt.",   "Battery alarm", "Alarme batt.",  "Akku-Alarm",    "Alarma bat.") \
  X(DEMO_13, "Carica completa", "Fully charged", "Charge complete", "Voll geladen", "Carga completa") \
  X(DEMO_14, "Commit",          "Commit",        "Commit",        "Commit",        "Commit") \
  X(DEMO_15, "Push",            "Push",          "Push",          "Push",          "Push") \
  X(DEMO_16, "Test ok",         "Tests OK",      "Tests OK",      "Tests OK",      "Tests OK") \
  X(DEMO_17, "Compattazione",   "Compacting",    "Compactage",    "Kompaktiert",   "Compactando") \
  X(DEMO_18, "Livello su",      "Level up",      "Niveau sup.",   "Level-Up",      "Subir nivel") \
  X(DEMO_19, "Allarme usage",   "Usage alarm",   "Alarme usage",  "Usage-Alarm",   "Alarma uso") \
  X(DEMO_20, "Limite azzerato", "Limit reset",   "Limite RAZ",    "Limit zurueck", "Limite reiniciado") \
  X(DEMO_21, "Carezza",         "Petting",       "Caresse",       "Streicheln",    "Caricia") \
  X(DEMO_22, "Risata",          "Laughing",      "Rire",          "Lacht",         "Risa") \
  X(DEMO_23, "Infastidito",     "Annoyed",       "Agace",         "Veraergert",    "Molesto") \
  X(DEMO_24, "Stordito",        "Dizzy",         "Etourdi",       "Schwindlig",    "Mareado") \
  X(DEMO_25, "Mezzanotte",      "Midnight",      "Minuit",        "Mitternacht",   "Medianoche") \
  X(DEMO_26, "Gioco a palla",   "Playing ball",  "Jeu de ballon", "Ballspiel",     "Pelota") \
  X(DEMO_27, "Lettura",         "Reading",       "Lecture",       "Lesen",         "Lectura") \
  X(DEMO_28, "Pausa caffe",     "Coffee break",  "Pause cafe",    "Kaffeepause",   "Cafe") \
  X(DEMO_29, "Musica",          "Music",         "Musique",       "Musik",         "Musica") \
  X(DEMO_30, "Turbo",           "Turbo",         "Turbo",         "Turbo",         "Turbo") \
  X(DEMO_31, "Cuffie",          "Headphones",    "Casque",        "Kopfhoerer",    "Auriculares") \
  X(DEMO_32, "Caccia al bug",   "Bug hunt",      "Chasse au bug", "Bug-Jagd",      "Caza de bug") \
  X(DEMO_33, "Martello",        "Hammer",        "Marteau",       "Hammer",        "Martillo") \
  X(DEMO_34, "Ingranaggi",      "Gears",         "Engrenages",    "Zahnraeder",    "Engranajes") \
  X(DEMO_35,        "Push rifiutato",        "Push rejected",         "Push refuse",           "Push abgelehnt",        "Push rechazado") \
  X(DEMO_36,        "Conflitto git",         "Git conflict",          "Conflit git",           "Git-Konflikt",          "Conflicto git") \
  X(DEMO_37,        "Commit fallito",        "Commit failed",         "Commit echoue",         "Commit fehlg.",         "Commit fallido")

enum StrId : uint16_t {
#define X(id, ...) id,
  STR_LIST(X)
#undef X
  STR_COUNT
};

static const char *const STRS[STR_COUNT][LANG_COUNT] = {
#define X(id, it, en, fr, de, es) { it, en, fr, de, es },
  STR_LIST(X)
#undef X
};

static uint8_t gLang = LANG_IT;  // lingua corrente (salvata in flash)

static inline const char *TR(StrId id) { return STRS[id][gLang]; }
