// SPDX-License-Identifier: MIT
#pragma once
// Costumi di Clawd: omaggi stilizzati a blocchi (nessuna grafica ufficiale copiata).
// Ogni costume e' una tabella di forme disegnate sopra/sotto Clawd. Le coordinate sono relative a un
// "ancora" sul corpo, cosi' seguono Clawd mentre salta o si schiaccia:
//   AN_TOP    x dal centro,        y dal bordo alto del corpo
//   AN_BOTTOM x dal centro,        y dal bordo basso del corpo
//   AN_LEFT   x dal bordo sinistro, y dal bordo alto
//   AN_RIGHT  x dal bordo destro,   y dal bordo alto
//   AN_ARML / AN_ARMR  x dal bordo sinistro / destro, y dalla cima del braccio (segue il braccio che si muove)
//   | AN_HELD  oggetto tenuto in mano: non si disegna nelle animazioni che usano le mani
// (corpo = 220 x 132 px, occhi a circa y = 55 dal bordo alto e x = +-66 dal centro)
// Anche tools/preview_costumes.py legge queste tabelle per generare un'anteprima: tieni il formato.
#include <stdint.h>

enum ShapeOp : uint8_t { SH_RECT, SH_RRECT, SH_TRI, SH_CIRCLE };
enum ShapeAnchor : uint8_t { AN_TOP, AN_BOTTOM, AN_LEFT, AN_RIGHT, AN_ARML, AN_ARMR };
// da combinare con un ancoraggio: la forma e' un oggetto in mano, nascosto quando le mani sono impegnate
static const uint8_t AN_HELD = 0x80;

struct Shape {
  uint8_t op, an;
  int16_t a, b, c, d, e, f;
  uint16_t col;
};

#define S_RECT(an, x, y, w, h, col) { SH_RECT, an, x, y, w, h, 0, 0, col }
#define S_RRECT(an, x, y, w, h, r, col) { SH_RRECT, an, x, y, w, h, r, 0, col }
#define S_TRI(an, x1, y1, x2, y2, x3, y3, col) { SH_TRI, an, x1, y1, x2, y2, x3, y3, col }
#define S_CIRC(an, x, y, r, col) { SH_CIRCLE, an, x, y, r, 0, 0, 0, col }

// ---------------------------------------------------------------- 1: eroe in verde (Zelda)
static const Shape COS_ZELDA_BACK[] = {
  // spada sulla schiena: lama d'argento che spunta oltre la spalla destra, elsa blu e oro
  S_TRI(AN_RIGHT | AN_HELD, -26, 30, -6, 40, 40, -92, RGB565(205, 214, 228)),
  S_TRI(AN_RIGHT | AN_HELD, -26, 30, 40, -92, 30, -98, RGB565(160, 172, 192)),
  S_RECT(AN_RIGHT | AN_HELD, -34, 26, 34, 10, RGB565(235, 190, 70)),
  S_RECT(AN_RIGHT | AN_HELD, -22, 34, 10, 26, RGB565(60, 90, 200)),
};
static const Shape COS_ZELDA_FRONT[] = {
  // ciuffi biondi ai lati del viso
  S_RECT(AN_LEFT, 0, 6, 16, 52, RGB565(232, 190, 92)),
  S_RECT(AN_RIGHT, -16, 6, 16, 52, RGB565(232, 190, 92)),
  // cintura con fibbia
  S_RECT(AN_LEFT, 0, 96, 220, 12, RGB565(124, 80, 42)),
  S_RECT(AN_TOP, -11, 93, 22, 18, RGB565(240, 200, 80)),
  S_RECT(AN_TOP, -5, 98, 10, 8, RGB565(124, 80, 42)),
  // berretto: un unico cono piegato (due triangoli che condividono il lato P-BR), con ombra e pon-pon
  S_TRI(AN_TOP, -104, -2, 66, -2, -14, -92, RGB565(48, 128, 60)),
  S_TRI(AN_TOP, -14, -92, 128, -26, 66, -2, RGB565(48, 128, 60)),
  S_TRI(AN_TOP, -14, -92, 128, -26, 40, -50, RGB565(34, 104, 50)),
  S_CIRC(AN_TOP, 130, -22, 12, RGB565(240, 232, 196)),
  S_RECT(AN_LEFT, -2, -22, 224, 30, RGB565(48, 128, 60)),
  S_RECT(AN_LEFT, -2, 4, 224, 5, RGB565(28, 86, 44)),
  // scudo blu con il triforce dorato, tenuto con la mano sinistra (segue il braccio)
  S_RRECT(AN_ARML | AN_HELD, -74, -8, 52, 66, 14, RGB565(214, 220, 232)),
  S_RRECT(AN_ARML | AN_HELD, -70, -4, 44, 58, 11, RGB565(44, 76, 168)),
  S_TRI(AN_ARML | AN_HELD, -48, -6, -58, 12, -38, 12, RGB565(240, 200, 80)),
  S_TRI(AN_ARML | AN_HELD, -58, 14, -68, 32, -48, 32, RGB565(240, 200, 80)),
  S_TRI(AN_ARML | AN_HELD, -38, 14, -48, 32, -28, 32, RGB565(240, 200, 80)),
};

// ---------------------------------------------------------------- 2: cavaliere errante (Dark Souls)
static const Shape COS_KNIGHT_BACK[] = {
  // mantello lacero dietro il corpo
  S_RECT(AN_LEFT, -12, 14, 244, 138, RGB565(96, 24, 28)),
  S_TRI(AN_LEFT, -12, 150, 38, 150, 8, 180, RGB565(96, 24, 28)),
  S_TRI(AN_LEFT, 56, 150, 108, 150, 80, 172, RGB565(96, 24, 28)),
  S_TRI(AN_LEFT, 124, 150, 176, 150, 154, 184, RGB565(96, 24, 28)),
  S_TRI(AN_LEFT, 188, 150, 232, 150, 212, 176, RGB565(96, 24, 28)),
};
static const Shape COS_KNIGHT_FRONT[] = {
  // pettorale: tabarda scura con croce d'osso
  S_RECT(AN_TOP, -36, 78, 72, 54, RGB565(70, 22, 28)),
  S_RECT(AN_TOP, -4, 86, 8, 38, RGB565(222, 208, 172)),
  S_RECT(AN_TOP, -16, 98, 32, 8, RGB565(222, 208, 172)),
  // spallacci
  S_RRECT(AN_LEFT, -8, 6, 36, 28, 8, RGB565(104, 110, 124)),
  S_RRECT(AN_RIGHT, -28, 6, 36, 28, 8, RGB565(104, 110, 124)),
  // elmo aperto sul viso: calotta, cresta rossa, guanciali e barra nasale
  S_RRECT(AN_TOP, -7, -84, 14, 54, 5, RGB565(160, 32, 34)),
  S_RRECT(AN_TOP, -92, -54, 184, 86, 44, RGB565(176, 182, 196)),
  S_RECT(AN_TOP, -92, 24, 184, 8, RGB565(60, 64, 78)),
  S_RECT(AN_TOP, -92, 32, 12, 54, RGB565(122, 128, 144)),
  S_RECT(AN_TOP, 80, 32, 12, 54, RGB565(122, 128, 144)),
  S_RECT(AN_TOP, -7, 32, 14, 46, RGB565(122, 128, 144)),
  S_RECT(AN_TOP, -40, -30, 12, 44, RGB565(214, 220, 232)),
  // scudo rotondo nella mano sinistra (segue il braccio)
  S_CIRC(AN_ARML | AN_HELD, -36, 26, 40, RGB565(58, 62, 76)),
  S_CIRC(AN_ARML | AN_HELD, -36, 26, 33, RGB565(112, 118, 134)),
  S_CIRC(AN_ARML | AN_HELD, -36, 26, 11, RGB565(188, 194, 208)),
  // spada dritta nel pugno destro (segue il braccio)
  S_TRI(AN_ARMR | AN_HELD, 12, -108, 32, -108, 22, -130, RGB565(204, 212, 226)),
  S_RECT(AN_ARMR | AN_HELD, 12, -108, 20, 112, RGB565(190, 198, 212)),
  S_RECT(AN_ARMR | AN_HELD, 20, -108, 4, 112, RGB565(226, 232, 242)),
  S_RECT(AN_ARMR | AN_HELD, -4, 2, 52, 10, RGB565(150, 120, 60)),
  S_RECT(AN_ARMR | AN_HELD, 18, 12, 8, 24, RGB565(96, 64, 38)),
};

struct Costume {
  const Shape *back;
  uint8_t nBack;
  const Shape *front;
  uint8_t nFront;
  uint16_t body;  // colore del corpo con il costume
  uint8_t lvl;    // livello di Clawd che lo sblocca
};
#define COS_N(a) ((uint8_t)(sizeof(a) / sizeof(a[0])))
// indice 0 = nessun costume
static const Costume COSTUMES[] = {
  { nullptr, 0, nullptr, 0, 0, 1 },
  { COS_ZELDA_BACK, COS_N(COS_ZELDA_BACK), COS_ZELDA_FRONT, COS_N(COS_ZELDA_FRONT), RGB565(96, 176, 84), 4 },
  { COS_KNIGHT_BACK, COS_N(COS_KNIGHT_BACK), COS_KNIGHT_FRONT, COS_N(COS_KNIGHT_FRONT), RGB565(86, 92, 108), 7 },
};
#define N_COSTUMES ((int)(sizeof(COSTUMES) / sizeof(COSTUMES[0])))
