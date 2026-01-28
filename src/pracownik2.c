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

/* Zmienne globalne procesu pracownika2 */
static volatile sig_atomic_t p2_dzialaj = 1;
static volatile sig_atomic_t p2_kolej_zatrzymana = 0;
static ZasobyIPC p2_zasoby;

/* Obsługa sygnałów */
static void p2_obsluz_zatrzymanie(int sig) {
    (void)sig;
    p2_kolej_zatrzymana = 1;
    LOG_W("PRACOWNIK2: Otrzymałem sygnał zatrzymania!");
}

static void p2_obsluz_wznowienie(int sig) {
    (void)sig;
    p2_kolej_zatrzymana = 0;
    LOG_I("PRACOWNIK2: Kolej wznowiona");
}

static void p2_obsluz_zakonczenie(int sig) {
    (void)sig;
    p2_dzialaj = 0;
}

/* Obsługa przyjazdu krzesełka */
void obsluz_przyjazd_krzeselka(int krzeselko_id) {
    StanWspoldzielony *stan = p2_zasoby.shm.stan;
    
    sem_czekaj(p2_zasoby.sem.stan);
    Krzeselko *k = &stan->krzeselka[krzeselko_id];
    
    if (k->aktywne && k->liczba_pasazerow > 0) {
        LOG_I("PRACOWNIK2: Krzesełko #%d - %d pasażerów",
              krzeselko_id, k->liczba_pasazerow);
        
        /* Kieruj pasażerów do wyjść */
        for (int i = 0; i < k->liczba_pasazerow; i++) {
            int wyjscie = rand() % LICZBA_WYJSC;
            LOG_D("PRACOWNIK2: Turysta #%d -> wyjście %d", 
                  k->pasazerowie[i], wyjscie);
        }
        
        /* Zwolnij krzesełko */
        k->aktywne = false;
        k->liczba_pasazerow = 0;
        k->liczba_rowerzystow = 0;
        stan->liczba_aktywnych_krzeselek--;
        stan->laczna_liczba_zjazdow++;
        
        /* Zwolnij semafor krzesełka */
        sem_sygnalizuj(p2_zasoby.sem.krzeselka);
    }
    
    sem_sygnalizuj(p2_zasoby.sem.stan);
}

/* Obsługa komunikatu wznowienia */
void p2_obsluz_wznowienie_komunikat(void) {
    StanWspoldzielony *stan = p2_zasoby.shm.stan;
    
    LOG_I("PRACOWNIK2: Potwierdzam gotowość do wznowienia");
    
    sem_czekaj(p2_zasoby.sem.stan);
    stan->pracownik2_gotowy = true;
    sem_sygnalizuj(p2_zasoby.sem.stan);
    
    /* Sygnalizuj gotowość */
    sem_sygnalizuj(p2_zasoby.sem.sync);
}

/* Zatrzymanie kolei przez pracownika2 */
void p2_zatrzymaj_kolej(void) {
    StanWspoldzielony *stan = p2_zasoby.shm.stan;
    
    LOG_W("PRACOWNIK2: ZATRZYMUJĘ KOLEJ!");
    
    sem_czekaj(p2_zasoby.sem.stan);
    stan->kolej_zatrzymana = true;
    stan->kto_zatrzymal = 2;
    sem_sygnalizuj(p2_zasoby.sem.stan);
    
    if (stan->pid_pracownik1 > 0) {
        kill(stan->pid_pracownik1, SIGUSR1);
    }
}

/* Główna funkcja pracownika2 */
int pracownik2_main(void) {
    signal(SIGTERM, p2_obsluz_zakonczenie);
    signal(SIGINT, p2_obsluz_zakonczenie);
    signal(SIGUSR1, p2_obsluz_zatrzymanie);
    signal(SIGUSR2, p2_obsluz_wznowienie);
    
    if (polacz_z_zasobami(&p2_zasoby) == -1) {
        fprintf(stderr, "PRACOWNIK2: Nie można połączyć z zasobami IPC\n");
        return 1;
    }
    
    logger_init("logs/pracownik2.log");
    LOG_I("PRACOWNIK2: Rozpoczynam pracę na stacji górnej (PID: %d)", getpid());
    
    StanWspoldzielony *stan = p2_zasoby.shm.stan;
    
    sem_czekaj(p2_zasoby.sem.stan);
    stan->pid_pracownik2 = getpid();
    stan->pracownik2_gotowy = true;
    sem_sygnalizuj(p2_zasoby.sem.stan);
    
    while (p2_dzialaj && stan->kolej_aktywna) {
        if (p2_kolej_zatrzymana) {
            usleep(100000);
            continue;
        }
        
        /* Sprawdź komunikaty wznowienia */
        Komunikat msg;
        int wynik = odbierz_komunikat_nieblokujaco(p2_zasoby.mq.mq_pracownicy, 
                                                    &msg, MSG_WZNOW_KOLEJ);
        if (wynik > 0) {
            p2_obsluz_wznowienie_komunikat();
        }
        
        /* Monitoruj krzesełka - symulacja przyjazdu */
        sem_czekaj(p2_zasoby.sem.stan);
        time_t teraz = time(NULL);
        for (int i = 0; i < MAX_AKTYWNYCH_KRZESELEK; i++) {
            Krzeselko *k = &stan->krzeselka[i];
            if (k->aktywne && k->czas_wyjazdu > 0) {
                int czas_jazdy = (int)(teraz - k->czas_wyjazdu);
                if (czas_jazdy >= 2) {  /* 2 sekundy jazdy */
                    sem_sygnalizuj(p2_zasoby.sem.stan);
                    obsluz_przyjazd_krzeselka(i);
                    sem_czekaj(p2_zasoby.sem.stan);
                }
            }
        }
        sem_sygnalizuj(p2_zasoby.sem.stan);
        
        /* Losowo zatrzymaj (0.03% szans) */
        if (rand() % 3000 == 0 && !p2_kolej_zatrzymana) {
            p2_zatrzymaj_kolej();
            sleep(2);
            
            /* Wznowienie */
            Komunikat wznow;
            memset(&wznow, 0, sizeof(Komunikat));
            wznow.mtype = MSG_WZNOW_KOLEJ;
            wznow.nadawca_id = 2;
            wyslij_komunikat(p2_zasoby.mq.mq_pracownicy, &wznow);
            
            sem_czekaj(p2_zasoby.sem.sync);
            
            sem_czekaj(p2_zasoby.sem.stan);
            stan->kolej_zatrzymana = false;
            p2_kolej_zatrzymana = 0;
            sem_sygnalizuj(p2_zasoby.sem.stan);
            
            LOG_I("PRACOWNIK2: Kolej wznowiona");
        }
        
        usleep(100000);
    }
    
    LOG_I("PRACOWNIK2: Kończę pracę. Zjazdów: %d", stan->laczna_liczba_zjazdow);
    logger_close();
    return 0;
}