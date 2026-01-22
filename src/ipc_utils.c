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