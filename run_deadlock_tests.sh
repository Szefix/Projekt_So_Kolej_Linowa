#!/bin/bash
# ============================================================
#       TESTY WYKRYWANIA DEADLOCKÓW - KOLEJ LINOWA
# ============================================================
# Automatyczne testy sprawdzające odporność na deadlocki

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
NC='\033[0m'

TEST_DIR="deadlock_tests"
RESULTS_DIR="${TEST_DIR}/results"
LOG_PREFIX="${RESULTS_DIR}/deadlock_$(date +%Y%m%d_%H%M%S)"

# ============================================================
#                    FUNKCJE POMOCNICZE
# ============================================================

log_test() {
    echo -e "${BLUE}[DEADLOCK TEST]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓ PASSED]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗ FAILED]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[! WARNING]${NC} $1"
}

log_deadlock() {
    echo -e "${MAGENTA}[⚠ DEADLOCK DETECTED]${NC} $1"
}

cleanup() {
    echo ""
    log_warning "Czyszczenie po testach deadlocków..."

    pkill -9 -f "bin/main" 2>/dev/null || true
    pkill -9 -f "bin/kasjer" 2>/dev/null || true
    pkill -9 -f "bin/pracownik" 2>/dev/null || true
    pkill -9 -f "bin/turysta" 2>/dev/null || true

    (cd .. && make clean-ipc) >/dev/null 2>&1 || true
    sleep 1
}

# ============================================================
#           DETEKTOR DEADLOCKÓW - GŁÓWNA FUNKCJA
# ============================================================

detect_deadlock() {
    local pid=$1
    local timeout=$2
    local test_name=$3
    local detection_log="${LOG_PREFIX}_${test_name}_detection.log"

    echo "=== ANALIZA DEADLOCKÓW: $test_name ===" > "$detection_log"
    echo "PID: $pid" >> "$detection_log"
    echo "Timeout: ${timeout}s" >> "$detection_log"
    echo "Start: $(date)" >> "$detection_log"
    echo "" >> "$detection_log"

    local start_time=$(date +%s)
    local last_log_count=0
    local stuck_iterations=0
    local max_stuck_iterations=6  # 6 * 2s = 12s bez postępu = deadlock

    local last_cpu_time=0
    local cpu_stuck_count=0

    while kill -0 "$pid" 2>/dev/null; do
        local current_time=$(date +%s)
        local elapsed=$((current_time - start_time))

        # Sprawdź timeout
        if [ $elapsed -gt $timeout ]; then
            echo "TIMEOUT after ${elapsed}s" >> "$detection_log"
            log_error "Test przekroczył timeout ${timeout}s"
            kill -9 $pid 2>/dev/null || true
            return 1
        fi

        # 1. SPRAWDŹ POSTĘP W LOGACH
        local current_log_count=$(cat logs/*.log 2>/dev/null | wc -l || echo "0")

        if [ "$current_log_count" == "$last_log_count" ]; then
            stuck_iterations=$((stuck_iterations + 1))
            echo "[${elapsed}s] Brak postępu w logach (iteracja $stuck_iterations/$max_stuck_iterations)" >> "$detection_log"
        else
            if [ $stuck_iterations -gt 0 ]; then
                echo "[${elapsed}s] Postęp wykryty (nowe linie: $((current_log_count - last_log_count)))" >> "$detection_log"
            fi
            stuck_iterations=0
        fi

        last_log_count=$current_log_count

        # 2. SPRAWDŹ CPU TIME (czy proces faktycznie coś robi)
        local current_cpu_time=$(ps -p "$pid" -o cputime --no-headers 2>/dev/null | tr -d ' :' || echo "0")

        if [ "$current_cpu_time" == "$last_cpu_time" ]; then
            cpu_stuck_count=$((cpu_stuck_count + 1))
        else
            cpu_stuck_count=0
        fi

        last_cpu_time=$current_cpu_time

        # 3. SPRAWDŹ STAN PROCESÓW
        local process_states=$(ps -eo pid,state,wchan:30,cmd | grep -E "kasjer|pracownik|turysta" | grep -v grep)
        echo "[${elapsed}s] Stany procesów:" >> "$detection_log"
        echo "$process_states" >> "$detection_log"

        # Sprawdź ile procesów jest w stanie D (uninterruptible sleep) lub Z (zombie)
        local blocked_count=$(echo "$process_states" | grep -c "D " || echo "0")
        local zombie_count=$(echo "$process_states" | grep -c "Z " || echo "0")

        echo "[${elapsed}s] Procesy zablokowane (D): $blocked_count, zombie (Z): $zombie_count" >> "$detection_log"

        # 4. ANALIZA SEMAFORÓW
        local sem_info=$(ipcs -s 2>/dev/null)
        local sem_count=$(echo "$sem_info" | grep "^0x" | wc -l || echo "0")
        echo "[${elapsed}s] Aktywne semafory: $sem_count" >> "$detection_log"

        # Sprawdź semafory z procesami czekającymi
        local waiting_processes=$(echo "$sem_info" | awk 'NR>3 && $7 > 0 {print $0}')
        if [ -n "$waiting_processes" ]; then
            echo "[${elapsed}s] Semafory z oczekującymi procesami:" >> "$detection_log"
            echo "$waiting_processes" >> "$detection_log"
        fi

        # 5. ANALIZA KOLEJEK KOMUNIKATÓW
        local msgq_info=$(ipcs -q 2>/dev/null)
        local msgq_count=$(echo "$msgq_info" | grep "^0x" | wc -l || echo "0")
        local full_queues=$(echo "$msgq_info" | awk 'NR>3 && $5 > 1000 {print $0}')

        if [ -n "$full_queues" ]; then
            echo "[${elapsed}s] Kolejki komunikatów prawie pełne:" >> "$detection_log"
            echo "$full_queues" >> "$detection_log"
        fi

        # 6. DETEKCJA DEADLOCKA
        # Deadlock jeśli:
        # - Brak postępu przez długi czas
        # - CPU time się nie zmienia
        # - Są zablokowane procesy lub czekające na semaforach

        if [ $stuck_iterations -ge $max_stuck_iterations ]; then
            if [ $cpu_stuck_count -ge 3 ]; then
                echo "" >> "$detection_log"
                echo "╔═══════════════════════════════════════╗" >> "$detection_log"
                echo "║    WYKRYTO DEADLOCK!                  ║" >> "$detection_log"
                echo "╚═══════════════════════════════════════╝" >> "$detection_log"
                echo "" >> "$detection_log"
                echo "Przyczyna:" >> "$detection_log"
                echo "- Brak postępu w logach przez $(($stuck_iterations * 2))s" >> "$detection_log"
                echo "- CPU time nie zmienia się przez $(($cpu_stuck_count * 2))s" >> "$detection_log"
                echo "- Procesy zablokowane: $blocked_count" >> "$detection_log"
                echo "- Aktywne semafory: $sem_count" >> "$detection_log"

                # Dodatkowa diagnostyka
                echo "" >> "$detection_log"
                echo "Stan procesów w momencie deadlocka:" >> "$detection_log"
                ps -eLf | grep -E "kasjer|pracownik|turysta" | grep -v grep >> "$detection_log" 2>&1

                echo "" >> "$detection_log"
                echo "Szczegóły semaforów:" >> "$detection_log"
                ipcs -s >> "$detection_log" 2>&1

                log_deadlock "Test: $test_name - Zobacz: $detection_log"
                kill -9 $pid 2>/dev/null || true
                return 2
            fi
        fi

        sleep 2
    done

    # Proces zakończył się normalnie
    echo "" >> "$detection_log"
    echo "Test zakończony pomyślnie po ${elapsed}s" >> "$detection_log"
    return 0
}

# ============================================================
#                    TESTY DEADLOCKÓW
# ============================================================

test_deadlock_01_high_concurrency() {
    log_test "DEADLOCK 01: Wysoka współbieżność (100 turystów, szybkie generowanie)"
    cleanup

    timeout 60 ../bin/main -t 50 -n 100 > "${LOG_PREFIX}_test01.log" 2>&1 &
    local pid=$!

    detect_deadlock "$pid" 60 "test01"
    local result=$?

    if [ $result -eq 2 ]; then
        log_error "DEADLOCK wykryty w teście 01"
        return 1
    elif [ $result -eq 1 ]; then
        log_error "Test 01 timeout"
        return 1
    else
        log_success "Test 01 - Brak deadlocka"
        return 0
    fi
}

test_deadlock_02_extreme_load() {
    log_test "DEADLOCK 02: Ekstremalne obciążenie (300 turystów)"
    cleanup

    timeout 90 ./bin/main -t 80 -n 300 > "${LOG_PREFIX}_test02.log" 2>&1 &
    local pid=$!

    detect_deadlock "$pid" 90 "test02"
    local result=$?

    if [ $result -eq 2 ]; then
        log_error "DEADLOCK wykryty w teście 02"
        return 1
    elif [ $result -eq 1 ]; then
        log_error "Test 02 timeout"
        return 1
    else
        log_success "Test 02 - Brak deadlocka"
        return 0
    fi
}

test_deadlock_03_rapid_tourists() {
    log_test "DEADLOCK 03: Bardzo szybkie generowanie turystów (200 turystów, krótki czas)"
    cleanup

    timeout 50 ./bin/main -t 40 -n 200 > "${LOG_PREFIX}_test03.log" 2>&1 &
    local pid=$!

    detect_deadlock "$pid" 50 "test03"
    local result=$?

    if [ $result -eq 2 ]; then
        log_error "DEADLOCK wykryty w teście 03"
        return 1
    elif [ $result -eq 1 ]; then
        log_error "Test 03 timeout"
        return 1
    else
        log_success "Test 03 - Brak deadlocka"
        return 0
    fi
}

test_deadlock_04_long_duration() {
    log_test "DEADLOCK 04: Długotrwały test stabilności (150s, 150 turystów)"
    cleanup

    log_warning "Test długotrwały - około 2.5 minuty..."

    timeout 180 ./bin/main -t 150 -n 150 > "${LOG_PREFIX}_test04.log" 2>&1 &
    local pid=$!

    detect_deadlock "$pid" 180 "test04"
    local result=$?

    if [ $result -eq 2 ]; then
        log_error "DEADLOCK wykryty w teście 04"
        return 1
    elif [ $result -eq 1 ]; then
        log_error "Test 04 timeout"
        return 1
    else
        log_success "Test 04 - System stabilny, brak deadlocka"
        return 0
    fi
}

test_deadlock_05_stress_test() {
    log_test "DEADLOCK 05: STRESS TEST - Maksymalne obciążenie (500 turystów)"
    cleanup

    log_warning "To może mocno obciążyć system..."

    timeout 120 ./bin/main -t 100 -n 500 > "${LOG_PREFIX}_test05.log" 2>&1 &
    local pid=$!

    detect_deadlock "$pid" 120 "test05"
    local result=$?

    if [ $result -eq 2 ]; then
        log_error "DEADLOCK wykryty w teście 05"
        return 1
    elif [ $result -eq 1 ]; then
        log_error "Test 05 timeout lub system nie udźwignął obciążenia"
        return 1
    else
        log_success "Test 05 - System wytrzymał maksymalne obciążenie bez deadlocka!"
        return 0
    fi
}

test_deadlock_06_repeated_runs() {
    log_test "DEADLOCK 06: Test powtarzalności (3 uruchomienia po 40s)"

    local all_passed=0

    for i in 1 2 3; do
        log_warning "Uruchomienie $i/3..."
        cleanup

        timeout 50 ./bin/main -t 40 -n 80 > "${LOG_PREFIX}_test06_run${i}.log" 2>&1 &
        local pid=$!

        detect_deadlock "$pid" 50 "test06_run${i}"
        local result=$?

        if [ $result -eq 2 ]; then
            log_error "DEADLOCK w uruchomieniu $i"
            all_passed=$((all_passed + 1))
        elif [ $result -eq 1 ]; then
            log_error "Timeout w uruchomieniu $i"
            all_passed=$((all_passed + 1))
        else
            log_success "Uruchomienie $i OK"
        fi

        sleep 3
    done

    if [ $all_passed -eq 0 ]; then
        log_success "Test 06 - Wszystkie 3 uruchomienia bez deadlocka"
        return 0
    else
        log_error "Test 06 - $all_passed/3 uruchomień z problemami"
        return 1
    fi
}

test_deadlock_07_resource_exhaustion() {
    log_test "DEADLOCK 07: Test wyczerpania zasobów IPC"
    cleanup

    log_warning "Sprawdzanie limitów IPC przed testem..."

    echo "=== LIMITY IPC PRZED TESTEM ===" > "${LOG_PREFIX}_test07_limits_before.log"
    ipcs -l >> "${LOG_PREFIX}_test07_limits_before.log" 2>&1
    ipcs -u >> "${LOG_PREFIX}_test07_limits_before.log" 2>&1

    timeout 70 ./bin/main -t 60 -n 250 > "${LOG_PREFIX}_test07.log" 2>&1 &
    local pid=$!

    detect_deadlock "$pid" 70 "test07"
    local result=$?

    echo "=== LIMITY IPC PO TEŚCIE ===" > "${LOG_PREFIX}_test07_limits_after.log"
    ipcs -l >> "${LOG_PREFIX}_test07_limits_after.log" 2>&1
    ipcs -u >> "${LOG_PREFIX}_test07_limits_after.log" 2>&1

    if [ $result -eq 2 ]; then
        log_error "DEADLOCK przy wyczerpaniu zasobów"
        return 1
    elif [ $result -eq 1 ]; then
        log_error "Test 07 timeout"
        return 1
    else
        log_success "Test 07 - System radzi sobie z wyczerpaniem zasobów"
        return 0
    fi
}

test_deadlock_08_signal_stress() {
    log_test "DEADLOCK 08: Test odporności na sygnały podczas obciążenia"
    cleanup

    ./bin/main -t 90 -n 150 > "${LOG_PREFIX}_test08.log" 2>&1 &
    local main_pid=$!

    # Czekaj na stabilizację
    sleep 10

    # Wysyłaj różne sygnały co jakiś czas
    for i in {1..5}; do
        sleep 8
        log_warning "Wysyłam sygnał SIGUSR1 do procesu głównego..."
        kill -USR1 $main_pid 2>/dev/null || true
    done

    detect_deadlock "$main_pid" 100 "test08"
    local result=$?

    if [ $result -eq 2 ]; then
        log_error "DEADLOCK po wysłaniu sygnałów"
        return 1
    elif [ $result -eq 1 ]; then
        log_error "Test 08 timeout"
        return 1
    else
        log_success "Test 08 - System odporny na sygnały"
        return 0
    fi
}

# ============================================================
#                  RAPORT KOŃCOWY
# ============================================================

generate_report() {
    local passed=$1
    local failed=$2
    local total=$3

    local report_file="${LOG_PREFIX}_FINAL_REPORT.txt"

    cat > "$report_file" <<EOF
╔═══════════════════════════════════════════════════════════╗
║          RAPORT KOŃCOWY - TESTY DEADLOCKÓW                ║
╚═══════════════════════════════════════════════════════════╝

Data wykonania: $(date)
Czas trwania: ~15-20 minut

═══════════════════════════════════════════════════════════

WYNIKI:
-------
Testy zaliczone:     ${passed}/${total}
Testy niezaliczone:  ${failed}/${total}
Procent sukcesu:     $(( passed * 100 / total ))%

═══════════════════════════════════════════════════════════

PODSUMOWANIE TESTÓW:

1. Test współbieżności (100 turystów)
2. Test ekstremalnego obciążenia (300 turystów)
3. Test szybkiego generowania (200 turystów)
4. Test długotrwałej stabilności (150s)
5. Stress test (500 turystów)
6. Test powtarzalności (3x)
7. Test wyczerpania zasobów IPC
8. Test odporności na sygnały

═══════════════════════════════════════════════════════════

ZALECENIA:

EOF

    if [ $failed -eq 0 ]; then
        cat >> "$report_file" <<EOF
✓ WSZYSTKIE TESTY ZALICZONE!

System nie wykazuje podatności na deadlocki w testowanych
scenariuszach. Implementacja synchronizacji jest prawidłowa.

Zalecenia:
- System jest gotowy do użycia
- Rozważ dodatkowe testy obciążeniowe w środowisku produkcyjnym
- Monitoruj zużycie zasobów IPC w rzeczywistym użyciu
EOF
    else
        cat >> "$report_file" <<EOF
✗ WYKRYTO PROBLEMY Z DEADLOCKAMI!

System wykazuje podatność na deadlocki w niektórych
scenariuszach testowych.

Zalecenia:
- Przeanalizuj szczegółowe logi w katalogu: ${RESULTS_DIR}
- Sprawdź implementację synchronizacji (semafory, mutexy)
- Upewnij się, że zasoby są zawsze zwalniane
- Sprawdź kolejność nabywania blokad (lock ordering)
- Rozważ użycie timeout'ów przy oczekiwaniu na zasoby
- Przeanalizuj pliki *_detection.log dla szczegółów

KRYTYCZNE PLIKI DO ANALIZY:
$(ls -1 ${LOG_PREFIX}_*_detection.log 2>/dev/null | tail -n 5)
EOF
    fi

    cat >> "$report_file" <<EOF

═══════════════════════════════════════════════════════════

Pełne logi: ${RESULTS_DIR}
Prefix logów: $(basename $LOG_PREFIX)

═══════════════════════════════════════════════════════════
EOF

    echo ""
    cat "$report_file"
    echo ""
    log_warning "Raport zapisany w: $report_file"
}

# ============================================================
#                    GŁÓWNA FUNKCJA
# ============================================================

run_all_deadlock_tests() {
    local passed=0
    local failed=0
    local total=8

    echo ""
    echo "╔═══════════════════════════════════════════════════════════╗"
    echo "║      KOMPLEKSOWE TESTY DEADLOCKÓW - KOLEJ LINOWA         ║"
    echo "╚═══════════════════════════════════════════════════════════╝"
    echo ""
    log_warning "Czas wykonania: ~15-20 minut"
    log_warning "Testy będą automatycznie wykrywać deadlocki"
    echo ""

    mkdir -p "$RESULTS_DIR"

    # Kompilacja
    log_test "Sprawdzanie kompilacji..."
    if command -v make >/dev/null 2>&1; then
        if ! make clean all >/dev/null 2>&1; then
            log_error "Kompilacja nie powiodła się!"
            exit 1
        fi
    else
        log_warning "make nie jest dostępny, sprawdzam czy binaria istnieją..."
        if [ ! -f "bin/main" ]; then
            log_error "Brak pliku bin/main i brak make do kompilacji!"
            exit 1
        fi
        chmod +x bin/* 2>/dev/null || true
    fi
    log_success "Kompilacja/Binaria OK"
    echo ""

    # Uruchom testy
    test_deadlock_01_high_concurrency && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_deadlock_02_extreme_load && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_deadlock_03_rapid_tourists && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_deadlock_04_long_duration && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_deadlock_05_stress_test && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_deadlock_06_repeated_runs && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_deadlock_07_resource_exhaustion && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    test_deadlock_08_signal_stress && passed=$((passed + 1)) || failed=$((failed + 1))
    echo ""

    # Generuj raport
    generate_report $passed $failed $total

    cleanup

    if [ $failed -eq 0 ]; then
        return 0
    else
        return 1
    fi
}

# ============================================================
#                         MAIN
# ============================================================

trap cleanup EXIT INT TERM

if [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
    echo "Testy deadlocków - Kolej Linowa"
    echo ""
    echo "Użycie: $0"
    echo ""
    echo "Automatycznie uruchamia 8 testów wykrywających deadlocki:"
    echo "  1. Wysoka współbieżność"
    echo "  2. Ekstremalne obciążenie"
    echo "  3. Szybkie generowanie turystów"
    echo "  4. Długotrwała stabilność"
    echo "  5. Stress test"
    echo "  6. Test powtarzalności"
    echo "  7. Wyczerpanie zasobów IPC"
    echo "  8. Odporność na sygnały"
    echo ""
    echo "Czas wykonania: ~15-20 minut"
    echo "Wyniki w: ${RESULTS_DIR}/"
    exit 0
fi

run_all_deadlock_tests
exit $?
