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