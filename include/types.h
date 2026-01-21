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

/* ========== STRUKTURA KRZESEŁKA ========== */
typedef struct {
    int id;
    bool aktywne;
    int pasazerowie[POJEMNOSC_KRZESELKA];   /* ID turystów */
    int liczba_pasazerow;
    int liczba_rowerzystow;
    time_t czas_wyjazdu;
} Krzeselko;

/* ========== STRUKTURA BRAMKI ========== */
typedef struct {
    int id;
    bool otwarta;
    int aktualny_turysta_id;
    time_t ostatnie_uzycie;
} Bramka;

/* ========== WPIS REJESTRU PRZEJŚĆ ========== */
typedef struct {
    int bilet_id;
    int turysta_id;
    time_t czas;
    int numer_bramki;
    int numer_zjazdu;
} WpisRejestru;

/* ========== MAKSYMALNA LICZBA WPISÓW W REJESTRZE ========== */
#define MAX_WPISOW_REJESTRU 2000

/* ========== STAN WSPÓŁDZIELONY ========== */
typedef struct {
    /* Flagi systemowe */
    bool kolej_aktywna;
    bool kolej_zatrzymana;
    bool godziny_pracy;
    time_t czas_startu;
    
    /* Liczniki */
    int liczba_osob_na_stacji;
    int liczba_osob_na_peronie;
    int liczba_aktywnych_krzeselek;
    int nastepny_turysta_id;
    int nastepny_bilet_id;
    
    /* Pracownicy */
    pid_t pid_pracownik1;
    pid_t pid_pracownik2;
    bool pracownik1_gotowy;
    bool pracownik2_gotowy;
    int kto_zatrzymal;          /* 1 lub 2, 0 = nikt */
    
    /* Bramki */
    Bramka bramki_wejsciowe[LICZBA_BRAMEK_WEJSCIOWYCH];
    Bramka bramki_peronowe[LICZBA_BRAMEK_PERONOWYCH];
    
    /* Krzesełka */
    Krzeselko krzeselka[MAX_AKTYWNYCH_KRZESELEK];
    int nastepne_krzeselko_idx;
    
    /* Statystyki */
    int laczna_liczba_zjazdow;
    int liczba_sprzedanych_biletow;
    
    /* Rejestr przejść */
    WpisRejestru rejestr[MAX_WPISOW_REJESTRU];
    int liczba_wpisow_rejestru;
} StanWspoldzielony;

/* ========== KOMUNIKAT IPC ========== */
typedef struct {
    long mtype;                 /* Typ komunikatu (wymagane przez System V) */
    int nadawca_id;
    int typ_komunikatu;         /* TypKomunikatu */
    int dane[8];                /* Dane dodatkowe */
    char tekst[64];             /* Opcjonalny tekst */
} Komunikat;

#endif