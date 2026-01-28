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

static volatile sig_atomic_t p1_dzialaj = 1;
static volatile sig_atomic_t p1_kolej_zatrzymana = 0;
static ZasobyIPC p1_zasoby;

typedef struct {
    int osoby[POJEMNOSC_KRZESELKA];
    int typy[POJEMNOSC_KRZESELKA];
    int liczba;
    int liczba_rowerzystow;
} GrupaKrzeselko;

#define MAX_OCZEKUJACYCH 200
static int kolejka_id[MAX_OCZEKUJACYCH];
static int kolejka_typ[MAX_OCZEKUJACYCH];
static int liczba_oczekujacych = 0;
static GrupaKrzeselko aktualna_grupa;

static void p1_obsluz_zatrzymanie(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info; (void)context;
    p1_kolej_zatrzymana = 1;
}

static void p1_obsluz_wznowienie(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info; (void)context;
    p1_kolej_zatrzymana = 0;
}

static void p1_obsluz_zakonczenie(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info; (void)context;
    p1_dzialaj = 0;
}

void p1_ustaw_sygnaly(void) {
    struct sigaction sa_stop, sa_cont, sa_term;
    
    memset(&sa_stop, 0, sizeof(sa_stop));
    sa_stop.sa_sigaction = p1_obsluz_zatrzymanie;
    sa_stop.sa_flags = SA_SIGINFO;
    sigemptyset(&sa_stop.sa_mask);
    
    memset(&sa_cont, 0, sizeof(sa_cont));
    sa_cont.sa_sigaction = p1_obsluz_wznowienie;
    sa_cont.sa_flags = SA_SIGINFO;
    sigemptyset(&sa_cont.sa_mask);
    
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_sigaction = p1_obsluz_zakonczenie;
    sa_term.sa_flags = SA_SIGINFO;
    sigemptyset(&sa_term.sa_mask);
    
    sigaction(SIGUSR1, &sa_stop, NULL);
    sigaction(SIGUSR2, &sa_cont, NULL);
    sigaction(SIGTERM, &sa_term, NULL);
    sigaction(SIGINT, &sa_term, NULL);
}

void inicjalizuj_grupe(void) {
    memset(&aktualna_grupa, 0, sizeof(GrupaKrzeselko));
    for (int i = 0; i < POJEMNOSC_KRZESELKA; i++) {
        aktualna_grupa.osoby[i] = -1;
        aktualna_grupa.typy[i] = -1;
    }
}

bool moze_dolaczyc(int typ_osoby) {
    if (aktualna_grupa.liczba >= POJEMNOSC_KRZESELKA) return false;
    
    if (typ_osoby == ROWERZYSTA) {
        if (aktualna_grupa.liczba_rowerzystow >= MAX_ROWERZYSTOW_NA_KRZESELKU) return false;
        if (aktualna_grupa.liczba_rowerzystow == 1 && 
            (aktualna_grupa.liczba - aktualna_grupa.liczba_rowerzystow) > 1) return false;
        return true;
    } else {
        if (aktualna_grupa.liczba_rowerzystow == 2) return false;
        if (aktualna_grupa.liczba_rowerzystow == 1 && 
            (aktualna_grupa.liczba - aktualna_grupa.liczba_rowerzystow) >= 2) return false;
        return true;
    }
}

void dodaj_do_grupy(int id, int typ) {
    aktualna_grupa.osoby[aktualna_grupa.liczba] = id;
    aktualna_grupa.typy[aktualna_grupa.liczba] = typ;
    aktualna_grupa.liczba++;
    if (typ == ROWERZYSTA) aktualna_grupa.liczba_rowerzystow++;
}

bool grupa_pelna(void) {
    if (aktualna_grupa.liczba >= POJEMNOSC_KRZESELKA) return true;
    if (aktualna_grupa.liczba_rowerzystow >= MAX_ROWERZYSTOW_NA_KRZESELKU) return true;
    if (aktualna_grupa.liczba_rowerzystow == 1 && aktualna_grupa.liczba >= 3) return true;
    return false;
}

void wyslij_grupe_na_krzeselko(void) {
    if (aktualna_grupa.liczba == 0 || !p1_dzialaj) return;
    
    StanWspoldzielony *stan = p1_zasoby.shm.stan;
    int sem_id = p1_zasoby.sem.sem_id;
    
    sem_czekaj_sysv(sem_id, SEM_IDX_KRZESELKA);
    if (!p1_dzialaj) {
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_KRZESELKA);
        return;
    }
    
    sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
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
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    
    LOG_I("PRACOWNIK1: Wysyłam krzesełko #%d z %d osobami", idx, aktualna_grupa.liczba);
    
    for (int i = 0; i < aktualna_grupa.liczba && p1_dzialaj; i++) {
        Komunikat odp;
        memset(&odp, 0, sizeof(Komunikat));
        odp.mtype = aktualna_grupa.osoby[i] + 10000;
        odp.typ_komunikatu = MSG_KRZESLO_GOTOWE;
        odp.dane[0] = idx;
        wyslij_komunikat(p1_zasoby.mq.mq_krzesla, &odp);
    }
    
    inicjalizuj_grupe();
}

void p1_zatrzymaj_kolej(void) {
    if (!p1_dzialaj) return;
    
    StanWspoldzielony *stan = p1_zasoby.shm.stan;
    int sem_id = p1_zasoby.sem.sem_id;
    
    LOG_W("PRACOWNIK1: ZATRZYMUJĘ KOLEJ!");
    
    sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
    stan->kolej_zatrzymana = true;
    stan->kto_zatrzymal = 1;
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    
    if (stan->pid_pracownik2 > 0) {
        kill(stan->pid_pracownik2, SIGUSR1);
    }
}

void p1_wznow_kolej(void) {
    if (!p1_dzialaj) return;
    
    StanWspoldzielony *stan = p1_zasoby.shm.stan;
    int sem_id = p1_zasoby.sem.sem_id;
    
    LOG_I("PRACOWNIK1: Wznawianie kolei...");
    
    Komunikat msg;
    memset(&msg, 0, sizeof(Komunikat));
    msg.mtype = MSG_WZNOW_KOLEJ;
    msg.nadawca_id = 1;
    msg.typ_komunikatu = MSG_WZNOW_KOLEJ;
    wyslij_komunikat(p1_zasoby.mq.mq_pracownicy, &msg);
    
    sem_czekaj_sysv(sem_id, SEM_IDX_SYNC);
    if (!p1_dzialaj) return;
    
    sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
    stan->kolej_zatrzymana = false;
    stan->kto_zatrzymal = 0;
    p1_kolej_zatrzymana = 0;
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    
    LOG_I("PRACOWNIK1: Kolej wznowiona!");
    
    if (stan->pid_pracownik2 > 0) {
        kill(stan->pid_pracownik2, SIGUSR2);
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    
    p1_ustaw_sygnaly();
    
    if (polacz_z_zasobami(&p1_zasoby) == -1) {
        fprintf(stderr, "PRACOWNIK1: Nie można połączyć z zasobami IPC\n");
        return 1;
    }
    
    logger_init("logs/pracownik1.log");
    LOG_I("PRACOWNIK1: Rozpoczynam pracę (PID: %d)", getpid());
    
    StanWspoldzielony *stan = p1_zasoby.shm.stan;
    int sem_id = p1_zasoby.sem.sem_id;
    
    sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
    stan->pid_pracownik1 = getpid();
    stan->pracownik1_gotowy = true;
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    
    inicjalizuj_grupe();
    
    while (p1_dzialaj && stan->kolej_aktywna) {
        if (p1_kolej_zatrzymana) {
            usleep(100000);
            continue;
        }
        
        Komunikat prosba;
        int wynik = odbierz_komunikat_nieblokujaco(p1_zasoby.mq.mq_pracownicy, 
                                                    &prosba, MSG_PROSBA_O_PERON);
        
        if (!p1_dzialaj) break;
        
        if (wynik > 0) {
            int id = prosba.nadawca_id;
            int typ = prosba.dane[0];
            
            LOG_I("PRACOWNIK1: Prośba od turysty #%d", id);
            
            if (liczba_oczekujacych < MAX_OCZEKUJACYCH) {
                kolejka_id[liczba_oczekujacych] = id;
                kolejka_typ[liczba_oczekujacych] = typ;
                liczba_oczekujacych++;
            }
        }
        
        for (int i = 0; i < liczba_oczekujacych && p1_dzialaj; ) {
            int id = kolejka_id[i];
            int typ = kolejka_typ[i];
            
            if (moze_dolaczyc(typ)) {
                dodaj_do_grupy(id, typ);
                sem_sygnalizuj_sysv(sem_id, SEM_IDX_PERON);
                
                for (int j = i; j < liczba_oczekujacych - 1; j++) {
                    kolejka_id[j] = kolejka_id[j + 1];
                    kolejka_typ[j] = kolejka_typ[j + 1];
                }
                liczba_oczekujacych--;
            } else {
                i++;
            }
            
            if (grupa_pelna()) wyslij_grupe_na_krzeselko();
        }
        
        if (aktualna_grupa.liczba > 0 && liczba_oczekujacych == 0 && p1_dzialaj) {
            usleep(200000);
            if (liczba_oczekujacych == 0 && aktualna_grupa.liczba > 0) {
                wyslij_grupe_na_krzeselko();
            }
        }
        
        if (rand() % 2000 == 0 && !p1_kolej_zatrzymana && p1_dzialaj) {
            p1_zatrzymaj_kolej();
            sleep(2);
            if (p1_dzialaj) p1_wznow_kolej();
        }
        
        usleep(50000);
    }
    
    if (aktualna_grupa.liczba > 0 && p1_dzialaj) {
        wyslij_grupe_na_krzeselko();
    }
    
    LOG_I("PRACOWNIK1: Kończę pracę");
    logger_close();
    return 0;
}