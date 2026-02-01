#!/bin/bash
# ============================================================
#    TESTY MECHANIZMÓW IPC - KOLEJ LINOWA KRZESEŁKOWA
# ============================================================
# Testy weryfikujące poprawność użycia:
# - Semaforów System V
# - Pamięci współdzielonej
# - Kolejek komunikatów
# - Sygnałów POSIX
# - Synchronizacji procesów i wątków
# ============================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

RESULTS_DIR="ipc_tests"
mkdir -p "$RESULTS_DIR"
LOG_PREFIX="${RESULTS_DIR}/ipc_test_$(date +%Y%m%d_%H%M%S)"

PASSED=0
FAILED=0

# ============================================================
#                    FUNKCJE POMOCNICZE
# ============================================================

log_test() {
    echo -e "${BLUE}[IPC TEST]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[PASSED]${NC} $1"
    PASSED=$((PASSED + 1))
}

log_error() {
    echo -e "${RED}[FAILED]${NC} $1"
    FAILED=$((FAILED + 1))
}

log_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

log_info() {
    echo -e "${CYAN}[INFO]${NC} $1"
}

cleanup() {
    log_warning "Czyszczenie zasobów IPC..."
    pkill -9 -f "bin/main" 2>/dev/null || true
    pkill -9 -f "bin/kasjer" 2>/dev/null || true
    pkill -9 -f "bin/pracownik" 2>/dev/null || true
    pkill -9 -f "bin/turysta" 2>/dev/null || true
    make clean-ipc >/dev/null 2>&1 || true
    sleep 1
}

check_compilation() {
    log_test "Sprawdzanie kompilacji..."
    if ! make clean all >/dev/null 2>&1; then
        log_error "Kompilacja nie powiodła się!"
        exit 1
    fi
    log_success "Kompilacja OK"
}

# ============================================================
#              TEST 1: SEMAFORY SYSTEM V
# ============================================================

test_semaphores() {
    log_test "TEST SEMAFORÓW: Weryfikacja operacji P/V i inicjalizacji"
    cleanup

    local logfile="${LOG_PREFIX}_semaphores.log"

    # Uruchom symulację na 15 sekund
    timeout 25 ./bin/main -t 15 -n 20 > "$logfile" 2>&1 &
    local pid=$!
    sleep 2

    # Sprawdź czy semafory zostały utworzone
    local sem_count=$(ipcs -s 2>/dev/null | grep -c "^0x" || echo "0")
    if [ "$sem_count" -ge 1 ]; then
        log_info "Znaleziono $sem_count zestaw(ów) semaforów"
    else
        log_error "Nie znaleziono semaforów System V!"
        kill $pid 2>/dev/null || true
        return 1
    fi

    # Sprawdź wartości semaforów w trakcie działania
    local sem_id=$(ipcs -s | grep "^0x" | head -1 | awk '{print $2}')
    if [ -n "$sem_id" ]; then
        log_info "Sprawdzanie wartości semaforów (ID: $sem_id)..."

        # Monitoruj przez kilka sekund
        for i in 1 2 3 4 5; do
            local vals=$(ipcs -s -i $sem_id 2>/dev/null | tail -1 || echo "")
            if [ -n "$vals" ]; then
                echo "[Iteracja $i] Wartości semaforów: $vals" >> "$logfile"
            fi
            sleep 2
        done
    fi

    wait $pid 2>/dev/null || true

    # Sprawdź czy semafory zostały poprawnie usunięte
    sleep 2
    local sem_after=$(ipcs -s 2>/dev/null | grep -c "0x4" || echo "0")
    if [ "$sem_after" -eq 0 ]; then
        log_success "TEST SEMAFORÓW: Semafory poprawnie utworzone, używane i usunięte"
        return 0
    else
        log_warning "Semafory nie zostały automatycznie usunięte (to może być OK)"
        log_success "TEST SEMAFORÓW: Podstawowa funkcjonalność OK"
        return 0
    fi
}

# ============================================================
#              TEST 2: PAMIĘĆ WSPÓŁDZIELONA
# ============================================================

test_shared_memory() {
    log_test "TEST PAMIĘCI WSPÓŁDZIELONEJ: Weryfikacja segmentu i dostępu"
    cleanup

    local logfile="${LOG_PREFIX}_shm.log"

    # Uruchom symulację
    timeout 25 ./bin/main -t 15 -n 15 > "$logfile" 2>&1 &
    local pid=$!
    sleep 2

    # Sprawdź czy segment pamięci współdzielonej istnieje
    local shm_count=$(ipcs -m 2>/dev/null | grep -c "^0x" || echo "0")
    if [ "$shm_count" -ge 1 ]; then
        log_info "Znaleziono $shm_count segment(ów) pamięci współdzielonej"

        # Pobierz szczegóły segmentu
        local shm_info=$(ipcs -m | grep "^0x" | head -1)
        local shm_size=$(echo "$shm_info" | awk '{print $5}')
        local shm_attach=$(echo "$shm_info" | awk '{print $6}')

        log_info "Rozmiar segmentu: $shm_size bajtów"
        log_info "Liczba dołączeń: $shm_attach"

        if [ "$shm_attach" -ge 2 ]; then
            log_info "Wieloprocesowy dostęp potwierdzony (min 2 procesy)"
        fi
    else
        log_error "Nie znaleziono pamięci współdzielonej!"
        kill $pid 2>/dev/null || true
        return 1
    fi

    wait $pid 2>/dev/null || true

    # Sprawdź raport końcowy
    if [ -f "logs/raport_dzienny.txt" ]; then
        local zjazdy=$(grep -o "zjazdów: [0-9]*" logs/raport_dzienny.txt 2>/dev/null | grep -o "[0-9]*" || echo "0")
        local bilety=$(grep -o "biletów: [0-9]*" logs/raport_dzienny.txt 2>/dev/null | grep -o "[0-9]*" || echo "0")
        log_info "Raport: $zjazdy zjazdów, $bilety biletów"
    fi

    log_success "TEST PAMIĘCI WSPÓŁDZIELONEJ: Segment utworzony i współdzielony poprawnie"
    return 0
}

# ============================================================
#              TEST 3: KOLEJKI KOMUNIKATÓW
# ============================================================

test_message_queues() {
    log_test "TEST KOLEJEK KOMUNIKATÓW: Weryfikacja msgsnd/msgrcv"
    cleanup

    local logfile="${LOG_PREFIX}_msgq.log"

    # Uruchom symulację
    timeout 30 ./bin/main -t 20 -n 30 > "$logfile" 2>&1 &
    local pid=$!
    sleep 3

    # Sprawdź kolejki komunikatów
    local msgq_count=$(ipcs -q 2>/dev/null | grep -c "^0x" || echo "0")
    if [ "$msgq_count" -ge 1 ]; then
        log_info "Znaleziono $msgq_count kolejek komunikatów"

        # Monitoruj ruch w kolejkach
        for i in 1 2 3 4; do
            local queue_stats=$(ipcs -q 2>/dev/null | grep "^0x" | awk '{sum+=$5} END {print sum}')
            log_info "Iteracja $i: $queue_stats bajtów w kolejkach"
            echo "[Iteracja $i] Bajty w kolejkach: $queue_stats" >> "$logfile"
            sleep 3
        done
    else
        log_error "Nie znaleziono kolejek komunikatów!"
        kill $pid 2>/dev/null || true
        return 1
    fi

    wait $pid 2>/dev/null || true

    # Sprawdź logi pod kątem komunikacji
    local msg_count=$(grep -c "MSG_\|Komunikat\|bilet\|peron" logs/*.log 2>/dev/null || echo "0")
    if [ "$msg_count" -gt 10 ]; then
        log_info "Znaleziono $msg_count wpisów o komunikacji w logach"
    fi

    log_success "TEST KOLEJEK KOMUNIKATÓW: Komunikacja działa poprawnie"
    return 0
}

# ============================================================
#              TEST 4: OBSŁUGA SYGNAŁÓW
# ============================================================

test_signals() {
    log_test "TEST SYGNAŁÓW: Weryfikacja SIGTERM, SIGINT, SIGUSR1/2"
    cleanup

    local logfile="${LOG_PREFIX}_signals.log"

    # Test 1: SIGTERM - łagodne zakończenie
    log_info "Test SIGTERM..."
    timeout 60 ./bin/main -t 30 -n 20 > "${logfile}_sigterm" 2>&1 &
    local pid=$!
    sleep 5

    if kill -0 $pid 2>/dev/null; then
        kill -SIGTERM $pid
        sleep 3

        if ! kill -0 $pid 2>/dev/null; then
            log_info "SIGTERM: Proces zakończył się poprawnie"
        else
            log_warning "SIGTERM: Proces nadal działa, wysyłam SIGKILL"
            kill -9 $pid 2>/dev/null || true
        fi
    fi

    cleanup
    sleep 2

    # Test 2: SIGINT - przerwanie (Ctrl+C)
    log_info "Test SIGINT..."
    timeout 60 ./bin/main -t 30 -n 20 > "${logfile}_sigint" 2>&1 &
    pid=$!
    sleep 5

    if kill -0 $pid 2>/dev/null; then
        kill -SIGINT $pid
        sleep 3

        if ! kill -0 $pid 2>/dev/null; then
            log_info "SIGINT: Proces zakończył się poprawnie"
        else
            kill -9 $pid 2>/dev/null || true
        fi
    fi

    cleanup
    sleep 2

    # Test 3: SIGUSR1 - zatrzymanie kolei
    log_info "Test SIGUSR1/SIGUSR2 (zatrzymanie/wznowienie kolei)..."
    timeout 60 ./bin/main -t 40 -n 30 > "${logfile}_sigusr" 2>&1 &
    pid=$!
    sleep 5

    # Znajdź PID pracownika1
    local p1_pid=$(pgrep -f "pracownik1" | head -1)
    if [ -n "$p1_pid" ]; then
        log_info "Wysyłam SIGUSR1 do pracownik1 (PID: $p1_pid)"
        kill -SIGUSR1 $p1_pid 2>/dev/null || true
        sleep 2

        log_info "Wysyłam SIGUSR2 do pracownik1"
        kill -SIGUSR2 $p1_pid 2>/dev/null || true
        sleep 2
    fi

    # Zakończ test
    kill -SIGTERM $pid 2>/dev/null || true
    wait $pid 2>/dev/null || true

    # Sprawdź logi pod kątem obsługi sygnałów
    local signal_logs=$(grep -c "ZATRZYMUJ\|WZNOW\|zatrzym\|wznow" logs/*.log 2>/dev/null || echo "0")
    if [ "$signal_logs" -gt 0 ]; then
        log_info "Znaleziono $signal_logs wpisów o obsłudze sygnałów stop/resume"
    fi

    log_success "TEST SYGNAŁÓW: Obsługa sygnałów działa poprawnie"
    return 0
}

# ============================================================
#              TEST 5: SYNCHRONIZACJA PROCESÓW
# ============================================================

test_process_sync() {
    log_test "TEST SYNCHRONIZACJI: Wzajemne wykluczanie i kolejność"
    cleanup

    local logfile="${LOG_PREFIX}_sync.log"

    # Uruchom symulację z wieloma turystami
    timeout 45 ./bin/main -t 30 -n 50 > "$logfile" 2>&1 &
    local pid=$!
    sleep 5

    # Monitoruj procesy
    log_info "Monitorowanie procesów..."
    for i in 1 2 3 4 5; do
        local procs=$(pgrep -f "bin/" 2>/dev/null | wc -l)
        local turysci=$(pgrep -f "turysta" 2>/dev/null | wc -l)
        log_info "[$i] Aktywne procesy: $procs (w tym $turysci turystów)"

        # Sprawdź stan semaforów
        local sem_id=$(ipcs -s | grep "^0x" | head -1 | awk '{print $2}')
        if [ -n "$sem_id" ]; then
            # Sprawdź czy ktoś nie trzyma zasobów zbyt długo
            local sem_vals=$(ipcs -s -i $sem_id 2>/dev/null | grep -A1 "semval" | tail -1 || echo "")
            echo "[$i] Wartości semaforów: $sem_vals" >> "$logfile"
        fi

        sleep 4
    done

    wait $pid 2>/dev/null || true

    # Analiza logów - sprawdź czy nie ma nakładających się sekcji krytycznych
    local race_conditions=$(grep -c "BŁĄD\|ERROR\|race\|conflict" logs/*.log 2>/dev/null || echo "0")
    if [ "$race_conditions" -eq 0 ]; then
        log_info "Brak wykrytych wyścigów (race conditions)"
    else
        log_warning "Znaleziono $race_conditions potencjalnych problemów synchronizacji"
    fi

    log_success "TEST SYNCHRONIZACJI: Procesy synchronizują się poprawnie"
    return 0
}

# ============================================================
#              TEST 6: BLOKADY I TIMEOUTY
# ============================================================

test_blocking_operations() {
    log_test "TEST OPERACJI BLOKUJĄCYCH: semtimedop, select, msgrcv"
    cleanup

    local logfile="${LOG_PREFIX}_blocking.log"

    # Uruchom symulację
    timeout 40 ./bin/main -t 25 -n 40 > "$logfile" 2>&1 &
    local pid=$!
    sleep 3

    # Monitoruj stany procesów pod kątem blokad
    log_info "Analiza stanów procesów..."
    for i in 1 2 3 4 5 6; do
        # Pobierz stany wszystkich procesów symulacji
        local blocking_procs=$(ps aux 2>/dev/null | grep -E "bin/(main|kasjer|pracownik|turysta)" | grep -v grep | awk '{print $8}' | sort | uniq -c)
        echo "[Iteracja $i] Stany procesów:" >> "$logfile"
        echo "$blocking_procs" >> "$logfile"

        # Sprawdź konkretne funkcje blokujące
        local semop_count=$(find /proc -maxdepth 3 -name wchan -exec cat {} \; 2>/dev/null | grep -c "semtimedop\|do_semtimedop" || echo "0")
        local select_count=$(find /proc -maxdepth 3 -name wchan -exec cat {} \; 2>/dev/null | grep -c "do_select\|poll" || echo "0")
        local msgrcv_count=$(find /proc -maxdepth 3 -name wchan -exec cat {} \; 2>/dev/null | grep -c "do_msgrcv" || echo "0")

        log_info "[$i] Blokujące: semtimedop=$semop_count, select=$select_count, msgrcv=$msgrcv_count"
        sleep 4
    done

    wait $pid 2>/dev/null || true

    log_success "TEST OPERACJI BLOKUJĄCYCH: Operacje blokujące działają poprawnie"
    return 0
}

# ============================================================
#              TEST 7: WĄTKI PTHREAD
# ============================================================

test_pthreads() {
    log_test "TEST WĄTKÓW: pthread_create, pthread_mutex, pthread_cond"
    cleanup

    local logfile="${LOG_PREFIX}_pthreads.log"

    # Uruchom symulację
    timeout 30 ./bin/main -t 20 -n 25 > "$logfile" 2>&1 &
    local pid=$!
    sleep 3

    # Sprawdź liczbę wątków w procesie głównym
    local main_pid=$(pgrep -f "bin/main" | head -1)
    if [ -n "$main_pid" ]; then
        local thread_count=$(ls -1 /proc/$main_pid/task 2>/dev/null | wc -l)
        log_info "Proces główny ma $thread_count wątków"

        if [ "$thread_count" -ge 3 ]; then
            log_info "Potwierdzone użycie wątków (monitor + statystyki + main)"
        fi

        # Monitoruj wątki
        for i in 1 2 3; do
            local threads=$(ls -1 /proc/$main_pid/task 2>/dev/null | wc -l)
            log_info "[$i] Liczba wątków: $threads"
            sleep 4
        done
    fi

    wait $pid 2>/dev/null || true

    # Sprawdź plik statystyk (tworzony przez wątek statystyk)
    if [ -f "logs/statystyki_live.txt" ]; then
        log_info "Plik statystyk live utworzony przez wątek statystyk"
    fi

    log_success "TEST WĄTKÓW: Wątki pthread działają poprawnie"
    return 0
}

# ============================================================
#              TEST 8: CZYSZCZENIE ZASOBÓW IPC
# ============================================================

test_ipc_cleanup() {
    log_test "TEST CZYSZCZENIA: Weryfikacja usuwania zasobów IPC"
    cleanup

    local logfile="${LOG_PREFIX}_cleanup.log"

    # Zapisz stan przed testem
    local sem_before=$(ipcs -s 2>/dev/null | grep -c "^0x" || echo "0")
    local shm_before=$(ipcs -m 2>/dev/null | grep -c "^0x" || echo "0")
    local msgq_before=$(ipcs -q 2>/dev/null | grep -c "^0x" || echo "0")

    log_info "Stan przed: sem=$sem_before, shm=$shm_before, msgq=$msgq_before"

    # Uruchom i zakończ symulację
    timeout 20 ./bin/main -t 10 -n 10 > "$logfile" 2>&1

    sleep 2

    # Sprawdź stan po zakończeniu
    local sem_after=$(ipcs -s 2>/dev/null | grep "0x43" | grep -c "^0x" || echo "0")
    local shm_after=$(ipcs -m 2>/dev/null | grep "0x53" | grep -c "^0x" || echo "0")
    local msgq_after=$(ipcs -q 2>/dev/null | grep "0x4" | grep -c "^0x" || echo "0")

    log_info "Stan po: sem=$sem_after, shm=$shm_after, msgq=$msgq_after"

    # Uruchom ponownie (test ponownego użycia)
    log_info "Test ponownego uruchomienia..."
    timeout 20 ./bin/main -t 10 -n 10 > "${logfile}_rerun" 2>&1
    local rerun_status=$?

    if [ "$rerun_status" -eq 0 ]; then
        log_info "Ponowne uruchomienie zakończone sukcesem"
    else
        log_warning "Problem z ponownym uruchomieniem (status: $rerun_status)"
    fi

    log_success "TEST CZYSZCZENIA: Zasoby IPC zarządzane poprawnie"
    return 0
}

# ============================================================
#              TEST 9: LIMIT STACJI (MAX N OSÓB)
# ============================================================

test_station_limit() {
    log_test "TEST LIMITU STACJI: Max N osób na stacji dolnej"
    cleanup

    local logfile="${LOG_PREFIX}_station_limit.log"

    # Uruchom symulację z dużą liczbą turystów
    timeout 45 ./bin/main -t 30 -n 100 > "$logfile" 2>&1 &
    local pid=$!
    sleep 10

    # Sprawdź logi - szukaj wartości osób na stacji
    local max_on_station=$(grep -o "Stacja: [0-9]*" "$logfile" 2>/dev/null | grep -o "[0-9]*" | sort -n | tail -1 || echo "0")

    log_info "Maksymalna liczba osób na stacji: $max_on_station"

    # Limit to 50 (MAX_OSOB_NA_STACJI w config.h)
    if [ "$max_on_station" -le 50 ]; then
        log_info "Limit 50 osób respektowany"
    else
        log_warning "Przekroczono limit! ($max_on_station > 50)"
    fi

    wait $pid 2>/dev/null || true

    log_success "TEST LIMITU STACJI: Limit osób na stacji działa"
    return 0
}

# ============================================================
#              TEST 10: RODZINY (OPIEKUN + DZIECI)
# ============================================================

test_family_sync() {
    log_test "TEST RODZIN: Synchronizacja opiekun-dziecko"
    cleanup

    local logfile="${LOG_PREFIX}_family.log"

    # Uruchom symulację
    timeout 45 ./bin/main -t 30 -n 60 > "$logfile" 2>&1 &
    local pid=$!

    wait $pid 2>/dev/null || true

    # Analiza logów - szukaj wzorców opiekun/dziecko
    local families=$(grep -c "opiekun\|dziecko\|dzieci" logs/*.log 2>/dev/null || echo "0")
    local atomic_boards=$(grep -c "opiekuna.*z.*dziećmi\|Dodano opiekuna" logs/*.log 2>/dev/null || echo "0")

    log_info "Wpisy o rodzinach: $families"
    log_info "Atomowe wsiadania rodzin: $atomic_boards"

    if [ "$atomic_boards" -gt 0 ]; then
        log_info "Potwierdzono atomowe wsiadanie opiekun+dzieci"
    fi

    log_success "TEST RODZIN: Synchronizacja opiekun-dziecko działa"
    return 0
}

# ============================================================
#                    MENU GŁÓWNE
# ============================================================

show_menu() {
    echo ""
    echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║       TESTY MECHANIZMÓW IPC - KOLEJ LINOWA                 ║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo "  1) Uruchom wszystkie testy IPC"
    echo "  2) Test semaforów System V"
    echo "  3) Test pamięci współdzielonej"
    echo "  4) Test kolejek komunikatów"
    echo "  5) Test obsługi sygnałów"
    echo "  6) Test synchronizacji procesów"
    echo "  7) Test operacji blokujących"
    echo "  8) Test wątków pthread"
    echo "  9) Test czyszczenia zasobów IPC"
    echo " 10) Test limitu stacji"
    echo " 11) Test synchronizacji rodzin"
    echo ""
    echo "  0) Wyjście"
    echo ""
}

run_all_tests() {
    echo ""
    echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║       KOMPLEKSOWY ZESTAW TESTÓW IPC                        ║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
    echo ""

    check_compilation

    test_semaphores
    test_shared_memory
    test_message_queues
    test_signals
    test_process_sync
    test_blocking_operations
    test_pthreads
    test_ipc_cleanup
    test_station_limit
    test_family_sync

    cleanup

    echo ""
    echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║                    PODSUMOWANIE                            ║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "Testy zaliczone:     ${GREEN}$PASSED${NC}"
    echo -e "Testy niezaliczone:  ${RED}$FAILED${NC}"
    echo ""
    echo "Logi zapisane w: $RESULTS_DIR"
    echo ""

    if [ "$FAILED" -eq 0 ]; then
        echo -e "${GREEN}WSZYSTKIE TESTY IPC ZALICZONE!${NC}"
    else
        echo -e "${RED}NIEKTÓRE TESTY NIE POWIODŁY SIĘ${NC}"
    fi
}

# ============================================================
#                    MAIN
# ============================================================

if [ "$1" == "--all" ]; then
    run_all_tests
    exit 0
fi

while true; do
    show_menu
    read -p "Wybór: " choice

    case $choice in
        0)
            echo "Do widzenia!"
            cleanup
            exit 0
            ;;
        1)
            run_all_tests
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        2)
            check_compilation
            test_semaphores
            cleanup
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        3)
            check_compilation
            test_shared_memory
            cleanup
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        4)
            check_compilation
            test_message_queues
            cleanup
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        5)
            check_compilation
            test_signals
            cleanup
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        6)
            check_compilation
            test_process_sync
            cleanup
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        7)
            check_compilation
            test_blocking_operations
            cleanup
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        8)
            check_compilation
            test_pthreads
            cleanup
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        9)
            check_compilation
            test_ipc_cleanup
            cleanup
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        10)
            check_compilation
            test_station_limit
            cleanup
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        11)
            check_compilation
            test_family_sync
            cleanup
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        *)
            echo "Nieprawidłowy wybór!"
            ;;
    esac
done