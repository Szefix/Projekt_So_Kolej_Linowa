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
| `-n LICZBA` | Maksymalna liczba turystów (1-500, domyślnie 100) |
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
| `config.h` | Stałe konfiguracyjne: liczba krzesełek (72), bramek (4+3), limit osób na stacji (50), czasy tras, ceny biletów, klucze IPC (SHM_KEY, MQ_KEY_*) |
| `types.h` | Struktury: `Turysta` (id, wiek, typ, bilet, status), `Bilet` (typ, ważność, VIP), `Krzeselko`, `Bramka`, `StanWspoldzielony` (flagi, liczniki, rejestr), `Komunikat` (mtype, dane) |
| `ipc_utils.h` | Deklaracje funkcji IPC: `sem_czekaj()`, `sem_sygnalizuj()`, `sem_czekaj_timeout_sysv()`, `shm_utworz()`, `mq_wyslij()`, `mq_odbierz_timeout()` oraz indeksy 17 semaforów |
| `logger.h` | Interfejs logowania: `logger_init()`, `logger_log()`, poziomy LOG_DEBUG/INFO/WARN/ERROR, makra upraszczające |
| `pipe_comm.h` | Funkcje FIFO: `fifo_utworz()`, `fifo_otworz()`, `fifo_wyslij()`, `fifo_odbierz()` dla komunikacji przez named pipes |

### 2.2 Pliki źródłowe (`src/`)

| Plik | Opis |
|------|------|
| `main.c` | Proces główny (~900 linii): parsowanie -t/-n/-T, inicjalizacja IPC, `fork()`+`execl()` dla procesów, wątek monitora z `pthread_cond_timedwait()`, `waitpid(WNOHANG)` dla zombie, handler SIGINT/SIGTERM, raport końcowy |
| `kasjer.c` | Sprzedaż biletów (~300 linii): pętla odbierająca MSG_PROSBA_O_BILET, obliczanie zniżek 25% (dzieci/seniorzy), tworzenie biletów czasowych/dziennych, mutex SEM_IDX_KASA |
| `pracownik1.c` | Stacja dolna (~450 linii): grupowanie turystów (max 4, max 2 rowerzystów), logika opiekun-dziecko, obsługa SIGUSR1/2 (zatrzymanie/wznowienie), komunikacja z pracownikiem2 |
| `pracownik2.c` | Stacja górna (~350 linii): rozładunek krzesełek, kierowanie do wyjść, losowe zatrzymanie kolei (1/3000), synchronizacja z `semtimedop()` (timeout 3s) |
| `turysta.c` | Cykl turysty (~500 linii): kupno biletu, bramki wejściowe (priorytet VIP), peron, wsiadanie na krzesełko, zjazd trasą T1/T2/T3, obsługa trybu turbo |
| `ipc_utils.c` | Implementacja IPC (~400 linii): `semget()`/`semop()`/`semtimedop()`, `shmget()`/`shmat()`/`shmdt()`, `msgget()`/`msgsnd()`/`msgrcv()` z obsługą błędów i timeoutów |
| `logger.c` | Logowanie asynchroniczne (~250 linii): wątek zapisu (`pthread_detach()`), bufor cykliczny 100 wpisów, `flock()` do synchronizacji pliku |
| `pipe_comm.c` | Obsługa FIFO (~150 linii): `mkfifo()` z 0666, `open()` blokujący, `read()`/`write()` struktur komunikatów |

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
| 0 | `SEM_IDX_STACJA_DOLNA` | Limit osób na stacji (counting semaphore, N=50) |
| 1 | `SEM_IDX_PERON` | Sygnalizacja wejścia na peron |
| 2 | `SEM_IDX_KRZESELKA` | Liczba dostępnych krzesełek |
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

**Kolejki komunikatów (4 kolejki):**
- `MQ_KEY_KASA` (0x5001) - prośby/odpowiedzi dot. biletów
- `MQ_KEY_BRAMKI` (0x5002) - komunikaty bramek
- `MQ_KEY_PRACOWNICY` (0x5003) - zatrzymanie/wznowienie kolei
- `MQ_KEY_KRZESLA` (0x5004) - komunikaty krzesełek

**FIFO (Named Pipes):**
```
/tmp/kolej_kasjer_req   - prośby do kasjera
/tmp/kolej_kasjer_resp  - odpowiedzi kasjera
/tmp/kolej_prac_sync    - synchronizacja pracowników
/tmp/kolej_raport       - dane do raportu
```

**Wątki:**
- `watek_monitor_funkcja()` - wyświetla stan co 500ms (`pthread_cond_timedwait()`)
- `watek_statystyk_funkcja()` - zapisuje statystyki do pliku (`pthread_detach()`)
- `watek_zapis_logow()` - asynchroniczny zapis logów z buforem cyklicznym

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
        3. Czekaj na potwierdzenie (semafor z timeout 3s)

PRACOWNIK1 (obsługa SIGUSR1):
    1. Ustaw p1_kolej_zatrzymana = TRUE
    2. Wstrzymaj załadunek
    3. Wyślij MSG_GOTOWY do pracownika2

PRACOWNIK2 (wznowienie):
    1. Po otrzymaniu MSG_GOTOWY
    2. Wyślij SIGUSR2 do pracownika1
    3. Ustaw stan.kolej_zatrzymana = FALSE
```

### 3.4 Protokół VIP

```
FUNKCJA przejdz_bramke_wejsciowa():
    JEŚLI ja.vip:
        sem_czekaj(SEM_IDX_VIP)      // Priorytet
        sem_czekaj(SEM_IDX_STACJA_DOLNA)
        sem_sygnalizuj(SEM_IDX_VIP)
    W_PRZECIWNYM_RAZIE:
        sem_czekaj(SEM_IDX_STACJA_DOLNA)

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
[Czas: 45s] Stacja: 12 | Peron: 8 | Krzesełka: 5 | Zjazdy: 127 | Bilety: 89
```

**Kompleksowe testy:**
- 16 testów funkcjonalnych (test_suite.sh)
- Testy mechanizmów IPC
- Testy wykrywania deadlocków
- Monitoring zasobów (CPU, RAM, semafory)

---

## 6. Testy

Projekt zawiera trzy zestawy testów uruchamiane przez `./run_all_tests.sh`:

### 6.1 Test Suite (`test_suite.sh`) - 16 testów funkcjonalnych

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

### 6.2 Test IPC Mechanisms (`test_ipc_mechanisms.sh`) - 10 testów

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

### 6.3 Deadlock Tests (`run_deadlock_tests.sh`) - 8 testów

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

### 6.4 Uruchomienie wszystkich testów

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

---

## 7. Funkcje systemowe i linki do kodu

## 7. Funkcje systemowe i linki do kodu

### Tworzenie i obsługa plików
- `open()`: [src/logger.c:139](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L139) - Otwarcie pliku logu
- `write()`: [src/logger.c:206](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L206) - Zapis logu
- `close()`: [src/logger.c:161](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L161) - Zamknięcie pliku
- `flock()`: [src/logger.c:175](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L175) - Blokada pliku

### Tworzenie procesów
- `fork()`: [src/main.c:162](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L162) - Tworzenie procesu kasjera
- `fork()`: [src/main.c:200](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L200) - Tworzenie procesu turysty
- `execl()`: [src/main.c:170](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L170) - Uruchomienie programu kasjera
- `execl()`: [src/main.c:225](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L225) - Uruchomienie programu turysty
- `waitpid()`: [src/main.c:786](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L786) - Zbieranie procesów zombie
- `dup2()`: [src/main.c:213](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L213) - Przekierowanie stderr

### Wątki
- `pthread_create()`: [src/main.c:708](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L708) - Tworzenie wątku monitora
- `pthread_create()`: [src/main.c:714](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L714) - Tworzenie wątku statystyk
- `pthread_create()`: [src/logger.c:104](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L104) - Tworzenie wątku logowania
- `pthread_join()`: [src/main.c:331](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L331) - Oczekiwanie na wątek monitora
- `pthread_detach()`: [src/main.c:48](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L48) - Odłączenie wątku statystyk
- `pthread_cond_timedwait()`: [src/main.c:58](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L58) - Czekanie z timeoutem (statystyki)
- `pthread_cond_timedwait()`: [src/main.c:122](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L122) - Czekanie z timeoutem (monitor)
- `pthread_mutex_lock()`: [src/main.c:57](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L57) - Blokada mutex
- `pthread_mutex_lock()`: [src/logger.c:50](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/logger.c#L50) - Blokada mutex bufora

### Obsługa sygnałów
- `sigaction()`: [src/main.c:149](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L149) - Rejestracja handlera SIGINT
- `sigaction()`: [src/pracownik1.c:81](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pracownik1.c#L81) - Obsługa SIGUSR1/2 pracownika
- `sigaction()`: [src/turysta.c:31](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/turysta.c#L31) - Obsługa sygnałów turysty
- `kill()`: [src/main.c:274](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L274) - Wysyłanie SIGTERM do turystów
- `kill()`: [src/pracownik2.c:117](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pracownik2.c#L117) - Wysyłanie SIGUSR1 (zatrzymanie)

### Semafory System V
- `semget()`: [src/ipc_utils.c:113](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L113) - Utworzenie zestawu semaforów
- `semctl()`: [src/ipc_utils.c:147](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L147) - Inicjalizacja wartości (SETALL)
- `semctl()`: [src/ipc_utils.c:62](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L62) - Odczyt wartości (GETVAL)
- `semop()`: [src/ipc_utils.c:21](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L21) - Operacja P (wait/czekaj)
- `semop()`: [src/ipc_utils.c:40](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L40) - Operacja V (signal/sygnalizuj)
- `semtimedop()`: [src/ipc_utils.c:81](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L81) - Wait z timeoutem

### Pamięć współdzielona
- `ftok()`: [src/ipc_utils.c:99](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L99) - Generowanie klucza (semafory)
- `ftok()`: [src/ipc_utils.c:189](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L189) - Generowanie klucza (pamięć)
- `shmget()`: [src/ipc_utils.c:202](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L202) - Utworzenie segmentu
- `shmat()`: [src/ipc_utils.c:210](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L210) - Dołączenie pamięci
- `shmdt()`: [src/ipc_utils.c:281](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L281) - Odłączenie
- `shmctl()`: [src/ipc_utils.c:285](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L285) - Usunięcie segmentu

### Kolejki komunikatów
- `msgget()`: [src/ipc_utils.c:329](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L329) - Utworzenie kolejki kasy
- `msgsnd()`: [src/ipc_utils.c:443](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L443) - Wysłanie komunikatu
- `msgrcv()`: [src/ipc_utils.c:471](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L471) - Odbiór komunikatu (blokujący)
- `msgrcv()`: [src/ipc_utils.c:483](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L483) - Odbiór komunikatu (nieblokujący)
- `msgctl()`: [src/ipc_utils.c:382](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L382) - Usunięcie kolejki

### FIFO (Named Pipes)
- `mkfifo()`: [src/pipe_comm.c:20](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pipe_comm.c#L20) - Utworzenie FIFO kasjer_request
- `open()`: [src/pipe_comm.c:54](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pipe_comm.c#L54) - Otwarcie FIFO (serwer)
- `popen()`: [src/main.c:380](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L380) - Sprawdzenie zasobów IPC
- `pclose()`: [src/main.c:403](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L403) - Zamknięcie popen

### Obsługa błędów
- `perror()`: [src/pipe_comm.c:21](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/pipe_comm.c#L21) - Wypisanie błędu systemowego
- `errno`: [src/ipc_utils.c:22](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L22) - Sprawdzanie kodu błędu (EINTR)
- `errno`: [src/ipc_utils.c:84](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/ipc_utils.c#L84) - Sprawdzanie timeout (EAGAIN/ETIMEDOUT)

### Inne
- `select()`: [src/main.c:801](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/main.c#L801) - Czekanie bez busy-waiting
- `select()`: [src/turysta.c:457](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/turysta.c#L457) - Blokujące czekanie turysty
- `time()`: [src/turysta.c:65](https://github.com/Szefix/Projekt_So_Kolej_Linowa/blob/main/src/turysta.c#L65) - Znaczniki czasowe
---

## 8. Podsumowanie

Projekt demonstruje kompleksowe wykorzystanie mechanizmów IPC w systemie Linux:

| Mechanizm | Zastosowanie |
|-----------|--------------|
| **Semafory (17)** | Mutex, counting semaphores, synchronizacja |
| **Pamięć współdzielona** | Stan systemu, statystyki, rejestr |
| **Kolejki komunikatów (4)** | Komunikacja kasjer-turysta, pracownicy |
| **FIFO** | Dodatkowy kanał komunikacji |
| **Wątki (3)** | Monitor, statystyki, asynchroniczne logi |
| **Sygnały (4)** | SIGINT, SIGTERM, SIGUSR1, SIGUSR2 |
| **Procesy (5+)** | main, kasjer, 2 pracowników, N turystów |

**Kluczowe cechy:**
- Brak busy-waiting (select(), pthread_cond_timedwait())
- Obsługa timeoutów zapobiegająca deadlockom
- Kompleksowy system testów (16+ testów)
- Tryb turbo do szybkiego testowania
- Szczegółowe logowanie i raportowanie
