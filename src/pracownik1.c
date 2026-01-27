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

/* Obsługa sygnałów */
static void p1_obsluz_zatrzymanie(int sig) {
    (void)sig;
    p1_kolej_zatrzymana = 1;
    LOG_W("PRACOWNIK1: Otrzymałem sygnał zatrzymania!");
}

static void p1_obsluz_wznowienie(int sig) {
    (void)sig;
    p1_kolej_zatrzymana = 0;
    LOG_I("PRACOWNIK1: Kolej wznowiona");
}

static void p1_obsluz_zakonczenie(int sig) {
    (void)sig;
    p1_dzialaj = 0;
}

/* Zatrzymanie kolei */
void p1_zatrzymaj_kolej(void) {
    StanWspoldzielony *stan = p1_zasoby.shm.stan;
    
    LOG_W("PRACOWNIK1: ZATRZYMUJĘ KOLEJ!");
    
    sem_czekaj(p1_zasoby.sem.stan);
    stan->kolej_zatrzymana = true;
    stan->kto_zatrzymal = 1;
    sem_sygnalizuj(p1_zasoby.sem.stan);
    
    /* Wyślij sygnał do pracownika2 */
    if (stan->pid_pracownik2 > 0) {
        kill(stan->pid_pracownik2, SIGUSR1);
    }
}

/* Wznowienie kolei */
void p1_wznow_kolej(void) {
    StanWspoldzielony *stan = p1_zasoby.shm.stan;
    
    LOG_I("PRACOWNIK1: Wznawianie kolei...");
    
    /* Wyślij komunikat do pracownika2 */
    Komunikat msg;
    memset(&msg, 0, sizeof(Komunikat));
    msg.mtype = MSG_WZNOW_KOLEJ;
    msg.nadawca_id = 1;
    msg.typ_komunikatu = MSG_WZNOW_KOLEJ;
    wyslij_komunikat(p1_zasoby.mq.mq_pracownicy, &msg);
    
    /* Czekaj na potwierdzenie */
    sem_czekaj(p1_zasoby.sem.sync);
    
    sem_czekaj(p1_zasoby.sem.stan);
    stan->kolej_zatrzymana = false;
    stan->kto_zatrzymal = 0;
    p1_kolej_zatrzymana = 0;
    sem_sygnalizuj(p1_zasoby.sem.stan);
    
    LOG_I("PRACOWNIK1: Kolej wznowiona!");
    
    if (stan->pid_pracownik2 > 0) {
        kill(stan->pid_pracownik2, SIGUSR2);
    }
}

/* Główna funkcja pracownika1 */
int pracownik1_main(void) {
    /* Rejestracja obsługi sygnałów */
    signal(SIGTERM, p1_obsluz_zakonczenie);
    signal(SIGINT, p1_obsluz_zakonczenie);
    signal(SIGUSR1, p1_obsluz_zatrzymanie);
    signal(SIGUSR2, p1_obsluz_wznowienie);
    
    /* Połączenie z zasobami IPC */
    if (polacz_z_zasobami(&p1_zasoby) == -1) {
        fprintf(stderr, "PRACOWNIK1: Nie można połączyć z zasobami IPC\n");
        return 1;
    }
    
    logger_init("logs/pracownik1.log");
    LOG_I("PRACOWNIK1: Rozpoczynam pracę na stacji dolnej (PID: %d)", getpid());
    
    StanWspoldzielony *stan = p1_zasoby.shm.stan;
    
    /* Zarejestruj się */
    sem_czekaj(p1_zasoby.sem.stan);
    stan->pid_pracownik1 = getpid();
    stan->pracownik1_gotowy = true;
    sem_sygnalizuj(p1_zasoby.sem.stan);
    
    inicjalizuj_grupe();
    
    /* Główna pętla */
    while (p1_dzialaj && stan->kolej_aktywna) {
        /* Sprawdź czy kolej zatrzymana */
        if (p1_kolej_zatrzymana) {
            usleep(100000);
            continue;
        }
        
        /* Odbierz prośby o wejście na peron */
        Komunikat prosba;
        int wynik = odbierz_komunikat_nieblokujaco(p1_zasoby.mq.mq_pracownicy, 
                                                    &prosba, MSG_PROSBA_O_PERON);
        
        if (wynik > 0) {
            int id = prosba.nadawca_id;
            int typ = prosba.dane[0];
            
            LOG_I("PRACOWNIK1: Prośba od turysty #%d (%s)",
                  id, typ == ROWERZYSTA ? "rowerzysta" : "pieszy");
            
            /* Dodaj do kolejki */
            if (liczba_oczekujacych < MAX_OCZEKUJACYCH) {
                kolejka_id[liczba_oczekujacych] = id;
                kolejka_typ[liczba_oczekujacych] = typ;
                liczba_oczekujacych++;
            }
        }
        
        /* Przetwarzaj kolejkę */
        for (int i = 0; i < liczba_oczekujacych; ) {
            int id = kolejka_id[i];
            int typ = kolejka_typ[i];
            
            if (moze_dolaczyc(typ)) {
                dodaj_do_grupy(id, typ);
                
                /* Pozwól wejść na peron */
                sem_sygnalizuj(p1_zasoby.sem.peron);
                
                LOG_I("PRACOWNIK1: Turysta #%d dołącza (%d/%d)",
                      id, aktualna_grupa.liczba, POJEMNOSC_KRZESELKA);
                
                /* Usuń z kolejki */
                for (int j = i; j < liczba_oczekujacych - 1; j++) {
                    kolejka_id[j] = kolejka_id[j + 1];
                    kolejka_typ[j] = kolejka_typ[j + 1];
                }
                liczba_oczekujacych--;
            } else {
                i++;
            }
            
            /* Wyślij grupę jeśli pełna */
            if (grupa_pelna()) {
                wyslij_grupe_na_krzeselko();
            }
        }
        
        /* Wyślij niepełną grupę jeśli czeka */
        if (aktualna_grupa.liczba > 0 && liczba_oczekujacych == 0) {
            usleep(200000);  /* Poczekaj 200ms na więcej osób */
            if (liczba_oczekujacych == 0 && aktualna_grupa.liczba > 0) {
                wyslij_grupe_na_krzeselko();
            }
        }
        
        /* Losowo zatrzymaj kolej (0.05% szans) - symulacja zagrożenia */
        if (rand() % 2000 == 0 && !p1_kolej_zatrzymana) {
            p1_zatrzymaj_kolej();
            sleep(2);
            p1_wznow_kolej();
        }
        
        usleep(50000);
    }
    
    /* Wyślij pozostałą grupę */
    if (aktualna_grupa.liczba > 0) {
        wyslij_grupe_na_krzeselko();
    }
    
    LOG_I("PRACOWNIK1: Kończę pracę");
    logger_close();
    return 0;
}