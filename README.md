# Projekt Systemy Operacyjne 2025/2026 - Temat 16: Kolej Linowa Krzesełkowa

**Autor:** Kamil Kędra

**Numer albumu:** 154921

**Grupa laboratoryjna:** GP02

**Repozytorium GitHub:** https://github.com/Szefix/Projekt_So_Kolej_Linowa

**Środowisko:** Linux Ubuntu 24.04.3 LTS (WSL)

**Kompilator:** GCC 13.3.0

---

## 1. Opis zadania i uruchomienie

### 1.1 Opis zadania

Projekt stanowi symulację działania krzesełkowej kolei linowej w systemie Linux, wykorzystującą mechanizmy **IPC Systemu V** (*pamięć dzielona, semafory, kolejki komunikatów*), **FIFO** oraz procesy potomne. Symulacja uwzględnia:

- Obsługę **72 krzesełek** (max 36 aktywnych jednocześnie) o pojemności 4 osób
- Ograniczenia dla **rowerzystów** (max 2 na krzesełku)
- System **biletów i karnetów** (jednorazowe, czasowe TK1/TK2/TK3, dzienne)
- **Zniżki** dla dzieci (<10 lat) i seniorów (>65 lat) - 25%
- Opiekę nad **dziećmi 4-8 lat** (max 2 dzieci na opiekuna)
- Priorytetowe wejście dla **VIP** (1% turystów)
- Dwie stacje z **bramkami wejściowymi** (4) i **peronowymi** (3)
- Mechanizm **zatrzymania i wznowienia** kolei przez pracowników
- **Raport dzienny** z rejestrem wszystkich przejazdów

### 1.2 Uruchomienie symulacji

**Kompilacja:**
```bash
make clean all
```

**Uruchomienie:**
```bash
# Tryb interaktywny (pytanie o czas i liczbę turystów)
./bin/main

# Z parametrami
./bin/main -t 60 -n 100           # 60 sekund, max 100 turystów

# Tryb TURBO (przyspieszenie symulacji)
./bin/main -t 60 -T               # 5x szybciej (domyślnie)
./bin/main -t 60 -T 10            # 10x szybciej

# Skróty w Makefile
make run-short                     # 30s, 20 turystów
make run-long                      # 120s, 100 turystów
```

**Parametry:**
| Parametr | Opis |
|----------|------|
| `-t CZAS` | Czas symulacji w sekundach (0 = nieskończoność) |
| `-n LICZBA` | Maksymalna liczba turystów (domyślnie 100) |
| `-T [MNOŻNIK]` | Tryb turbo - przyspieszenie 2-10x (domyślnie 5x) |
| `-h, --help` | Wyświetl pomoc |

**Czyszczenie zasobów:**
```bash
make clean-ipc    # Usuń zasoby IPC (semafory, kolejki, pamięć)
make clean        # Usuń binaria i logi
make clean-all    # Oba powyższe
```

---

## 2. Struktura kodu

### 2.1 Pliki nagłówkowe (`include/`)

| Plik | Opis |
|------|------|
| `config.h` | Stałe konfiguracyjne: liczba krzesełek (72), bramek (4+3), wyjścia górnej stacji (2), czasy tras T1/T2/T3, ceny biletów, klucze IPC (SHM_KEY, MQ_KEY_*) |
| `types.h` | Struktury: `Turysta` (id, wiek, typ, bilet, status), `Bilet` (typ, ważność, VIP), `Krzeselko`, `Bramka`, `StanWspoldzielony` (flagi, liczniki, rejestr), `Komunikat` (mtype, dane) |
| `ipc_utils.h` | Deklaracje funkcji IPC: `sem_czekaj_sysv()`, `sem_sygnalizuj_sysv()`, `sem_probuj_sysv()`, `sem_czekaj_timeout_sysv()`, `wyslij_komunikat()`, `odbierz_komunikat_timeout()` oraz indeksy 17 semaforów |
| `logger.h` | Interfejs logowania: `logger_init()`, `logger_log()`, poziomy LOG_DEBUG/INFO/WARN/ERROR, makra LOG_D/LOG_I/LOG_W/LOG_E |
| `pipe_comm.h` | Funkcje FIFO: `utworz_fifo_wszystkie()`, `otworz_fifo_kasjer_serwer()`, `wyslij_przez_fifo()`, `odbierz_z_fifo()` dla komunikacji przez named pipes |

### 2.2 Pliki źródłowe (`src/`)

| Plik | Opis |
|------|------|
| `main.c` | Proces główny (~850 linii): parsowanie -t/-n/-T, inicjalizacja IPC, `fork()`+`execl()` dla procesów, wątek monitora z `pthread_mutex_trylock()` i `pthread_cond_timedwait()`, `waitpid(WNOHANG)` dla zombie, handler SIGINT/SIGTERM, raport końcowy |
| `kasjer.c` | Sprzedaż biletów (~190 linii): pętla odbierająca MSG_PROSBA_O_BILET, obliczanie zniżek 25% (dzieci/seniorzy), tworzenie biletów czasowych/dziennych, mutex SEM_IDX_KASA |
| `pracownik1.c` | Stacja dolna (~650 linii): grupowanie turystów (max 4, max 2 rowerzystów), logika opiekun-dziecko, obsługa SIGUSR1/2 (zatrzymanie/wznowienie), O(1) usuwanie z kolejki (swap-with-last) |
| `pracownik2.c` | Stacja górna (~236 linii): rozładunek krzesełek, kierowanie do wyjść (2 wyjścia), losowe zatrzymanie kolei (1/3000), synchronizacja z `semtimedop()` (timeout 3s) |
| `turysta.c` | Cykl turysty (~650 linii): kupno biletu, bramki wejściowe (VIP nieblokujący `sem_probuj_sysv()`), peron, wsiadanie na krzesełko, zjazd trasą T1/T2/T3, obsługa trybu turbo |
| `ipc_utils.c` | Implementacja IPC (~524 linii): `semget()`/`semop()`/`semtimedop()`, `shmget()`/`shmat()`/`shmdt()`, `msgget()`/`msgsnd()`/`msgrcv()` z obsługą błędów i timeoutów |
| `logger.c` | Logowanie asynchroniczne (~344 linii): wątek zapisu (`pthread_detach()`), bufor cykliczny 100 wpisów, `flock()` do synchronizacji pliku, generowanie raportu (`creat()`) |
| `pipe_comm.c` | Obsługa FIFO (~203 linii): `mkfifo()` z 0666, `open()` blokujący, `read()`/`write()` struktur komunikatów |

### 2.3 Pozostałe katalogi i pliki

- **`bin/`** - Pliki wykonywalne (generowane przez `make`)
- **`logs/`** - Logi z wykonania symulacji
- **`test_results/`**, **`ipc_tests/`**, **`deadlock_tests/`** - Wyniki testów
- **`Makefile`** - Konfiguracja kompilacji
- **`*.sh`** - Skrypty testowe (test_suite, test_ipc_mechanisms, run_deadlock_tests)

---

## 3. Implementacja i algorytmy

### 3.1 Mechanizmy synchronizacji i IPC

**Semafory System V (17 semaforów):**

| Indeks | Nazwa | Funkcja |
|--------|-------|---------|
| 0 | `SEM_IDX_STACJA_DOLNA` | Limit osób na stacji (counting semaphore, N=MAX_OSOB_NA_STACJI) |
| 1 | `SEM_IDX_PERON` | Sygnalizacja wejścia na peron |
| 2 | `SEM_IDX_KRZESELKA` | Liczba dostępnych krzesełek (N=36) |
| 3 | `SEM_IDX_KASA` | Mutex ochrony kasy |
| 4 | `SEM_IDX_REJESTR` | Mutex ochrony rejestru przejść |
| 5 | `SEM_IDX_STAN` | Mutex stanu współdzielonego |
| 6-7 | `SEM_IDX_PRACOWNIK1/2` | Sygnalizacja dla pracowników |
| 8 | `SEM_IDX_SYNC` | Synchronizacja zatrzymania kolei |
| 9 | `SEM_IDX_VIP` | Priorytet VIP |
| 10-13 | `SEM_IDX_BRAMKA_WEJ_*` | 4 bramki wejściowe |
| 14-16 | `SEM_IDX_BRAMKA_PER_*` | 3 bramki peronowe |

**Pamięć współdzielona:**

Struktura `StanWspoldzielony` zawiera:
- Flagi systemowe (`kolej_aktywna`, `kolej_zatrzymana`, `godziny_pracy`)
- Liczniki (osoby na stacji, na peronie, aktywne krzesełka)
- Stan bramek i krzesełek
- Rejestr przejść (max 2000 wpisów)
- Statystyki (zjazdy, sprzedane bilety)
- PID-y pracowników i flagi gotowości

**Kolejki komunikatów (4 kolejki):**
- `MQ_KEY_KASA` (0x5001) - prośby/odpowiedzi dot. biletów
- `MQ_KEY_BRAMKI` (0x5002) - komunikaty bramek
- `MQ_KEY_PRACOWNICY` (0x5003) - prośby o peron, zatrzymanie/wznowienie kolei (4MB)
- `MQ_KEY_KRZESLA` (0x5004) - komunikaty krzesełek (4MB)

**FIFO (Named Pipes):**
```
/tmp/kolej_kasjer_req   - prośby do kasjera
/tmp/kolej_kasjer_resp  - odpowiedzi kasjera
/tmp/kolej_prac_sync    - synchronizacja pracowników
/tmp/kolej_raport       - dane do raportu
```
FIFO są tworzone (`mkfifo()`) i usuwane (`unlink()`) przez proces główny. Stanowią dodatkowy kanał komunikacji obok kolejek komunikatów System V.

**Wątki:**
- `watek_monitor_funkcja()` - wyświetla stan co 500ms (`pthread_mutex_trylock()`, `pthread_cond_timedwait()`)
- `watek_statystyk_funkcja()` - zapisuje statystyki do pliku (`pthread_detach()`, `pthread_cond_timedwait()`)
- Wątek asynchronicznych logów w `logger.c` - zapis z bufora cyklicznego (`pthread_cond_wait()`, `pthread_cond_signal()`)

### 3.2 Algorytm grupowania na krzesełka

```
FUNKCJA moze_dolaczyc(turysta, aktualna_grupa):
    // Sprawdź limit osób
    JEŚLI grupa.liczba >= 4:
        ZWRÓĆ FALSE

    // Reguły dla rowerzystów
    JEŚLI turysta.typ == ROWERZYSTA:
        JEŚLI grupa.liczba_rowerzystow >= 2:
            ZWRÓĆ FALSE
        JEŚLI grupa.liczba_rowerzystow == 1 ORAZ grupa.liczba > 1:
            ZWRÓĆ FALSE  // 1 rowerzysta + max 1 pieszy

    // Reguły dla dzieci (4-8 lat)
    JEŚLI turysta.dziecko_pod_opieka:
        JEŚLI opiekun_w_grupie(turysta.opiekun_id):
            ZWRÓĆ TRUE
        W_PRZECIWNYM_RAZIE:
            czekaj_na_opiekuna(timeout=10s)

    ZWRÓĆ TRUE
```

### 3.3 Mechanizm zatrzymania kolei

```
PRACOWNIK2:
    JEŚLI losowo(1/3000) I kolej_działa:
        1. Ustaw stan.kolej_zatrzymana = TRUE
        2. Wyślij SIGUSR1 do pracownika1
        3. Czekaj na potwierdzenie (semafor z timeout 2s)
        4. Wyślij MSG_WZNOW_KOLEJ do kolejki pracowników
        5. Czekaj na gotowość pracownika1 (semafor z timeout 3s)

PRACOWNIK1 (obsługa SIGUSR1):
    1. Ustaw p1_kolej_zatrzymana = TRUE
    2. Wstrzymaj załadunek

PRACOWNIK1 (obsługa MSG_WZNOW_KOLEJ):
    1. Ustaw pracownik1_gotowy = TRUE
    2. Sygnalizuj semafor SEM_IDX_SYNC

PRACOWNIK2 (wznowienie):
    1. Po otrzymaniu potwierdzenia lub timeout
    2. Ustaw stan.kolej_zatrzymana = FALSE
    3. Wyślij SIGUSR2 do pracownika1
```

### 3.4 Protokół VIP

```
FUNKCJA przejdz_bramke_wejsciowa():
    JEŚLI ja.vip:
        sem_czekaj(SEM_IDX_VIP)           // Priorytet - mutex VIP
        sem_probuj(SEM_IDX_STACJA_DOLNA)  // Nieblokujące - wchodzi niezależnie od wyniku
        sem_sygnalizuj(SEM_IDX_VIP)
        LOG "Wchodzę na stację bez kolejki"
    W_PRZECIWNYM_RAZIE:
        sem_czekaj(SEM_IDX_STACJA_DOLNA)  // Blokujące - czeka na wolne miejsce

    // Znajdź wolną bramkę
    DLA każdej bramki i = 0..3:
        JEŚLI sem_probuj(SEM_IDX_BRAMKA_WEJ + i):
            rejestruj_przejscie(bramka=i)
            sem_sygnalizuj(SEM_IDX_BRAMKA_WEJ + i)
            ZWRÓĆ
```

---

## 4. Problemy i rozwiązania

**Problem 1: Procesy zombie**
- *Opis*: Procesy turystów po zakończeniu pozostawały jako zombie, zajmując zasoby systemu.
- *Rozwiązanie*: Główna pętla w `main.c` używa `waitpid(-1, &status, WNOHANG)` do nieblokującego zbierania zakończonych procesów potomnych.

**Problem 2: Zakleszczenie przy zatrzymaniu kolei**
- *Opis*: Pracownik2 wysyłał sygnał zatrzymania, ale pracownik1 mógł być zablokowany na semaforze.
- *Rozwiązanie*: Implementacja `sem_czekaj_timeout_sysv()` z użyciem `semtimedop()`. Timeout 3s zapobiega nieskończonemu oczekiwaniu.

**Problem 3: Busy-waiting**
- *Opis*: Pętle główne procesów mogły zużywać 100% CPU.
- *Rozwiązanie*: Wszystkie pętle używają `select()` z timeoutem zamiast aktywnego czekania. Wątki używają `pthread_cond_timedwait()`.

---

## 5. Elementy wyróżniające

**Tryb TURBO:**
Flaga `-T` przyspiesza symulację 2-10x poprzez:
- Szybsze generowanie turystów
- Skrócone czasy przejazdów
- Mniejsze pauzy w pętlach
- Przydatne do testowania i demonstracji

**System logowania z wątkami:**
- Asynchroniczny zapis logów nie blokuje głównej logiki
- Bufor cykliczny (100 wpisów)
- `pthread_detach()` - wątek nie wymaga join()
- `flock()` do synchronizacji dostępu do pliku

**Monitoring w czasie rzeczywistym:**
```
[Czas: 45s] Stacja: 12 | Peron: 8 | Krzesełka: 5 | Zjazdy: 127 | Bilety: 89 | Procesy: 312
```

**Kompleksowe testy:**
- 16 testów funkcjonalnych (test_suite.sh)
- Testy mechanizmów IPC
- Testy wykrywania deadlocków
- Monitoring zasobów (CPU, RAM, semafory)

---

## 6. Testy

Projekt zawiera trzy zestawy testów uruchamiane przez `./run_all_tests.sh`:

### 6.1a Test Suite (`test_suite.sh`) - 16 testów funkcjonalnych

```bash
./test_suite.sh --all    # Wszystkie testy
./test_suite.sh          # Menu interaktywne
```

| Test | Nazwa | Opis |
|------|-------|------|
| 01 | Podstawowe uruchomienie | 10s, 5 turystów - weryfikacja startu |
| 02 | Średnie obciążenie | 30s, 50 turystów - monitoring zasobów |
| 03 | Duże obciążenie | 60s, 200 turystów - skalowalność |
| 04 | Ekstremalny test | 90s, 500 turystów - wykrywanie deadlocków |
| 05 | Szybkie generowanie | 30s, 100 turystów - stress test |
| 06 | Długotrwałe działanie | 120s, 150 turystów - wycieki pamięci |
| 07 | Limity zasobów | Sprawdzenie limitów systemowych IPC |
| 08 | Obsługa sygnałów | Test SIGINT/SIGTERM |
| 09 | Czyszczenie IPC | Weryfikacja usuwania zasobów |
| 10 | Procesy zombie | Sprawdzenie waitpid() |
| 11 | Synchronizacja dziecko-opiekun | Logika rodzin |
| 12 | Max dzieci na opiekuna | Limit 2 dzieci |
| 13 | Priorytet VIP | Wejście bez kolejki |
| 14 | Pojemność krzesełka | Max 4 osoby, max 2 rowerzystów |
| 15 | Limit stacji | Max N osób na stacji dolnej |
| 16 | Zamknięcie kolei | Transport pozostałych osób |

### 6.1b Test IPC Mechanisms (`test_ipc_mechanisms.sh`) - 10 testów

```bash
./test_ipc_mechanisms.sh --all    # Wszystkie testy
./test_ipc_mechanisms.sh          # Menu interaktywne
```

| Test | Nazwa | Opis |
|------|-------|------|
| 1 | Semafory System V | Weryfikacja operacji P/V, inicjalizacji |
| 2 | Pamięć współdzielona | Tworzenie segmentu, wieloprocesowy dostęp |
| 3 | Kolejki komunikatów | msgsnd/msgrcv, przepustowość |
| 4 | Obsługa sygnałów | SIGTERM, SIGINT, SIGUSR1/2 |
| 5 | Synchronizacja procesów | Wzajemne wykluczanie, race conditions |
| 6 | Operacje blokujące | semtimedop, select, msgrcv |
| 7 | Wątki pthread | pthread_create, mutex, cond_wait |
| 8 | Czyszczenie IPC | Usuwanie zasobów po zakończeniu |
| 9 | Limit stacji | Counting semaphore dla N osób |
| 10 | Synchronizacja rodzin | Atomowe wsiadanie opiekun+dzieci |

### 6.1c Deadlock Tests (`run_deadlock_tests.sh`) - 8 testów

```bash
./run_deadlock_tests.sh    # Automatyczne uruchomienie
```

| Test | Nazwa | Opis |
|------|-------|------|
| 01 | Wysoka współbieżność | 100 turystów, szybkie generowanie |
| 02 | Ekstremalne obciążenie | 300 turystów |
| 03 | Szybkie generowanie | 200 turystów w krótkim czasie |
| 04 | Długotrwała stabilność | 150s, 150 turystów |
| 05 | Stress test | 500 turystów - maksymalne obciążenie |
| 06 | Powtarzalność | 3 uruchomienia po 40s |
| 07 | Wyczerpanie zasobów IPC | Test limitów systemowych |
| 08 | Odporność na sygnały | Sygnały podczas obciążenia |

**Algorytm detekcji deadlocka:**
- Monitorowanie postępu w logach
- Sprawdzanie CPU time procesów
- Analiza stanów procesów (D - uninterruptible, Z - zombie)
- Monitorowanie semaforów i kolejek komunikatów

### 6.1d Uruchomienie wszystkich testów

```bash
./run_all_tests.sh
```

Uruchamia sekwencyjnie:
1. `test_suite.sh --all` (~15-20 min)
2. `test_ipc_mechanisms.sh --all` (~10-15 min)
3. `run_deadlock_tests.sh` (~15-20 min)

**Całkowity czas:** ~45-60 minut

**Wyniki zapisywane w:**
- `test_results/` - logi test_suite
- `ipc_tests/` - logi testów IPC
- `deadlock_tests/results/` - logi testów deadlock
- `all_tests_results/` - zbiorcze raporty

### 6.2 Testy Logiczne - Manualne (5000 turystów)

Testy stresowe sprawdzają czy system poprawnie obsługuje dużą liczbę turystów w sytuacjach gdy jeden z komponentów jest zablokowany. Turysty powinny cierpliwie czekać (stan Sleeping na blokującym `msgrcv`), a NIE być zabijane.

#### Przygotowanie testu

Sleepy testowe są już wpisane w kod i **zakomentowane**. Aby przetestować blokadę danego komponentu, wystarczy odkomentować odpowiedni fragment:

| Komponent | Plik | Linia | Co odkomentować |
|-----------|------|-------|-----------------|
| Kasjer | `src/kasjer.c` | 156 | `/*sleep(30);*/` → `sleep(30);` |
| Pracownik1 | `src/pracownik1.c` | 531-540 | Pętla z `select()` odporna na sygnały (40s) |
| Pracownik2 | `src/pracownik2.c` | 155 | `/*sleep(30);*/` → `sleep(30);` |

**Uwaga:** Pracownik1 używa pętli `select()` zamiast surowego `sleep()`, ponieważ `sleep()` jest przerywany przez sygnały SIGUSR1/SIGUSR2 (zatrzymanie/wznowienie kolei). Pętla sprawdza czas co 1s i kontynuuje do upływu 40s niezależnie od otrzymanych sygnałów.

#### Przeprowadzenie testu

**Terminal 1 - Uruchomienie programu:**
```bash
make clean && make
./bin/main -t 9999 -n 5000
```

**Terminal 2 - Monitorowanie procesów:**
Liczba żywych procesów jest widoczna w linii statusu monitora (pole "Procesy"). Alternatywnie:
```bash
MAX=0; while true; do T=$(pgrep turysta | wc -l); [ $T -gt $MAX ] && MAX=$T; echo "Żyje: $T | Max: $MAX | Zabito: $((MAX-T))"; sleep 2; done
```

#### Oczekiwane wyniki

| Test | Blokada | Gdzie czekają turysty | Oczekiwany wynik |
|------|---------|----------------------|------------------|
| 1 | Kasjer | Kolejka do kasy (przed stacją) | Stacja: 0, Bilety: 0 |
| 2 | Pracownik1 | Na stacji (po kasie, przed peronem) | Stacja: rośnie, Krzesełka: 0 |
| 3 | Pracownik2 | Na peronie + krzesełkach (góra) | Krzesełka: zablokowane, Stacja: rośnie |

**Cel:** Weryfikacja że procesy NIE są zabijane - turyści cierpliwie czekają w stanie Sleeping.

#### Weryfikacja stanu procesów

```bash
# Sprawdź stan procesów (S = Sleeping = OK)
ps aux | grep turysta | head -5

# Sprawdź procesy zombie (powinno być 0)
ps aux | grep defunct | grep -v grep | wc -l
```

---

## 7. Funkcje systemowe i linki do kodu

### Tworzenie i obsługa plików
- `creat()`: [src/logger.c:273](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L273) - Utworzenie pliku raportu
- `open()`: [src/logger.c:140](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L140) - Otwarcie pliku logu
- `open()`: [src/main.c:64](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L64) - Otwarcie pliku statystyk (O_CREAT | O_WRONLY | O_TRUNC)
- `write()`: [src/logger.c:69](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L69) - Zapis logu do pliku
- `close()`: [src/logger.c:130](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L130) - Zamknięcie pliku logu
- `flock()`: [src/logger.c:68](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L68) - Blokada pliku (LOCK_EX/LOCK_UN)

### Tworzenie procesów
- `fork()`: [src/main.c:174](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L174) - Tworzenie procesu kasjera
- `fork()`: [src/main.c:195](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L195) - Tworzenie procesu pracownika
- `fork()`: [src/main.c:212](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L212) - Tworzenie procesu turysty
- `execl()`: [src/main.c:182](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L182) - Uruchomienie programu kasjera
- `execl()`: [src/main.c:203](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L203) - Uruchomienie programu pracownika
- `execl()`: [src/main.c:237](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L237) - Uruchomienie programu turysty
- `waitpid()`: [src/main.c:798](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L798) - Zbieranie procesów zombie (WNOHANG)
- `waitpid()`: [src/main.c:303](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L303) - Zbieranie przy zamykaniu
- `dup2()`: [src/main.c:225](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L225) - Przekierowanie stderr turysty do pliku

### Wątki
- `pthread_create()`: [src/main.c:720](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L720) - Tworzenie wątku monitora
- `pthread_create()`: [src/main.c:726](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L726) - Tworzenie wątku statystyk
- `pthread_create()`: [src/logger.c:105](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L105) - Tworzenie wątku logowania
- `pthread_join()`: [src/main.c:343](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L343) - Oczekiwanie na wątek monitora
- `pthread_detach()`: [src/main.c:48](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L48) - Odłączenie wątku statystyk
- `pthread_cond_timedwait()`: [src/main.c:58](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L58) - Czekanie z timeoutem (statystyki)
- `pthread_cond_timedwait()`: [src/main.c:134](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L134) - Czekanie z timeoutem (monitor)
- `pthread_mutex_trylock()`: [src/main.c:94](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L94) - Nieblokująca próba blokady mutex (monitor)
- `pthread_mutex_lock()`: [src/main.c:57](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L57) - Blokada mutex statystyk
- `pthread_mutex_lock()`: [src/logger.c:51](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L51) - Blokada mutex bufora logów
- `pthread_cond_wait()`: [src/logger.c:86](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L86) - Czekanie na wpisy do zapisu
- `pthread_cond_signal()`: [src/logger.c:73](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L73) - Sygnalizacja nowego wpisu

### Obsługa sygnałów
- `sigaction()`: [src/main.c:161](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L161) - Rejestracja handlera SIGINT
- `sigaction()`: [src/main.c:164](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L164) - Rejestracja handlera SIGTERM
- `sigaction()`: [src/pracownik1.c:145](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pracownik1.c#L145) - Obsługa SIGUSR1/2 pracownika1
- `sigaction()`: [src/turysta.c:32](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/turysta.c#L32) - Obsługa sygnałów turysty
- `kill()`: [src/main.c:286](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L286) - Wysyłanie SIGTERM do turystów
- `kill()`: [src/pracownik2.c:127](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pracownik2.c#L127) - Wysyłanie SIGUSR1 do pracownika1 (zatrzymanie)
- `kill()`: [src/pracownik2.c:221](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pracownik2.c#L221) - Wysyłanie SIGUSR2 do pracownika1 (wznowienie)

### Semafory System V
- `semget()`: [src/ipc_utils.c:113](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L113) - Utworzenie zestawu semaforów (IPC_CREAT | IPC_EXCL)
- `semctl()`: [src/ipc_utils.c:147](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L147) - Inicjalizacja wartości (SETALL)
- `semctl()`: [src/ipc_utils.c:62](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L62) - Odczyt wartości (GETVAL)
- `semop()`: [src/ipc_utils.c:21](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L21) - Operacja P (wait/czekaj)
- `semop()`: [src/ipc_utils.c:40](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L40) - Operacja V (signal/sygnalizuj)
- `semop()`: [src/ipc_utils.c:51](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L51) - Operacja P nieblokująca (IPC_NOWAIT)
- `semtimedop()`: [src/ipc_utils.c:81](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L81) - Wait z timeoutem

### Pamięć współdzielona
- `ftok()`: [src/ipc_utils.c:99](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L99) - Generowanie klucza (semafory)
- `ftok()`: [src/ipc_utils.c:189](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L189) - Generowanie klucza (pamięć)
- `shmget()`: [src/ipc_utils.c:202](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L202) - Utworzenie segmentu (IPC_CREAT | IPC_EXCL)
- `shmat()`: [src/ipc_utils.c:210](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L210) - Dołączenie pamięci
- `shmdt()`: [src/ipc_utils.c:281](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L281) - Odłączenie
- `shmctl()`: [src/ipc_utils.c:285](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L285) - Usunięcie segmentu (IPC_RMID)

### Kolejki komunikatów
- `msgget()`: [src/ipc_utils.c:329](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L329) - Utworzenie kolejki kasy (IPC_CREAT | IPC_EXCL)
- `msgctl()`: [src/ipc_utils.c:294](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L294) - Odczyt/zmiana rozmiaru kolejki (IPC_STAT/IPC_SET)
- `msgsnd()`: [src/ipc_utils.c:443](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L443) - Wysłanie komunikatu (IPC_NOWAIT z retry)
- `msgrcv()`: [src/ipc_utils.c:470](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L470) - Odbiór komunikatu (blokujący)
- `msgrcv()`: [src/ipc_utils.c:482](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L482) - Odbiór komunikatu (nieblokujący, IPC_NOWAIT)
- `msgctl()`: [src/ipc_utils.c:382](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L382) - Usunięcie kolejek (IPC_RMID)

### FIFO (Named Pipes) i Potoki
- `mkfifo()`: [src/pipe_comm.c:20](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pipe_comm.c#L20) - Utworzenie FIFO kasjer_request
- `open()`: [src/pipe_comm.c:54](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pipe_comm.c#L54) - Otwarcie FIFO (serwer)
- `read()`: [src/pipe_comm.c:142](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pipe_comm.c#L142) - Odczyt z potoku
- `write()`: [src/pipe_comm.c:127](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pipe_comm.c#L127) - Zapis do potoku
- `unlink()`: [src/pipe_comm.c:40](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pipe_comm.c#L40) - Usunięcie FIFO
- `popen()`: [src/main.c:392](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L392) - Sprawdzenie zasobów IPC (ipcs -a)
- `pclose()`: [src/main.c:415](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L415) - Zamknięcie popen

### Obsługa błędów
- `perror()`: [src/pipe_comm.c:21](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pipe_comm.c#L21) - Wypisanie błędu systemowego
- `errno`: [src/ipc_utils.c:22](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L22) - Sprawdzanie kodu błędu (EINTR)
- `errno`: [src/ipc_utils.c:84](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L84) - Sprawdzanie timeout (EAGAIN/ETIMEDOUT)
- `strtol()`: [src/turysta.c:475](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/turysta.c#L475) - Bezpieczne parsowanie argumentów (zamiast atoi)

### Inne
- `select()`: [src/main.c:813](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L813) - Czekanie bez busy-waiting (pętla główna)
- `select()`: [src/turysta.c:599](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/turysta.c#L599) - Blokujące czekanie turysty (jazda)
- `time()`: [src/turysta.c:66](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/turysta.c#L66) - Znaczniki czasowe
- `clock_gettime()`: [src/main.c:128](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L128) - Czas dla pthread_cond_timedwait

---

## 8. Podsumowanie

Projekt demonstruje kompleksowe wykorzystanie mechanizmów IPC w systemie Linux:

| Mechanizm | Zastosowanie |
|-----------|--------------|
| **Semafory (17)** | Mutex, counting semaphores, synchronizacja, nieblokujące (IPC_NOWAIT) |
| **Pamięć współdzielona** | Stan systemu, statystyki, rejestr przejść, flagi pracowników |
| **Kolejki komunikatów (4)** | Komunikacja kasjer-turysta, pracownicy, prośby o peron |
| **FIFO** | Dodatkowy kanał komunikacji (named pipes) |
| **Wątki (3)** | Monitor, statystyki, asynchroniczne logi |
| **Sygnały (4)** | SIGINT, SIGTERM, SIGUSR1, SIGUSR2 |
| **Procesy (5+)** | main, kasjer, 2 pracowników, N turystów |

**Kluczowe cechy:**
- Brak busy-waiting (select(), pthread_cond_timedwait())
- Obsługa timeoutów zapobiegająca deadlockom (semtimedop())
- Kompleksowy system testów (16+ testów)
- Tryb turbo do szybkiego testowania
- Szczegółowe logowanie i raportowanie
- O(1) operacje na kolejce turystów (swap-with-last)
