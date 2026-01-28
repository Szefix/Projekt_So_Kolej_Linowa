#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include "config.h"
#include "types.h"
#include "ipc_utils.h"
#include "logger.h"

/* Deklaracje zewnętrznych funkcji main */
extern int kasjer_main(void);
extern int pracownik1_main(void);
extern int pracownik2_main(void);
extern int turysta_main(int id, int wiek, int opiekun);

/* Zmienne globalne */
static ZasobyIPC zasoby;
static pid_t pid_kasjer = 0;
static pid_t pid_pracownik1 = 0;
static pid_t pid_pracownik2 = 0;
static pid_t pidy_turystow[200];
static int liczba_turystow = 0;
static volatile sig_atomic_t zakonczenie = 0;

/* Obsługa sygnałów */
void obsluz_sygnal_glowny(int sig) {
    (void)sig;
    zakonczenie = 1;
}

/* Wyświetlanie banera */
void wyswietl_banner(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                 KOLEJ LINOWA KRZESEŁKOWA                      ║\n");
    printf("║                    Symulacja Systemu                          ║\n");
    printf("╠═══════════════════════════════════════════════════════════════╣\n");
    printf("║  Krzesełka: %2d aktywnych / %2d łącznie                        ║\n", 
           MAX_AKTYWNYCH_KRZESELEK, LICZBA_KRZESELEK);
    printf("║  Bramki wejściowe: %d    Bramki peronowe: %d                   ║\n",
           LICZBA_BRAMEK_WEJSCIOWYCH, LICZBA_BRAMEK_PERONOWYCH);
    printf("║  Max osób na stacji: %2d                                       ║\n",
           MAX_OSOB_NA_STACJI);
    printf("║  Czas symulacji: %d sekund                                    ║\n",
           CZAS_ZAMKNIECIA);
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/* Uruchomienie procesów pomocniczych */
void uruchom_procesy(void) {
    /* Kasjer */
    pid_kasjer = fork();
    if (pid_kasjer == 0) {
        exit(kasjer_main());
    } else if (pid_kasjer > 0) {
        LOG_I("MAIN: Uruchomiono kasjera (PID: %d)", pid_kasjer);
    } else {
        perror("fork kasjer");
    }
    
    usleep(50000);
    
    /* Pracownik 1 */
    pid_pracownik1 = fork();
    if (pid_pracownik1 == 0) {
        exit(pracownik1_main());
    } else if (pid_pracownik1 > 0) {
        LOG_I("MAIN: Uruchomiono pracownika1 (PID: %d)", pid_pracownik1);
    } else {
        perror("fork pracownik1");
    }
    
    usleep(50000);
    
    /* Pracownik 2 */
    pid_pracownik2 = fork();
    if (pid_pracownik2 == 0) {
        exit(pracownik2_main());
    } else if (pid_pracownik2 > 0) {
        LOG_I("MAIN: Uruchomiono pracownika2 (PID: %d)", pid_pracownik2);
    } else {
        perror("fork pracownik2");
    }
    
    usleep(100000);
}

/* Generowanie turysty */
void generuj_turystę(int id, int wiek, int opiekun) {
    pid_t pid = fork();
    if (pid == 0) {
        exit(turysta_main(id, wiek, opiekun));
    } else if (pid > 0) {
        if (liczba_turystow < 200) {
            pidy_turystow[liczba_turystow++] = pid;
        }
        LOG_D("MAIN: Uruchomiono turystę #%d (PID: %d, wiek: %d)", id, pid, wiek);
    }
}

/* Generowanie grupy (dorosły + dzieci) */
void generuj_grupe(int *id) {
    int dorosly_id = (*id)++;
    int wiek_dorosly = 20 + (rand() % 50);
    
    /* Liczba dzieci pod opieką (0-2) */
    int dzieci = 0;
    if (rand() % 100 < 25) {
        dzieci = 1 + (rand() % MAX_DZIECI_POD_OPIEKA);
    }
    
    LOG_I("MAIN: Generuję turystę #%d (wiek: %d) z %d dziećmi",
          dorosly_id, wiek_dorosly, dzieci);
    
    generuj_turystę(dorosly_id, wiek_dorosly, -1);
    
    for (int i = 0; i < dzieci; i++) {
        int dziecko_id = (*id)++;
        int wiek_dziecka = WIEK_MIN_DZIECKO + (rand() % (WIEK_DZIECKO_OPIEKA - WIEK_MIN_DZIECKO));
        generuj_turystę(dziecko_id, wiek_dziecka, dorosly_id);
    }
}

/* Zatrzymanie wszystkich procesów */
void zatrzymaj_wszystko(void) {
    LOG_I("MAIN: Zatrzymuję wszystkie procesy...");
    
    /* Wyślij SIGTERM do wszystkich turystów */
    for (int i = 0; i < liczba_turystow; i++) {
        if (pidy_turystow[i] > 0) {
            kill(pidy_turystow[i], SIGTERM);
        }
    }
    
    /* Poczekaj chwilę */
    usleep(500000);
    
    /* Zatrzymaj pracowników i kasjera */
    if (pid_pracownik1 > 0) kill(pid_pracownik1, SIGTERM);
    if (pid_pracownik2 > 0) kill(pid_pracownik2, SIGTERM);
    if (pid_kasjer > 0) kill(pid_kasjer, SIGTERM);
}

/* Zbieranie procesów potomnych */
void zbierz_procesy(void) {
    LOG_I("MAIN: Czekam na zakończenie procesów potomnych...");
    
    int status;
    pid_t pid;
    int zebrane = 0;
    
    /* Zbierz wszystkie procesy potomne */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        zebrane++;
        if (WIFEXITED(status)) {
            LOG_D("MAIN: Proces %d zakończył się z kodem %d", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            LOG_D("MAIN: Proces %d zabity sygnałem %d", pid, WTERMSIG(status));
        }
    }
    
    /* Czekaj na pozostałe z timeoutem */
    int timeout = 50;  /* 5 sekund */
    while (timeout > 0) {
        pid = waitpid(-1, &status, WNOHANG);
        if (pid <= 0) {
            if (pid == -1 && errno == ECHILD) {
                break;  /* Nie ma więcej dzieci */
            }
            usleep(100000);
            timeout--;
        } else {
            zebrane++;
        }
    }
    
    LOG_I("MAIN: Zebrano %d procesów potomnych", zebrane);
}

/* Wyświetlanie stanu w czasie rzeczywistym */
void wyswietl_stan(StanWspoldzielony *stan) {
    printf("\r[Czas: %3ld s] Stacja: %2d osób | Peron: %2d | Krzesełka: %2d | Zjazdy: %3d | Bilety: %3d",
           time(NULL) - stan->czas_startu,
           stan->liczba_osob_na_stacji,
           stan->liczba_osob_na_peronie,
           stan->liczba_aktywnych_krzeselek,
           stan->laczna_liczba_zjazdow,
           stan->liczba_sprzedanych_biletow);
    fflush(stdout);
}

/* Tworzenie katalogu logs */
void utworz_katalog_logs(void) {
    struct stat st = {0};
    if (stat("logs", &st) == -1) {
        mkdir("logs", 0755);
    }
}

/* ========== GŁÓWNA FUNKCJA PROGRAMU ========== */
int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;
    
    /* Inicjalizacja generatora losowego */
    srand(time(NULL) ^ getpid());
    
    /* Tworzenie katalogu logs */
    utworz_katalog_logs();
    
    /* Wyświetl banner */
    wyswietl_banner();
    
    /* Rejestracja obsługi sygnałów */
    signal(SIGINT, obsluz_sygnal_glowny);
    signal(SIGTERM, obsluz_sygnal_glowny);
    signal(SIGCHLD, SIG_IGN);  /* Ignoruj zakończenie dzieci na bieżąco */
    
    printf("Inicjalizacja zasobów IPC...\n");
    
    /* Inicjalizacja zasobów IPC */
    if (inicjalizuj_wszystkie_zasoby(&zasoby) == -1) {
        fprintf(stderr, "BŁĄD: Nie można zainicjalizować zasobów IPC\n");
        fprintf(stderr, "Spróbuj: make clean-ipc\n");
        return 1;
    }
    
    printf("Zasoby IPC zainicjalizowane pomyślnie.\n");
    
    /* Inicjalizacja logowania głównego procesu */
    logger_init("logs/main.log");
    LOG_I("=== ROZPOCZĘCIE SYMULACJI KOLEI LINOWEJ ===");
    LOG_I("Parametry: %d sekund, max %d osób na stacji", 
          CZAS_ZAMKNIECIA, MAX_OSOB_NA_STACJI);
    
    StanWspoldzielony *stan = zasoby.shm.stan;
    
    printf("Uruchamianie procesów obsługi...\n");
    
    /* Uruchomienie procesów pomocniczych */
    uruchom_procesy();
    
    /* Poczekaj na gotowość pracowników */
    usleep(500000);
    
    printf("Symulacja rozpoczęta! Naciśnij Ctrl+C aby przerwać.\n\n");
    
    /* Główna pętla symulacji */
    time_t czas_start = time(NULL);
    int nastepny_id = 1;
    time_t ostatni_turysta = 0;
    
    while (!zakonczenie) {
        time_t teraz = time(NULL);
        time_t czas_dzialania = teraz - czas_start;
        
        /* Sprawdź czy minął czas symulacji */
        if (czas_dzialania >= CZAS_ZAMKNIECIA) {
            LOG_I("MAIN: Koniec godzin pracy kolei");
            
            /* Ustaw flagę zamknięcia */
            sem_czekaj(zasoby.sem.stan);
            stan->godziny_pracy = false;
            sem_sygnalizuj(zasoby.sem.stan);
            
            printf("\n\nKolej zamknięta! Oczekiwanie na opuszczenie stacji...\n");
            
            /* Czekaj na opróżnienie stacji */
            int timeout = CZAS_WYLACZENIA_PO_ZAMKNIECIU;
            while (timeout > 0 && (stan->liczba_osob_na_stacji > 0 || 
                                    stan->liczba_osob_na_peronie > 0 ||
                                    stan->liczba_aktywnych_krzeselek > 0)) {
                sleep(1);
                timeout--;
                printf("\rOczekiwanie... Stacja: %d, Peron: %d, Krzesełka: %d  ",
                       stan->liczba_osob_na_stacji,
                       stan->liczba_osob_na_peronie,
                       stan->liczba_aktywnych_krzeselek);
                fflush(stdout);
            }
            
            /* Wyłącz kolej */
            sem_czekaj(zasoby.sem.stan);
            stan->kolej_aktywna = false;
            sem_sygnalizuj(zasoby.sem.stan);
            
            break;
        }
        
        /* Generuj nowych turystów (średnio co 0.5-2 sekundy) */
        if (teraz - ostatni_turysta >= 1 && stan->godziny_pracy) {
            if (rand() % 100 < 70) {  /* 70% szans na nowego turystę */
                generuj_grupe(&nastepny_id);
                ostatni_turysta = teraz;
            }
        }
        
        /* Wyświetl stan */
        wyswietl_stan(stan);
        
        /* Krótka pauza */
        usleep(200000);  /* 200ms */
    }
    
    printf("\n\nZatrzymywanie symulacji...\n");
    LOG_I("MAIN: Zatrzymywanie symulacji");
    
    /* Zatrzymaj wszystkie procesy */
    zatrzymaj_wszystko();
    
    /* Zbierz procesy potomne */
    signal(SIGCHLD, SIG_DFL);
    zbierz_procesy();
    
    /* Generuj raport */
    printf("Generowanie raportu...\n");
    generuj_raport(stan, "logs/raport_dzienny.txt");
    
    /* Wyświetl podsumowanie */
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════╗\n");
    printf("║                    PODSUMOWANIE DNIA                          ║\n");
    printf("╠═══════════════════════════════════════════════════════════════╣\n");
    printf("║  Łączna liczba zjazdów:     %-34d ║\n", stan->laczna_liczba_zjazdow);
    printf("║  Sprzedanych biletów:       %-34d ║\n", stan->liczba_sprzedanych_biletow);
    printf("║  Wpisów w rejestrze:        %-34d ║\n", stan->liczba_wpisow_rejestru);
    printf("╚═══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("Raport zapisany do: logs/raport_dzienny.txt\n");
    printf("Logi procesów dostępne w katalogu: logs/\n");
    
    LOG_I("=== ZAKOŃCZENIE SYMULACJI ===");
    LOG_I("Zjazdy: %d, Bilety: %d", stan->laczna_liczba_zjazdow, stan->liczba_sprzedanych_biletow);
    
    /* Zamknij logger */
    logger_close();
    
    /* Usuń zasoby IPC */
    printf("Czyszczenie zasobów IPC...\n");
    usun_wszystkie_zasoby(&zasoby);
    
    printf("Symulacja zakończona pomyślnie.\n");
    
    return 0;
}
