# Note sulle componenti di terze parti

Il codice di questo repository e' rilasciato con licenza MIT (vedi `LICENSE`). Il progetto usa o
si ispira a quanto segue, ognuno con la propria licenza.

## TamaPoke (MIT)

Alcune parti del firmware (`claude_buddy/hw.cpp`, inizializzazione del touch in `claude_buddy.ino`)
seguono le sequenze verificate sulla scheda nel progetto **TamaPoke**: configurazione del codec
ES8311 e sintesi dei toni, gestione dell'interrupt del touch CST9217, lettura a registri diretti del
sensore QMI8658, lettura di RTC e PMU.

```
MIT License

Copyright (c) 2026 Quique Tortosa

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Librerie (non incluse nel repository, da installare)

| Libreria | Uso | Licenza |
|---|---|---|
| GFX Library for Arduino (moononournation) | driver del display CO5300 | BSD-style, vedi la libreria |
| SensorLib (Lewis He) | touch CST9217, RTC PCF85063 | MIT |
| XPowersLib (Lewis He) | PMU AXP2101 (batteria) | MIT |
| Core esp32 (Espressif) | framework, I2S, Preferences | LGPL-2.1 / Apache-2.0 |

La sequenza di inizializzazione del codec ES8311 deriva dal driver ufficiale Espressif (esp-bsp,
Apache-2.0) e dal progetto TamaPoke.

## Marchi

Questo e' un progetto non ufficiale e indipendente, **non affiliato ne' approvato da Anthropic**.
"Claude", "Claude Code" e la mascotte "Clawd" appartengono ad Anthropic. Il disegno di Clawd nel
firmware e' una semplice riproduzione a blocchi pensata per uso personale.
