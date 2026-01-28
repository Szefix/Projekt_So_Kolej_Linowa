#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>
#include "logger.h"
#include "types.h"

/* Deskryptor pliku logu (systemowy, nie FILE*) */
static int fd_logu = -1;
static pthread_mutex_t mutex_logu = PTHREAD_MUTEX_INITIALIZER;
static char nazwa_pliku_logu[256] = {0};

/* Konwersja poziomu na tekst */
static const char *poziom_do_tekstu(PoziomLogu poziom) {
    switch (poziom) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        default:        return "?????";
    }
}

/* ========== INICJALIZACJA LOGGERA - SYSTEMOWE open() ========== */
void logger_init(const char *nazwa_pliku) {
    pthread_mutex_lock(&mutex_logu);
    
    /* Zamknij poprzedni plik */
    if (fd_logu != -1 && fd_logu != STDERR_FILENO) {
        close(fd_logu);
        fd_logu = -1;
    }
    
    if (nazwa_pliku != NULL) {
        /* Otwórz plik używając systemowego open() */
        /* O_CREAT - utwórz jeśli nie istnieje */
        /* O_WRONLY - tylko do zapisu */
        /* O_APPEND - dopisuj na końcu */
        /* Uprawnienia 0640 - właściciel: rw, grupa: r */
        fd_logu = open(nazwa_pliku, O_CREAT | O_WRONLY | O_APPEND, 0640);
        
        if (fd_logu == -1) {
            perror("open log file");
            fd_logu = STDERR_FILENO;
        } else {
            strncpy(nazwa_pliku_logu, nazwa_pliku, sizeof(nazwa_pliku_logu) - 1);
        }
    } else {
        fd_logu = STDERR_FILENO;
    }
    
    pthread_mutex_unlock(&mutex_logu);
}

/* ========== ZAMKNIĘCIE LOGGERA - SYSTEMOWE close() ========== */
void logger_close(void) {
    pthread_mutex_lock(&mutex_logu);
    
    if (fd_logu != -1 && fd_logu != STDERR_FILENO) {
        /* Synchronizuj dane z dyskiem przed zamknięciem */
        fsync(fd_logu);
        close(fd_logu);
        fd_logu = -1;
    }
    
    pthread_mutex_unlock(&mutex_logu);
}

/* ========== GŁÓWNA FUNKCJA LOGOWANIA - SYSTEMOWE write() ========== */
void logger_log(PoziomLogu poziom, const char *format, ...) {
    pthread_mutex_lock(&mutex_logu);
    
    int fd = (fd_logu != -1) ? fd_logu : STDERR_FILENO;
    
    /* Blokada pliku dla wielu procesów */
    flock(fd, LOCK_EX);
    
    /* Przygotuj bufor */
    char bufor[1024];
    int offset = 0;
    
    /* Timestamp */
    time_t teraz = time(NULL);
    struct tm *tm_info = localtime(&teraz);
    char bufor_czasu[32];
    strftime(bufor_czasu, sizeof(bufor_czasu), "%H:%M:%S", tm_info);
    
    /* Nagłówek */
    offset = snprintf(bufor, sizeof(bufor), "[%s][%s][PID:%5d] ", 
                      bufor_czasu, 
                      poziom_do_tekstu(poziom), 
                      getpid());
    
    /* Treść - formatowanie zmiennej liczby argumentów */
    va_list args;
    va_start(args, format);
    offset += vsnprintf(bufor + offset, sizeof(bufor) - offset - 2, format, args);
    va_end(args);
    
    /* Dodaj newline */
    if (offset < (int)sizeof(bufor) - 1) {
        bufor[offset++] = '\n';
        bufor[offset] = '\0';
    }
    
    /* Zapisz używając systemowego write() */
    ssize_t napisano = write(fd, bufor, offset);
    if (napisano == -1) {
        /* Nie możemy użyć loggera do zalogowania błędu loggera */
        perror("write log");
    }
    
    flock(fd, LOCK_UN);
    pthread_mutex_unlock(&mutex_logu);
}

/* ========== REJESTROWANIE PRZEJŚCIA ========== */
void logger_rejestruj_przejscie(int bilet_id, int turysta_id, int bramka, int zjazd) {
    LOG_I("REJESTR: Bilet #%d, Turysta #%d, Bramka %d, Zjazd #%d",
          bilet_id, turysta_id, bramka, zjazd);
}

/* ========== GENEROWANIE RAPORTU - SYSTEMOWE creat(), write() ========== */
void generuj_raport(StanWspoldzielony *stan, const char *plik_wyjsciowy) {
    /* Utwórz plik używając creat() - równoważne open() z O_CREAT|O_WRONLY|O_TRUNC */
    int fd = creat(plik_wyjsciowy, 0644);
    if (fd == -1) {
        perror("creat raport");
        return;
    }
    
    char bufor[4096];
    int len;
    
    /* Nagłówek raportu */
    time_t teraz = time(NULL);
    struct tm *tm_info = localtime(&teraz);
    char bufor_daty[64];
    strftime(bufor_daty, sizeof(bufor_daty), "%Y-%m-%d %H:%M:%S", tm_info);
    
    len = snprintf(bufor, sizeof(bufor),
        "╔══════════════════════════════════════════════════════════════╗\n"
        "║              RAPORT DZIENNY - KOLEJ LINOWA                   ║\n"
        "╠══════════════════════════════════════════════════════════════╣\n"
        "║ Data wygenerowania: %-40s ║\n"
        "╠══════════════════════════════════════════════════════════════╣\n"
        "║                     STATYSTYKI OGÓLNE                        ║\n"
        "╠══════════════════════════════════════════════════════════════╣\n"
        "║ Łączna liczba zjazdów:          %-28d ║\n"
        "║ Sprzedanych biletów:            %-28d ║\n"
        "║ Wpisów w rejestrze:             %-28d ║\n"
        "╠══════════════════════════════════════════════════════════════╣\n",
        bufor_daty,
        stan->laczna_liczba_zjazdow,
        stan->liczba_sprzedanych_biletow,
        stan->liczba_wpisow_rejestru);
    
    write(fd, bufor, len);
    
    /* Rejestr przejść */
    len = snprintf(bufor, sizeof(bufor),
        "║                    REJESTR PRZEJŚĆ                           ║\n"
        "╠══════════════════════════════════════════════════════════════╣\n"
        "║ ID Biletu │ ID Turysty │ Bramka │ Zjazd │      Czas          ║\n"
        "╠───────────┼────────────┼────────┼───────┼────────────────────╣\n");
    write(fd, bufor, len);
    
    int max_wpisow = (stan->liczba_wpisow_rejestru < 100) ? 
                      stan->liczba_wpisow_rejestru : 100;
    
    for (int i = 0; i < max_wpisow; i++) {
        WpisRejestru *wpis = &stan->rejestr[i];
        struct tm *tm_wpis = localtime(&wpis->czas);
        char czas_wpis[32];
        strftime(czas_wpis, sizeof(czas_wpis), "%H:%M:%S", tm_wpis);
        
        len = snprintf(bufor, sizeof(bufor),
            "║ %9d │ %10d │ %6d │ %5d │ %18s ║\n",
            wpis->bilet_id, wpis->turysta_id, wpis->numer_bramki, 
            wpis->numer_zjazdu, czas_wpis);
        write(fd, bufor, len);
    }
    
    if (stan->liczba_wpisow_rejestru > 100) {
        len = snprintf(bufor, sizeof(bufor),
            "║ ... i %d więcej wpisów                                        ║\n",
            stan->liczba_wpisow_rejestru - 100);
        write(fd, bufor, len);
    }
    
    len = snprintf(bufor, sizeof(bufor),
        "╚══════════════════════════════════════════════════════════════╝\n");
    write(fd, bufor, len);
    
    /* Podsumowanie per bilet */
    len = snprintf(bufor, sizeof(bufor),
        "\n╔══════════════════════════════════════════════════════════════╗\n"
        "║              PODSUMOWANIE ZJAZDÓW PER BILET                  ║\n"
        "╠══════════════════════════════════════════════════════════════╣\n");
    write(fd, bufor, len);
    
    /* Zlicz zjazdy per bilet */
    int zjazdy_per_bilet[1000] = {0};
    for (int i = 0; i < stan->liczba_wpisow_rejestru && i < MAX_WPISOW_REJESTRU; i++) {
        int bid = stan->rejestr[i].bilet_id;
        if (bid > 0 && bid < 1000) {
            zjazdy_per_bilet[bid]++;
        }
    }
    
    for (int i = 1; i < 1000; i++) {
        if (zjazdy_per_bilet[i] > 0) {
            len = snprintf(bufor, sizeof(bufor),
                "║ Bilet #%-4d: %-3d zjazdów                                      ║\n",
                i, zjazdy_per_bilet[i]);
            write(fd, bufor, len);
        }
    }
    
    len = snprintf(bufor, sizeof(bufor),
        "╚══════════════════════════════════════════════════════════════╝\n");
    write(fd, bufor, len);
    
    /* Synchronizuj i zamknij */
    fsync(fd);
    close(fd);
    
    printf("Raport zapisany do: %s\n", plik_wyjsciowy);
}