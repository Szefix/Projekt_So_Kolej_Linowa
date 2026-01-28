#ifndef PIPE_COMM_H
#define PIPE_COMM_H

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* ========== NAZWY ŁĄCZY NAZWANYCH (FIFO) ========== */
#define FIFO_KASJER_REQUEST  "/tmp/kolej_kasjer_req"
#define FIFO_KASJER_RESPONSE "/tmp/kolej_kasjer_resp"
#define FIFO_PRACOWNIK_SYNC  "/tmp/kolej_prac_sync"
#define FIFO_RAPORT          "/tmp/kolej_raport"

/* ========== STRUKTURA DLA PIPE ========== */
typedef struct {
    int fd_read;
    int fd_write;
} PipeKanaly;

/* ========== STRUKTURA DLA FIFO ========== */
typedef struct {
    int fd_kasjer_req;      /* Prośby do kasjera */
    int fd_kasjer_resp;     /* Odpowiedzi od kasjera */
    int fd_prac_sync;       /* Synchronizacja pracowników */
    int fd_raport;          /* Dane do raportu */
} FifoKanaly;

/* ========== KOMUNIKAT PRZEZ POTOK ========== */
typedef struct {
    int typ;
    int nadawca_id;
    int dane[4];
} KomunikatPipe;

/* ========== FUNKCJE TWORZENIA ========== */
int utworz_fifo_wszystkie(void);
int otworz_fifo_kasjer_serwer(FifoKanaly *kanaly);
int otworz_fifo_kasjer_klient(FifoKanaly *kanaly);
int otworz_fifo_pracownik(FifoKanaly *kanaly, int numer_pracownika);

/* ========== FUNKCJE USUWANIA ========== */
void usun_fifo_wszystkie(void);
void zamknij_fifo(FifoKanaly *kanaly);

/* ========== OPERACJE NA PIPE ========== */
int utworz_pipe(PipeKanaly *kanaly);
int wyslij_przez_pipe(int fd, KomunikatPipe *msg);
int odbierz_z_pipe(int fd, KomunikatPipe *msg);

/* ========== OPERACJE NA FIFO ========== */
int wyslij_przez_fifo(int fd, KomunikatPipe *msg);
int odbierz_z_fifo(int fd, KomunikatPipe *msg);
int odbierz_z_fifo_nieblokujaco(int fd, KomunikatPipe *msg);

#endif