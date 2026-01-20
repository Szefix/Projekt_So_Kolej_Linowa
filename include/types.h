#ifndef TYPES_H
#define TYPES_H

#include <sys/types.h>
#include <time.h>
#include <stdbool.h>
#include "config.h"

/* ========== TYPY OSÓB ========== */
typedef enum {
    PIESZY = 0,
    ROWERZYSTA = 1
} TypOsoby;

/* ========== STATUS TURYSTY ========== */
typedef enum {
    STATUS_NOWY = 0,
    STATUS_OCZEKUJE_NA_BILET,
    STATUS_MA_BILET,
    STATUS_PRZED_BRAMKA_WEJSCIOWA,
    STATUS_NA_STACJI_DOLNEJ,
    STATUS_OCZEKUJE_NA_PERON,
    STATUS_NA_PERONIE,
    STATUS_NA_KRZESELKU,
    STATUS_NA_STACJI_GORNEJ,
    STATUS_NA_TRASIE,
    STATUS_ZAKONCZONY
} StatusTurysty;

/* ========== TYPY KOMUNIKATÓW ========== */
typedef enum {
    MSG_PROSBA_O_BILET = 1,
    MSG_BILET_WYDANY = 2,
    MSG_PROSBA_O_WEJSCIE = 3,
    MSG_WEJSCIE_DOZWOLONE = 4,
    MSG_WEJSCIE_ODRZUCONE = 5,
    MSG_PROSBA_O_PERON = 6,
    MSG_PERON_DOZWOLONY = 7,
    MSG_WSIADANIE_NA_KRZESLO = 8,
    MSG_KRZESLO_GOTOWE = 9,
    MSG_ZATRZYMAJ_KOLEJ = 10,
    MSG_WZNOW_KOLEJ = 11,
    MSG_GOTOWY = 12,
    MSG_KONIEC_DNIA = 13
} TypKomunikatu;

#endif