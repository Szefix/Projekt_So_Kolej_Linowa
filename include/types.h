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


/* ========== STRUKTURA BILETU ========== */
typedef struct {
    int id;
    int typ;                    /* BILET_JEDNORAZOWY, BILET_CZASOWY_*, BILET_DZIENNY */
    time_t czas_zakupu;
    time_t czas_waznosci;       /* dla biletów czasowych */
    int liczba_uzyc;
    int max_uzyc;               /* -1 dla dziennych/czasowych */
    bool aktywny;
    bool vip;
    int wlasciciel_id;
} Bilet;

/* ========== STRUKTURA TURYSTY ========== */
typedef struct {
    int id;
    pid_t pid;
    int wiek;
    TypOsoby typ;               /* PIESZY lub ROWERZYSTA */
    bool vip;
    bool dziecko_pod_opieka;    /* czy wymaga opieki (4-8 lat) */
    int opiekun_id;             /* ID opiekuna jeśli dziecko */
    int dzieci_pod_opieka[MAX_DZIECI_POD_OPIEKA];  /* ID dzieci pod opieką */
    int liczba_dzieci;
    Bilet bilet;
    StatusTurysty status;
    int liczba_zjazdow;
} Turysta;


#endif