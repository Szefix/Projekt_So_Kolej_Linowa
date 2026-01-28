#ifndef IPC_UTILS_H
#define IPC_UTILS_H

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include "types.h"

/* ========== KLUCZE IPC ========== */
#define SHM_KEY     0x1234
#define SEM_KEY     0x2345
#define MQ_KEY_KASA      0x5001
#define MQ_KEY_BRAMKI    0x5002
#define MQ_KEY_PRACOWNICY 0x5003
#define MQ_KEY_KRZESLA   0x5004

/* ========== INDEKSY SEMAFORÓW W ZESTAWIE ========== */
#define SEM_IDX_STACJA_DOLNA    0   /* Limit osób na stacji */
#define SEM_IDX_PERON           1   /* Sygnalizacja wejścia na peron */
#define SEM_IDX_KRZESELKA       2   /* Dostępne krzesełka */
#define SEM_IDX_KASA            3   /* Mutex kasy */
#define SEM_IDX_REJESTR         4   /* Mutex rejestru */
#define SEM_IDX_STAN            5   /* Mutex stanu */
#define SEM_IDX_PRACOWNIK1      6   /* Sygnalizacja dla P1 */
#define SEM_IDX_PRACOWNIK2      7   /* Sygnalizacja dla P2 */
#define SEM_IDX_SYNC            8   /* Synchronizacja zatrzymania */
#define SEM_IDX_VIP             9   /* Priorytet VIP */
#define SEM_IDX_BRAMKA_WEJ_BASE 10  /* Bramki wejściowe 10-13 */
#define SEM_IDX_BRAMKA_PER_BASE 14  /* Bramki peronowe 14-16 */
#define LICZBA_SEMAFOROW        17

/* ========== UNION DLA semctl ========== */
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

/* ========== STRUKTURA SEMAFORÓW SYSTEM V ========== */
typedef struct {
    int sem_id;                     /* ID zestawu semaforów */
    key_t klucz;
} SemaforySysV;

/* ========== STRUKTURA PAMIĘCI WSPÓŁDZIELONEJ ========== */
typedef struct {
    int shm_id;
    key_t klucz;
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
    SemaforySysV sem;
    PamiecWspoldzielona shm;
    KolejkiKomunikatow mq;
} ZasobyIPC;

/* ========== FUNKCJE INICJALIZACJI ========== */
int inicjalizuj_semafory_sysv(SemaforySysV *sem);
int inicjalizuj_pamiec_wspoldzielona(PamiecWspoldzielona *shm);
int inicjalizuj_kolejki(KolejkiKomunikatow *mq);
int inicjalizuj_wszystkie_zasoby(ZasobyIPC *zasoby);

/* ========== FUNKCJE ŁĄCZENIA ========== */
int polacz_semafory_sysv(SemaforySysV *sem);
int polacz_pamiec_wspoldzielona(PamiecWspoldzielona *shm);
int polacz_kolejki(KolejkiKomunikatow *mq);
int polacz_z_zasobami(ZasobyIPC *zasoby);

/* ========== FUNKCJE USUWANIA ========== */
void usun_semafory_sysv(SemaforySysV *sem);
void usun_pamiec_wspoldzielona(PamiecWspoldzielona *shm);
void usun_kolejki(KolejkiKomunikatow *mq);
void usun_wszystkie_zasoby(ZasobyIPC *zasoby);

/* ========== OPERACJE NA SEMAFORACH SYSTEM V ========== */
void sem_czekaj_sysv(int sem_id, int sem_num);
void sem_sygnalizuj_sysv(int sem_id, int sem_num);
int sem_probuj_sysv(int sem_id, int sem_num);
int sem_pobierz_wartosc(int sem_id, int sem_num);

/* ========== OPERACJE NA KOLEJKACH ========== */
int wyslij_komunikat(int mq_id, Komunikat *msg);
int odbierz_komunikat(int mq_id, Komunikat *msg, long mtype);
int odbierz_komunikat_nieblokujaco(int mq_id, Komunikat *msg, long mtype);

#endif