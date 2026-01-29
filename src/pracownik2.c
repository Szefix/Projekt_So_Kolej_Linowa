#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/select.h>
#include "config.h"
#include "types.h"
#include "ipc_utils.h"
#include "logger.h"

static volatile sig_atomic_t p2_dzialaj = 1;
static volatile sig_atomic_t p2_kolej_zatrzymana = 0;
static ZasobyIPC p2_zasoby;

static void p2_obsluz_zatrzymanie(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info; (void)context;
    p2_kolej_zatrzymana = 1;
}

static void p2_obsluz_wznowienie(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info; (void)context;
    p2_kolej_zatrzymana = 0;
}

static void p2_obsluz_zakonczenie(int sig, siginfo_t *info, void *context) {
    (void)sig; (void)info; (void)context;
    p2_dzialaj = 0;
}

void p2_ustaw_sygnaly(void) {
    struct sigaction sa_stop, sa_cont, sa_term;
    
    memset(&sa_stop, 0, sizeof(sa_stop));
    sa_stop.sa_sigaction = p2_obsluz_zatrzymanie;
    sa_stop.sa_flags = SA_SIGINFO;
    sigemptyset(&sa_stop.sa_mask);
    
    memset(&sa_cont, 0, sizeof(sa_cont));
    sa_cont.sa_sigaction = p2_obsluz_wznowienie;
    sa_cont.sa_flags = SA_SIGINFO;
    sigemptyset(&sa_cont.sa_mask);
    
    memset(&sa_term, 0, sizeof(sa_term));
    sa_term.sa_sigaction = p2_obsluz_zakonczenie;
    sa_term.sa_flags = SA_SIGINFO;
    sigemptyset(&sa_term.sa_mask);
    
    sigaction(SIGUSR1, &sa_stop, NULL);
    sigaction(SIGUSR2, &sa_cont, NULL);
    sigaction(SIGTERM, &sa_term, NULL);
    sigaction(SIGINT, &sa_term, NULL);
}

void obsluz_przyjazd_krzeselka(int krzeselko_id) {
    if (!p2_dzialaj) return;
    
    StanWspoldzielony *stan = p2_zasoby.shm.stan;
    int sem_id = p2_zasoby.sem.sem_id;
    
    sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
    Krzeselko *k = &stan->krzeselka[krzeselko_id];
    
    if (k->aktywne && k->liczba_pasazerow > 0) {
        LOG_I("PRACOWNIK2: Krzesełko #%d - %d pasażerów", krzeselko_id, k->liczba_pasazerow);
        
        for (int i = 0; i < k->liczba_pasazerow; i++) {
            int wyjscie = rand() % LICZBA_WYJSC;
            LOG_D("PRACOWNIK2: Turysta #%d -> wyjście %d", k->pasazerowie[i], wyjscie);
        }
        
        k->aktywne = false;
        k->liczba_pasazerow = 0;
        k->liczba_rowerzystow = 0;
        stan->liczba_aktywnych_krzeselek--;
        stan->laczna_liczba_zjazdow++;
        
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_KRZESELKA);
    } else {
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    }
}

void p2_obsluz_wznowienie_komunikat(void) {
    if (!p2_dzialaj) return;
    
    StanWspoldzielony *stan = p2_zasoby.shm.stan;
    int sem_id = p2_zasoby.sem.sem_id;
    
    LOG_I("PRACOWNIK2: Potwierdzam gotowość do wznowienia");
    
    sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
    stan->pracownik2_gotowy = true;
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_SYNC);
}

void p2_zatrzymaj_kolej(void) {
    if (!p2_dzialaj) return;
    
    StanWspoldzielony *stan = p2_zasoby.shm.stan;
    int sem_id = p2_zasoby.sem.sem_id;
    
    LOG_W("PRACOWNIK2: ZATRZYMUJĘ KOLEJ!");
    
    sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
    stan->kolej_zatrzymana = true;
    stan->kto_zatrzymal = 2;
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    
    if (stan->pid_pracownik1 > 0) {
        kill(stan->pid_pracownik1, SIGUSR1);
    }
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    
    p2_ustaw_sygnaly();
    
    if (polacz_z_zasobami(&p2_zasoby) == -1) {
        fprintf(stderr, "PRACOWNIK2: Nie można połączyć z zasobami IPC\n");
        return 1;
    }
    
    logger_init("logs/pracownik2.log");
    LOG_I("PRACOWNIK2: Rozpoczynam pracę (PID: %d)", getpid());
    
    StanWspoldzielony *stan = p2_zasoby.shm.stan;
    int sem_id = p2_zasoby.sem.sem_id;
    
    sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
    stan->pid_pracownik2 = getpid();
    stan->pracownik2_gotowy = true;
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    
    while (p2_dzialaj && stan->kolej_aktywna) {
        if (p2_kolej_zatrzymana) {
            /* BLOKUJĄCE czekanie na semaforze z timeoutem */
            sem_czekaj_timeout_sysv(sem_id, SEM_IDX_PRACOWNIK2, 1);
            continue;
        }
        
        Komunikat msg;
        int wynik = odbierz_komunikat_nieblokujaco(p2_zasoby.mq.mq_pracownicy, 
                                                    &msg, MSG_WZNOW_KOLEJ);
        if (wynik > 0 && p2_dzialaj) {
            p2_obsluz_wznowienie_komunikat();
        }
        
        if (!p2_dzialaj) break;
        
        sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
        time_t teraz = time(NULL);
        for (int i = 0; i < MAX_AKTYWNYCH_KRZESELEK && p2_dzialaj; i++) {
            Krzeselko *k = &stan->krzeselka[i];
            if (k->aktywne && k->czas_wyjazdu > 0) {
                int czas_jazdy = (int)(teraz - k->czas_wyjazdu);
                if (czas_jazdy >= 2) {
                    sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
                    obsluz_przyjazd_krzeselka(i);
                    sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
                }
            }
        }
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
        
        if (rand() % 3000 == 0 && !p2_kolej_zatrzymana && p2_dzialaj) {
            p2_zatrzymaj_kolej();

            /* BLOKUJĄCE czekanie z timeoutem 2 sekundy */
            sem_czekaj_timeout_sysv(sem_id, SEM_IDX_SYNC, 2);

            if (!p2_dzialaj) break;
            
            Komunikat wznow;
            memset(&wznow, 0, sizeof(Komunikat));
            wznow.mtype = MSG_WZNOW_KOLEJ;
            wznow.nadawca_id = 2;
            wyslij_komunikat(p2_zasoby.mq.mq_pracownicy, &wznow);
            
            sem_czekaj_sysv(sem_id, SEM_IDX_SYNC);
            if (!p2_dzialaj) break;
            
            sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
            stan->kolej_zatrzymana = false;
            p2_kolej_zatrzymana = 0;
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
            
            LOG_I("PRACOWNIK2: Kolej wznowiona");
        }

        /* BLOKUJĄCE czekanie 100ms zamiast busy waiting */
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        select(0, NULL, NULL, NULL, &tv);
    }
    
    LOG_I("PRACOWNIK2: Kończę pracę. Zjazdów: %d", stan->laczna_liczba_zjazdow);
    logger_close();
    return 0;
}