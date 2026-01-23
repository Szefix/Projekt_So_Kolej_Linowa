#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <time.h>
#include "types.h"

/* ========== POZIOMY LOGOWANIA ========== */
typedef enum {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3
} PoziomLogu;

/* ========== FUNKCJE LOGOWANIA ========== */
void logger_init(const char *nazwa_pliku);
void logger_close(void);
void logger_log(PoziomLogu poziom, const char *format, ...);

/* ========== MAKRA DLA WYGODY ========== */
#define LOG_D(fmt, ...) logger_log(LOG_DEBUG, fmt, ##__VA_ARGS__)
#define LOG_I(fmt, ...) logger_log(LOG_INFO, fmt, ##__VA_ARGS__)
#define LOG_W(fmt, ...) logger_log(LOG_WARN, fmt, ##__VA_ARGS__)
#define LOG_E(fmt, ...) logger_log(LOG_ERROR, fmt, ##__VA_ARGS__)

/* ========== REJESTROWANIE I RAPORT ========== */
void logger_rejestruj_przejscie(int bilet_id, int turysta_id, int bramka, int zjazd);
void generuj_raport(StanWspoldzielony *stan, const char *plik_wyjsciowy);

#endif