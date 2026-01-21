#ifndef IPC_UTILS_H
#define IPC_UTILS_H

#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include "types.h"

/* ========== NAZWY SEMAFORÓW POSIX ========== */
#define SEM_STACJA_DOLNA "/kolej_stacja_dolna"
#define SEM_PERON "/kolej_peron"
#define SEM_KRZESELKA "/kolej_krzeselka"
#define SEM_KASA "/kolej_kasa"
#define SEM_REJESTR "/kolej_rejestr"
#define SEM_STAN "/kolej_stan"
#define SEM_PRACOWNIK1 "/kolej_pracownik1"
#define SEM_PRACOWNIK2 "/kolej_pracownik2"
#define SEM_SYNC "/kolej_sync"
#define SEM_VIP "/kolej_vip"
#define SEM_BRAMKA_WEJ_PREFIX "/kolej_bramka_wej_"
#define SEM_BRAMKA_PER_PREFIX "/kolej_bramka_per_"

/* ========== STRUKTURA SEMAFORÓW ========== */
typedef struct {
    sem_t *stacja_dolna;        /* Limit osób na stacji dolnej */
    sem_t *peron;               /* Sygnalizacja wejścia na peron */
    sem_t *krzeselka;           /* Dostępne krzesełka */
    sem_t *kasa;                /* Mutex kasy */
    sem_t *rejestr;             /* Mutex rejestru */
    sem_t *stan;                /* Mutex stanu współdzielonego */
    sem_t *pracownik1;          /* Sygnalizacja dla pracownika1 */
    sem_t *pracownik2;          /* Sygnalizacja dla pracownika2 */
    sem_t *sync;                /* Synchronizacja zatrzymania */
    sem_t *vip;                 /* Priorytet VIP */
    sem_t *bramki_wejsciowe[LICZBA_BRAMEK_WEJSCIOWYCH];
    sem_t *bramki_peronowe[LICZBA_BRAMEK_PERONOWYCH];
} Semafory;

/* ========== STRUKTURA PAMIĘCI WSPÓŁDZIELONEJ ========== */
typedef struct {
    int shm_id;
    StanWspoldzielony *stan;
} PamiecWspoldzielona;

/* ========== STRUKTURA KOLEJEK KOMUNIKATÓW ========== */
typedef struct {
    int mq_kasa;
    int mq_bramki;
    int mq_pracownicy;
    int mq_krzesla;
} KolejkiKomunikatow;

/* ========== STRUKTURA ZBIORCZA ZASOBÓW ========== */
typedef struct {
    Semafory sem;
    PamiecWspoldzielona shm;
    KolejkiKomunikatow mq;
} ZasobyIPC;

/* ========== FUNKCJE INICJALIZACJI (tworzenie nowych) ========== */
int inicjalizuj_semafory(Semafory *sem);
int inicjalizuj_pamiec_wspoldzielona(PamiecWspoldzielona *shm);
int inicjalizuj_kolejki(KolejkiKomunikatow *mq);
int inicjalizuj_wszystkie_zasoby(ZasobyIPC *zasoby);

/* ========== FUNKCJE ŁĄCZENIA (do istniejących) ========== */
int polacz_semafory(Semafory *sem);
int polacz_pamiec_wspoldzielona(PamiecWspoldzielona *shm);
int polacz_kolejki(KolejkiKomunikatow *mq);
int polacz_z_zasobami(ZasobyIPC *zasoby);

/* ========== FUNKCJE USUWANIA ========== */
void usun_semafory(Semafory *sem);
void usun_pamiec_wspoldzielona(PamiecWspoldzielona *shm);
void usun_kolejki(KolejkiKomunikatow *mq);
void usun_wszystkie_zasoby(ZasobyIPC *zasoby);

/* ========== OPERACJE NA SEMAFORACH ========== */
void sem_czekaj(sem_t *sem);
void sem_sygnalizuj(sem_t *sem);
int sem_probuj(sem_t *sem);

/* ========== OPERACJE NA KOLEJKACH ========== */
int wyslij_komunikat(int mq_id, Komunikat *msg);
int odbierz_komunikat(int mq_id, Komunikat *msg, long mtype);
int odbierz_komunikat_nieblokujaco(int mq_id, Komunikat *msg, long mtype);

#endif