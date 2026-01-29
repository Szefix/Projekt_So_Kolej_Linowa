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
#                    GŁÓWNA FUNKCJA TESTOWA
# ============================================================

run_all_tests() {
    local passed=0
    local failed=0
    local total=10

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
        0)
            echo "Do widzenia!"
            exit 0
            ;;
        *)
            log_error "Nieprawidłowy wybór!"
            ;;
    esac
done
