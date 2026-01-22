#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ipc_utils.h"

/* ========== OPERACJE NA SEMAFORACH ========== */

void sem_czekaj(sem_t *sem) {
    if (sem == NULL) return;
    while (sem_wait(sem) == -1) {
        if (errno != EINTR) {
            perror("sem_wait");
            break;
        }
    }
}

void sem_sygnalizuj(sem_t *sem) {
    if (sem == NULL) return;
    if (sem_post(sem) == -1) {
        perror("sem_post");
    }
}

int sem_probuj(sem_t *sem) {
    if (sem == NULL) return -1;
    return sem_trywait(sem);
}

/* ========== INICJALIZACJA SEMAFORÓW ========== */

int inicjalizuj_semafory(Semafory *sem) {
    char nazwa[64];
    
    /* Główne semafory */
    sem->stacja_dolna = sem_open(SEM_STACJA_DOLNA, O_CREAT | O_EXCL, 0666, MAX_OSOB_NA_STACJI);
    if (sem->stacja_dolna == SEM_FAILED) {
        perror("sem_open stacja_dolna");
        return -1;
    }
    
    sem->peron = sem_open(SEM_PERON, O_CREAT | O_EXCL, 0666, 0);
    if (sem->peron == SEM_FAILED) {
        perror("sem_open peron");
        return -1;
    }
    
    sem->krzeselka = sem_open(SEM_KRZESELKA, O_CREAT | O_EXCL, 0666, MAX_AKTYWNYCH_KRZESELEK);
    if (sem->krzeselka == SEM_FAILED) {
        perror("sem_open krzeselka");
        return -1;
    }
    
    sem->kasa = sem_open(SEM_KASA, O_CREAT | O_EXCL, 0666, 1);
    if (sem->kasa == SEM_FAILED) {
        perror("sem_open kasa");
        return -1;
    }
    
    sem->rejestr = sem_open(SEM_REJESTR, O_CREAT | O_EXCL, 0666, 1);
    if (sem->rejestr == SEM_FAILED) {
        perror("sem_open rejestr");
        return -1;
    }
    
    sem->stan = sem_open(SEM_STAN, O_CREAT | O_EXCL, 0666, 1);
    if (sem->stan == SEM_FAILED) {
        perror("sem_open stan");
        return -1;
    }
    
    sem->pracownik1 = sem_open(SEM_PRACOWNIK1, O_CREAT | O_EXCL, 0666, 0);
    if (sem->pracownik1 == SEM_FAILED) {
        perror("sem_open pracownik1");
        return -1;
    }
    
    sem->pracownik2 = sem_open(SEM_PRACOWNIK2, O_CREAT | O_EXCL, 0666, 0);
    if (sem->pracownik2 == SEM_FAILED) {
        perror("sem_open pracownik2");
        return -1;
    }
    
    sem->sync = sem_open(SEM_SYNC, O_CREAT | O_EXCL, 0666, 0);
    if (sem->sync == SEM_FAILED) {
        perror("sem_open sync");
        return -1;
    }
    
    sem->vip = sem_open(SEM_VIP, O_CREAT | O_EXCL, 0666, 1);
    if (sem->vip == SEM_FAILED) {
        perror("sem_open vip");
        return -1;
    }
    
    /* Semafory bramek wejściowych */
    for (int i = 0; i < LICZBA_BRAMEK_WEJSCIOWYCH; i++) {
        snprintf(nazwa, sizeof(nazwa), "%s%d", SEM_BRAMKA_WEJ_PREFIX, i);
        sem->bramki_wejsciowe[i] = sem_open(nazwa, O_CREAT | O_EXCL, 0666, 1);
        if (sem->bramki_wejsciowe[i] == SEM_FAILED) {
            perror("sem_open bramka_wejsciowa");
            return -1;
        }
    }
    
    /* Semafory bramek peronowych */
    for (int i = 0; i < LICZBA_BRAMEK_PERONOWYCH; i++) {
        snprintf(nazwa, sizeof(nazwa), "%s%d", SEM_BRAMKA_PER_PREFIX, i);
        sem->bramki_peronowe[i] = sem_open(nazwa, O_CREAT | O_EXCL, 0666, 0);
        if (sem->bramki_peronowe[i] == SEM_FAILED) {
            perror("sem_open bramka_peronowa");
            return -1;
        }
    }
    
    return 0;
}

/* ========== ŁĄCZENIE Z SEMAFORAMI ========== */

int polacz_semafory(Semafory *sem) {
    char nazwa[64];
    
    sem->stacja_dolna = sem_open(SEM_STACJA_DOLNA, 0);
    sem->peron = sem_open(SEM_PERON, 0);
    sem->krzeselka = sem_open(SEM_KRZESELKA, 0);
    sem->kasa = sem_open(SEM_KASA, 0);
    sem->rejestr = sem_open(SEM_REJESTR, 0);
    sem->stan = sem_open(SEM_STAN, 0);
    sem->pracownik1 = sem_open(SEM_PRACOWNIK1, 0);
    sem->pracownik2 = sem_open(SEM_PRACOWNIK2, 0);
    sem->sync = sem_open(SEM_SYNC, 0);
    sem->vip = sem_open(SEM_VIP, 0);
    
    for (int i = 0; i < LICZBA_BRAMEK_WEJSCIOWYCH; i++) {
        snprintf(nazwa, sizeof(nazwa), "%s%d", SEM_BRAMKA_WEJ_PREFIX, i);
        sem->bramki_wejsciowe[i] = sem_open(nazwa, 0);
    }
    
    for (int i = 0; i < LICZBA_BRAMEK_PERONOWYCH; i++) {
        snprintf(nazwa, sizeof(nazwa), "%s%d", SEM_BRAMKA_PER_PREFIX, i);
        sem->bramki_peronowe[i] = sem_open(nazwa, 0);
    }
    
    /* Sprawdź czy wszystkie się otworzyły */
    if (sem->stacja_dolna == SEM_FAILED || sem->peron == SEM_FAILED ||
        sem->krzeselka == SEM_FAILED || sem->kasa == SEM_FAILED ||
        sem->stan == SEM_FAILED) {
        return -1;
    }
    
    return 0;
}

/* ========== USUWANIE SEMAFORÓW ========== */

void usun_semafory(Semafory *sem) {
    char nazwa[64];
    
    if (sem->stacja_dolna && sem->stacja_dolna != SEM_FAILED) sem_close(sem->stacja_dolna);
    if (sem->peron && sem->peron != SEM_FAILED) sem_close(sem->peron);
    if (sem->krzeselka && sem->krzeselka != SEM_FAILED) sem_close(sem->krzeselka);
    if (sem->kasa && sem->kasa != SEM_FAILED) sem_close(sem->kasa);
    if (sem->rejestr && sem->rejestr != SEM_FAILED) sem_close(sem->rejestr);
    if (sem->stan && sem->stan != SEM_FAILED) sem_close(sem->stan);
    if (sem->pracownik1 && sem->pracownik1 != SEM_FAILED) sem_close(sem->pracownik1);
    if (sem->pracownik2 && sem->pracownik2 != SEM_FAILED) sem_close(sem->pracownik2);
    if (sem->sync && sem->sync != SEM_FAILED) sem_close(sem->sync);
    if (sem->vip && sem->vip != SEM_FAILED) sem_close(sem->vip);
    
    sem_unlink(SEM_STACJA_DOLNA);
    sem_unlink(SEM_PERON);
    sem_unlink(SEM_KRZESELKA);
    sem_unlink(SEM_KASA);
    sem_unlink(SEM_REJESTR);
    sem_unlink(SEM_STAN);
    sem_unlink(SEM_PRACOWNIK1);
    sem_unlink(SEM_PRACOWNIK2);
    sem_unlink(SEM_SYNC);
    sem_unlink(SEM_VIP);
    
    for (int i = 0; i < LICZBA_BRAMEK_WEJSCIOWYCH; i++) {
        if (sem->bramki_wejsciowe[i] && sem->bramki_wejsciowe[i] != SEM_FAILED) {
            sem_close(sem->bramki_wejsciowe[i]);
        }
        snprintf(nazwa, sizeof(nazwa), "%s%d", SEM_BRAMKA_WEJ_PREFIX, i);
        sem_unlink(nazwa);
    }
    
    for (int i = 0; i < LICZBA_BRAMEK_PERONOWYCH; i++) {
        if (sem->bramki_peronowe[i] && sem->bramki_peronowe[i] != SEM_FAILED) {
            sem_close(sem->bramki_peronowe[i]);
        }
        snprintf(nazwa, sizeof(nazwa), "%s%d", SEM_BRAMKA_PER_PREFIX, i);
        sem_unlink(nazwa);
    }
}

/* ========== INICJALIZACJA PAMIĘCI WSPÓŁDZIELONEJ ========== */

int inicjalizuj_pamiec_wspoldzielona(PamiecWspoldzielona *shm) {
    /* Usuń starą pamięć jeśli istnieje */
    int stary_shm = shmget(SHM_KEY, sizeof(StanWspoldzielony), 0666);
    if (stary_shm != -1) {
        shmctl(stary_shm, IPC_RMID, NULL);
    }
    
    shm->shm_id = shmget(SHM_KEY, sizeof(StanWspoldzielony), IPC_CREAT | IPC_EXCL | 0666);
    if (shm->shm_id == -1) {
        perror("shmget create");
        return -1;
    }
    
    shm->stan = (StanWspoldzielony *)shmat(shm->shm_id, NULL, 0);
    if (shm->stan == (void *)-1) {
        perror("shmat");
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
    shm->stan->liczba_osob_na_stacji = 0;
    shm->stan->liczba_osob_na_peronie = 0;
    shm->stan->liczba_aktywnych_krzeselek = 0;
    shm->stan->laczna_liczba_zjazdow = 0;
    shm->stan->liczba_sprzedanych_biletow = 0;
    shm->stan->liczba_wpisow_rejestru = 0;
    shm->stan->kto_zatrzymal = 0;
    
    /* Inicjalizacja bramek wejściowych */
    for (int i = 0; i < LICZBA_BRAMEK_WEJSCIOWYCH; i++) {
        shm->stan->bramki_wejsciowe[i].id = i;
        shm->stan->bramki_wejsciowe[i].otwarta = true;
        shm->stan->bramki_wejsciowe[i].aktualny_turysta_id = -1;
    }
    
    /* Inicjalizacja bramek peronowych */
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
        shm->stan->krzeselka[i].liczba_rowerzystow = 0;
        for (int j = 0; j < POJEMNOSC_KRZESELKA; j++) {
            shm->stan->krzeselka[i].pasazerowie[j] = -1;
        }
    }
    
    return 0;
}

/* ========== ŁĄCZENIE Z PAMIĘCIĄ WSPÓŁDZIELONĄ ========== */

int polacz_pamiec_wspoldzielona(PamiecWspoldzielona *shm) {
    shm->shm_id = shmget(SHM_KEY, sizeof(StanWspoldzielony), 0666);
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
    }
    if (shm->shm_id != -1) {
        shmctl(shm->shm_id, IPC_RMID, NULL);
    }
}

/* ========== INICJALIZACJA KOLEJEK KOMUNIKATÓW ========== */

int inicjalizuj_kolejki(KolejkiKomunikatow *mq) {
    /* Usuń stare kolejki jeśli istnieją */
    int stara_mq;
    stara_mq = msgget(MQ_KEY_KASA, 0666);
    if (stara_mq != -1) msgctl(stara_mq, IPC_RMID, NULL);
    stara_mq = msgget(MQ_KEY_BRAMKI, 0666);
    if (stara_mq != -1) msgctl(stara_mq, IPC_RMID, NULL);
    stara_mq = msgget(MQ_KEY_PRACOWNICY, 0666);
    if (stara_mq != -1) msgctl(stara_mq, IPC_RMID, NULL);
    stara_mq = msgget(MQ_KEY_KRZESLA, 0666);
    if (stara_mq != -1) msgctl(stara_mq, IPC_RMID, NULL);
    
    mq->mq_kasa = msgget(MQ_KEY_KASA, IPC_CREAT | IPC_EXCL | 0666);
    if (mq->mq_kasa == -1) {
        perror("msgget kasa");
        return -1;
    }
    
    mq->mq_bramki = msgget(MQ_KEY_BRAMKI, IPC_CREAT | IPC_EXCL | 0666);
    if (mq->mq_bramki == -1) {
        perror("msgget bramki");
        return -1;
    }
    
    mq->mq_pracownicy = msgget(MQ_KEY_PRACOWNICY, IPC_CREAT | IPC_EXCL | 0666);
    if (mq->mq_pracownicy == -1) {
        perror("msgget pracownicy");
        return -1;
    }
    
    mq->mq_krzesla = msgget(MQ_KEY_KRZESLA, IPC_CREAT | IPC_EXCL | 0666);
    if (mq->mq_krzesla == -1) {
        perror("msgget krzesla");
        return -1;
    }
    
    return 0;
}

/* ========== ŁĄCZENIE Z KOLEJKAMI ========== */

int polacz_kolejki(KolejkiKomunikatow *mq) {
    mq->mq_kasa = msgget(MQ_KEY_KASA, 0666);
    mq->mq_bramki = msgget(MQ_KEY_BRAMKI, 0666);
    mq->mq_pracownicy = msgget(MQ_KEY_PRACOWNICY, 0666);
    mq->mq_krzesla = msgget(MQ_KEY_KRZESLA, 0666);
    
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
            return 0;  /* Brak komunikatu - to nie błąd */
        }
        return -1;
    }
    return 1;  /* Odebrano komunikat */
}

/* ========== FUNKCJE ZBIORCZE ========== */

int inicjalizuj_wszystkie_zasoby(ZasobyIPC *zasoby) {
    memset(zasoby, 0, sizeof(ZasobyIPC));
    
    if (inicjalizuj_semafory(&zasoby->sem) == -1) {
        fprintf(stderr, "Błąd inicjalizacji semaforów\n");
        return -1;
    }
    
    if (inicjalizuj_pamiec_wspoldzielona(&zasoby->shm) == -1) {
        fprintf(stderr, "Błąd inicjalizacji pamięci współdzielonej\n");
        usun_semafory(&zasoby->sem);
        return -1;
    }
    
    if (inicjalizuj_kolejki(&zasoby->mq) == -1) {
        fprintf(stderr, "Błąd inicjalizacji kolejek\n");
        usun_semafory(&zasoby->sem);
        usun_pamiec_wspoldzielona(&zasoby->shm);
        return -1;
    }
    
    return 0;
}

int polacz_z_zasobami(ZasobyIPC *zasoby) {
    memset(zasoby, 0, sizeof(ZasobyIPC));
    
    if (polacz_semafory(&zasoby->sem) == -1) return -1;
    if (polacz_pamiec_wspoldzielona(&zasoby->shm) == -1) return -1;
    if (polacz_kolejki(&zasoby->mq) == -1) return -1;
    
    return 0;
}

void usun_wszystkie_zasoby(ZasobyIPC *zasoby) {
    usun_kolejki(&zasoby->mq);
    usun_pamiec_wspoldzielona(&zasoby->shm);
    usun_semafory(&zasoby->sem);
}