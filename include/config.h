#ifndef CONFIG_H
#define CONFIG_H

/* ========== PARAMETRY KOLEI ========== */
#define LICZBA_KRZESELEK 72
#define MAX_AKTYWNYCH_KRZESELEK 36
#define POJEMNOSC_KRZESELKA 4
#define MAX_ROWERZYSTOW_NA_KRZESELKU 2

/* ========== BRAMKI ========== */
#define LICZBA_BRAMEK_WEJSCIOWYCH 4
#define LICZBA_BRAMEK_PERONOWYCH 3

/* ========== STACJA ========== */
#define MAX_OSOB_NA_STACJI 50  /* N osób między bramkami */

/* ========== WYJŚCIA STACJA GÓRNA ========== */
#define LICZBA_WYJSC 2

/* ========== TRASY ROWEROWE (sekundy symulacji) ========== */
#define CZAS_TRASY_T1 3
#define CZAS_TRASY_T2 5
#define CZAS_TRASY_T3 8

/* ========== GODZINY PRACY (sekundy symulacji) ========== */
#define CZAS_OTWARCIA 0
#define CZAS_ZAMKNIECIA 60
#define CZAS_WYLACZENIA_PO_ZAMKNIECIU 3

/* ========== TYPY BILETÓW ========== */
#define BILET_JEDNORAZOWY 1
#define BILET_CZASOWY_TK1 2
#define BILET_CZASOWY_TK2 3
#define BILET_CZASOWY_TK3 4
#define BILET_DZIENNY 5

/* ========== CZASY KARNETÓW CZASOWYCH (sekundy) ========== */
#define CZAS_TK1 15
#define CZAS_TK2 30
#define CZAS_TK3 45

/* ========== ZNIŻKI I WIEK ========== */
#define PROCENT_ZNIZKI 25
#define WIEK_DZIECKO_ZNIZKA 10
#define WIEK_SENIOR_ZNIZKA 65
#define WIEK_DZIECKO_OPIEKA 8
#define WIEK_MIN_DZIECKO 4
#define MAX_DZIECI_POD_OPIEKA 2

/* ========== VIP ========== */
#define PROCENT_VIP 1  /* 1% */

/* ========== CENY BILETÓW ========== */
#define CENA_JEDNORAZOWY 15
#define CENA_TK1 30
#define CENA_TK2 50
#define CENA_TK3 70
#define CENA_DZIENNY 100

/* ========== IPC KLUCZE ========== */
#define SHM_KEY 0x1234
#define MQ_KEY_KASA 0x5001
#define MQ_KEY_BRAMKI 0x5002
#define MQ_KEY_PRACOWNICY 0x5003
#define MQ_KEY_KRZESLA 0x5004

#endif