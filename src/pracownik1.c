#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>
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
    time_t czas_dodania;  /* Kiedy turysta został dodany do kolejki */
} OczekujacyTurysta;

/* Timeout w sekundach - po tym czasie dziecko bez opiekuna jest usuwane */
#define TIMEOUT_OCZEKIWANIA_NA_OPIEKUNA 10

#define MAX_OCZEKUJACYCH 300
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

/* ========== POLICZ DZIECI OPIEKUNA W KOLEJCE ========== */
int policz_dzieci_opiekuna_w_kolejce(int opiekun_id) {
    int licznik = 0;
    for (int i = 0; i < liczba_oczekujacych; i++) {
        if (kolejka[i].opiekun_id == opiekun_id && kolejka[i].dziecko_pod_opieka) {
            licznik++;
        }
    }
    return licznik;
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

    /* ========== LOGIKA OPIEKUNA Z DZIEĆMI ========== */
    if (!turysta->dziecko_pod_opieka && turysta->liczba_dzieci > 0) {
        /* Opiekun ma dzieci - sprawdź ile jest w kolejce */
        int dzieci_w_kolejce = policz_dzieci_opiekuna_w_kolejce(turysta->id);

        if (dzieci_w_kolejce < turysta->liczba_dzieci) {
            /* Nie wszystkie dzieci są jeszcze w kolejce - sprawdź timeout */
            time_t teraz = time(NULL);
            time_t czas_oczekiwania = teraz - turysta->czas_dodania;

            if (czas_oczekiwania < TIMEOUT_OCZEKIWANIA_NA_OPIEKUNA) {
                /* Jeszcze czekamy na dzieci */
                return false;
            }

            /* Timeout - jedź z tymi dziećmi co są, lub sam */
            LOG_W("PRACOWNIK1: Opiekun #%d timeout - ma %d/%d dzieci, jedzie z tym co jest",
                  turysta->id, dzieci_w_kolejce, turysta->liczba_dzieci);

            /* Zaktualizuj liczbę dzieci do tych co faktycznie są */
            /* (aby dodaj_opiekuna_z_dziecmi działało poprawnie) */
        }

        /* WAŻNE: Sprawdź czy jest miejsce na opiekuna + dzieci które SĄ w kolejce */
        int wolne_miejsca = POJEMNOSC_KRZESELKA - aktualna_grupa.liczba;
        if (wolne_miejsca < 1 + dzieci_w_kolejce) {
            /* Nie ma miejsca na całą rodzinę - opiekun musi czekać na następne krzesełko */
            return false;
        }

        if (dzieci_w_kolejce > 0) {
            LOG_I("PRACOWNIK1: Opiekun #%d z %d dziećmi wchodzi na krzesełko",
                  turysta->id, dzieci_w_kolejce);
        }
    }

    /* ========== LOGIKA DZIECI POD OPIEKĄ (4-8 LAT) ========== */
    if (turysta->dziecko_pod_opieka) {
        /* Dziecko potrzebuje opiekuna w grupie - MUSI już być dodany */
        if (!opiekun_w_grupie(turysta->opiekun_id)) {
            /* Opiekun nie jest w grupie - dziecko NIE może wejść samo */
            /* Dziecko zostanie dodane automatycznie gdy opiekun wejdzie (patrz dodaj_opiekuna_z_dziecmi) */
            return false;
        }

        /* Sprawdź limit dzieci na opiekuna (max 2) */
        int dzieci_opiekuna = policz_dzieci_opiekuna_w_grupie(turysta->opiekun_id);
        if (dzieci_opiekuna >= MAX_DZIECI_POD_OPIEKA) {
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

/* ========== ATOMOWE DODANIE OPIEKUNA Z DZIEĆMI ========== */
/* Zwraca liczbę dodanych osób (opiekun + dzieci), lub 0 jeśli nie można dodać */
/* UWAGA: Nie wysyła powiadomień - to robi wyslij_grupe_na_krzeselko() */
int dodaj_opiekuna_z_dziecmi(int idx_opiekuna) {
    /* Skopiuj dane opiekuna PRZED usunięciem z kolejki */
    OczekujacyTurysta opiekun_kopia = kolejka[idx_opiekuna];
    int opiekun_id = opiekun_kopia.id;
    int dodano = 0;

    /* Znajdź wszystkie dzieci tego opiekuna FAKTYCZNIE w kolejce */
    int idx_dzieci[MAX_DZIECI_POD_OPIEKA];
    int liczba_dzieci_znalezionych = 0;

    /* Szukaj WSZYSTKICH dzieci w kolejce (nie ograniczaj do liczba_dzieci - może być mniej) */
    for (int i = 0; i < liczba_oczekujacych && liczba_dzieci_znalezionych < MAX_DZIECI_POD_OPIEKA; i++) {
        if (kolejka[i].opiekun_id == opiekun_id && kolejka[i].dziecko_pod_opieka) {
            idx_dzieci[liczba_dzieci_znalezionych++] = i;
        }
    }

    /* Dodaj opiekuna */
    dodaj_do_grupy(&opiekun_kopia);
    dodano++;

    /* Usuń opiekuna z kolejki */
    for (int j = idx_opiekuna; j < liczba_oczekujacych - 1; j++) {
        kolejka[j] = kolejka[j + 1];
    }
    liczba_oczekujacych--;

    /* Zaktualizuj indeksy dzieci (przesunęły się przez usunięcie opiekuna) */
    for (int i = 0; i < liczba_dzieci_znalezionych; i++) {
        if (idx_dzieci[i] > idx_opiekuna) {
            idx_dzieci[i]--;
        }
    }

    /* Dodaj dzieci (od końca żeby indeksy były poprawne) */
    for (int i = liczba_dzieci_znalezionych - 1; i >= 0; i--) {
        int idx = idx_dzieci[i];
        if (idx < liczba_oczekujacych && aktualna_grupa.liczba < POJEMNOSC_KRZESELKA) {
            OczekujacyTurysta *dziecko = &kolejka[idx];
            if (dziecko->opiekun_id == opiekun_id) {
                dodaj_do_grupy(dziecko);
                dodano++;

                /* Usuń dziecko z kolejki */
                for (int j = idx; j < liczba_oczekujacych - 1; j++) {
                    kolejka[j] = kolejka[j + 1];
                }
                liczba_oczekujacych--;

                /* Zaktualizuj pozostałe indeksy */
                for (int k = 0; k < i; k++) {
                    if (idx_dzieci[k] > idx) {
                        idx_dzieci[k]--;
                    }
                }
            }
        }
    }

    LOG_I("PRACOWNIK1: Dodano opiekuna #%d z %d dziećmi do grupy",
          opiekun_id, dodano - 1);

    return dodano;
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

    /* Śledzenie nabycia semaforów */
    bool krzeselka_nabyte = false;
    bool stan_nabyty = false;

    if (sem_czekaj_sysv(sem_id, SEM_IDX_KRZESELKA) == 0) {
        krzeselka_nabyte = true;
    }
    if (!p1_dzialaj || !krzeselka_nabyte) {
        if (krzeselka_nabyte) sem_sygnalizuj_sysv(sem_id, SEM_IDX_KRZESELKA);
        return;
    }

    if (sem_czekaj_sysv(sem_id, SEM_IDX_STAN) == 0) {
        stan_nabyty = true;
    }
    if (!p1_dzialaj || !stan_nabyty) {
        if (stan_nabyty) sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
        if (krzeselka_nabyte) sem_sygnalizuj_sysv(sem_id, SEM_IDX_KRZESELKA);
        return;
    }

    /* Znajdź DOWOLNE wolne krzesełko zamiast czekać na konkretne */
    int idx = -1;
    for (int i = 0; i < MAX_AKTYWNYCH_KRZESELEK; i++) {
        if (!stan->krzeselka[i].aktywne) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        /* Wszystkie krzesełka aktywne - to nie powinno się zdarzyć jeśli semafor działa */
        LOG_W("PRACOWNIK1: Brak wolnych krzesełek mimo nabytego semafora!");
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_KRZESELKA);
        return;
    }

    Krzeselko *k = &stan->krzeselka[idx];
    k->aktywne = true;
    k->liczba_pasazerow = aktualna_grupa.liczba;
    k->liczba_rowerzystow = aktualna_grupa.liczba_rowerzystow;
    k->czas_wyjazdu = time(NULL);

    for (int i = 0; i < aktualna_grupa.liczba; i++) {
        k->pasazerowie[i] = aktualna_grupa.osoby[i];
    }

    stan->liczba_aktywnych_krzeselek++;
    sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);

    LOG_I("PRACOWNIK1: Wysyłam krzesełko #%d z %d osobami", idx, aktualna_grupa.liczba);

    /* WAŻNE: Najpierw wyślij MSG_PERON_DOZWOLONY do każdego turysty */
    /* Turysta czeka na tę wiadomość w czekaj_na_peron() */
    for (int i = 0; i < aktualna_grupa.liczba && p1_dzialaj; i++) {
        Komunikat peron_msg;
        memset(&peron_msg, 0, sizeof(Komunikat));
        peron_msg.mtype = aktualna_grupa.osoby[i] + 20000;  /* Typ dla peronu */
        peron_msg.typ_komunikatu = MSG_PERON_DOZWOLONY;
        peron_msg.dane[0] = idx;
        wyslij_komunikat(p1_zasoby.mq.mq_pracownicy, &peron_msg);
    }

    /* Potem wyślij MSG_KRZESLO_GOTOWE - turysta czeka na to w wsiadz_na_krzeselko() */
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

    if (sem_czekaj_sysv(sem_id, SEM_IDX_STAN) == 0) {
        stan->kolej_zatrzymana = true;
        stan->kto_zatrzymal = 1;
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);

        if (stan->pid_pracownik2 > 0) {
            kill(stan->pid_pracownik2, SIGUSR1);
        }
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

    /* Czekaj z timeoutem - nie blokuj na zawsze */
    if (sem_czekaj_timeout_sysv(sem_id, SEM_IDX_SYNC, 3) != 0) {
        LOG_W("PRACOWNIK1: Timeout oczekiwania na pracownik2 - wznawianie mimo to");
    }
    if (!p1_dzialaj) return;

    if (sem_czekaj_sysv(sem_id, SEM_IDX_STAN) == 0) {
        stan->kolej_zatrzymana = false;
        stan->kto_zatrzymal = 0;
        p1_kolej_zatrzymana = 0;
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);

        LOG_I("PRACOWNIK1: Kolej wznowiona!");

        if (stan->pid_pracownik2 > 0) {
            kill(stan->pid_pracownik2, SIGUSR2);
        }
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

    if (sem_czekaj_sysv(sem_id, SEM_IDX_STAN) == 0) {
        stan->pid_pracownik1 = getpid();
        stan->pracownik1_gotowy = true;
        sem_sygnalizuj_sysv(sem_id, SEM_IDX_STAN);
    }

    inicjalizuj_grupe();
    
    while (p1_dzialaj && stan->kolej_aktywna) {
        /* Odbierz prośby od turystów ZAWSZE (nawet gdy kolej zatrzymana) */
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
                t->liczba_dzieci = prosba.dane[4];  /* Ile dzieci ma ten opiekun */
                t->czas_dodania = time(NULL);       /* Zapisz czas dodania */
                liczba_oczekujacych++;
            }
        }

        /* Gdy kolej zatrzymana - nie przetwarzaj kolejki, tylko czekaj */
        if (p1_kolej_zatrzymana) {
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 100000;  /* 100ms */
            select(0, NULL, NULL, NULL, &tv);
            continue;
        }

        /* Przetwarzanie kolejki - opiekunowie są dodawani razem z dziećmi */
        for (int i = 0; i < liczba_oczekujacych && p1_dzialaj; ) {
            OczekujacyTurysta *turysta = &kolejka[i];

            /* Usuń osierocone dzieci - ale tylko po upływie timeout */
            if (turysta->dziecko_pod_opieka) {
                int idx_opiekuna = znajdz_opiekuna_w_kolejce(turysta->opiekun_id);
                if (idx_opiekuna == -1 && !opiekun_w_grupie(turysta->opiekun_id)) {
                    /* Opiekun nie jest jeszcze w kolejce - sprawdź timeout */
                    time_t teraz = time(NULL);
                    time_t czas_oczekiwania = teraz - turysta->czas_dodania;

                    if (czas_oczekiwania < TIMEOUT_OCZEKIWANIA_NA_OPIEKUNA) {
                        /* Jeszcze czekamy na opiekuna - pomiń to dziecko na razie */
                        i++;
                        continue;
                    }

                    /* Timeout - opiekun nie przyszedł, usuń dziecko */
                    LOG_W("PRACOWNIK1: Usuwam osierocone dziecko #%d (opiekun #%d nie przyszedł przez %ld s)",
                          turysta->id, turysta->opiekun_id, czas_oczekiwania);

                    /* Wyślij powiadomienie żeby dziecko mogło kontynuować */
                    Komunikat peron_msg;
                    memset(&peron_msg, 0, sizeof(Komunikat));
                    peron_msg.mtype = turysta->id + 20000;
                    peron_msg.typ_komunikatu = MSG_PERON_DOZWOLONY;
                    peron_msg.dane[0] = -1;  /* -1 oznacza "wyjdź bez jazdy" */
                    wyslij_komunikat(p1_zasoby.mq.mq_pracownicy, &peron_msg);

                    /* Usuń z kolejki */
                    for (int j = i; j < liczba_oczekujacych - 1; j++) {
                        kolejka[j] = kolejka[j + 1];
                    }
                    liczba_oczekujacych--;
                    continue;  /* Nie inkrementuj i */
                }
            }

            if (moze_dolaczyc(turysta)) {
                /* Sprawdź czy to opiekun z dziećmi - jeśli tak, dodaj atomowo */
                if (!turysta->dziecko_pod_opieka && turysta->liczba_dzieci > 0) {
                    /* Opiekun z dziećmi - dodaj ich wszystkich razem */
                    dodaj_opiekuna_z_dziecmi(i);
                    /* i nie jest inkrementowane bo elementy się przesunęły */
                } else {
                    /* Zwykły turysta lub dziecko którego opiekun jest już w grupie */
                    dodaj_do_grupy(turysta);

                    /* Usuń z kolejki */
                    for (int j = i; j < liczba_oczekujacych - 1; j++) {
                        kolejka[j] = kolejka[j + 1];
                    }
                    liczba_oczekujacych--;
                    /* i nie jest inkrementowane bo elementy się przesunęły */
                }
            } else {
                i++;
            }

            if (grupa_pelna()) wyslij_grupe_na_krzeselko();
        }

        /* Wyślij niepełną grupę jeśli:
         * - grupa ma kogoś
         * - kolejka jest pusta LUB wszyscy w kolejce czekają (dzieci na opiekunów)
         */
        if (aktualna_grupa.liczba > 0 && p1_dzialaj) {
            bool wszyscy_czekaja = true;
            for (int i = 0; i < liczba_oczekujacych; i++) {
                OczekujacyTurysta *t = &kolejka[i];
                /* Sprawdź czy ten turysta mógłby dołączyć */
                if (!t->dziecko_pod_opieka || opiekun_w_grupie(t->opiekun_id)) {
                    /* Ten turysta mógłby dołączyć - nie wysyłaj jeszcze */
                    wszyscy_czekaja = false;
                    break;
                }
            }

            if (liczba_oczekujacych == 0 || wszyscy_czekaja) {
                wyslij_grupe_na_krzeselko();
            }
        }

        if (rand() % 2000 == 0 && !p1_kolej_zatrzymana && p1_dzialaj) {
            p1_zatrzymaj_kolej();
            /* BLOKUJĄCE czekanie z timeoutem 2 sekundy */
            sem_czekaj_timeout_sysv(sem_id, SEM_IDX_SYNC, 2);
            if (p1_dzialaj) p1_wznow_kolej();
        }

        /* BLOKUJĄCE czekanie - w turbo skróć czas */
        StanWspoldzielony *stan = p1_zasoby.shm.stan;
        int turbo = (stan && stan->turbo_mnoznik > 1) ? stan->turbo_mnoznik : 1;
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 50000 / turbo;  /* 50ms / turbo */
        select(0, NULL, NULL, NULL, &tv);
    }
    
    if (aktualna_grupa.liczba > 0 && p1_dzialaj) {
        wyslij_grupe_na_krzeselko();
    }
    
    LOG_I("PRACOWNIK1: Kończę pracę");
    logger_close();
    return 0;
}