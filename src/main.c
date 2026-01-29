#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include "config.h"
#include "types.h"
#include "ipc_utils.h"
#include "pipe_comm.h"
#include "logger.h"

/* Zmienne globalne */
static ZasobyIPC zasoby;
static pid_t pid_kasjer = 0;
static pid_t pid_pracownik1 = 0;
static pid_t pid_pracownik2 = 0;
static pid_t pidy_turystow[200];
static int liczba_turystow = 0;
static volatile sig_atomic_t zakonczenie = 0;

/* Wątek monitorowania stanu */
static pthread_t watek_monitora;
static volatile int monitor_aktywny = 1;

/* Pipe do komunikacji z wątkiem monitora */
static PipeKanaly pipe_monitor;

/* Mutex do statystyk z użyciem pthread_mutex_trylock() */
static pthread_mutex_t mutex_statystyki = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_statystyki = PTHREAD_COND_INITIALIZER;
static int licznik_przetworzen = 0;

/* Zmienna warunkowa dla monitora */
static pthread_mutex_t mutex_monitor = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cond_monitor = PTHREAD_COND_INITIALIZER;

/* ========== WĄTEK STATYSTYK (używa pthread_detach i pthread_cond_timedwait) ========== */
void *watek_statystyk_funkcja(void *arg) {
    (void)arg;
    StanWspoldzielony *stan = zasoby.shm.stan;

    /* Odłącz wątek - nie wymaga join() */
    pthread_detach(pthread_self());
    LOG_I("STATYSTYKI: Wątek statystyk odłączony (pthread_detach)");

    while (monitor_aktywny && stan->kolej_aktywna) {
        /* BLOKUJĄCE czekanie z timeoutem 1 sekunda - pthread_cond_timedwait() */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;  /* Timeout 1 sekunda */

        pthread_mutex_lock(&mutex_statystyki);
        pthread_cond_timedwait(&cond_statystyki, &mutex_statystyki, &ts);

        /* Po obudzeniu - zapisz statystyki */
        licznik_przetworzen++;
        int fd = open("logs/statystyki_live.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd != -1) {
            char buf[256];
            int len = snprintf(buf, sizeof(buf),
                "Statystyki (iteracja %d):\n"
                "- Osoby na stacji: %d\n"
                "- Zjazdy: %d\n"
                "- Bilety: %d\n",
                licznik_przetworzen,
                stan->liczba_osob_na_stacji,
                stan->laczna_liczba_zjazdow,
                stan->liczba_sprzedanych_biletow);
            write(fd, buf, len);
            close(fd);
        }

        pthread_mutex_unlock(&mutex_statystyki);
    }

    LOG_I("STATYSTYKI: Wątek zakończony (był odłączony, nie wymaga join)");
    return NULL;
}

/* ========== WĄTEK MONITOROWANIA ========== */
void *watek_monitor_funkcja(void *arg) {
    (void)arg;
    StanWspoldzielony *stan = zasoby.shm.stan;

    LOG_I("MONITOR: Wątek monitorowania uruchomiony");

    while (monitor_aktywny && stan->kolej_aktywna) {
        /* Sprawdź czy jest komunikat przez pipe */
        KomunikatPipe msg;
        int wynik = odbierz_z_fifo_nieblokujaco(pipe_monitor.fd_read, &msg);

        if (wynik > 0) {
            LOG_D("MONITOR: Otrzymano komunikat typu %d", msg.typ);
        }

        /* Wyświetl stan */
        printf("\r[Czas: %3ld s] Stacja: %2d | Peron: %2d | Krzesełka: %2d | Zjazdy: %3d | Bilety: %3d   ",
               time(NULL) - stan->czas_startu,
               stan->liczba_osob_na_stacji,
               stan->liczba_osob_na_peronie,
               stan->liczba_aktywnych_krzeselek,
               stan->laczna_liczba_zjazdow,
               stan->liczba_sprzedanych_biletow);
        fflush(stdout);

        /* BLOKUJĄCE czekanie z timeoutem 500ms - pthread_cond_timedwait() */
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 500000000;  /* +500ms */
        if (ts.tv_nsec >= 1000000000) {
            ts.tv_sec += 1;
            ts.tv_nsec -= 1000000000;
        }

        pthread_mutex_lock(&mutex_monitor);
        pthread_cond_timedwait(&cond_monitor, &mutex_monitor, &ts);
        pthread_mutex_unlock(&mutex_monitor);
    }

    LOG_I("MONITOR: Wątek monitorowania zakończony");
    pthread_exit(NULL);
}

/* ========== OBSŁUGA SYGNAŁÓW Z sigaction() ========== */
void obsluz_sygnal_glowny(int sig, siginfo_t *info, void *context) {
    (void)info;
    (void)context;
    
    if (sig == SIGINT || sig == SIGTERM) {
        zakonczenie = 1;
    }
    /* Nie obsługujemy SIGCHLD tutaj - zbieramy ręcznie */
}

void ustaw_obsluge_sygnalow(void) {
    struct sigaction sa;
    
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = obsluz_sygnal_glowny;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&sa.sa_mask);
    
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction SIGINT");
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction SIGTERM");
    }
    
    /* Ignoruj SIGCHLD - zbieramy ręcznie w zbierz_procesy() */
    signal(SIGCHLD, SIG_IGN);
}

/* ========== URUCHAMIANIE PROCESÓW Z exec() ========== */
void uruchom_kasjer(void) {
    pid_kasjer = fork();
    
    if (pid_kasjer == -1) {
        perror("fork kasjer");
        return;
    }
    
    if (pid_kasjer == 0) {
        execl("./bin/kasjer", "kasjer", NULL);
        perror("execl kasjer");
        _exit(1);
    }
    
    LOG_I("MAIN: Uruchomiono kasjera (PID: %d)", pid_kasjer);
}

void uruchom_pracownika(int numer) {
    pid_t *pid = (numer == 1) ? &pid_pracownik1 : &pid_pracownik2;
    const char *nazwa = (numer == 1) ? "./bin/pracownik1" : "./bin/pracownik2";
    const char *arg = (numer == 1) ? "pracownik1" : "pracownik2";
    
    *pid = fork();
    
    if (*pid == -1) {
        perror("fork pracownik");
        return;
    }
    
    if (*pid == 0) {
        execl(nazwa, arg, NULL);
        perror("execl pracownik");
        _exit(1);
    }
    
    LOG_I("MAIN: Uruchomiono pracownika%d (PID: %d)", numer, *pid);
}

void uruchom_turystę(int id, int wiek, int opiekun) {
    pid_t pid = fork();
    
    if (pid == -1) {
        perror("fork turysta");
        return;
    }
    
    if (pid == 0) {
        /* ========== UŻYCIE dup2() - przekierowanie stderr do pliku ========== */
        /* Wszyscy turysci piszą błędy do wspólnego pliku */
        int fd_err = open("logs/wszyscy_turysci_stderr.log", O_CREAT | O_WRONLY | O_APPEND, 0644);
        if (fd_err != -1) {
            /* dup2() duplikuje deskryptor fd_err na STDERR_FILENO */
            if (dup2(fd_err, STDERR_FILENO) == -1) {
                perror("dup2 stderr");
            }
            close(fd_err);  /* Zamknij oryginalny, stderr teraz wskazuje na plik */
        }
        
        char arg_id[16], arg_wiek[16], arg_opiekun[16];
        snprintf(arg_id, sizeof(arg_id), "%d", id);
        snprintf(arg_wiek, sizeof(arg_wiek), "%d", wiek);
        snprintf(arg_opiekun, sizeof(arg_opiekun), "%d", opiekun);
        
        execl("./bin/turysta", "turysta", arg_id, arg_wiek, arg_opiekun, NULL);
        perror("execl turysta");
        _exit(1);
    }
    
    if (liczba_turystow < 200) {
        pidy_turystow[liczba_turystow++] = pid;
    }
    LOG_D("MAIN: Uruchomiono turystę #%d (PID: %d, wiek: %d)", id, pid, wiek);
}

/* ========== GENEROWANIE GRUPY ========== */
void generuj_grupe(int *id) {
    int dorosly_id = (*id)++;
    int wiek_dorosly = 20 + (rand() % 50);
    
    int dzieci = 0;
    if (rand() % 100 < 25) {
        dzieci = 1 + (rand() % MAX_DZIECI_POD_OPIEKA);
    }
    
    LOG_I("MAIN: Generuję turystę #%d (wiek: %d) z %d dziećmi",
          dorosly_id, wiek_dorosly, dzieci);
    
    uruchom_turystę(dorosly_id, wiek_dorosly, -1);
    
    for (int i = 0; i < dzieci; i++) {
        int dziecko_id = (*id)++;
        int wiek_dziecka = WIEK_MIN_DZIECKO + 
                           (rand() % (WIEK_DZIECKO_OPIEKA - WIEK_MIN_DZIECKO));
        uruchom_turystę(dziecko_id, wiek_dziecka, dorosly_id);
    }
}

/* ========== ZATRZYMANIE I OCZEKIWANIE NA PROCESY ========== */
void zatrzymaj_i_czekaj_na_procesy(void) {
    LOG_I("MAIN: Zatrzymuję wszystkie procesy...");
    
    /* Zatrzymaj wątek monitora */
    monitor_aktywny = 0;
    
    printf("\n\nWysyłanie sygnałów zakończenia...\n");
    
    /* Wyślij SIGTERM do wszystkich turystów */
    for (int i = 0; i < liczba_turystow; i++) {
        if (pidy_turystow[i] > 0) {
            kill(pidy_turystow[i], SIGTERM);
        }
    }
    
    /* Wyślij SIGTERM do pracowników i kasjera */
    if (pid_pracownik1 > 0) kill(pid_pracownik1, SIGTERM);
    if (pid_pracownik2 > 0) kill(pid_pracownik2, SIGTERM);
    if (pid_kasjer > 0) kill(pid_kasjer, SIGTERM);
    
    printf("Oczekiwanie na zakończenie procesów potomnych...\n");

    /* Zbierz wszystkie procesy potomne z timeoutem */
    int zebrane = 0;
    int timeout = 30;  /* 30 sekund */

    while (timeout > 0) {
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {
            zebrane++;
            LOG_D("MAIN: Proces %d zakończony", pid);
        } else if (pid == -1) {
            if (errno == ECHILD) {
                /* Nie ma więcej procesów potomnych */
                break;
            }
        } else {
            /* pid == 0, brak zakończonych procesów - BLOKUJ zamiast busy waiting */
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            select(0, NULL, NULL, NULL, &tv);  /* Blokuje na 1 sekundę */
            timeout--;
        }
    }
    
    /* Jeśli timeout - wymuś zakończenie */
    if (timeout == 0) {
        printf("Timeout - wymuszam zakończenie (SIGKILL)...\n");
        
        for (int i = 0; i < liczba_turystow; i++) {
            if (pidy_turystow[i] > 0) {
                kill(pidy_turystow[i], SIGKILL);
            }
        }
        if (pid_pracownik1 > 0) kill(pid_pracownik1, SIGKILL);
        if (pid_pracownik2 > 0) kill(pid_pracownik2, SIGKILL);
        if (pid_kasjer > 0) kill(pid_kasjer, SIGKILL);
        
        /* Zbierz pozostałe */
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            zebrane++;
        }
    }
    
    /* Dołącz wątek monitora */
    pthread_join(watek_monitora, NULL);
    
    LOG_I("MAIN: Zebrano %d procesów potomnych", zebrane);
    printf("Zebrano %d procesów potomnych.\n", zebrane);
}

/* ========== WYŚWIETLANIE BANERA ========== */
void wyswietl_banner(void) {
    printf("\n");
    printf("---------------------------------------------------------------\n");
    printf("                 KOLEJ LINOWA KRZESEŁKOWA                      \n");
    printf("                    Symulacja Systemu                          \n");
    printf("---------------------------------------------------------------\n");
    printf("\n");
    printf("  Krzesełka: %2d aktywnych / %2d łącznie                        \n", 
           MAX_AKTYWNYCH_KRZESELEK, LICZBA_KRZESELEK);
    printf("  Bramki wejściowe: %d    Bramki peronowe: %d                   \n",
           LICZBA_BRAMEK_WEJSCIOWYCH, LICZBA_BRAMEK_PERONOWYCH);
    printf("  Max osób na stacji: %2d                                       \n",
           MAX_OSOB_NA_STACJI);
    printf("  Czas symulacji: %d sekund                                    \n",
           CZAS_ZAMKNIECIA);
    printf("\n");
}

/* ========== TWORZENIE KATALOGU LOGS ========== */
void utworz_katalog_logs(void) {
    struct stat st = {0};
    if (stat("logs", &st) == -1) {
        if (mkdir("logs", 0755) == -1) {
            perror("mkdir logs");
        }
    }
}

/* ========== UŻYCIE popen() - sprawdzenie zasobów IPC ========== */
void sprawdz_zasoby_ipc(void) {
    FILE *fp;

    printf("Sprawdzanie istniejących zasobów IPC...\n");
    
    /* Użycie popen() do wykonania komendy ipcs i odczytania wyniku */
    fp = popen("ipcs -a 2>/dev/null | head -20", "r");
    if (fp == NULL) {
        perror("popen ipcs");
        return;
    }
    
   
    /* Zamknij strumień popen - WAŻNE! */
    int status = pclose(fp);
    if (status == -1) {
        fprintf(stderr, "UWAGA: pclose - proces potomny już zakończony (to normalne)\n");
    } else {
        LOG_D("MAIN: ipcs zakończone ze statusem %d", WEXITSTATUS(status));
    }
}

/* ========== WALIDACJA PARAMETRÓW ========== */
int waliduj_parametry(int argc, char *argv[], int *czas_symulacji, int *max_turystow) {
    *czas_symulacji = CZAS_ZAMKNIECIA;
    *max_turystow = 100;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            int t = atoi(argv[i + 1]);
            if (t < 10 || t > 3600) {
                fprintf(stderr, "BŁĄD: Czas symulacji musi być między 10 a 3600 sekund\n");
                return -1;
            }
            *czas_symulacji = t;
            i++;
        } else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
            int n = atoi(argv[i + 1]);
            if (n < 1 || n > 500) {
                fprintf(stderr, "BŁĄD: Liczba turystów musi być między 1 a 500\n");
                return -1;
            }
            *max_turystow = n;
            i++;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Użycie: %s [-t czas] [-n liczba_turystow]\n", argv[0]);
            printf("  -t czas    Czas symulacji w sekundach (10-3600, domyślnie %d)\n", 
                   CZAS_ZAMKNIECIA);
            printf("  -n liczba  Max liczba turystów (1-500, domyślnie 100)\n");
            printf("  -h         Wyświetl tę pomoc\n");
            return 1;
        } else {
            fprintf(stderr, "BŁĄD: Nieznany parametr: %s\n", argv[i]);
            fprintf(stderr, "Użyj -h aby wyświetlić pomoc\n");
            return -1;
        }
    }
    
    return 0;
}

/* ========== GŁÓWNA FUNKCJA PROGRAMU ========== */
int main(int argc, char *argv[]) {
    int czas_symulacji, max_turystow;
    
    /* Walidacja parametrów */
    int wynik = waliduj_parametry(argc, argv, &czas_symulacji, &max_turystow);
    if (wynik != 0) {
        return (wynik > 0) ? 0 : 1;
    }
    
    /* Inicjalizacja */
    srand(time(NULL) ^ getpid());
    utworz_katalog_logs();
    wyswietl_banner();
    
    /* Ustawienie obsługi sygnałów */
    ustaw_obsluge_sygnalow();
    
    printf("Inicjalizacja zasobów IPC...\n");
    
    /* Sprawdź istniejące zasoby IPC używając popen() */
    sprawdz_zasoby_ipc();
    
    /* Tworzenie potoków (FIFO) */
    if (utworz_fifo_wszystkie() == -1) {
        fprintf(stderr, "BŁĄD: Nie można utworzyć potoków FIFO\n");
        return 1;
    }
    
    /* Tworzenie pipe dla wątku monitora */
    if (utworz_pipe(&pipe_monitor) == -1) {
        fprintf(stderr, "BŁĄD: Nie można utworzyć pipe\n");
        usun_fifo_wszystkie();
        return 1;
    }
    
    /* Inicjalizacja zasobów IPC */
    if (inicjalizuj_wszystkie_zasoby(&zasoby) == -1) {
        fprintf(stderr, "BŁĄD: Nie można zainicjalizować zasobów IPC\n");
        fprintf(stderr, "Spróbuj: make clean-ipc\n");
        usun_fifo_wszystkie();
        close(pipe_monitor.fd_read);
        close(pipe_monitor.fd_write);
        return 1;
    }
    
    printf("Zasoby IPC zainicjalizowane pomyślnie.\n");
    
    /* Inicjalizacja logowania */
    logger_init("logs/main.log");
    LOG_I("=== ROZPOCZĘCIE SYMULACJI KOLEI LINOWEJ ===");
    LOG_I("Parametry: %d sekund, max %d turystów", czas_symulacji, max_turystow);
    
    StanWspoldzielony *stan = zasoby.shm.stan;
    
    printf("Uruchamianie procesów obsługi...\n");

    /* Uruchomienie procesów */
    uruchom_kasjer();
    uruchom_pracownika(1);
    uruchom_pracownika(2);
    
    /* Uruchomienie wątku monitorowania */
    if (pthread_create(&watek_monitora, NULL, watek_monitor_funkcja, NULL) != 0) {
        perror("pthread_create monitor");
    }
    
    /* Uruchomienie wątku statystyk (używa pthread_detach wewnętrznie) */
    pthread_t watek_stat;
    if (pthread_create(&watek_stat, NULL, watek_statystyk_funkcja, NULL) != 0) {
        perror("pthread_create statystyki");
    }
    /* UWAGA: watek_stat nie wymaga join() bo używa pthread_detach() */
    
    printf("Symulacja rozpoczęta! Naciśnij Ctrl+C aby przerwać.\n\n");
    
    /* Główna pętla symulacji */
    time_t czas_start = time(NULL);
    int nastepny_id = 1;
    time_t ostatni_turysta = 0;
    
    while (!zakonczenie) {
        time_t teraz = time(NULL);
        time_t czas_dzialania = teraz - czas_start;
        
        /* Sprawdź koniec symulacji */
        if (czas_dzialania >= czas_symulacji) {
            LOG_I("MAIN: Koniec godzin pracy kolei");
            
            sem_czekaj_sysv(zasoby.sem.sem_id, SEM_IDX_STAN);
            stan->godziny_pracy = false;
            sem_sygnalizuj_sysv(zasoby.sem.sem_id, SEM_IDX_STAN);
            
            printf("\n\nKolej zamknięta! Oczekiwanie na opuszczenie stacji...\n");

            int timeout = CZAS_WYLACZENIA_PO_ZAMKNIECIU;
            while (timeout > 0 && (stan->liczba_osob_na_stacji > 0 ||
                                    stan->liczba_osob_na_peronie > 0)) {
                /* BLOKUJĄCE czekanie z timeoutem zamiast busy waiting */
                struct timeval tv;
                tv.tv_sec = 1;
                tv.tv_usec = 0;
                select(0, NULL, NULL, NULL, &tv);  /* Blokuje na 1 sekundę */
                timeout--;
            }
            
            sem_czekaj_sysv(zasoby.sem.sem_id, SEM_IDX_STAN);
            stan->kolej_aktywna = false;
            sem_sygnalizuj_sysv(zasoby.sem.sem_id, SEM_IDX_STAN);
            
            break;
        }
        
        /* Generuj nowych turystów */
        if (teraz - ostatni_turysta >= 1 && stan->godziny_pracy &&
            nastepny_id <= max_turystow) {
            if (rand() % 100 < 70) {
                generuj_grupe(&nastepny_id);
                ostatni_turysta = teraz;
            }
        }

        /* Zamiast busy waiting - użyj select() z timeoutem 100ms (BLOKUJĄCE) */
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  /* 100ms */
        select(0, NULL, NULL, NULL, &tv);  /* Blokuje proces na 100ms */
    }
    
    printf("\n\nZatrzymywanie symulacji...\n");
    LOG_I("MAIN: Zatrzymywanie symulacji");
    
    /* WAŻNE: Najpierw zatrzymaj i poczekaj na wszystkie procesy */
    zatrzymaj_i_czekaj_na_procesy();
    
    /* Generuj raport */
    printf("Generowanie raportu...\n");
    generuj_raport(stan, "logs/raport_dzienny.txt");
    
    /* Podsumowanie */
    printf("\n");
    printf("---------------------------------------------------------------\n");
    printf("                    PODSUMOWANIE DNIA                          \n");
    printf("  Łączna liczba zjazdów:     %-34d \n", stan->laczna_liczba_zjazdow);
    printf("  Sprzedanych biletów:       %-34d \n", stan->liczba_sprzedanych_biletow);
    printf("  Wpisów w rejestrze:        %-34d \n", stan->liczba_wpisow_rejestru);
    printf("---------------------------------------------------------------\n");
    printf("\n");
    
    LOG_I(" ZAKOŃCZENIE SYMULACJI ");
    logger_close();         
    
    /* DOPIERO TERAZ czyść zasoby IPC - po zakończeniu wszystkich procesów */
    printf("Czyszczenie zasobów IPC...\n");
    close(pipe_monitor.fd_read);
    close(pipe_monitor.fd_write);
    usun_fifo_wszystkie();
    usun_wszystkie_zasoby(&zasoby);
    
    printf("Symulacja zakończona pomyślnie.\n");
    
    return 0;
}