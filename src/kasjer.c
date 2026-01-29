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

static volatile sig_atomic_t kasjer_dzialaj = 1;
static ZasobyIPC kasjer_zasoby;
static int zasoby_polaczone = 0;

static void kasjer_obsluz_sygnal(int sig, siginfo_t *info, void *context) {
    (void)info; (void)context; (void)sig;
    kasjer_dzialaj = 0;
}

void kasjer_ustaw_sygnaly(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = kasjer_obsluz_sygnal;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}

int oblicz_cene(int typ_biletu, int wiek) {
    int cena_bazowa;
    
    switch (typ_biletu) {
        case BILET_JEDNORAZOWY: cena_bazowa = CENA_JEDNORAZOWY; break;
        case BILET_CZASOWY_TK1: cena_bazowa = CENA_TK1; break;
        case BILET_CZASOWY_TK2: cena_bazowa = CENA_TK2; break;
        case BILET_CZASOWY_TK3: cena_bazowa = CENA_TK3; break;
        case BILET_DZIENNY:     cena_bazowa = CENA_DZIENNY; break;
        default: cena_bazowa = CENA_JEDNORAZOWY;
    }
    
    if (wiek < WIEK_DZIECKO_ZNIZKA || wiek > WIEK_SENIOR_ZNIZKA) {
        cena_bazowa = cena_bazowa * (100 - PROCENT_ZNIZKI) / 100;
    }
    
    return cena_bazowa;
}

Bilet utworz_bilet(int typ, bool vip, int wlasciciel_id) {
    Bilet bilet;
    StanWspoldzielony *stan = kasjer_zasoby.shm.stan;
    int sem_id = kasjer_zasoby.sem.sem_id;
    
    /* Sprawdź czy jeszcze działamy */
    if (!kasjer_dzialaj) {
        memset(&bilet, 0, sizeof(Bilet));
        return bilet;
    }
    
    sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
    bilet.id = stan->nastepny_bilet_id++;
    stan->liczba_sprzedanych_biletow++;
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    
    bilet.typ = typ;
    bilet.czas_zakupu = time(NULL);
    bilet.liczba_uzyc = 0;
    bilet.aktywny = true;
    bilet.vip = vip;
    bilet.wlasciciel_id = wlasciciel_id;
    
    switch (typ) {
        case BILET_JEDNORAZOWY:
            bilet.max_uzyc = 1;
            bilet.czas_waznosci = 0;
            break;
        case BILET_CZASOWY_TK1:
            bilet.max_uzyc = -1;
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

void obsluz_klienta(Komunikat *prosba) {
    if (!kasjer_dzialaj) return;
    
    int turysta_id = prosba->nadawca_id;
    int typ_biletu = prosba->dane[0];
    int wiek = prosba->dane[1];
    bool vip = (prosba->dane[2] != 0);
    
    LOG_I("KASJER: Obsługuję turystę #%d (wiek: %d, VIP: %s)",
          turysta_id, wiek, vip ? "TAK" : "NIE");
    
    int cena = oblicz_cene(typ_biletu, wiek);
    Bilet bilet = utworz_bilet(typ_biletu, vip, turysta_id);
    
    if (!kasjer_dzialaj) return;
    
    LOG_I("KASJER: Wydaję bilet #%d, cena: %d zł", bilet.id, cena);
    
    Komunikat odpowiedz;
    memset(&odpowiedz, 0, sizeof(Komunikat));
    odpowiedz.mtype = turysta_id;
    odpowiedz.typ_komunikatu = MSG_BILET_WYDANY;
    odpowiedz.nadawca_id = 0;
    odpowiedz.dane[0] = bilet.id;
    odpowiedz.dane[1] = bilet.typ;
    odpowiedz.dane[2] = bilet.max_uzyc;
    odpowiedz.dane[3] = (int)(bilet.czas_waznosci);
    odpowiedz.dane[4] = bilet.vip ? 1 : 0;
    odpowiedz.dane[5] = cena;
    
    wyslij_komunikat(kasjer_zasoby.mq.mq_kasa, &odpowiedz);
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    
    kasjer_ustaw_sygnaly();
    
    if (polacz_z_zasobami(&kasjer_zasoby) == -1) {
        fprintf(stderr, "KASJER: Nie można połączyć z zasobami IPC\n");
        return 1;
    }
    zasoby_polaczone = 1;
    
    logger_init("logs/kasjer.log");
    LOG_I("KASJER: Rozpoczynam pracę (PID: %d)", getpid());
    
    StanWspoldzielony *stan = kasjer_zasoby.shm.stan;
    int sem_id = kasjer_zasoby.sem.sem_id;
    
    while (kasjer_dzialaj) {
        /* Sprawdź stan kolei */
        if (!stan->kolej_aktywna) {
            break;
        }
        
        Komunikat prosba;
        int wynik = odbierz_komunikat_nieblokujaco(kasjer_zasoby.mq.mq_kasa, 
                                                    &prosba, MSG_PROSBA_O_BILET);
        
        if (!kasjer_dzialaj) break;
        
        if (wynik > 0) {
            sem_czekaj_sysv(sem_id, SEM_IDX_KASA);
            if (kasjer_dzialaj) {
                obsluz_klienta(&prosba);
            }
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_KASA);
        } else if (wynik == 0) {
            /* BLOKUJĄCE czekanie 10ms gdy brak komunikatów */
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 10000;
            select(0, NULL, NULL, NULL, &tv);
        }
    }
    
    LOG_I("KASJER: Kończę pracę. Sprzedano %d biletów.", 
          stan->liczba_sprzedanych_biletow);
    logger_close();
    
    return 0;
}