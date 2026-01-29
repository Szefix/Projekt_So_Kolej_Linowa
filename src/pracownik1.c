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

/* ========== ROZSZERZONA STRUKTURA GRUPY KRZESEŁKA ========== */
typedef struct {
    int osoby[POJEMNOSC_KRZESELKA];
    int typy[POJEMNOSC_KRZESELKA];
    int opiekunowie[POJEMNOSC_KRZESELKA];  /* ID opiekuna dla dzieci */
    bool czy_dziecko[POJEMNOSC_KRZESELKA]; /* Czy to dziecko pod opieką */
    int liczba;
    int liczba_rowerzystow;
    int liczba_dzieci;
} GrupaKrzeselko;

/* ========== STRUKTURA OCZEKUJĄCEGO W KOLEJCE ========== */
typedef struct {
    int id;
    int typ;              /* PIESZY / ROWERZYSTA */
    bool dziecko_pod_opieka;
    int opiekun_id;       /* -1 jeśli dorosły lub dziecko bez opieki */
    int wiek;
    int liczba_dzieci;    /* Ile dzieci ten dorosły ma pod opieką (w kolejce) */
} OczekujacyTurysta;

#define MAX_OCZEKUJACYCH 200
static OczekujacyTurysta kolejka[MAX_OCZEKUJACYCH];
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
        aktualna_grupa.opiekunowie[i] = -1;
        aktualna_grupa.czy_dziecko[i] = false;
    }
    aktualna_grupa.liczba_dzieci = 0;
}

/* ========== SPRAWDZENIE CZY OPIEKUN JEST W GRUPIE ========== */
bool opiekun_w_grupie(int opiekun_id) {
    for (int i = 0; i < aktualna_grupa.liczba; i++) {
        if (aktualna_grupa.osoby[i] == opiekun_id && !aktualna_grupa.czy_dziecko[i]) {
            return true;
        }
    }
    return false;
}

/* ========== POLICZ DZIECI OPIEKUNA W GRUPIE ========== */
int policz_dzieci_opiekuna_w_grupie(int opiekun_id) {
    int licznik = 0;
    for (int i = 0; i < aktualna_grupa.liczba; i++) {
        if (aktualna_grupa.opiekunowie[i] == opiekun_id) {
            licznik++;
        }
    }
    return licznik;
}

/* ========== ZNAJDŹ OPIEKUNA W KOLEJCE ========== */
int znajdz_opiekuna_w_kolejce(int opiekun_id) {
    for (int i = 0; i < liczba_oczekujacych; i++) {
        if (kolejka[i].id == opiekun_id && !kolejka[i].dziecko_pod_opieka) {
            return i;
        }
    }
    return -1;
}

/* ========== SPRAWDZENIE CZY TURYSTA MOŻE DOŁĄCZYĆ DO GRUPY ========== */
bool moze_dolaczyc(OczekujacyTurysta *turysta) {
    if (aktualna_grupa.liczba >= POJEMNOSC_KRZESELKA) return false;
    
    /* Sprawdzenie reguł dla rowerzystów */
    if (turysta->typ == ROWERZYSTA) {
        if (aktualna_grupa.liczba_rowerzystow >= MAX_ROWERZYSTOW_NA_KRZESELKU) return false;
        if (aktualna_grupa.liczba_rowerzystow == 1 && 
            (aktualna_grupa.liczba - aktualna_grupa.liczba_rowerzystow) > 1) return false;
    } else {
        if (aktualna_grupa.liczba_rowerzystow == 2) return false;
        if (aktualna_grupa.liczba_rowerzystow == 1 && 
            (aktualna_grupa.liczba - aktualna_grupa.liczba_rowerzystow) >= 2) return false;
    }
    
    /* ========== LOGIKA DZIECI POD OPIEKĄ (4-8 LAT) ========== */
    if (turysta->dziecko_pod_opieka) {
        /* Dziecko potrzebuje opiekuna w grupie lub musi z nim wejść */
        if (!opiekun_w_grupie(turysta->opiekun_id)) {
            /* Opiekun nie jest jeszcze w grupie */
            int idx_opiekuna = znajdz_opiekuna_w_kolejce(turysta->opiekun_id);
            if (idx_opiekuna == -1) {
                /* Opiekun nie jest ani w grupie, ani w kolejce - nie wpuszczamy dziecka! */
                LOG_W("PRACOWNIK1: Dziecko #%d bez opiekuna #%d - czeka", 
                      turysta->id, turysta->opiekun_id);
                return false;
            }
            /* Opiekun jest w kolejce - dziecko musi poczekać aż opiekun wejdzie */
            return false;
        }
        
        /* Sprawdź limit dzieci na opiekuna (max 2) */
        int dzieci_opiekuna = policz_dzieci_opiekuna_w_grupie(turysta->opiekun_id);
        if (dzieci_opiekuna >= MAX_DZIECI_POD_OPIEKA) {
            LOG_W("PRACOWNIK1: Opiekun #%d ma już %d dzieci w grupie - limit!", 
                  turysta->opiekun_id, dzieci_opiekuna);
            return false;
        }
        
        LOG_I("PRACOWNIK1: Dziecko #%d dołącza do opiekuna #%d", 
              turysta->id, turysta->opiekun_id);
    }
    
    return true;
}

/* ========== DODANIE DO GRUPY Z OBSŁUGĄ DZIECI ========== */
void dodaj_do_grupy(OczekujacyTurysta *turysta) {
    int idx = aktualna_grupa.liczba;
    aktualna_grupa.osoby[idx] = turysta->id;
    aktualna_grupa.typy[idx] = turysta->typ;
    aktualna_grupa.opiekunowie[idx] = turysta->opiekun_id;
    aktualna_grupa.czy_dziecko[idx] = turysta->dziecko_pod_opieka;
    aktualna_grupa.liczba++;
    
    if (turysta->typ == ROWERZYSTA) {
        aktualna_grupa.liczba_rowerzystow++;
    }
    if (turysta->dziecko_pod_opieka) {
        aktualna_grupa.liczba_dzieci++;
    }
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
            bool dziecko = (prosba.dane[1] != 0);
            int opiekun_id = prosba.dane[2];
            int wiek = prosba.dane[3];
            
            LOG_I("PRACOWNIK1: Prośba od turysty #%d (wiek: %d, dziecko: %s, opiekun: %d)", 
                  id, wiek, dziecko ? "TAK" : "NIE", opiekun_id);
            
            if (liczba_oczekujacych < MAX_OCZEKUJACYCH) {
                OczekujacyTurysta *t = &kolejka[liczba_oczekujacych];
                t->id = id;
                t->typ = typ;
                t->dziecko_pod_opieka = dziecko;
                t->opiekun_id = opiekun_id;
                t->wiek = wiek;
                t->liczba_dzieci = 0;
                liczba_oczekujacych++;
            }
        }
        
        /* Najpierw dodajemy dorosłych, potem ich dzieci */
        for (int i = 0; i < liczba_oczekujacych && p1_dzialaj; ) {
            OczekujacyTurysta *turysta = &kolejka[i];
            
            if (moze_dolaczyc(turysta)) {
                dodaj_do_grupy(turysta);
                sem_sygnalizuj_sysv(sem_id, SEM_IDX_PERON);
                
                /* Usuń z kolejki */
                for (int j = i; j < liczba_oczekujacych - 1; j++) {
                    kolejka[j] = kolejka[j + 1];
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