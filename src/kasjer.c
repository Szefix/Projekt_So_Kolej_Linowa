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

/* Zmienne globalne procesu kasjera */
static volatile sig_atomic_t kasjer_dzialaj = 1;
static ZasobyIPC kasjer_zasoby;

/* Obsługa sygnałów */
static void kasjer_obsluz_sygnal(int sig) {
    (void)sig;
    kasjer_dzialaj = 0;
}

/* Obliczanie ceny biletu z uwzględnieniem zniżek */
int oblicz_cene(int typ_biletu, int wiek) {
    int cena_bazowa;
    
    switch (typ_biletu) {
        case BILET_JEDNORAZOWY: 
            cena_bazowa = CENA_JEDNORAZOWY; 
            break;
        case BILET_CZASOWY_TK1: 
            cena_bazowa = CENA_TK1; 
            break;
        case BILET_CZASOWY_TK2: 
            cena_bazowa = CENA_TK2; 
            break;
        case BILET_CZASOWY_TK3: 
            cena_bazowa = CENA_TK3; 
            break;
        case BILET_DZIENNY:     
            cena_bazowa = CENA_DZIENNY; 
            break;
        default: 
            cena_bazowa = CENA_JEDNORAZOWY;
    }
    
    /* Zniżka 25% dla dzieci < 10 lat i seniorów > 65 lat */
    if (wiek < WIEK_DZIECKO_ZNIZKA || wiek > WIEK_SENIOR_ZNIZKA) {
        cena_bazowa = cena_bazowa * (100 - PROCENT_ZNIZKI) / 100;
    }
    
    return cena_bazowa;
}

/* Nazwa typu biletu */
const char* nazwa_typu_biletu(int typ) {
    switch (typ) {
        case BILET_JEDNORAZOWY: return "jednorazowy";
        case BILET_CZASOWY_TK1: return "czasowy TK1";
        case BILET_CZASOWY_TK2: return "czasowy TK2";
        case BILET_CZASOWY_TK3: return "czasowy TK3";
        case BILET_DZIENNY:     return "dzienny";
        default:                return "nieznany";
    }
}