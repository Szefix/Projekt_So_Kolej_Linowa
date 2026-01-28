#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include "config.h"
#include "types.h"
#include "ipc_utils.h"
#include "logger.h"

static volatile sig_atomic_t turysta_dzialaj = 1;
static ZasobyIPC turysta_zasoby;
static Turysta ja;

/* ========== OBSŁUGA SYGNAŁÓW Z sigaction() ========== */
static void turysta_obsluz_sygnal(int sig, siginfo_t *info, void *context) {
    (void)info; (void)context; (void)sig;
    turysta_dzialaj = 0;
}

void turysta_ustaw_sygnaly(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = turysta_obsluz_sygnal;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
}

/* Inicjalizacja turysty */
void inicjalizuj_turystę(int id, int zadany_wiek, int opiekun_id) {
    memset(&ja, 0, sizeof(Turysta));
    
    ja.id = id;
    ja.pid = getpid();
    ja.wiek = (zadany_wiek > 0) ? zadany_wiek : ((rand() % 76) + 4);
    
    if (ja.wiek >= 12 && rand() % 100 < 40) {
        ja.typ = ROWERZYSTA;
    } else {
        ja.typ = PIESZY;
    }
    
    ja.vip = (rand() % 100 < PROCENT_VIP);
    ja.dziecko_pod_opieka = (ja.wiek >= WIEK_MIN_DZIECKO && ja.wiek < WIEK_DZIECKO_OPIEKA);
    ja.opiekun_id = opiekun_id;
    ja.status = STATUS_NOWY;
    ja.liczba_zjazdow = 0;
    
    for (int i = 0; i < MAX_DZIECI_POD_OPIEKA; i++) {
        ja.dzieci_pod_opieka[i] = -1;
    }
}

/* Sprawdzenie ważności biletu */
bool sprawdz_waznosc_biletu(void) {
    if (!ja.bilet.aktywny) return false;
    
    time_t teraz = time(NULL);
    
    if (ja.bilet.typ == BILET_JEDNORAZOWY) {
        return ja.bilet.liczba_uzyc < ja.bilet.max_uzyc;
    }
    
    if (ja.bilet.czas_waznosci > 0) {
        return teraz < ja.bilet.czas_waznosci;
    }
    
    return true;
}

/* Kupowanie biletu */
int kup_bilet(void) {
    /* Sprawdź czy jeszcze działamy */
    if (!turysta_dzialaj) return -1;
    
    LOG_I("TURYSTA #%d: Podchodzę do kasy", ja.id);
    
    int typy[] = {BILET_JEDNORAZOWY, BILET_CZASOWY_TK1, BILET_CZASOWY_TK2, 
                  BILET_CZASOWY_TK3, BILET_DZIENNY};
    int typ = typy[rand() % 5];
    
    Komunikat prosba;
    memset(&prosba, 0, sizeof(Komunikat));
    prosba.mtype = MSG_PROSBA_O_BILET;
    prosba.nadawca_id = ja.id;
    prosba.typ_komunikatu = MSG_PROSBA_O_BILET;
    prosba.dane[0] = typ;
    prosba.dane[1] = ja.wiek;
    prosba.dane[2] = ja.vip ? 1 : 0;
    
    if (!turysta_dzialaj) return -1;
    
    if (wyslij_komunikat(turysta_zasoby.mq.mq_kasa, &prosba) == -1) {
        if (!turysta_dzialaj) return -1;
        LOG_E("TURYSTA #%d: Błąd wysyłania prośby", ja.id);
        return -1;
    }
    
    ja.status = STATUS_OCZEKUJE_NA_BILET;
    
    if (!turysta_dzialaj) return -1;
    
    Komunikat odpowiedz;
    if (odbierz_komunikat(turysta_zasoby.mq.mq_kasa, &odpowiedz, ja.id) == -1) {
        if (!turysta_dzialaj) return -1;
        LOG_E("TURYSTA #%d: Błąd odbierania biletu", ja.id);
        return -1;
    }
    
    if (!turysta_dzialaj) return -1;
    
    ja.bilet.id = odpowiedz.dane[0];
    ja.bilet.typ = odpowiedz.dane[1];
    ja.bilet.max_uzyc = odpowiedz.dane[2];
    ja.bilet.czas_waznosci = (time_t)odpowiedz.dane[3];
    ja.bilet.vip = (odpowiedz.dane[4] != 0);
    ja.bilet.aktywny = true;
    ja.bilet.liczba_uzyc = 0;
    ja.bilet.czas_zakupu = time(NULL);
    
    ja.status = STATUS_MA_BILET;
    LOG_I("TURYSTA #%d: Kupiłem bilet #%d (typ: %d)", ja.id, ja.bilet.id, ja.bilet.typ);
    
    return 0;
}

/* Przejście przez bramkę wejściową */
int przejdz_bramke_wejsciowa(void) {
    if (!turysta_dzialaj) return -1;
    
    StanWspoldzielony *stan = turysta_zasoby.shm.stan;
    int sem_id = turysta_zasoby.sem.sem_id;
    
    if (!stan->godziny_pracy) {
        LOG_W("TURYSTA #%d: Kolej zamknięta!", ja.id);
        return -1;
    }
    
    if (!sprawdz_waznosc_biletu()) {
        LOG_W("TURYSTA #%d: Bilet nieważny!", ja.id);
        return -1;
    }
    
    ja.status = STATUS_PRZED_BRAMKA_WEJSCIOWA;
    
    /* VIP - priorytet */
    if (ja.vip) {
        LOG_I("TURYSTA #%d [VIP]: Wchodzę bez kolejki", ja.id);
        if (!turysta_dzialaj) return -1;
        sem_czekaj_sysv(sem_id, SEM_IDX_VIP);
        if (!turysta_dzialaj) {
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
            return -1;
        }
    }
    
    LOG_I("TURYSTA #%d: Czekam na miejsce na stacji", ja.id);
    
    if (!turysta_dzialaj) {
        if (ja.vip) sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
        return -1;
    }
    
    /* Czekaj na miejsce na stacji */
    sem_czekaj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
    
    if (!turysta_dzialaj) {
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
        if (ja.vip) sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
        return -1;
    }
    
    /* Znajdź wolną bramkę */
    int bramka = -1;
    for (int i = 0; i < LICZBA_BRAMEK_WEJSCIOWYCH && turysta_dzialaj; i++) {
        if (sem_probuj_sysv(sem_id, SEM_IDX_BRAMKA_WEJ_BASE + i) == 0) {
            bramka = i;
            break;
        }
    }
    
    if (!turysta_dzialaj) {
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
        if (ja.vip) sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
        if (bramka >= 0) sem_sygnalizuj_sysv(sem_id, SEM_IDX_BRAMKA_WEJ_BASE + bramka);
        return -1;
    }
    
    if (bramka == -1) {
        bramka = rand() % LICZBA_BRAMEK_WEJSCIOWYCH;
        sem_czekaj_sysv(sem_id, SEM_IDX_BRAMKA_WEJ_BASE + bramka);
        
        if (!turysta_dzialaj) {
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_BRAMKA_WEJ_BASE + bramka);
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
            if (ja.vip) sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
            return -1;
        }
    }
    
    ja.bilet.liczba_uzyc++;
    
    /* Rejestruj przejście */
    if (turysta_dzialaj) {
        sem_czekaj_sysv(sem_id, SEM_IDX_REJESTR);
        if (turysta_dzialaj && stan->liczba_wpisow_rejestru < MAX_WPISOW_REJESTRU) {
            WpisRejestru *wpis = &stan->rejestr[stan->liczba_wpisow_rejestru];
            wpis->bilet_id = ja.bilet.id;
            wpis->turysta_id = ja.id;
            wpis->czas = time(NULL);
            wpis->numer_bramki = bramka;
            wpis->numer_zjazdu = ja.liczba_zjazdow + 1;
            stan->liczba_wpisow_rejestru++;
        }
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_REJESTR);
    }
    
    /* Aktualizuj licznik */
    if (turysta_dzialaj) {
        sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
        stan->liczba_osob_na_stacji++;
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    }
    
    LOG_I("TURYSTA #%d: Przeszedłem przez bramkę %d", ja.id, bramka);
    
    /* Zwolnij bramkę */
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_BRAMKA_WEJ_BASE + bramka);
    
    if (ja.vip) {
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
    }
    
    ja.status = STATUS_NA_STACJI_DOLNEJ;
    return turysta_dzialaj ? 0 : -1;
}

/* Czekanie na wejście na peron */
int czekaj_na_peron(void) {
    if (!turysta_dzialaj) return -1;
    
    StanWspoldzielony *stan = turysta_zasoby.shm.stan;
    int sem_id = turysta_zasoby.sem.sem_id;
    
    ja.status = STATUS_OCZEKUJE_NA_PERON;
    LOG_I("TURYSTA #%d: Czekam na pozwolenie wejścia na peron", ja.id);
    
    /* Wyślij prośbę do pracownika1 */
    Komunikat prosba;
    memset(&prosba, 0, sizeof(Komunikat));
    prosba.mtype = MSG_PROSBA_O_PERON;
    prosba.nadawca_id = ja.id;
    prosba.typ_komunikatu = MSG_PROSBA_O_PERON;
    prosba.dane[0] = ja.typ;
    prosba.dane[1] = ja.dziecko_pod_opieka ? 1 : 0;
    prosba.dane[2] = ja.opiekun_id;
    prosba.dane[3] = ja.wiek;
    
    if (!turysta_dzialaj) return -1;
    
    if (wyslij_komunikat(turysta_zasoby.mq.mq_pracownicy, &prosba) == -1) {
        if (!turysta_dzialaj) return -1;
        LOG_E("TURYSTA #%d: Błąd wysyłania prośby o peron", ja.id);
        return -1;
    }
    
    if (!turysta_dzialaj) return -1;
    
    /* Czekaj na semaforze */
    sem_czekaj_sysv(sem_id, SEM_IDX_PERON);
    
    if (!turysta_dzialaj) return -1;
    
    /* Sprawdź zatrzymanie kolei */
    if (stan->kolej_zatrzymana && turysta_dzialaj) {
        LOG_W("TURYSTA #%d: Kolej zatrzymana! Czekam...", ja.id);
        sem_czekaj_sysv(sem_id, SEM_IDX_PERON);
        if (!turysta_dzialaj) return -1;
    }
    
    ja.status = STATUS_NA_PERONIE;
    LOG_I("TURYSTA #%d: Wchodzę na peron", ja.id);
    
    /* Aktualizuj liczniki */
    if (turysta_dzialaj) {
        sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
        stan->liczba_osob_na_stacji--;
        stan->liczba_osob_na_peronie++;
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    }
    
    return turysta_dzialaj ? 0 : -1;
}

/* Wsiadanie na krzesełko */
int wsiadz_na_krzeselko(void) {
    if (!turysta_dzialaj) return -1;
    
    StanWspoldzielony *stan = turysta_zasoby.shm.stan;
    int sem_id = turysta_zasoby.sem.sem_id;
    
    LOG_I("TURYSTA #%d: Czekam na krzesełko", ja.id);
    
    /* Wyślij gotowość */
    Komunikat msg;
    memset(&msg, 0, sizeof(Komunikat));
    msg.mtype = MSG_WSIADANIE_NA_KRZESLO;
    msg.nadawca_id = ja.id;
    msg.typ_komunikatu = MSG_WSIADANIE_NA_KRZESLO;
    msg.dane[0] = ja.typ;
    msg.dane[1] = ja.dziecko_pod_opieka ? 1 : 0;
    
    if (!turysta_dzialaj) return -1;
    
    if (wyslij_komunikat(turysta_zasoby.mq.mq_krzesla, &msg) == -1) {
        if (!turysta_dzialaj) return -1;
        LOG_E("TURYSTA #%d: Błąd wsiadania", ja.id);
        return -1;
    }
    
    if (!turysta_dzialaj) return -1;
    
    /* Czekaj na potwierdzenie */
    Komunikat odp;
    long moj_typ = ja.id + 10000;
    
    if (odbierz_komunikat(turysta_zasoby.mq.mq_krzesla, &odp, moj_typ) == -1) {
        if (!turysta_dzialaj) return -1;
        LOG_E("TURYSTA #%d: Błąd oczekiwania na krzesełko", ja.id);
        return -1;
    }
    
    if (!turysta_dzialaj) return -1;
    
    int krzeselko_id = odp.dane[0];
    
    ja.status = STATUS_NA_KRZESELKU;
    LOG_I("TURYSTA #%d: Wsiadłem na krzesełko #%d", ja.id, krzeselko_id);
    
    /* Aktualizuj licznik */
    if (turysta_dzialaj) {
        sem_czekaj_sysv(sem_id, SEM_IDX_STAN);
        stan->liczba_osob_na_peronie--;
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    }
    
    return turysta_dzialaj ? krzeselko_id : -1;
}

/* Jazda na trasie rowerowej */
void jedz_na_trasie(void) {
    if (!turysta_dzialaj) return;
    
    if (ja.typ != ROWERZYSTA) {
        LOG_I("TURYSTA #%d (pieszy): Schodzę ze stacji górnej", ja.id);
        ja.liczba_zjazdow++;
        return;
    }
    
    int czasy_tras[] = {CZAS_TRASY_T1, CZAS_TRASY_T2, CZAS_TRASY_T3};
    const char *nazwy_tras[] = {"T1 (łatwa)", "T2 (średnia)", "T3 (trudna)"};
    int wybor = rand() % 3;
    int czas_trasy = czasy_tras[wybor];
    
    ja.status = STATUS_NA_TRASIE;
    LOG_I("TURYSTA #%d (rowerzysta): Wybieram trasę %s (czas: %ds)", 
          ja.id, nazwy_tras[wybor], czas_trasy);
    
    /* Symulacja czasu przejazdu - podziel na krótsze odcinki dla szybszego wyjścia */
    for (int i = 0; i < czas_trasy && turysta_dzialaj; i++) {
        sleep(1);
    }
    
    if (turysta_dzialaj) {
        ja.liczba_zjazdow++;
        LOG_I("TURYSTA #%d: Ukończyłem zjazd #%d", ja.id, ja.liczba_zjazdow);
    }
}

/* ========== GŁÓWNA FUNKCJA ========== */
int main(int argc, char *argv[]) {
    /* Parsuj argumenty */
    if (argc < 4) {
        fprintf(stderr, "Użycie: %s <id> <wiek> <opiekun_id>\n", argv[0]);
        return 1;
    }
    
    int id = atoi(argv[1]);
    int wiek = atoi(argv[2]);
    int opiekun = atoi(argv[3]);
    
    /* Walidacja */
    if (id <= 0 || id > 10000) {
        fprintf(stderr, "Błędne ID turysty: %d\n", id);
        return 1;
    }
    if (wiek < WIEK_MIN_DZIECKO || wiek > 100) {
        fprintf(stderr, "Błędny wiek: %d (dozwolone: %d-100)\n", wiek, WIEK_MIN_DZIECKO);
        return 1;
    }
    
    srand(time(NULL) ^ (getpid() << 16) ^ id);
    turysta_ustaw_sygnaly();
    
    if (polacz_z_zasobami(&turysta_zasoby) == -1) {
        fprintf(stderr, "TURYSTA #%d: Nie można połączyć z zasobami IPC\n", id);
        return 1;
    }
    
    char log_name[64];
    snprintf(log_name, sizeof(log_name), "logs/turysta_%d.log", id);
    logger_init(log_name);
    
    inicjalizuj_turystę(id, wiek, opiekun);
    
    LOG_I("TURYSTA #%d: Przychodzę (wiek: %d, %s, %s)", 
          ja.id, ja.wiek, 
          ja.typ == ROWERZYSTA ? "rowerzysta" : "pieszy",
          ja.vip ? "VIP" : "zwykły");
    
    StanWspoldzielony *stan = turysta_zasoby.shm.stan;
    int sem_id = turysta_zasoby.sem.sem_id;
    
    /* Główna pętla */
    while (turysta_dzialaj && stan->kolej_aktywna) {
        /* Kup bilet jeśli nie masz ważnego */
        if (!sprawdz_waznosc_biletu()) {
            if (!stan->godziny_pracy || !turysta_dzialaj) {
                LOG_I("TURYSTA #%d: Kolej zamknięta, wychodzę", ja.id);
                break;
            }
            if (kup_bilet() == -1) {
                if (!turysta_dzialaj) break;
                LOG_E("TURYSTA #%d: Nie udało się kupić biletu", ja.id);
                break;
            }
        }
        
        if (!turysta_dzialaj) break;
        
        /* Przejdź przez bramkę wejściową */
        if (przejdz_bramke_wejsciowa() == -1) {
            break;
        }
        
        if (!turysta_dzialaj) {
            /* Zwolnij miejsce jeśli już weszliśmy na stację */
            if (ja.status == STATUS_NA_STACJI_DOLNEJ) {
                sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
            }
            break;
        }
        
        /* Czekaj na wejście na peron */
        if (czekaj_na_peron() == -1) {
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
            break;
        }
        
        if (!turysta_dzialaj) {
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
            break;
        }
        
        /* Wsiądź na krzesełko */
        int krzeselko = wsiadz_na_krzeselko();
        if (krzeselko == -1) {
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
            break;
        }
        
        if (!turysta_dzialaj) {
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
            break;
        }
        
        /* Symulacja jazdy na górę - podzielona dla szybszego wyjścia */
        for (int i = 0; i < 2 && turysta_dzialaj; i++) {
            sleep(1);
        }
        
        if (!turysta_dzialaj) {
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
            break;
        }
        
        /* Na stacji górnej */
        ja.status = STATUS_NA_STACJI_GORNEJ;
        LOG_I("TURYSTA #%d: Dojechałem na stację górną", ja.id);
        
        /* Wyjdź jednym z wyjść */
        int wyjscie = rand() % LICZBA_WYJSC;
        LOG_I("TURYSTA #%d: Wychodzę wyjściem %d", ja.id, wyjscie);
        
        /* Jedź na trasie */
        jedz_na_trasie();
        
        /* Zwolnij miejsce na stacji */
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
        
        if (!turysta_dzialaj) break;
        
        /* Sprawdź czy kontynuować */
        if (!sprawdz_waznosc_biletu()) {
            LOG_I("TURYSTA #%d: Bilet nieważny, kończę", ja.id);
            break;
        }
        
        /* 30% szans na zakończenie */
        if (rand() % 100 < 30) {
            LOG_I("TURYSTA #%d: Wystarczy na dziś, wychodzę", ja.id);
            break;
        }
        
        /* Sprawdź godziny pracy */
        if (!stan->godziny_pracy) {
            LOG_I("TURYSTA #%d: Kolej się zamyka, wychodzę", ja.id);
            break;
        }
    }
    
    ja.status = STATUS_ZAKONCZONY;
    LOG_I("TURYSTA #%d: Kończę dzień z %d zjazdami", ja.id, ja.liczba_zjazdow);
    
    logger_close();
    return 0;
}