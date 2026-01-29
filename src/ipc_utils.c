#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include "ipc_utils.h"
#include "config.h"

/* ========== OPERACJE NA SEMAFORACH SYSTEM V ========== */

void sem_czekaj_sysv(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;         /* Dekrementuj (P/wait) */
    op.sem_flg = 0;
    
    while (semop(sem_id, &op, 1) == -1) {
        if (errno != EINTR) {
            perror("semop wait");
            break;
        }
        /* EINTR - przerwane przez sygnał, spróbuj ponownie */
    }
}

void sem_sygnalizuj_sysv(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = 1;          /* Inkrementuj (V/signal) */
    op.sem_flg = 0;
    
    if (semop(sem_id, &op, 1) == -1) {
        perror("semop signal");
    }
}

int sem_probuj_sysv(int sem_id, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = IPC_NOWAIT;  /* Nieblokujące */
    
    if (semop(sem_id, &op, 1) == -1) {
        if (errno == EAGAIN) {
            return -1;  /* Semafor zajęty */
        }
        perror("semop trywait");
        return -1;
    }
    return 0;  /* Sukces */
}

int sem_pobierz_wartosc(int sem_id, int sem_num) {
    int val = semctl(sem_id, sem_num, GETVAL);
    if (val == -1) {
        perror("semctl getval");
    }
    return val;
}

/* Czekaj na semafor z timeoutem - BLOKUJĄCE z timeoutem */
int sem_czekaj_timeout_sysv(int sem_id, int sem_num, int timeout_sec) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;         /* Dekrementuj (P/wait) */
    op.sem_flg = 0;         /* Blokujące */

    struct timespec timeout;
    timeout.tv_sec = timeout_sec;
    timeout.tv_nsec = 0;

    /* semtimedop() blokuje proces/wątek do timeout lub uzyskania semafora */
    int result = semtimedop(sem_id, &op, 1, &timeout);

    if (result == -1) {
        if (errno == EAGAIN || errno == ETIMEDOUT) {
            return -1;  /* Timeout */
        }
        if (errno != EINTR) {
            perror("semtimedop");
        }
        return -1;
    }
    return 0;  /* Sukces */
}

/* ========== INICJALIZACJA SEMAFORÓW SYSTEM V ========== */

int inicjalizuj_semafory_sysv(SemaforySysV *sem) {
    /* Generuj klucz używając ftok */
    sem->klucz = ftok("/tmp", 'K');
    if (sem->klucz == -1) {
        perror("ftok semafory");
        return -1;
    }
    
    /* Usuń stary zestaw jeśli istnieje */
    int stary = semget(sem->klucz, LICZBA_SEMAFOROW, 0666);
    if (stary != -1) {
        semctl(stary, 0, IPC_RMID);
    }
    
    /* Utwórz nowy zestaw semaforów */
    /* Uprawnienia 0660 - właściciel i grupa: rw */
    sem->sem_id = semget(sem->klucz, LICZBA_SEMAFOROW, IPC_CREAT | IPC_EXCL | 0660);
    if (sem->sem_id == -1) {
        perror("semget create");
        return -1;
    }
    
    /* Tablica wartości początkowych */
    unsigned short wartosci[LICZBA_SEMAFOROW];
    
    wartosci[SEM_IDX_STACJA_DOLNA] = MAX_OSOB_NA_STACJI;
    wartosci[SEM_IDX_PERON] = 0;
    wartosci[SEM_IDX_KRZESELKA] = MAX_AKTYWNYCH_KRZESELEK;
    wartosci[SEM_IDX_KASA] = 1;
    wartosci[SEM_IDX_REJESTR] = 1;
    wartosci[SEM_IDX_STAN] = 1;
    wartosci[SEM_IDX_PRACOWNIK1] = 0;
    wartosci[SEM_IDX_PRACOWNIK2] = 0;
    wartosci[SEM_IDX_SYNC] = 0;
    wartosci[SEM_IDX_VIP] = 1;
    
    /* Bramki wejściowe - każda wolna (1) */
    for (int i = 0; i < LICZBA_BRAMEK_WEJSCIOWYCH; i++) {
        wartosci[SEM_IDX_BRAMKA_WEJ_BASE + i] = 1;
    }
    
    /* Bramki peronowe - zamknięte (0) */
    for (int i = 0; i < LICZBA_BRAMEK_PERONOWYCH; i++) {
        wartosci[SEM_IDX_BRAMKA_PER_BASE + i] = 0;
    }
    
    /* Ustaw wszystkie wartości za jednym razem */
    union semun arg;
    arg.array = wartosci;
    
    if (semctl(sem->sem_id, 0, SETALL, arg) == -1) {
        perror("semctl SETALL");
        semctl(sem->sem_id, 0, IPC_RMID);
        return -1;
    }
    
    return 0;
}

/* ========== ŁĄCZENIE Z SEMAFORAMI ========== */

int polacz_semafory_sysv(SemaforySysV *sem) {
    sem->klucz = ftok("/tmp", 'K');
    if (sem->klucz == -1) {
        perror("ftok semafory (connect)");
        return -1;
    }
    
    sem->sem_id = semget(sem->klucz, LICZBA_SEMAFOROW, 0660);
    if (sem->sem_id == -1) {
        perror("semget connect");
        return -1;
    }
    
    return 0;
}

/* ========== USUWANIE SEMAFORÓW ========== */

void usun_semafory_sysv(SemaforySysV *sem) {
    if (sem->sem_id != -1) {
        if (semctl(sem->sem_id, 0, IPC_RMID) == -1) {
            perror("semctl IPC_RMID");
        }
        sem->sem_id = -1;
    }
}

/* ========== INICJALIZACJA PAMIĘCI WSPÓŁDZIELONEJ ========== */

int inicjalizuj_pamiec_wspoldzielona(PamiecWspoldzielona *shm) {
    /* Generuj klucz używając ftok */
    shm->klucz = ftok("/tmp", 'S');
    if (shm->klucz == -1) {
        perror("ftok shm");
        return -1;
    }
    
    /* Usuń starą pamięć jeśli istnieje */
    int stary = shmget(shm->klucz, sizeof(StanWspoldzielony), 0666);
    if (stary != -1) {
        shmctl(stary, IPC_RMID, NULL);
    }
    
    /* Utwórz nowy segment - uprawnienia 0660 */
    shm->shm_id = shmget(shm->klucz, sizeof(StanWspoldzielony), 
                          IPC_CREAT | IPC_EXCL | 0660);
    if (shm->shm_id == -1) {
        perror("shmget create");
        return -1;
    }
    
    /* Dołącz segment */
    shm->stan = (StanWspoldzielony *)shmat(shm->shm_id, NULL, 0);
    if (shm->stan == (void *)-1) {
        perror("shmat");
        shmctl(shm->shm_id, IPC_RMID, NULL);
        return -1;
    }
    
    /* Inicjalizacja stanu początkowego */
    memset(shm->stan, 0, sizeof(StanWspoldzielony));
    
    shm->stan->kolej_aktywna = true;
    shm->stan->kolej_zatrzymana = false;
    shm->stan->godziny_pracy = true;
    shm->stan->czas_startu = time(NULL);
    shm->stan->nastepny_turysta_id = 1;
    shm->stan->nastepny_bilet_id = 1;
    
    /* Inicjalizacja bramek */
    for (int i = 0; i < LICZBA_BRAMEK_WEJSCIOWYCH; i++) {
        shm->stan->bramki_wejsciowe[i].id = i;
        shm->stan->bramki_wejsciowe[i].otwarta = true;
        shm->stan->bramki_wejsciowe[i].aktualny_turysta_id = -1;
    }
    
    for (int i = 0; i < LICZBA_BRAMEK_PERONOWYCH; i++) {
        shm->stan->bramki_peronowe[i].id = i;
        shm->stan->bramki_peronowe[i].otwarta = false;
        shm->stan->bramki_peronowe[i].aktualny_turysta_id = -1;
    }
    
    /* Inicjalizacja krzesełek */
    for (int i = 0; i < MAX_AKTYWNYCH_KRZESELEK; i++) {
        shm->stan->krzeselka[i].id = i;
        shm->stan->krzeselka[i].aktywne = false;
        shm->stan->krzeselka[i].liczba_pasazerow = 0;
        for (int j = 0; j < POJEMNOSC_KRZESELKA; j++) {
            shm->stan->krzeselka[i].pasazerowie[j] = -1;
        }
    }
    
    return 0;
}

/* ========== ŁĄCZENIE Z PAMIĘCIĄ WSPÓŁDZIELONĄ ========== */

int polacz_pamiec_wspoldzielona(PamiecWspoldzielona *shm) {
    shm->klucz = ftok("/tmp", 'S');
    if (shm->klucz == -1) {
        perror("ftok shm (connect)");
        return -1;
    }
    
    shm->shm_id = shmget(shm->klucz, sizeof(StanWspoldzielony), 0660);
    if (shm->shm_id == -1) {
        perror("shmget connect");
        return -1;
    }
    
    shm->stan = (StanWspoldzielony *)shmat(shm->shm_id, NULL, 0);
    if (shm->stan == (void *)-1) {
        perror("shmat connect");
        return -1;
    }
    
    return 0;
}

/* ========== USUWANIE PAMIĘCI WSPÓŁDZIELONEJ ========== */

void usun_pamiec_wspoldzielona(PamiecWspoldzielona *shm) {
    if (shm->stan && shm->stan != (void *)-1) {
        shmdt(shm->stan);
        shm->stan = NULL;
    }
    if (shm->shm_id != -1) {
        shmctl(shm->shm_id, IPC_RMID, NULL);
        shm->shm_id = -1;
    }
}

/* ========== INICJALIZACJA KOLEJEK ========== */

int inicjalizuj_kolejki(KolejkiKomunikatow *mq) {
    /* Usuń stare kolejki */
    key_t klucz;
    int stary;
    
    klucz = ftok("/tmp", 'A');
    stary = msgget(klucz, 0666);
    if (stary != -1) msgctl(stary, IPC_RMID, NULL);
    
    klucz = ftok("/tmp", 'B');
    stary = msgget(klucz, 0666);
    if (stary != -1) msgctl(stary, IPC_RMID, NULL);
    
    klucz = ftok("/tmp", 'C');
    stary = msgget(klucz, 0666);
    if (stary != -1) msgctl(stary, IPC_RMID, NULL);
    
    klucz = ftok("/tmp", 'D');
    stary = msgget(klucz, 0666);
    if (stary != -1) msgctl(stary, IPC_RMID, NULL);
    
    /* Tworzenie nowych kolejek - uprawnienia 0660 */
    mq->mq_kasa = msgget(ftok("/tmp", 'A'), IPC_CREAT | IPC_EXCL | 0660);
    if (mq->mq_kasa == -1) {
        perror("msgget kasa");
        return -1;
    }
    
    mq->mq_bramki = msgget(ftok("/tmp", 'B'), IPC_CREAT | IPC_EXCL | 0660);
    if (mq->mq_bramki == -1) {
        perror("msgget bramki");
        return -1;
    }
    
    mq->mq_pracownicy = msgget(ftok("/tmp", 'C'), IPC_CREAT | IPC_EXCL | 0660);
    if (mq->mq_pracownicy == -1) {
        perror("msgget pracownicy");
        return -1;
    }
    
    mq->mq_krzesla = msgget(ftok("/tmp", 'D'), IPC_CREAT | IPC_EXCL | 0660);
    if (mq->mq_krzesla == -1) {
        perror("msgget krzesla");
        return -1;
    }
    
    return 0;
}

/* ========== ŁĄCZENIE Z KOLEJKAMI ========== */

int polacz_kolejki(KolejkiKomunikatow *mq) {
    mq->mq_kasa = msgget(ftok("/tmp", 'A'), 0660);
    mq->mq_bramki = msgget(ftok("/tmp", 'B'), 0660);
    mq->mq_pracownicy = msgget(ftok("/tmp", 'C'), 0660);
    mq->mq_krzesla = msgget(ftok("/tmp", 'D'), 0660);
    
    if (mq->mq_kasa == -1 || mq->mq_bramki == -1 || 
        mq->mq_pracownicy == -1 || mq->mq_krzesla == -1) {
        perror("msgget connect");
        return -1;
    }
    
    return 0;
}

/* ========== USUWANIE KOLEJEK ========== */

void usun_kolejki(KolejkiKomunikatow *mq) {
    if (mq->mq_kasa != -1) msgctl(mq->mq_kasa, IPC_RMID, NULL);
    if (mq->mq_bramki != -1) msgctl(mq->mq_bramki, IPC_RMID, NULL);
    if (mq->mq_pracownicy != -1) msgctl(mq->mq_pracownicy, IPC_RMID, NULL);
    if (mq->mq_krzesla != -1) msgctl(mq->mq_krzesla, IPC_RMID, NULL);
}

/* ========== FUNKCJE ZBIORCZE ========== */

int inicjalizuj_wszystkie_zasoby(ZasobyIPC *zasoby) {
    memset(zasoby, 0, sizeof(ZasobyIPC));
    zasoby->sem.sem_id = -1;
    zasoby->shm.shm_id = -1;
    zasoby->mq.mq_kasa = -1;
    zasoby->mq.mq_bramki = -1;
    zasoby->mq.mq_pracownicy = -1;
    zasoby->mq.mq_krzesla = -1;
    
    if (inicjalizuj_semafory_sysv(&zasoby->sem) == -1) {
        fprintf(stderr, "Błąd inicjalizacji semaforów\n");
        return -1;
    }
    
    if (inicjalizuj_pamiec_wspoldzielona(&zasoby->shm) == -1) {
        fprintf(stderr, "Błąd inicjalizacji pamięci współdzielonej\n");
        usun_semafory_sysv(&zasoby->sem);
        return -1;
    }
    
    if (inicjalizuj_kolejki(&zasoby->mq) == -1) {
        fprintf(stderr, "Błąd inicjalizacji kolejek\n");
        usun_semafory_sysv(&zasoby->sem);
        usun_pamiec_wspoldzielona(&zasoby->shm);
        return -1;
    }
    
    return 0;
}

int polacz_z_zasobami(ZasobyIPC *zasoby) {
    memset(zasoby, 0, sizeof(ZasobyIPC));
    
    if (polacz_semafory_sysv(&zasoby->sem) == -1) return -1;
    if (polacz_pamiec_wspoldzielona(&zasoby->shm) == -1) return -1;
    if (polacz_kolejki(&zasoby->mq) == -1) return -1;
    
    return 0;
}

void usun_wszystkie_zasoby(ZasobyIPC *zasoby) {
    usun_kolejki(&zasoby->mq);
    usun_pamiec_wspoldzielona(&zasoby->shm);
    usun_semafory_sysv(&zasoby->sem);
}

/* ========== WYSYŁANIE KOMUNIKATU ========== */

int wyslij_komunikat(int mq_id, Komunikat *msg) {
    if (msgsnd(mq_id, msg, sizeof(Komunikat) - sizeof(long), 0) == -1) {
        if (errno != EINTR) {
            perror("msgsnd");
        }
        return -1;
    }
    return 0;
}

/* ========== ODBIERANIE KOMUNIKATU (BLOKUJĄCE) ========== */

int odbierz_komunikat(int mq_id, Komunikat *msg, long mtype) {
    if (msgrcv(mq_id, msg, sizeof(Komunikat) - sizeof(long), mtype, 0) == -1) {
        if (errno != EINTR) {
            perror("msgrcv");
        }
        return -1;
    }
    return 0;
}

/* ========== ODBIERANIE KOMUNIKATU (NIEBLOKUJĄCE) ========== */

int odbierz_komunikat_nieblokujaco(int mq_id, Komunikat *msg, long mtype) {
    if (msgrcv(mq_id, msg, sizeof(Komunikat) - sizeof(long), mtype, IPC_NOWAIT) == -1) {
        if (errno == ENOMSG) {
            return 0;
        }
        return -1;
    }
    return 1;
}