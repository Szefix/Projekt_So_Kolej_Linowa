#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>
#include <pthread.h>
#include "logger.h"

static FILE *plik_logu = NULL;
static pthread_mutex_t mutex_logu = PTHREAD_MUTEX_INITIALIZER;

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

/* Inicjalizacja loggera */
void logger_init(const char *nazwa_pliku) {
    pthread_mutex_lock(&mutex_logu);
    
    if (plik_logu != NULL && plik_logu != stderr) {
        fclose(plik_logu);
    }
    
    if (nazwa_pliku != NULL) {
        plik_logu = fopen(nazwa_pliku, "a");
        if (plik_logu == NULL) {
            perror("Nie można otworzyć pliku logu");
            plik_logu = stderr;
        }
    } else {
        plik_logu = stderr;
    }
    
    pthread_mutex_unlock(&mutex_logu);
}

/* Zamknięcie loggera */
void logger_close(void) {
    pthread_mutex_lock(&mutex_logu);
    
    if (plik_logu != NULL && plik_logu != stderr) {
        fclose(plik_logu);
        plik_logu = NULL;
    }
    
    pthread_mutex_unlock(&mutex_logu);
}

/* Główna funkcja logowania */
void logger_log(PoziomLogu poziom, const char *format, ...) {
    pthread_mutex_lock(&mutex_logu);
    
    FILE *out = (plik_logu != NULL) ? plik_logu : stderr;
    
    /* Blokada pliku dla wielu procesów */
    int fd = fileno(out);
    flock(fd, LOCK_EX);
    
    /* Timestamp */
    time_t teraz = time(NULL);
    struct tm *tm_info = localtime(&teraz);
    char bufor_czasu[32];
    strftime(bufor_czasu, sizeof(bufor_czasu), "%H:%M:%S", tm_info);
    
    /* Nagłówek */
    fprintf(out, "[%s][%s][PID:%5d] ", 
            bufor_czasu, 
            poziom_do_tekstu(poziom), 
            getpid());
    
    /* Treść */
    va_list args;
    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);
    
    fprintf(out, "\n");
    fflush(out);
    
    flock(fd, LOCK_UN);
    pthread_mutex_unlock(&mutex_logu);
}

/* Rejestrowanie przejścia przez bramkę */
void logger_rejestruj_przejscie(int bilet_id, int turysta_id, int bramka, int zjazd) {
    LOG_I("REJESTR: Bilet #%d, Turysta #%d, Bramka %d, Zjazd #%d",
          bilet_id, turysta_id, bramka, zjazd);
}

/* Generowanie raportu końcowego */
void generuj_raport(StanWspoldzielony *stan, const char *plik_wyjsciowy) {
    FILE *raport = fopen(plik_wyjsciowy, "w");
    if (!raport) {
        perror("Nie można utworzyć raportu");
        return;
    }
    
    time_t teraz = time(NULL);
    struct tm *tm_info = localtime(&teraz);
    char bufor_daty[64];
    strftime(bufor_daty, sizeof(bufor_daty), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(raport, "╔══════════════════════════════════════════════════════════════╗\n");
    fprintf(raport, "║              RAPORT DZIENNY - KOLEJ LINOWA                   ║\n");
    fprintf(raport, "╠══════════════════════════════════════════════════════════════╣\n");
    fprintf(raport, "║ Data wygenerowania: %-40s ║\n", bufor_daty);
    fprintf(raport, "╠══════════════════════════════════════════════════════════════╣\n");
    fprintf(raport, "║                     STATYSTYKI OGÓLNE                        ║\n");
    fprintf(raport, "╠══════════════════════════════════════════════════════════════╣\n");
    fprintf(raport, "║ Łączna liczba zjazdów:          %-28d ║\n", stan->laczna_liczba_zjazdow);
    fprintf(raport, "║ Sprzedanych biletów:            %-28d ║\n", stan->liczba_sprzedanych_biletow);
    fprintf(raport, "║ Wpisów w rejestrze:             %-28d ║\n", stan->liczba_wpisow_rejestru);
    fprintf(raport, "╠══════════════════════════════════════════════════════════════╣\n");
    fprintf(raport, "║                    REJESTR PRZEJŚĆ                           ║\n");
    fprintf(raport, "╠══════════════════════════════════════════════════════════════╣\n");
    fprintf(raport, "║ ID Biletu │ ID Turysty │ Bramka │ Zjazd │      Czas          ║\n");
    fprintf(raport, "╠───────────┼────────────┼────────┼───────┼────────────────────╣\n");
    
    int max_wpisow = (stan->liczba_wpisow_rejestru < 100) ? stan->liczba_wpisow_rejestru : 100;
    
    for (int i = 0; i < max_wpisow; i++) {
        WpisRejestru *wpis = &stan->rejestr[i];
        struct tm *tm_wpis = localtime(&wpis->czas);
        char czas_wpis[32];
        strftime(czas_wpis, sizeof(czas_wpis), "%H:%M:%S", tm_wpis);
        
        fprintf(raport, "║ %9d │ %10d │ %6d │ %5d │ %18s ║\n",
                wpis->bilet_id, wpis->turysta_id, wpis->numer_bramki, 
                wpis->numer_zjazdu, czas_wpis);
    }
    
    if (stan->liczba_wpisow_rejestru > 100) {
        fprintf(raport, "║ ... i %d więcej wpisów                                        ║\n",
                stan->liczba_wpisow_rejestru - 100);
    }
    
    fprintf(raport, "╚══════════════════════════════════════════════════════════════╝\n");
    
    /* Podsumowanie per bilet */
    fprintf(raport, "\n");
    fprintf(raport, "╔══════════════════════════════════════════════════════════════╗\n");
    fprintf(raport, "║              PODSUMOWANIE ZJAZDÓW PER BILET                  ║\n");
    fprintf(raport, "╠══════════════════════════════════════════════════════════════╣\n");
    
    /* Zlicz zjazdy per bilet */
    int zjazdy_per_bilet[1000] = {0};
    for (int i = 0; i < stan->liczba_wpisow_rejestru; i++) {
        int bid = stan->rejestr[i].bilet_id;
        if (bid > 0 && bid < 1000) {
            zjazdy_per_bilet[bid]++;
        }
    }
    
    for (int i = 1; i < 1000; i++) {
        if (zjazdy_per_bilet[i] > 0) {
            fprintf(raport, "║ Bilet #%-4d: %-3d zjazdów                                      ║\n",
                    i, zjazdy_per_bilet[i]);
        }
    }
    
    fprintf(raport, "╚══════════════════════════════════════════════════════════════╝\n");
    
    fclose(raport);
    printf("Raport zapisany do: %s\n", plik_wyjsciowy);
}