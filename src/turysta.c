#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include "config.h"
#include "types.h"
#include "ipc_utils.h"
#include "logger.h"

/* Zmienne globalne procesu turysty */
static volatile sig_atomic_t turysta_dzialaj = 1;
static ZasobyIPC turysta_zasoby;
static Turysta ja;  /* Dane tego turysty */

/* Obsługa sygnałów */
static void turysta_obsluz_sygnal(int sig) {
    (void)sig;
    turysta_dzialaj = 0;
}

/* Generowanie losowych danych turysty */
void inicjalizuj_turystę(int id, int zadany_wiek, int opiekun_id) {
    memset(&ja, 0, sizeof(Turysta));
    
    ja.id = id;
    ja.pid = getpid();
    
    /* Wiek - jeśli podany użyj go, inaczej losuj */
    if (zadany_wiek > 0) {
        ja.wiek = zadany_wiek;
    } else {
        ja.wiek = (rand() % 76) + 4;  /* Wiek 4-79 */
    }
    
    /* Typ - 40% szans na rowerzystę (tylko dorośli) */
    if (ja.wiek >= 12 && rand() % 100 < 40) {
        ja.typ = ROWERZYSTA;
    } else {
        ja.typ = PIESZY;
    }
    
    /* VIP - 1% szans */
    ja.vip = (rand() % 100 < PROCENT_VIP);
    
    /* Czy wymaga opieki */
    ja.dziecko_pod_opieka = (ja.wiek >= WIEK_MIN_DZIECKO && ja.wiek < WIEK_DZIECKO_OPIEKA);
    ja.opiekun_id = opiekun_id;
    ja.liczba_dzieci = 0;
    
    ja.status = STATUS_NOWY;
    ja.liczba_zjazdow = 0;
    
    memset(&ja.bilet, 0, sizeof(Bilet));
    for (int i = 0; i < MAX_DZIECI_POD_OPIEKA; i++) {
        ja.dzieci_pod_opieka[i] = -1;
    }
}

/* Sprawdzenie ważności biletu */
bool sprawdz_waznosc_biletu(void) {
    if (!ja.bilet.aktywny) return false;
    
    time_t teraz = time(NULL);
    
    /* Bilet jednorazowy - sprawdź liczbę użyć */
    if (ja.bilet.typ == BILET_JEDNORAZOWY) {
        return ja.bilet.liczba_uzyc < ja.bilet.max_uzyc;
    }
    
    /* Bilety czasowe i dzienne - sprawdź czas */
    if (ja.bilet.czas_waznosci > 0) {
        return teraz < ja.bilet.czas_waznosci;
    }
    
    return true;
}

/* Kupowanie biletu */
int kup_bilet(void) {
    LOG_I("TURYSTA #%d: Podchodzę do kasy", ja.id);
    
    /* Losowy wybór typu biletu */
    int typy[] = {BILET_JEDNORAZOWY, BILET_CZASOWY_TK1, BILET_CZASOWY_TK2, 
                  BILET_CZASOWY_TK3, BILET_DZIENNY};
    int typ = typy[rand() % 5];
    
    /* Przygotuj prośbę */
    Komunikat prosba;
    memset(&prosba, 0, sizeof(Komunikat));
    prosba.mtype = MSG_PROSBA_O_BILET;
    prosba.nadawca_id = ja.id;
    prosba.typ_komunikatu = MSG_PROSBA_O_BILET;
    prosba.dane[0] = typ;
    prosba.dane[1] = ja.wiek;
    prosba.dane[2] = ja.vip ? 1 : 0;
    
    /* Wyślij prośbę */
    if (wyslij_komunikat(turysta_zasoby.mq.mq_kasa, &prosba) == -1) {
        LOG_E("TURYSTA #%d: Błąd wysyłania prośby o bilet", ja.id);
        return -1;
    }
    
    ja.status = STATUS_OCZEKUJE_NA_BILET;
    LOG_I("TURYSTA #%d: Czekam na bilet...", ja.id);
    
    /* Czekaj na odpowiedź - blokujące */
    Komunikat odpowiedz;
    if (odbierz_komunikat(turysta_zasoby.mq.mq_kasa, &odpowiedz, ja.id) == -1) {
        LOG_E("TURYSTA #%d: Błąd odbierania biletu", ja.id);
        return -1;
    }
    
    /* Zapisz dane biletu */
    ja.bilet.id = odpowiedz.dane[0];
    ja.bilet.typ = odpowiedz.dane[1];
    ja.bilet.max_uzyc = odpowiedz.dane[2];
    ja.bilet.czas_waznosci = (time_t)odpowiedz.dane[3];
    ja.bilet.vip = (odpowiedz.dane[4] != 0);
    ja.bilet.aktywny = true;
    ja.bilet.liczba_uzyc = 0;
    ja.bilet.czas_zakupu = time(NULL);
    ja.bilet.wlasciciel_id = ja.id;
    
    int cena = odpowiedz.dane[5];
    
    ja.status = STATUS_MA_BILET;
    LOG_I("TURYSTA #%d: Kupiłem bilet #%d (typ: %d), cena: %d zł", 
          ja.id, ja.bilet.id, ja.bilet.typ, cena);
    
    return 0;
}

/* Przejście przez bramkę wejściową */
int przejdz_bramke_wejsciowa(void) {
    StanWspoldzielony *stan = turysta_zasoby.shm.stan;
    
    /* Sprawdź czy kolej działa */
    if (!stan->godziny_pracy) {
        LOG_W("TURYSTA #%d: Kolej zamknięta!", ja.id);
        return -1;
    }
    
    /* Sprawdź ważność biletu */
    if (!sprawdz_waznosc_biletu()) {
        LOG_W("TURYSTA #%d: Bilet #%d jest nieważny!", ja.id, ja.bilet.id);
        return -1;
    }
    
    ja.status = STATUS_PRZED_BRAMKA_WEJSCIOWA;
    
    /* VIP wchodzi bez kolejki - priorytetowy semafor */
    if (ja.vip) {
        LOG_I("TURYSTA #%d [VIP]: Wchodzę bez kolejki", ja.id);
        sem_czekaj(turysta_zasoby.sem.vip);
    }
    
    LOG_I("TURYSTA #%d: Czekam na miejsce na stacji dolnej", ja.id);
    
    /* Czekaj na miejsce na stacji dolnej (semafor licznikowy) */
    sem_czekaj(turysta_zasoby.sem.stacja_dolna);
    
    /* Znajdź wolną bramkę wejściową */
    int bramka = -1;
    for (int i = 0; i < LICZBA_BRAMEK_WEJSCIOWYCH; i++) {
        if (sem_probuj(turysta_zasoby.sem.bramki_wejsciowe[i]) == 0) {
            bramka = i;
            break;
        }
    }
    
    /* Jeśli żadna bramka nie była wolna, czekaj na losową */
    if (bramka == -1) {
        bramka = rand() % LICZBA_BRAMEK_WEJSCIOWYCH;
        sem_czekaj(turysta_zasoby.sem.bramki_wejsciowe[bramka]);
    }
    
    /* Użyj biletu */
    ja.bilet.liczba_uzyc++;
    
    /* Zarejestruj przejście */
    sem_czekaj(turysta_zasoby.sem.rejestr);
    if (stan->liczba_wpisow_rejestru < MAX_WPISOW_REJESTRU) {
        WpisRejestru *wpis = &stan->rejestr[stan->liczba_wpisow_rejestru];
        wpis->bilet_id = ja.bilet.id;
        wpis->turysta_id = ja.id;
        wpis->czas = time(NULL);
        wpis->numer_bramki = bramka;
        wpis->numer_zjazdu = ja.liczba_zjazdow + 1;
        stan->liczba_wpisow_rejestru++;
    }
    sem_sygnalizuj(turysta_zasoby.sem.rejestr);
    
    /* Aktualizuj liczbę osób na stacji */
    sem_czekaj(turysta_zasoby.sem.stan);
    stan->liczba_osob_na_stacji++;
    sem_sygnalizuj(turysta_zasoby.sem.stan);
    
    LOG_I("TURYSTA #%d: Przeszedłem przez bramkę %d, bilet #%d użyty %d raz(y)",
          ja.id, bramka, ja.bilet.id, ja.bilet.liczba_uzyc);
    
    /* Zwolnij bramkę */
    sem_sygnalizuj(turysta_zasoby.sem.bramki_wejsciowe[bramka]);
    
    /* Zwolnij priorytet VIP */
    if (ja.vip) {
        sem_sygnalizuj(turysta_zasoby.sem.vip);
    }
    
    ja.status = STATUS_NA_STACJI_DOLNEJ;
    return 0;
}

/* Czekanie na wejście na peron */
int czekaj_na_peron(void) {
    StanWspoldzielony *stan = turysta_zasoby.shm.stan;
    
    ja.status = STATUS_OCZEKUJE_NA_PERON;
    LOG_I("TURYSTA #%d: Czekam na pozwolenie wejścia na peron", ja.id);
    
    /* Wyślij prośbę do pracownika1 */
    Komunikat prosba;
    memset(&prosba, 0, sizeof(Komunikat));
    prosba.mtype = MSG_PROSBA_O_PERON;
    prosba.nadawca_id = ja.id;
    prosba.typ_komunikatu = MSG_PROSBA_O_PERON;
    prosba.dane[0] = ja.typ;  /* PIESZY lub ROWERZYSTA */
    prosba.dane[1] = ja.dziecko_pod_opieka ? 1 : 0;
    prosba.dane[2] = ja.opiekun_id;
    prosba.dane[3] = ja.wiek;
    
    if (wyslij_komunikat(turysta_zasoby.mq.mq_pracownicy, &prosba) == -1) {
        LOG_E("TURYSTA #%d: Błąd wysyłania prośby o peron", ja.id);
        return -1;
    }
    
    /* Czekaj na semaforze - pracownik1 go zwolni gdy będzie można wejść */
    sem_czekaj(turysta_zasoby.sem.peron);
    
    /* Sprawdź czy kolej nie została zatrzymana */
    if (stan->kolej_zatrzymana) {
        LOG_W("TURYSTA #%d: Kolej zatrzymana! Czekam...", ja.id);
        /* Będziemy czekać aż pracownik zwolni kolejny semafor */
        sem_czekaj(turysta_zasoby.sem.peron);
    }
    
    ja.status = STATUS_NA_PERONIE;
    LOG_I("TURYSTA #%d: Wchodzę na peron", ja.id);
    
    /* Aktualizuj liczniki */
    sem_czekaj(turysta_zasoby.sem.stan);
    stan->liczba_osob_na_stacji--;
    stan->liczba_osob_na_peronie++;
    sem_sygnalizuj(turysta_zasoby.sem.stan);
    
    return 0;
}

/* Wsiadanie na krzesełko */
int wsiadz_na_krzeselko(void) {
    StanWspoldzielony *stan = turysta_zasoby.shm.stan;
    
    LOG_I("TURYSTA #%d: Czekam na krzesełko", ja.id);
    
    /* Wyślij gotowość do wsiadania */
    Komunikat msg;
    memset(&msg, 0, sizeof(Komunikat));
    msg.mtype = MSG_WSIADANIE_NA_KRZESLO;
    msg.nadawca_id = ja.id;
    msg.typ_komunikatu = MSG_WSIADANIE_NA_KRZESLO;
    msg.dane[0] = ja.typ;
    msg.dane[1] = ja.dziecko_pod_opieka ? 1 : 0;
    
    if (wyslij_komunikat(turysta_zasoby.mq.mq_krzesla, &msg) == -1) {
        LOG_E("TURYSTA #%d: Błąd wysyłania gotowości do wsiadania", ja.id);
        return -1;
    }
    
    /* Czekaj na potwierdzenie - specjalny typ wiadomości dla tego turysty */
    Komunikat odp;
    long moj_typ = ja.id + 10000;  /* Unikalne ID odpowiedzi */
    
    if (odbierz_komunikat(turysta_zasoby.mq.mq_krzesla, &odp, moj_typ) == -1) {
        LOG_E("TURYSTA #%d: Błąd oczekiwania na krzesełko", ja.id);
        return -1;
    }
    
    int krzeselko_id = odp.dane[0];
    
    ja.status = STATUS_NA_KRZESELKU;
    LOG_I("TURYSTA #%d: Wsiadłem na krzesełko #%d", ja.id, krzeselko_id);
    
    /* Aktualizuj licznik osób na peronie */
    sem_czekaj(turysta_zasoby.sem.stan);
    stan->liczba_osob_na_peronie--;
    sem_sygnalizuj(turysta_zasoby.sem.stan);
    
    return krzeselko_id;
}