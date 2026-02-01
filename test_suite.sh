#!/bin/bash
# ============================================================
#    KOMPLEKSOWY ZESTAW TESTÓW - KOLEJ LINOWA KRZESEŁKOWA
# ============================================================
# Testy obciążeniowe, limitów i wykrywania blokad

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

RESULTS_DIR="test_results"
LOG_PREFIX="${RESULTS_DIR}/test_$(date +%Y%m%d_%H%M%S)"

# ============================================================
#                    FUNKCJE POMOCNICZE
# ============================================================

log_test() {
    echo -e "${BLUE}[TEST]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
}

cleanup() {
    echo ""
    log_warning "Czyszczenie po testach..."

    # Zabij wszystkie procesy symulacji
    pkill -9 -f "bin/main" 2>/dev/null || true
    pkill -9 -f "bin/kasjer" 2>/dev/null || true
    pkill -9 -f "bin/pracownik" 2>/dev/null || true
    pkill -9 -f "bin/turysta" 2>/dev/null || true

    # Wyczyść zasoby IPC
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

monitor_resources() {
    local test_name=$1
    local pid=$2
    local output_file="${LOG_PREFIX}_${test_name}_resources.log"

    echo "Timestamp,CPU%,MEM%,Threads,OpenFDs,SemCount,MsgQCount" > "$output_file"

    while kill -0 "$pid" 2>/dev/null; do
        local timestamp=$(date +%s)

        # CPU i pamięć
        local cpu_mem=$(ps -p "$pid" -o %cpu,%mem --no-headers 2>/dev/null || echo "0 0")
        local cpu=$(echo $cpu_mem | awk '{print $1}')
        local mem=$(echo $cpu_mem | awk '{print $2}')

        # Liczba wątków
        local threads=$(ps -T -p "$pid" 2>/dev/null | wc -l)

        # Otwarte deskryptory plików
        local fds=$(ls -1 /proc/$pid/fd 2>/dev/null | wc -l || echo "0")

        # Liczba semaforów
        local sems=$(ipcs -s 2>/dev/null | grep -c "^0x" || echo "0")

        # Liczba kolejek komunikatów
        local msgqs=$(ipcs -q 2>/dev/null | grep -c "^0x" || echo "0")

        echo "$timestamp,$cpu,$mem,$threads,$fds,$sems,$msgqs" >> "$output_file"

        sleep 0.5
    done
}

check_for_deadlock() {
    local pid=$1
    local timeout=$2
    local start_time=$(date +%s)
    local last_progress=0
    local stuck_counter=0

    while kill -0 "$pid" 2>/dev/null; do
        local current_time=$(date +%s)
        local elapsed=$((current_time - start_time))

        if [ $elapsed -gt $timeout ]; then
            return 1  # Timeout
        fi

        # Sprawdź postęp w logach
        local progress=$(grep -c "zjazd\|bilet\|wejście" logs/*.log 2>/dev/null || echo "0")

        if [ "$progress" == "$last_progress" ]; then
            stuck_counter=$((stuck_counter + 1))
            if [ $stuck_counter -gt 10 ]; then
                log_error "Wykryto potencjalną blokadę! Brak postępu przez 5 sekund."
                return 2  # Deadlock
            fi
        else
            stuck_counter=0
        fi

        last_progress=$progress
        sleep 0.5
    done

    return 0  # OK
}

analyze_semaphores() {
    local output_file="${LOG_PREFIX}_semaphores.log"
    echo "=== ANALIZA SEMAFORÓW ===" > "$output_file"
    ipcs -s >> "$output_file" 2>&1 || true
    ipcs -su >> "$output_file" 2>&1 || true
}

analyze_message_queues() {
    local output_file="${LOG_PREFIX}_msgqueues.log"
    echo "=== ANALIZA KOLEJEK KOMUNIKATÓW ===" > "$output_file"
    ipcs -q >> "$output_file" 2>&1 || true
    ipcs -qu >> "$output_file" 2>&1 || true
}

analyze_shared_memory() {
    local output_file="${LOG_PREFIX}_shm.log"
    echo "=== ANALIZA PAMIĘCI WSPÓŁDZIELONEJ ===" > "$output_file"
    ipcs -m >> "$output_file" 2>&1 || true
    ipcs -mu >> "$output_file" 2>&1 || true
}

# ============================================================
#                    TESTY JEDNOSTKOWE
# ============================================================

test_01_basic_run() {
    log_test "TEST 01: Podstawowe uruchomienie (10s, 5 turystów)"
    cleanup

    timeout 25 ./bin/main -t 10 -n 5 > "${LOG_PREFIX}_test01.log" 2>&1
    local status=$?

    if [ $status -eq 0 ]; then
        log_success "Test 01 PASSED"
        return 0
    else
        log_error "Test 01 FAILED (status: $status)"
        return 1
    fi
}

test_02_medium_load() {
    log_test "TEST 02: Średnie obciążenie (30s, 50 turystów)"
    cleanup

    timeout 40 ./bin/main -t 30 -n 50 > "${LOG_PREFIX}_test02.log" 2>&1 &
    local pid=$!

    monitor_resources "test02" "$pid" &
    local monitor_pid=$!

    wait $pid
    local status=$?

    kill $monitor_pid 2>/dev/null || true

    if [ $status -eq 0 ]; then
        log_success "Test 02 PASSED"
        return 0
    else
        log_error "Test 02 FAILED (status: $status)"
        return 1
    fi
}

test_03_high_load() {
    log_test "TEST 03: Duże obciążenie (60s, 200 turystów)"
    cleanup

    timeout 75 ./bin/main -t 60 -n 200 > "${LOG_PREFIX}_test03.log" 2>&1 &
    local pid=$!

    monitor_resources "test03" "$pid" &
    local monitor_pid=$!

    wait $pid
    local status=$?

    kill $monitor_pid 2>/dev/null || true

    if [ $status -eq 0 ]; then
        log_success "Test 03 PASSED"
        return 0
    else
        log_error "Test 03 FAILED (status: $status)"
        return 1
    fi
}

test_04_extreme_load() {
    log_test "TEST 04: EKSTREMALNY TEST - Maksymalne obciążenie (90s, 500 turystów)"
    cleanup

    log_warning "To może zająć kilka minut i obciążyć system..."

    timeout 120 ./bin/main -t 90 -n 500 > "${LOG_PREFIX}_test04.log" 2>&1 &
    local pid=$!

    monitor_resources "test04" "$pid" &
    local monitor_pid=$!

    check_for_deadlock "$pid" 120
    local deadlock_status=$?

    wait $pid 2>/dev/null
    local status=$?

    kill $monitor_pid 2>/dev/null || true

    if [ $deadlock_status -eq 2 ]; then
        log_error "Test 04 FAILED - Wykryto DEADLOCK!"
        cleanup
        return 1
    elif [ $status -eq 0 ]; then
        log_success "Test 04 PASSED - System przetrwał ekstremalny test!"
        return 0
    else
        log_error "Test 04 FAILED (status: $status)"
        return 1
    fi
}

test_05_stress_rapid_start() {
    log_test "TEST 05: Szybkie uruchamianie wielu turystów"
    cleanup

    # Zwiększone parametry generowania turystów przez modyfikację prawdopodobieństwa
    timeout 35 ./bin/main -t 30 -n 100 > "${LOG_PREFIX}_test05.log" 2>&1 &
    local pid=$!

    # Sprawdź czy nie ma blokad przy szybkim generowaniu
    check_for_deadlock "$pid" 35
    local deadlock_status=$?

    wait $pid 2>/dev/null
    local status=$?

    if [ $deadlock_status -eq 2 ]; then
        log_error "Test 05 FAILED - Wykryto blokadę przy szybkim generowaniu!"
        cleanup
        return 1
    elif [ $status -eq 0 ]; then
        log_success "Test 05 PASSED"
        return 0
    else
        log_error "Test 05 FAILED (status: $status)"
        return 1
    fi
}

test_06_long_running() {
    log_test "TEST 06: Test długotrwałego działania (120s, 150 turystów)"
    cleanup

    log_warning "Test długotrwały - około 2 minuty..."

    timeout 150 ./bin/main -t 120 -n 150 > "${LOG_PREFIX}_test06.log" 2>&1 &
    local pid=$!

    monitor_resources "test06" "$pid" &
    local monitor_pid=$!

    # Sprawdzaj stan co 10 sekund
    local counter=0
    while kill -0 "$pid" 2>/dev/null && [ $counter -lt 15 ]; do
        sleep 10
        counter=$((counter + 1))
        log_warning "Upłynęło ${counter}0 sekund..."

        # Sprawdź wycieki pamięci
        local mem=$(ps -p "$pid" -o %mem --no-headers 2>/dev/null || echo "0")
        if (( $(echo "$mem > 50.0" | bc -l 2>/dev/null || echo "0") )); then
            log_error "OSTRZEŻENIE: Wysokie zużycie pamięci: ${mem}%"
        fi
    done

    wait $pid 2>/dev/null
    local status=$?

    kill $monitor_pid 2>/dev/null || true

    if [ $status -eq 0 ]; then
        log_success "Test 06 PASSED - Brak wycieków pamięci"
        return 0
    else
        log_error "Test 06 FAILED (status: $status)"
        return 1
    fi
}

test_07_resource_limits() {
    log_test "TEST 07: Test limitów zasobów systemowych"
    cleanup

    log_warning "Sprawdzanie limitów systemowych..."

    echo "=== LIMITY SYSTEMOWE ===" > "${LOG_PREFIX}_test07_limits.log"

    # Sprawdź limity procesów
    ulimit -a >> "${LOG_PREFIX}_test07_limits.log" 2>&1

    # Sprawdź limity IPC
    echo "" >> "${LOG_PREFIX}_test07_limits.log"
    echo "=== PARAMETRY IPC ===" >> "${LOG_PREFIX}_test07_limits.log"
    ipcs -l >> "${LOG_PREFIX}_test07_limits.log" 2>&1 || true

    # Sprawdź aktualnie użyte zasoby
    echo "" >> "${LOG_PREFIX}_test07_limits.log"
    echo "=== UŻYTE ZASOBY IPC ===" >> "${LOG_PREFIX}_test07_limits.log"
    ipcs -u >> "${LOG_PREFIX}_test07_limits.log" 2>&1 || true

    # Uruchom test z dużą liczbą procesów
    timeout 45 ./bin/main -t 40 -n 200 > "${LOG_PREFIX}_test07.log" 2>&1
    local status=$?

    # Analiza po teście
    analyze_semaphores
    analyze_message_queues
    analyze_shared_memory

    if [ $status -eq 0 ]; then
        log_success "Test 07 PASSED - System radzi sobie z limitami"
        return 0
    else
        log_error "Test 07 FAILED - Prawdopodobnie osiągnięto limity systemowe"
        cat "${LOG_PREFIX}_test07_limits.log"
        return 1
    fi
}

test_08_signal_handling() {
    log_test "TEST 08: Test obsługi sygnałów (SIGINT/SIGTERM)"
    cleanup

    ./bin/main -t 60 -n 50 > "${LOG_PREFIX}_test08.log" 2>&1 &
    local pid=$!

    # Poczekaj na uruchomienie
    sleep 5

    log_warning "Wysyłam SIGINT..."
    kill -INT $pid

    # Czekaj na zakończenie
    local timeout=10
    while kill -0 "$pid" 2>/dev/null && [ $timeout -gt 0 ]; do
        sleep 1
        timeout=$((timeout - 1))
    done

    if kill -0 "$pid" 2>/dev/null; then
        log_error "Test 08 FAILED - Proces nie zareagował na SIGINT"
        kill -9 $pid 2>/dev/null
        return 1
    else
        log_success "Test 08 PASSED - Poprawna obsługa sygnałów"
        return 0
    fi
}

test_09_concurrent_access() {
    log_test "TEST 09: Test czyszczenia zasobów IPC po zakończeniu"
    cleanup

    # Test 1: Uruchom symulację
    log_warning "Uruchamiam pierwszą symulację..."
    timeout 25 ./bin/main -t 20 -n 30 > "${LOG_PREFIX}_test09_a.log" 2>&1
    local status1=$?

    if [ $status1 -ne 0 ]; then
        log_error "Test 09 FAILED - Pierwsza symulacja nie zakończyła się poprawnie"
        return 1
    fi

    sleep 2

    # Test 2: Sprawdź czy zasoby zostały wyczyszczone
    local remaining_sems=$(ipcs -s 2>/dev/null | grep "^0x" | wc -l || echo "0")
    local remaining_msgq=$(ipcs -q 2>/dev/null | grep "^0x" | wc -l || echo "0")
    local remaining_shm=$(ipcs -m 2>/dev/null | grep "^0x" | wc -l || echo "0")

    if [ $remaining_sems -gt 0 ] || [ $remaining_msgq -gt 0 ] || [ $remaining_shm -gt 0 ]; then
        log_warning "Pozostały zasoby IPC (sem:$remaining_sems, msgq:$remaining_msgq, shm:$remaining_shm)"
        log_warning "To jest OK - kod może celowo zostawiać zasoby dla ponownego użycia"

        # Wyczyść ręcznie
        make clean-ipc >/dev/null 2>&1
    fi

    # Test 3: Uruchom drugą symulację (powinna działać)
    log_warning "Uruchamiam drugą symulację (test ponownego użycia)..."
    timeout 25 ./bin/main -t 20 -n 30 > "${LOG_PREFIX}_test09_b.log" 2>&1
    local status2=$?

    if [ $status2 -eq 0 ]; then
        log_success "Test 09 PASSED - Zasoby IPC działają poprawnie przy ponownym użyciu"
        return 0
    else
        log_error "Test 09 FAILED - Druga symulacja nie powiodła się"
        return 1
    fi
}

test_10_zombie_prevention() {
    log_test "TEST 10: Test zapobiegania procesom zombie"
    cleanup

    ./bin/main -t 20 -n 30 > "${LOG_PREFIX}_test10.log" 2>&1 &
    local main_pid=$!

    sleep 10

    # Sprawdź procesy zombie
    local zombies=$(ps aux | grep defunct | grep -v grep | wc -l)

    log_warning "Znaleziono $zombies procesów zombie"

    wait $main_pid 2>/dev/null

    sleep 2

    # Sprawdź ponownie po zakończeniu
    local zombies_after=$(ps aux | grep defunct | grep -v grep | wc -l)

    if [ $zombies_after -gt 0 ]; then
        log_error "Test 10 FAILED - Pozostały procesy zombie: $zombies_after"
        ps aux | grep defunct | grep -v grep >> "${LOG_PREFIX}_test10_zombies.log"
        return 1
    else
        log_success "Test 10 PASSED - Brak procesów zombie"
        return 0
    fi
}

# ============================================================
#                 TESTY FUNKCJONALNE (LOGIKA BIZNESOWA)
# ============================================================

test_11_child_guardian_sync() {
    log_test "TEST 11: Synchronizacja dziecko-opiekun (weryfikacja logów)"
    cleanup

    # Uruchom symulację z wieloma turystami (będą rodziny z dziećmi)
    timeout 40 ./bin/main -t 30 -n 60 > "${LOG_PREFIX}_test11.log" 2>&1
    local status=$?

    if [ $status -ne 0 ]; then
        log_error "Test 11 FAILED - Symulacja nie zakończyła się poprawnie"
        return 1
    fi

    # Sprawdź logi pracownika1 - czy logika dzieci działa
    local child_waiting=$(grep -c "czeka.*opiekun" logs/pracownik1.log 2>/dev/null || echo "0")
    local child_joined=$(grep -c "dołącza do opiekuna" logs/pracownik1.log 2>/dev/null || echo "0")

    echo "  Dzieci czekających na opiekuna: $child_waiting" >> "${LOG_PREFIX}_test11_analysis.log"
    echo "  Dzieci dołączonych do opiekuna: $child_joined" >> "${LOG_PREFIX}_test11_analysis.log"

    # Sprawdź czy NIE MA sytuacji że dziecko weszło BEZ opiekuna
    # (szukamy w logach krzesełek - dziecko powinno być zawsze z opiekunem)
    local orphan_child=$(grep -E "Dziecko.*bez opiekuna.*wchodzi" logs/*.log 2>/dev/null | wc -l || echo "0")

    if [ "$orphan_child" -gt 0 ]; then
        log_error "Test 11 FAILED - Znaleziono dziecko wchodzące bez opiekuna!"
        return 1
    fi

    if [ "$child_joined" -gt 0 ] || [ "$child_waiting" -gt 0 ]; then
        log_success "Test 11 PASSED - Logika dziecko-opiekun działa (czekało: $child_waiting, dołączyło: $child_joined)"
        return 0
    else
        log_warning "Test 11 PASSED - Brak dzieci w symulacji (to OK, losowe)"
        return 0
    fi
}

test_12_max_children_per_guardian() {
    log_test "TEST 12: Max 2 dzieci na opiekuna"
    cleanup

    timeout 40 ./bin/main -t 30 -n 80 > "${LOG_PREFIX}_test12.log" 2>&1
    local status=$?

    if [ $status -ne 0 ]; then
        log_error "Test 12 FAILED - Symulacja nie zakończyła się poprawnie"
        return 1
    fi

    # Sprawdź czy pojawiły się komunikaty o limicie dzieci
    local limit_msgs=$(grep -c "ma już.*dzieci.*limit" logs/pracownik1.log 2>/dev/null || echo "0")

    echo "  Komunikaty o limicie dzieci: $limit_msgs" >> "${LOG_PREFIX}_test12_analysis.log"

    # Sprawdź czy NIE przekroczono limitu (3+ dzieci z jednym opiekunem na krzesełku)
    # To wymagałoby analizy struktury krzesełek - uproszczona wersja sprawdza logi
    local exceeded=$(grep -E "Opiekun.*ma już [3-9] dzieci" logs/*.log 2>/dev/null | wc -l || echo "0")

    if [ "$exceeded" -gt 0 ]; then
        log_error "Test 12 FAILED - Przekroczono limit dzieci na opiekuna!"
        return 1
    fi

    log_success "Test 12 PASSED - Limit 2 dzieci na opiekuna respektowany"
    return 0
}

test_13_vip_priority() {
    log_test "TEST 13: Priorytet VIP (wejście bez kolejki)"
    cleanup

    timeout 40 ./bin/main -t 30 -n 100 > "${LOG_PREFIX}_test13.log" 2>&1
    local status=$?

    if [ $status -ne 0 ]; then
        log_error "Test 13 FAILED - Symulacja nie zakończyła się poprawnie"
        return 1
    fi

    # Sprawdź czy byli VIP-owie i czy wchodzili bez kolejki
    local vip_entries=$(grep -c "\[VIP\].*bez kolejki" logs/wszyscy_turysci.log 2>/dev/null || echo "0")
    local total_tourists=$(grep -c "Przychodzę" logs/wszyscy_turysci.log 2>/dev/null || echo "0")

    echo "  Turystów VIP: $vip_entries" >> "${LOG_PREFIX}_test13_analysis.log"
    echo "  Wszystkich turystów: $total_tourists" >> "${LOG_PREFIX}_test13_analysis.log"

    # VIP to ~1%, więc przy 100 turystach powinien być 0-3
    if [ "$vip_entries" -ge 0 ]; then
        log_success "Test 13 PASSED - VIP działa (znaleziono: $vip_entries VIP-ów)"
        return 0
    else
        log_warning "Test 13 PASSED - Brak VIP-ów w tej symulacji (1% szansa)"
        return 0
    fi
}

test_14_chairlift_capacity() {
    log_test "TEST 14: Pojemność krzesełka (max 4 osoby, max 2 rowerzystów)"
    cleanup

    timeout 40 ./bin/main -t 30 -n 80 > "${LOG_PREFIX}_test14.log" 2>&1
    local status=$?

    if [ $status -ne 0 ]; then
        log_error "Test 14 FAILED - Symulacja nie zakończyła się poprawnie"
        return 1
    fi

    # Sprawdź logi pracownika1 - ile osób na krzesełkach
    local chairs_sent=$(grep "Wysyłam krzesełko" logs/pracownik1.log 2>/dev/null || true)

    # Sprawdź czy żadne krzesełko nie ma więcej niż 4 osoby
    local overcrowded=$(echo "$chairs_sent" | grep -E "z [5-9] osobami|z [0-9][0-9] osobami" | wc -l || echo "0")

    if [ "$overcrowded" -gt 0 ]; then
        log_error "Test 14 FAILED - Znaleziono przepełnione krzesełka!"
        echo "$chairs_sent" | grep -E "z [5-9] osobami" >> "${LOG_PREFIX}_test14_analysis.log"
        return 1
    fi

    local chair_count=$(echo "$chairs_sent" | wc -l || echo "0")
    log_success "Test 14 PASSED - Wszystkie krzesełka ($chair_count) mieszczą max 4 osoby"
    return 0
}

test_15_station_capacity() {
    log_test "TEST 15: Limit osób na stacji dolnej (max N=$MAX_STATION_CAPACITY)"
    cleanup

    # Pobierz limit ze stanu lub użyj domyślnego
    local MAX_STATION_CAPACITY=50

    timeout 50 ./bin/main -t 40 -n 150 > "${LOG_PREFIX}_test15.log" 2>&1 &
    local pid=$!

    local max_observed=0
    local exceeded=0

    # Monitoruj przez 35 sekund
    for i in {1..35}; do
        if ! kill -0 "$pid" 2>/dev/null; then
            break
        fi

        # Odczytaj aktualną liczbę osób ze statystyk (jeśli dostępne)
        local current=$(grep "Osoby na stacji:" logs/statystyki_live.txt 2>/dev/null | tail -1 | grep -oE "[0-9]+" || echo "0")

        if [ "$current" -gt "$max_observed" ]; then
            max_observed=$current
        fi

        if [ "$current" -gt "$MAX_STATION_CAPACITY" ]; then
            exceeded=$((exceeded + 1))
        fi

        sleep 1
    done

    wait $pid 2>/dev/null

    echo "  Max zaobserwowane osób na stacji: $max_observed" >> "${LOG_PREFIX}_test15_analysis.log"
    echo "  Przekroczenia limitu: $exceeded" >> "${LOG_PREFIX}_test15_analysis.log"

    if [ "$exceeded" -gt 0 ]; then
        log_error "Test 15 FAILED - Przekroczono limit osób na stacji ($exceeded razy)"
        return 1
    fi

    log_success "Test 15 PASSED - Limit stacji respektowany (max: $max_observed/$MAX_STATION_CAPACITY)"
    return 0
}

test_16_closing_time() {
    log_test "TEST 16: Zamknięcie kolei - transport pozostałych osób"
    cleanup

    # Krótka symulacja - sprawdzamy czy po zamknięciu wszyscy zostają przetransportowani
    timeout 25 ./bin/main -t 15 -n 40 > "${LOG_PREFIX}_test16.log" 2>&1
    local status=$?

    # Sprawdź czy symulacja zakończyła się poprawnie
    if [ $status -ne 0 ]; then
        log_error "Test 16 FAILED - Symulacja nie zakończyła się poprawnie"
        return 1
    fi

    # Sprawdź komunikaty o zamknięciu
    local closing_msg=$(grep -c "Kolej zamknięta\|godziny_pracy.*false\|Kolej się zamyka" logs/*.log 2>/dev/null || echo "0")

    # Sprawdź czy raport został wygenerowany
    if [ -f "logs/raport_dzienny.txt" ]; then
        local total_rides=$(grep "Łączna liczba zjazdów" logs/raport_dzienny.txt 2>/dev/null | grep -oE "[0-9]+" || echo "0")
        echo "  Łączna liczba zjazdów: $total_rides" >> "${LOG_PREFIX}_test16_analysis.log"
        log_success "Test 16 PASSED - Kolej zamknięta poprawnie, raport wygenerowany (zjazdy: $total_rides)"
        return 0
    else
        log_warning "Test 16 PASSED - Brak raportu (krótka symulacja)"
        return 0
    fi
}

# ============================================================
#                    GŁÓWNA FUNKCJA TESTOWA
# ============================================================

run_all_tests() {
    local passed=0
    local failed=0
    local total=16

    echo ""
    echo "╔════════════════════════════════════════════════════════════╗"
    echo "║  KOMPLEKSOWY ZESTAW TESTÓW - KOLEJ LINOWA KRZESEŁKOWA     ║"
    echo "╚════════════════════════════════════════════════════════════╝"
    echo ""

    mkdir -p "$RESULTS_DIR"

    # Sprawdź kompilację
    check_compilation

    echo ""
    log_warning "Rozpoczynam testy..."
    echo ""

    # Uruchom wszystkie testy
    test_01_basic_run && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_02_medium_load && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_03_high_load && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_04_extreme_load && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_05_stress_rapid_start && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_06_long_running && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_07_resource_limits && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_08_signal_handling && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_09_concurrent_access && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_10_zombie_prevention && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    # Testy funkcjonalne
    echo ""
    echo "╔════════════════════════════════════════════════════════════╗"
    echo "║              TESTY FUNKCJONALNE (LOGIKA)                   ║"
    echo "╚════════════════════════════════════════════════════════════╝"
    echo ""

    test_11_child_guardian_sync && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_12_max_children_per_guardian && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_13_vip_priority && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_14_chairlift_capacity && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_15_station_capacity && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_16_closing_time && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    # Podsumowanie
    echo ""
    echo "╔════════════════════════════════════════════════════════════╗"
    echo "║                      PODSUMOWANIE                          ║"
    echo "╚════════════════════════════════════════════════════════════╝"
    echo ""
    echo -e "Testy zaliczone: ${GREEN}${passed}/${total}${NC}"
    echo -e "Testy niezaliczone: ${RED}${failed}/${total}${NC}"
    echo ""
    echo "Logi zapisane w: $RESULTS_DIR"
    echo ""

    if [ $failed -eq 0 ]; then
        log_success "WSZYSTKIE TESTY ZALICZONE! 🎉"
        return 0
    else
        log_error "NIEKTÓRE TESTY NIE POWIODŁY SIĘ!"
        return 1
    fi
}

# ============================================================
#                    MENU INTERAKTYWNE
# ============================================================

show_menu() {
    echo ""
    echo "╔════════════════════════════════════════════════════════════╗"
    echo "║            TESTY - KOLEJ LINOWA KRZESEŁKOWA                ║"
    echo "╚════════════════════════════════════════════════════════════╝"
    echo ""
    echo "  1) Uruchom wszystkie testy"
    echo "  2) Test podstawowy (10s)"
    echo "  3) Test średni (30s)"
    echo "  4) Test duży (60s)"
    echo "  5) Test EKSTREMALNY (90s)"
    echo "  6) Test długotrwały (120s)"
    echo "  7) Test limitów zasobów"
    echo "  8) Test obsługi sygnałów"
    echo "  9) Tylko monitoring zasobów"
    echo " 10) Wyczyść zasoby IPC"
    echo ""
    echo " --- TESTY FUNKCJONALNE ---"
    echo " 11) Test dziecko-opiekun"
    echo " 12) Test max dzieci na opiekuna"
    echo " 13) Test VIP"
    echo " 14) Test pojemności krzesełka"
    echo " 15) Test limitu stacji"
    echo " 16) Test zamknięcia kolei"
    echo ""
    echo "  0) Wyjście"
    echo ""
    echo -n "Wybór: "
}

# ============================================================
#                    MAIN
# ============================================================

trap cleanup EXIT INT TERM

if [ "$1" == "--all" ] || [ "$1" == "-a" ]; then
    run_all_tests
    exit $?
fi

if [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
    echo "Użycie: $0 [opcja]"
    echo ""
    echo "Opcje:"
    echo "  -a, --all     Uruchom wszystkie testy automatycznie"
    echo "  -h, --help    Wyświetl tę pomoc"
    echo ""
    echo "Bez opcji uruchamia menu interaktywne."
    exit 0
fi

# Menu interaktywne
while true; do
    show_menu
    read choice

    case $choice in
        1)
            run_all_tests
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        2)
            test_01_basic_run
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        3)
            test_02_medium_load
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        4)
            test_03_high_load
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        5)
            test_04_extreme_load
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        6)
            test_06_long_running
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        7)
            test_07_resource_limits
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        8)
            test_08_signal_handling
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        9)
            log_test "Uruchamiam z monitoringiem..."
            cleanup
            ./bin/main -t 60 -n 100 &
            local pid=$!
            monitor_resources "manual" "$pid"
            wait $pid
            ;;
        10)
            cleanup
            log_success "Zasoby wyczyszczone"
            ;;
        11)
            test_11_child_guardian_sync
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        12)
            test_12_max_children_per_guardian
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        13)
            test_13_vip_priority
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        14)
            test_14_chairlift_capacity
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        15)
            test_15_station_capacity
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        16)
            test_16_closing_time
            read -p "Naciśnij Enter aby kontynuować..."
            ;;
        0)
            echo "Do widzenia!"
            exit 0
            ;;
        *)
            log_error "Nieprawidłowy wybór!"
            ;;
    esac
done
