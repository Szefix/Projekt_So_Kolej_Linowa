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