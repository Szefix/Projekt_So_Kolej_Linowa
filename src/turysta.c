#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/select.h>
#include <limits.h>
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
void inicjalizuj_turystę(int id, int zadany_wiek, int opiekun_id, int liczba_dzieci) {
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
    ja.liczba_dzieci = liczba_dzieci;  /* Ile dzieci ten opiekun ma */
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

    StanWspoldzielony *stan = turysta_zasoby.shm.stan;

    Komunikat odpowiedz;
    /* Używamy timeout aby uniknąć deadlocka */
    /* WAŻNE: mtype = id + 1000 żeby nie kolidować z MSG_PROSBA_O_BILET=1 */
    long moj_typ_kasa = ja.id + 1000;
    while (turysta_dzialaj && stan->kolej_aktywna) {
        int wynik = odbierz_komunikat_timeout(turysta_zasoby.mq.mq_kasa, &odpowiedz, moj_typ_kasa, 2);

        if (wynik == 1) {
            break;  /* Sukces - odebrano komunikat */
        }

        if (wynik == -1) {
            if (!turysta_dzialaj) return -1;
            LOG_E("TURYSTA #%d: Błąd odbierania biletu", ja.id);
            return -1;
        }

        /* wynik == 0 (timeout) - sprawdź flagi i spróbuj ponownie */
        if (!stan->kolej_aktywna || !stan->godziny_pracy) {
            LOG_W("TURYSTA #%d: Kolej zamknięta podczas oczekiwania na bilet", ja.id);
            return -1;
        }
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

    /* Flagi do śledzenia które semafory zostały nabyte */
    bool vip_nabyty = false;
    bool stacja_nabyta = false;
    int bramka = -1;

    /* VIP - priorytet */
    if (ja.vip) {
        LOG_I("TURYSTA #%d [VIP]: Wchodzę bez kolejki", ja.id);
        if (!turysta_dzialaj) return -1;
        if (sem_czekaj_sysv(sem_id, SEM_IDX_VIP) == 0) {
            vip_nabyty = true;
        }
        if (!turysta_dzialaj) {
            if (vip_nabyty) sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
            return -1;
        }
    }

    if (!turysta_dzialaj) {
        if (vip_nabyty) sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
        return -1;
    }

    /* Czekaj na miejsce na stacji - VIP wchodzi bez kolejki */
    if (ja.vip) {
        /* VIP próbuje nabyć semafor nieblokująco - wchodzi niezależnie od wyniku */
        if (sem_probuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA) == 0) {
            stacja_nabyta = true;
        }
        LOG_I("TURYSTA #%d [VIP]: Wchodzę na stację bez kolejki", ja.id);
    } else {
        LOG_I("TURYSTA #%d: Czekam na miejsce na stacji", ja.id);
        if (sem_czekaj_sysv(sem_id, SEM_IDX_STACJA_DOLNA) == 0) {
            stacja_nabyta = true;
        }
    }

    if (!turysta_dzialaj) {
        if (stacja_nabyta) sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
        if (vip_nabyty) sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
        return -1;
    }

    /* Znajdź wolną bramkę */
    for (int i = 0; i < LICZBA_BRAMEK_WEJSCIOWYCH && turysta_dzialaj; i++) {
        if (sem_probuj_sysv(sem_id, SEM_IDX_BRAMKA_WEJ_BASE + i) == 0) {
            bramka = i;
            break;
        }
    }

    if (!turysta_dzialaj) {
        if (stacja_nabyta) sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
        if (vip_nabyty) sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
        if (bramka >= 0) sem_sygnalizuj_sysv(sem_id, SEM_IDX_BRAMKA_WEJ_BASE + bramka);
        return -1;
    }

    if (bramka == -1) {
        bramka = rand() % LICZBA_BRAMEK_WEJSCIOWYCH;
        if (sem_czekaj_sysv(sem_id, SEM_IDX_BRAMKA_WEJ_BASE + bramka) != 0) {
            /* Nie udało się nabyć bramki - anuluj */
            bramka = -1;
        }

        if (!turysta_dzialaj) {
            if (bramka >= 0) sem_sygnalizuj_sysv(sem_id, SEM_IDX_BRAMKA_WEJ_BASE + bramka);
            if (stacja_nabyta) sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
            if (vip_nabyty) sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
            return -1;
        }
    }

    if (bramka == -1) {
        /* Nie udało się zdobyć bramki */
        if (stacja_nabyta) sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
        if (vip_nabyty) sem_sygnalizuj_sysv(sem_id, SEM_IDX_VIP);
        return -1;
    }

    ja.bilet.liczba_uzyc++;

    /* Rejestruj przejście */
    if (turysta_dzialaj) {
        if (sem_czekaj_sysv(sem_id, SEM_IDX_REJESTR) == 0) {
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
    }

    /* Aktualizuj licznik */
    if (turysta_dzialaj) {
        if (sem_czekaj_sysv(sem_id, SEM_IDX_STAN) == 0) {
            stan->liczba_osob_na_stacji++;
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
        }
    }

    LOG_I("TURYSTA #%d: Przeszedłem przez bramkę %d", ja.id, bramka);

    /* Zwolnij bramkę */
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_BRAMKA_WEJ_BASE + bramka);

    if (vip_nabyty) {
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
    /* WAŻNE: Przy kolejnych zjazdach turysta jedzie samodzielnie
     * (dzieci i opiekun jadą osobno jako niezależne procesy) */
    int dzieci_do_wyslania = (ja.liczba_zjazdow == 0) ? ja.liczba_dzieci : 0;
    int opiekun_do_wyslania = (ja.liczba_zjazdow == 0) ? ja.opiekun_id : -1;
    bool dziecko_do_wyslania = (ja.liczba_zjazdow == 0) ? ja.dziecko_pod_opieka : false;

    Komunikat prosba;
    memset(&prosba, 0, sizeof(Komunikat));
    prosba.mtype = MSG_PROSBA_O_PERON;
    prosba.nadawca_id = ja.id;
    prosba.typ_komunikatu = MSG_PROSBA_O_PERON;
    prosba.dane[0] = ja.typ;
    prosba.dane[1] = dziecko_do_wyslania ? 1 : 0;
    prosba.dane[2] = opiekun_do_wyslania;
    prosba.dane[3] = ja.wiek;
    prosba.dane[4] = dzieci_do_wyslania;  /* Ile dzieci ma ten opiekun */

    if (!turysta_dzialaj) return -1;

    /* Retry wysyłania aż się uda - wysyłamy dokładnie 1 wiadomość */
    while (turysta_dzialaj && stan->kolej_aktywna) {
        if (wyslij_komunikat(turysta_zasoby.mq.mq_pracownicy, &prosba) == 0) break;
        if (!turysta_dzialaj) return -1;
        struct timeval tv_send;
        tv_send.tv_sec = 0;
        tv_send.tv_usec = 500000;  /* 500ms */
        select(0, NULL, NULL, NULL, &tv_send);
    }

    if (!turysta_dzialaj) return -1;

    /* Czekaj na wiadomość MSG_PERON_DOZWOLONY od pracownika1 */
    /* Pracownik1 wysyła tę wiadomość gdy turysta zostaje dodany do grupy */
    Komunikat odp;
    long moj_typ = ja.id + 20000;  /* Typ wiadomości dla peronu: id + 20000 */

    /* Używamy timeout aby uniknąć deadlocka gdy kolej zostanie zatrzymana */
    while (turysta_dzialaj && stan->kolej_aktywna) {
        int wynik = odbierz_komunikat_timeout(turysta_zasoby.mq.mq_pracownicy, &odp, moj_typ, 2);

        if (wynik == 1) {
            break;  /* Sukces - odebrano komunikat */
        }

        if (wynik == -1) {
            if (!turysta_dzialaj) return -1;
            LOG_E("TURYSTA #%d: Błąd oczekiwania na pozwolenie na peron", ja.id);
            return -1;
        }

        /* wynik == 0 (timeout) - sprawdź flagi i spróbuj ponownie */
        if (!stan->kolej_aktywna || !stan->godziny_pracy) {
            LOG_W("TURYSTA #%d: Kolej zamknięta podczas oczekiwania na peron", ja.id);
            return -1;
        }
    }

    if (!turysta_dzialaj) return -1;

    /* Sprawdź czy zostaliśmy odrzuceni (osierocone dziecko) */
    if (odp.dane[0] == -1) {
        LOG_W("TURYSTA #%d: Odrzucony - opiekun wyjechał beze mnie", ja.id);
        return -1;
    }

    ja.status = STATUS_NA_PERONIE;
    LOG_I("TURYSTA #%d: Wchodzę na peron", ja.id);

    /* Aktualizuj liczniki */
    if (turysta_dzialaj) {
        if (sem_czekaj_sysv(sem_id, SEM_IDX_STAN) == 0) {
            stan->liczba_osob_na_stacji--;
            stan->liczba_osob_na_peronie++;
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
        }
    }

    return turysta_dzialaj ? 0 : -1;
}

/* Wsiadanie na krzesełko */
int wsiadz_na_krzeselko(void) {
    if (!turysta_dzialaj) return -1;

    StanWspoldzielony *stan = turysta_zasoby.shm.stan;
    int sem_id = turysta_zasoby.sem.sem_id;

    LOG_I("TURYSTA #%d: Czekam na krzesełko", ja.id);

    if (!turysta_dzialaj) return -1;

    /* Czekaj na potwierdzenie od pracownika1 - on wysyła gdy krzesełko jest gotowe */
    /* Pracownik1 już wie że turysta jest na peronie (z prośby o peron) */
    Komunikat odp;
    long moj_typ = ja.id + 10000;

    /* Używamy timeout aby uniknąć deadlocka gdy kolej zostanie zatrzymana */
    while (turysta_dzialaj && stan->kolej_aktywna) {
        int wynik = odbierz_komunikat_timeout(turysta_zasoby.mq.mq_krzesla, &odp, moj_typ, 2);

        if (wynik == 1) {
            break;  /* Sukces - odebrano komunikat */
        }

        if (wynik == -1) {
            if (!turysta_dzialaj) return -1;
            LOG_E("TURYSTA #%d: Błąd oczekiwania na krzesełko", ja.id);
            return -1;
        }

        /* wynik == 0 (timeout) - sprawdź flagi i spróbuj ponownie */
        if (!stan->kolej_aktywna || !stan->godziny_pracy) {
            LOG_W("TURYSTA #%d: Kolej zamknięta podczas oczekiwania na krzesełko", ja.id);
            return -1;
        }
    }

    if (!turysta_dzialaj) return -1;

    int krzeselko_id = odp.dane[0];
    
    ja.status = STATUS_NA_KRZESELKU;
    LOG_I("TURYSTA #%d: Wsiadłem na krzesełko #%d", ja.id, krzeselko_id);

    /* Aktualizuj licznik */
    if (turysta_dzialaj) {
        if (sem_czekaj_sysv(sem_id, SEM_IDX_STAN) == 0) {
            stan->liczba_osob_na_peronie--;
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
        }
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

    /* Pobierz mnożnik turbo ze stanu współdzielonego */
    StanWspoldzielony *stan = turysta_zasoby.shm.stan;
    int turbo = (stan && stan->turbo_mnoznik > 1) ? stan->turbo_mnoznik : 1;

    int czas_trasy = czasy_tras[wybor] / turbo;  /* Skróć czas w turbo */
    if (czas_trasy < 1) czas_trasy = 1;          /* Minimum 1 sekunda */

    ja.status = STATUS_NA_TRASIE;
    LOG_I("TURYSTA #%d (rowerzysta): Wybieram trasę %s (czas: %ds%s)",
          ja.id, nazwy_tras[wybor], czas_trasy, turbo > 1 ? " [TURBO]" : "");

    /* Symulacja czasu przejazdu - BLOKUJĄCE czekanie zamiast busy waiting */
    for (int i = 0; i < czas_trasy && turysta_dzialaj; i++) {
        /* select() blokuje proces */
        struct timeval tv;
        tv.tv_sec = (turbo > 1) ? 0 : 1;
        tv.tv_usec = (turbo > 1) ? (1000000 / turbo) : 0;
        select(0, NULL, NULL, NULL, &tv);
    }
    
    if (turysta_dzialaj) {
        ja.liczba_zjazdow++;
        LOG_I("TURYSTA #%d: Ukończyłem zjazd #%d", ja.id, ja.liczba_zjazdow);
    }
}

/* ========== BEZPIECZNE PARSOWANIE ARGUMENTU (zamiast atoi) ========== */
static int bezpieczne_parsuj(const char *str, int *wynik) {
    if (str == NULL || *str == '\0') return -1;
    char *koniec;
    errno = 0;
    long wartosc = strtol(str, &koniec, 10);
    if (*koniec != '\0' || errno == ERANGE || wartosc > INT_MAX || wartosc < INT_MIN) return -1;
    *wynik = (int)wartosc;
    return 0;
}

/* ========== GŁÓWNA FUNKCJA ========== */
int main(int argc, char *argv[]) {
    /* Parsuj argumenty */
    if (argc < 5) {
        fprintf(stderr, "Użycie: %s <id> <wiek> <opiekun_id> <liczba_dzieci>\n", argv[0]);
        return 1;
    }

    int id, wiek, opiekun, liczba_dzieci;
    if (bezpieczne_parsuj(argv[1], &id) != 0 ||
        bezpieczne_parsuj(argv[2], &wiek) != 0 ||
        bezpieczne_parsuj(argv[3], &opiekun) != 0 ||
        bezpieczne_parsuj(argv[4], &liczba_dzieci) != 0) {
        fprintf(stderr, "Błąd: niepoprawne argumenty liczbowe\n");
        return 1;
    }
    
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
    
    /* Wszyscy turysci piszą do wspólnego pliku */
    logger_init("logs/wszyscy_turysci.log");
    logger_set_level(LOG_WARN);
    
    inicjalizuj_turystę(id, wiek, opiekun, liczba_dzieci);
    
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

        /* Symulacja jazdy na górę - BLOKUJĄCE czekanie */
        /* W turbo skróć czas jazdy */
        int turbo = (stan && stan->turbo_mnoznik > 1) ? stan->turbo_mnoznik : 1;
        int czas_jazdy = (turbo > 1) ? 1 : 2;  /* 2s normalnie, 1s w turbo */
        for (int i = 0; i < czas_jazdy && turysta_dzialaj; i++) {
            struct timeval tv;
            tv.tv_sec = (turbo > 1) ? 0 : 1;
            tv.tv_usec = (turbo > 1) ? (500000 / turbo) : 0;  /* 0.5s / turbo */
            select(0, NULL, NULL, NULL, &tv);
        }

        if (!turysta_dzialaj) {
            sem_sygnalizuj_sysv(sem_id, SEM_IDX_STACJA_DOLNA);
            break;
        }

        /* Na stacji górnej */
        ja.status = STATUS_NA_STACJI_GORNEJ;
        LOG_I("TURYSTA #%d: Dojechałem na stację górną", ja.id);
        /* UWAGA: SEM_IDX_KRZESELKA jest zwalniany przez pracownik2
         * gdy krzesełko dojedzie na górę - NIE zwalniamy tutaj */

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
