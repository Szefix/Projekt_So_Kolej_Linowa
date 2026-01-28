#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "pipe_comm.h"

/* ========== TWORZENIE WSZYSTKICH FIFO ========== */
int utworz_fifo_wszystkie(void) {
    /* Usuń stare FIFO jeśli istnieją */
    unlink(FIFO_KASJER_REQUEST);
    unlink(FIFO_KASJER_RESPONSE);
    unlink(FIFO_PRACOWNIK_SYNC);
    unlink(FIFO_RAPORT);
    
    /* Tworzenie łączy nazwanych z minimalnymi uprawnieniami */
    /* 0620 = właściciel: rw, grupa: w, inni: brak */
    if (mkfifo(FIFO_KASJER_REQUEST, 0620) == -1 && errno != EEXIST) {
        perror("mkfifo kasjer_request");
        return -1;
    }
    
    if (mkfifo(FIFO_KASJER_RESPONSE, 0640) == -1 && errno != EEXIST) {
        perror("mkfifo kasjer_response");
        return -1;
    }
    
    if (mkfifo(FIFO_PRACOWNIK_SYNC, 0660) == -1 && errno != EEXIST) {
        perror("mkfifo pracownik_sync");
        return -1;
    }
    
    if (mkfifo(FIFO_RAPORT, 0640) == -1 && errno != EEXIST) {
        perror("mkfifo raport");
        return -1;
    }
    
    return 0;
}

/* ========== USUWANIE WSZYSTKICH FIFO ========== */
void usun_fifo_wszystkie(void) {
    unlink(FIFO_KASJER_REQUEST);
    unlink(FIFO_KASJER_RESPONSE);
    unlink(FIFO_PRACOWNIK_SYNC);
    unlink(FIFO_RAPORT);
}

/* ========== OTWIERANIE FIFO - KASJER (SERWER) ========== */
int otworz_fifo_kasjer_serwer(FifoKanaly *kanaly) {
    /* Kasjer czyta prośby, pisze odpowiedzi */
    kanaly->fd_kasjer_req = open(FIFO_KASJER_REQUEST, O_RDONLY | O_NONBLOCK);
    if (kanaly->fd_kasjer_req == -1) {
        perror("open fifo kasjer_req (serwer)");
        return -1;
    }
    
    kanaly->fd_kasjer_resp = open(FIFO_KASJER_RESPONSE, O_WRONLY);
    if (kanaly->fd_kasjer_resp == -1) {
        perror("open fifo kasjer_resp (serwer)");
        close(kanaly->fd_kasjer_req);
        return -1;
    }
    
    kanaly->fd_prac_sync = -1;
    kanaly->fd_raport = -1;
    
    return 0;
}

/* ========== OTWIERANIE FIFO - TURYSTA (KLIENT) ========== */
int otworz_fifo_kasjer_klient(FifoKanaly *kanaly) {
    /* Turysta pisze prośby, czyta odpowiedzi */
    kanaly->fd_kasjer_req = open(FIFO_KASJER_REQUEST, O_WRONLY);
    if (kanaly->fd_kasjer_req == -1) {
        perror("open fifo kasjer_req (klient)");
        return -1;
    }
    
    kanaly->fd_kasjer_resp = open(FIFO_KASJER_RESPONSE, O_RDONLY);
    if (kanaly->fd_kasjer_resp == -1) {
        perror("open fifo kasjer_resp (klient)");
        close(kanaly->fd_kasjer_req);
        return -1;
    }
    
    kanaly->fd_prac_sync = -1;
    kanaly->fd_raport = -1;
    
    return 0;
}

/* ========== OTWIERANIE FIFO - PRACOWNIK ========== */
int otworz_fifo_pracownik(FifoKanaly *kanaly, int numer_pracownika) {
    int flags = (numer_pracownika == 1) ? O_WRONLY : O_RDONLY;
    
    kanaly->fd_prac_sync = open(FIFO_PRACOWNIK_SYNC, flags | O_NONBLOCK);
    if (kanaly->fd_prac_sync == -1) {
        perror("open fifo prac_sync");
        return -1;
    }
    
    /* Pracownik może pisać do raportu */
    kanaly->fd_raport = open(FIFO_RAPORT, O_WRONLY | O_NONBLOCK);
    
    kanaly->fd_kasjer_req = -1;
    kanaly->fd_kasjer_resp = -1;
    
    return 0;
}

/* ========== ZAMYKANIE FIFO ========== */
void zamknij_fifo(FifoKanaly *kanaly) {
    if (kanaly->fd_kasjer_req != -1) close(kanaly->fd_kasjer_req);
    if (kanaly->fd_kasjer_resp != -1) close(kanaly->fd_kasjer_resp);
    if (kanaly->fd_prac_sync != -1) close(kanaly->fd_prac_sync);
    if (kanaly->fd_raport != -1) close(kanaly->fd_raport);
}

/* ========== TWORZENIE PIPE (NIENAZWANE) ========== */
int utworz_pipe(PipeKanaly *kanaly) {
    int pipefd[2];
    
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return -1;
    }
    
    kanaly->fd_read = pipefd[0];
    kanaly->fd_write = pipefd[1];
    
    return 0;
}

/* ========== WYSYŁANIE PRZEZ PIPE/FIFO ========== */
int wyslij_przez_pipe(int fd, KomunikatPipe *msg) {
    ssize_t napisano = write(fd, msg, sizeof(KomunikatPipe));
    if (napisano == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("write pipe");
        }
        return -1;
    }
    if (napisano != sizeof(KomunikatPipe)) {
        fprintf(stderr, "write pipe: niepełny zapis\n");
        return -1;
    }
    return 0;
}

int wyslij_przez_fifo(int fd, KomunikatPipe *msg) {
    return wyslij_przez_pipe(fd, msg);
}

/* ========== ODBIERANIE Z PIPE/FIFO ========== */
int odbierz_z_pipe(int fd, KomunikatPipe *msg) {
    ssize_t przeczytano = read(fd, msg, sizeof(KomunikatPipe));
    if (przeczytano == -1) {
        if (errno != EINTR) {
            perror("read pipe");
        }
        return -1;
    }
    if (przeczytano == 0) {
        return -2;  /* Koniec strumienia (EOF) */
    }
    if (przeczytano != sizeof(KomunikatPipe)) {
        fprintf(stderr, "read pipe: niepełny odczyt\n");
        return -1;
    }
    return 0;
}

int odbierz_z_fifo(int fd, KomunikatPipe *msg) {
    return odbierz_z_pipe(fd, msg);
}

/* ========== ODBIERANIE NIEBLOKUJĄCE ========== */
int odbierz_z_fifo_nieblokujaco(int fd, KomunikatPipe *msg) {
    /* Ustaw flagę nieblokującą */
    int flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    ssize_t przeczytano = read(fd, msg, sizeof(KomunikatPipe));
    
    /* Przywróć flagi */
    fcntl(fd, F_SETFL, flags);
    
    if (przeczytano == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;  /* Brak danych - nie błąd */
        }
        return -1;
    }
    if (przeczytano == 0) {
        return 0;  /* Brak danych */
    }
    if (przeczytano == sizeof(KomunikatPipe)) {
        return 1;  /* Odebrano komunikat */
    }
    return -1;
}