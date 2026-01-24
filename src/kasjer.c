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

/* Tworzenie nowego biletu */
Bilet utworz_bilet(int typ, int wiek, bool vip, int wlasciciel_id) {
    Bilet bilet;
    StanWspoldzielony *stan = kasjer_zasoby.shm.stan;
    
    /* Pobierz unikalne ID biletu */
    sem_czekaj(kasjer_zasoby.sem.stan);
    bilet.id = stan->nastepny_bilet_id++;
    stan->liczba_sprzedanych_biletow++;
    sem_sygnalizuj(kasjer_zasoby.sem.stan);
    
    bilet.typ = typ;
    bilet.czas_zakupu = time(NULL);
    bilet.liczba_uzyc = 0;
    bilet.aktywny = true;
    bilet.vip = vip;
    bilet.wlasciciel_id = wlasciciel_id;
    
    /* Ustaw parametry w zależności od typu */
    switch (typ) {
        case BILET_JEDNORAZOWY:
            bilet.max_uzyc = 1;
            bilet.czas_waznosci = 0;  /* Bez limitu czasowego */
            break;
        case BILET_CZASOWY_TK1:
            bilet.max_uzyc = -1;      /* Bez limitu użyć */
            bilet.czas_waznosci = bilet.czas_zakupu + CZAS_TK1;
            break;
        case BILET_CZASOWY_TK2:
            bilet.max_uzyc = -1;
            bilet.czas_waznosci = bilet.czas_zakupu + CZAS_TK2;
            break;
        case BILET_CZASOWY_TK3:
            bilet.max_uzyc = -1;
            bilet.czas_waznosci = bilet.czas_zakupu + CZAS_TK3;
            break;
        case BILET_DZIENNY:
            bilet.max_uzyc = -1;
            bilet.czas_waznosci = bilet.czas_zakupu + CZAS_ZAMKNIECIA;
            break;
        default:
            bilet.max_uzyc = 1;
            bilet.czas_waznosci = 0;
    }
    
    return bilet;
}

/* Obsługa pojedynczego klienta */
void obsluz_klienta(Komunikat *prosba) {
    int turysta_id = prosba->nadawca_id;
    int typ_biletu = prosba->dane[0];
    int wiek = prosba->dane[1];
    bool vip = (prosba->dane[2] != 0);
    
    LOG_I("KASJER: Obsługuję turystę #%d (wiek: %d, VIP: %s, typ: %s)",
          turysta_id, wiek, vip ? "TAK" : "NIE", nazwa_typu_biletu(typ_biletu));
    
    /* Oblicz cenę */
    int cena = oblicz_cene(typ_biletu, wiek);
    
    /* Utwórz bilet */
    Bilet bilet = utworz_bilet(typ_biletu, wiek, vip, turysta_id);
    
    LOG_I("KASJER: Wydaję bilet #%d dla turysty #%d, cena: %d zł",
          bilet.id, turysta_id, cena);
    
    /* Przygotuj odpowiedź */
    Komunikat odpowiedz;
    memset(&odpowiedz, 0, sizeof(Komunikat));
    odpowiedz.mtype = turysta_id;  /* Adresuj do konkretnego turysty */
    odpowiedz.typ_komunikatu = MSG_BILET_WYDANY;
    odpowiedz.nadawca_id = 0;  /* Kasjer */
    odpowiedz.dane[0] = bilet.id;
    odpowiedz.dane[1] = bilet.typ;
    odpowiedz.dane[2] = bilet.max_uzyc;
    odpowiedz.dane[3] = (int)(bilet.czas_waznosci);
    odpowiedz.dane[4] = bilet.vip ? 1 : 0;
    odpowiedz.dane[5] = cena;
    
    /* Wyślij odpowiedź */
    wyslij_komunikat(kasjer_zasoby.mq.mq_kasa, &odpowiedz);
}

/* Główna funkcja procesu kasjera */
int kasjer_main(void) {
    /* Rejestracja obsługi sygnałów */
    signal(SIGTERM, kasjer_obsluz_sygnal);
    signal(SIGINT, kasjer_obsluz_sygnal);
    
    /* Połączenie z zasobami IPC */
    if (polacz_z_zasobami(&kasjer_zasoby) == -1) {
        fprintf(stderr, "KASJER: Nie można połączyć z zasobami IPC\n");
        return 1;
    }
    
    /* Inicjalizacja logowania */
    logger_init("logs/kasjer.log");
    LOG_I("KASJER: Rozpoczynam pracę (PID: %d)", getpid());
    
    StanWspoldzielony *stan = kasjer_zasoby.shm.stan;
    
    /* Główna pętla obsługi */
    while (kasjer_dzialaj && stan->kolej_aktywna) {
        Komunikat prosba;
        
        /* Czekaj na prośbę o bilet - blokujące na kolejce */
        int wynik = odbierz_komunikat_nieblokujaco(kasjer_zasoby.mq.mq_kasa, 
                                                    &prosba, MSG_PROSBA_O_BILET);
        
        if (wynik > 0) {
            /* Obsłuż klienta - mutex kasy */
            sem_czekaj(kasjer_zasoby.sem.kasa);
            obsluz_klienta(&prosba);
            sem_sygnalizuj(kasjer_zasoby.sem.kasa);
        } else if (wynik == 0) {
            /* Brak komunikatów - krótka pauza */
            usleep(10000);  /* 10ms */
        }
        
        /* Sprawdź czy kolej jeszcze działa */
        if (!stan->godziny_pracy && !stan->kolej_aktywna) {
            break;
        }
    }
    
    LOG_I("KASJER: Kończę pracę. Sprzedano %d biletów.", 
          stan->liczba_sprzedanych_biletow);
    logger_close();
    
    return 0;
}