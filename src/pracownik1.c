#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include "config.h"
#include "types.h"
#include "ipc_utils.h"
#include "logger.h"

/* Zmienne globalne procesu pracownika1 */
static volatile sig_atomic_t p1_dzialaj = 1;
static volatile sig_atomic_t p1_kolej_zatrzymana = 0;
static ZasobyIPC p1_zasoby;

/* Struktura grupy na krzesełko */
typedef struct {
    int osoby[POJEMNOSC_KRZESELKA];
    int typy[POJEMNOSC_KRZESELKA];  /* 0=pieszy, 1=rowerzysta */
    int liczba;
    int liczba_rowerzystow;
} GrupaKrzeselko;

/* Kolejka oczekujących */
#define MAX_OCZEKUJACYCH 200
static int kolejka_id[MAX_OCZEKUJACYCH];
static int kolejka_typ[MAX_OCZEKUJACYCH];
static int liczba_oczekujacych = 0;

/* Aktualna grupa */
static GrupaKrzeselko aktualna_grupa;

/* Inicjalizacja grupy */
void inicjalizuj_grupe(void) {
    memset(&aktualna_grupa, 0, sizeof(GrupaKrzeselko));
    for (int i = 0; i < POJEMNOSC_KRZESELKA; i++) {
        aktualna_grupa.osoby[i] = -1;
        aktualna_grupa.typy[i] = -1;
    }
}

/* Sprawdzenie czy osoba może dołączyć do grupy */
bool moze_dolaczyc(int typ_osoby) {
    if (aktualna_grupa.liczba >= POJEMNOSC_KRZESELKA) return false;
    
    /* Zasady:
     * - max 2 rowerzystów na krzesełko
     * - 2 rowerzystów = brak miejsca dla pieszych
     * - 1 rowerzysta + max 2 pieszych
     * - 0 rowerzystów + max 4 pieszych
     */
    
    if (typ_osoby == ROWERZYSTA) {
        if (aktualna_grupa.liczba_rowerzystow >= MAX_ROWERZYSTOW_NA_KRZESELKU) {
            return false;
        }
        /* Jeśli jest już 1 rowerzysta i więcej niż 1 pieszy - nie mieści się */
        if (aktualna_grupa.liczba_rowerzystow == 1 && 
            (aktualna_grupa.liczba - aktualna_grupa.liczba_rowerzystow) > 1) {
            return false;
        }
        return true;
    } else {
        /* Pieszy */
        if (aktualna_grupa.liczba_rowerzystow == 2) {
            return false;  /* 2 rowerzystów = brak miejsca */
        }
        if (aktualna_grupa.liczba_rowerzystow == 1 && 
            (aktualna_grupa.liczba - aktualna_grupa.liczba_rowerzystow) >= 2) {
            return false;  /* 1 rowerzysta + 2 pieszych = pełne */
        }
        return true;
    }
}

/* Dodanie osoby do grupy */
void dodaj_do_grupy(int id, int typ) {
    aktualna_grupa.osoby[aktualna_grupa.liczba] = id;
    aktualna_grupa.typy[aktualna_grupa.liczba] = typ;
    aktualna_grupa.liczba++;
    if (typ == ROWERZYSTA) {
        aktualna_grupa.liczba_rowerzystow++;
    }
}

/* Sprawdzenie czy grupa jest pełna */
bool grupa_pelna(void) {
    if (aktualna_grupa.liczba >= POJEMNOSC_KRZESELKA) return true;
    if (aktualna_grupa.liczba_rowerzystow >= MAX_ROWERZYSTOW_NA_KRZESELKU) return true;
    /* 1 rowerzysta + 2 pieszych też jest pełne */
    if (aktualna_grupa.liczba_rowerzystow == 1 && aktualna_grupa.liczba >= 3) return true;
    return false;
}

/* Wysłanie grupy na krzesełko */
void wyslij_grupe_na_krzeselko(void) {
    if (aktualna_grupa.liczba == 0) return;
    
    StanWspoldzielony *stan = p1_zasoby.shm.stan;
    
    /* Zajmij krzesełko */
    sem_czekaj(p1_zasoby.sem.krzeselka);
    
    sem_czekaj(p1_zasoby.sem.stan);
    int idx = stan->nastepne_krzeselko_idx;
    Krzeselko *k = &stan->krzeselka[idx];
    
    k->aktywne = true;
    k->liczba_pasazerow = aktualna_grupa.liczba;
    k->liczba_rowerzystow = aktualna_grupa.liczba_rowerzystow;
    k->czas_wyjazdu = time(NULL);
    
    for (int i = 0; i < aktualna_grupa.liczba; i++) {
        k->pasazerowie[i] = aktualna_grupa.osoby[i];
    }
    
    stan->nastepne_krzeselko_idx = (idx + 1) % MAX_AKTYWNYCH_KRZESELEK;
    stan->liczba_aktywnych_krzeselek++;
    sem_sygnalizuj(p1_zasoby.sem.stan);
    
    LOG_I("PRACOWNIK1: Wysyłam krzesełko #%d z %d osobami (%d rowerzystów)",
          idx, aktualna_grupa.liczba, aktualna_grupa.liczba_rowerzystow);
    
    /* Powiadom wszystkich pasażerów */
    for (int i = 0; i < aktualna_grupa.liczba; i++) {
        Komunikat odp;
        memset(&odp, 0, sizeof(Komunikat));
        odp.mtype = aktualna_grupa.osoby[i] + 10000;
        odp.typ_komunikatu = MSG_KRZESLO_GOTOWE;
        odp.dane[0] = idx;
        wyslij_komunikat(p1_zasoby.mq.mq_krzesla, &odp);
    }
    
    /* Wyczyść grupę */
    inicjalizuj_grupe();
}