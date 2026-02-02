#!/bin/bash
# ============================================================
#       GŁÓWNY SKRYPT TESTOWY - URUCHAMIA WSZYSTKIE TESTY
# ============================================================
# Uruchamia po kolei:
#   1. test_suite.sh       - Testy funkcjonalne i obciążeniowe
#   2. test_ipc_mechanisms.sh - Testy mechanizmów IPC
#   3. run_deadlock_tests.sh  - Testy wykrywania deadlocków
# ============================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

# Katalog na wyniki wszystkich testów
MASTER_RESULTS_DIR="all_tests_results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

# Utwórz katalog OD RAZU na początku
mkdir -p "$MASTER_RESULTS_DIR"

MASTER_LOG="${MASTER_RESULTS_DIR}/master_log_${TIMESTAMP}.txt"

# Liczniki
SUITE_PASSED=0
SUITE_FAILED=0

# ============================================================
#                    FUNKCJE POMOCNICZE
# ============================================================

log_header() {
    echo -e "${CYAN}╔════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║${NC}  $1"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════╝${NC}"
}

log_master() {
    echo -e "${MAGENTA}[MASTER]${NC} $1"
    echo "[$(date '+%H:%M:%S')] $1" >> "$MASTER_LOG"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
    echo "[PASSED] $1" >> "$MASTER_LOG"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
    echo "[FAILED] $1" >> "$MASTER_LOG"
}

log_warning() {
    echo -e "${YELLOW}[!]${NC} $1"
    echo "[WARNING] $1" >> "$MASTER_LOG" 2>/dev/null || true
}

cleanup_all() {
    echo -e "${YELLOW}[!]${NC} Czyszczenie wszystkich zasobów..."
    pkill -9 -f "bin/main" 2>/dev/null || true
    pkill -9 -f "bin/kasjer" 2>/dev/null || true
    pkill -9 -f "bin/pracownik" 2>/dev/null || true
    pkill -9 -f "bin/turysta" 2>/dev/null || true
    make clean-ipc >/dev/null 2>&1 || true
    sleep 2
}

# ============================================================
#                    URUCHOMIENIE TESTÓW
# ============================================================

run_test_suite() {
    log_header "1/3 - TEST SUITE (Testy funkcjonalne i obciążeniowe)"
    echo ""

    if [ -f "./test_suite.sh" ]; then
        log_master "Uruchamiam test_suite.sh --all (wyniki każdego testu będą widoczne)..."
        echo ""

        chmod +x ./test_suite.sh 2>/dev/null || true

        # Użyj tee żeby wyświetlać wyniki na ekranie I zapisywać do pliku
        if ./test_suite.sh --all 2>&1 | tee "${MASTER_RESULTS_DIR}/test_suite_${TIMESTAMP}.log"; then
            log_success "test_suite.sh zakończony pomyślnie"
            SUITE_PASSED=$((SUITE_PASSED + 1))
            return 0
        else
            log_error "test_suite.sh zakończony z błędami"
            SUITE_FAILED=$((SUITE_FAILED + 1))
            return 1
        fi
    else
        log_error "Nie znaleziono test_suite.sh!"
        SUITE_FAILED=$((SUITE_FAILED + 1))
        return 1
    fi
}

run_ipc_tests() {
    log_header "2/3 - TEST IPC MECHANISMS (Testy mechanizmów IPC)"
    echo ""

    if [ -f "./test_ipc_mechanisms.sh" ]; then
        log_master "Uruchamiam test_ipc_mechanisms.sh --all (wyniki każdego testu będą widoczne)..."
        echo ""

        chmod +x ./test_ipc_mechanisms.sh 2>/dev/null || true

        # Użyj tee żeby wyświetlać wyniki na ekranie I zapisywać do pliku
        if ./test_ipc_mechanisms.sh --all 2>&1 | tee "${MASTER_RESULTS_DIR}/test_ipc_${TIMESTAMP}.log"; then
            log_success "test_ipc_mechanisms.sh zakończony pomyślnie"
            SUITE_PASSED=$((SUITE_PASSED + 1))
            return 0
        else
            log_error "test_ipc_mechanisms.sh zakończony z błędami"
            SUITE_FAILED=$((SUITE_FAILED + 1))
            return 1
        fi
    else
        log_error "Nie znaleziono test_ipc_mechanisms.sh!"
        SUITE_FAILED=$((SUITE_FAILED + 1))
        return 1
    fi
}

run_deadlock_tests() {
    log_header "3/3 - DEADLOCK TESTS (Testy wykrywania deadlocków)"
    echo ""

    if [ -f "./run_deadlock_tests.sh" ]; then
        log_master "Uruchamiam run_deadlock_tests.sh (wyniki każdego testu będą widoczne)..."
        echo ""

        chmod +x ./run_deadlock_tests.sh 2>/dev/null || true

        # Użyj tee żeby wyświetlać wyniki na ekranie I zapisywać do pliku
        if ./run_deadlock_tests.sh 2>&1 | tee "${MASTER_RESULTS_DIR}/deadlock_${TIMESTAMP}.log"; then
            log_success "run_deadlock_tests.sh zakończony pomyślnie"
            SUITE_PASSED=$((SUITE_PASSED + 1))
            return 0
        else
            log_error "run_deadlock_tests.sh zakończony z błędami"
            SUITE_FAILED=$((SUITE_FAILED + 1))
            return 1
        fi
    else
        log_error "Nie znaleziono run_deadlock_tests.sh!"
        SUITE_FAILED=$((SUITE_FAILED + 1))
        return 1
    fi
}

# ============================================================
#                    RAPORT KOŃCOWY
# ============================================================

generate_final_report() {
    local total=$((SUITE_PASSED + SUITE_FAILED))
    local report_file="${MASTER_RESULTS_DIR}/FINAL_REPORT_${TIMESTAMP}.txt"

    cat > "$report_file" <<EOF
╔═══════════════════════════════════════════════════════════════════╗
║           RAPORT KOŃCOWY - WSZYSTKIE TESTY                        ║
╚═══════════════════════════════════════════════════════════════════╝

Data wykonania: $(date)
Katalog wyników: ${MASTER_RESULTS_DIR}

═══════════════════════════════════════════════════════════════════

PODSUMOWANIE ZESTAWÓW TESTÓW:
─────────────────────────────
EOF

    echo "" >> "$report_file"

    if [ -f "${MASTER_RESULTS_DIR}/test_suite_${TIMESTAMP}.log" ]; then
        echo "1. TEST SUITE (funkcjonalne i obciążeniowe):" >> "$report_file"
        if grep -q "WSZYSTKIE TESTY ZALICZONE" "${MASTER_RESULTS_DIR}/test_suite_${TIMESTAMP}.log" 2>/dev/null; then
            echo "   Status: ✓ PASSED" >> "$report_file"
        else
            echo "   Status: ✗ FAILED lub częściowo zaliczone" >> "$report_file"
        fi
        echo "   Log: test_suite_${TIMESTAMP}.log" >> "$report_file"
    else
        echo "1. TEST SUITE: NIE URUCHOMIONY" >> "$report_file"
    fi

    echo "" >> "$report_file"

    if [ -f "${MASTER_RESULTS_DIR}/test_ipc_${TIMESTAMP}.log" ]; then
        echo "2. TEST IPC MECHANISMS (mechanizmy IPC):" >> "$report_file"
        if grep -q "WSZYSTKIE TESTY IPC ZALICZONE" "${MASTER_RESULTS_DIR}/test_ipc_${TIMESTAMP}.log" 2>/dev/null; then
            echo "   Status: ✓ PASSED" >> "$report_file"
        else
            echo "   Status: ✗ FAILED lub częściowo zaliczone" >> "$report_file"
        fi
        echo "   Log: test_ipc_${TIMESTAMP}.log" >> "$report_file"
    else
        echo "2. TEST IPC MECHANISMS: NIE URUCHOMIONY" >> "$report_file"
    fi

    echo "" >> "$report_file"

    if [ -f "${MASTER_RESULTS_DIR}/deadlock_${TIMESTAMP}.log" ]; then
        echo "3. DEADLOCK TESTS (wykrywanie deadlocków):" >> "$report_file"
        if grep -q "WSZYSTKIE TESTY ZALICZONE" "${MASTER_RESULTS_DIR}/deadlock_${TIMESTAMP}.log" 2>/dev/null; then
            echo "   Status: ✓ PASSED" >> "$report_file"
        else
            echo "   Status: ✗ FAILED lub wykryto deadlocki" >> "$report_file"
        fi
        echo "   Log: deadlock_${TIMESTAMP}.log" >> "$report_file"
    else
        echo "3. DEADLOCK TESTS: NIE URUCHOMIONY" >> "$report_file"
    fi

    cat >> "$report_file" <<EOF

═══════════════════════════════════════════════════════════════════

WYNIK OGÓLNY:
─────────────
Zestawy zaliczone:     ${SUITE_PASSED}/${total}
Zestawy niezaliczone:  ${SUITE_FAILED}/${total}

EOF

    if [ $SUITE_FAILED -eq 0 ]; then
        cat >> "$report_file" <<EOF
╔═══════════════════════════════════════════════════════════════════╗
║                    ✓ WSZYSTKO ZALICZONE!                          ║
╚═══════════════════════════════════════════════════════════════════╝

System przeszedł wszystkie testy pomyślnie.
EOF
    else
        cat >> "$report_file" <<EOF
╔═══════════════════════════════════════════════════════════════════╗
║                    ✗ WYKRYTO PROBLEMY                             ║
╚═══════════════════════════════════════════════════════════════════╝

Niektóre zestawy testów nie przeszły pomyślnie.
Sprawdź szczegółowe logi w katalogu: ${MASTER_RESULTS_DIR}
EOF
    fi

    cat >> "$report_file" <<EOF

═══════════════════════════════════════════════════════════════════
PLIKI LOGÓW:
  - ${MASTER_LOG}
  - test_suite_${TIMESTAMP}.log
  - test_ipc_${TIMESTAMP}.log
  - deadlock_${TIMESTAMP}.log
═══════════════════════════════════════════════════════════════════
EOF

    # Wyświetl raport
    echo ""
    cat "$report_file"
    echo ""
    log_master "Raport zapisany: $report_file"
}

# ============================================================
#                         MAIN
# ============================================================

main() {
    # Inicjalizuj master log
    echo "=== MASTER TEST LOG ===" > "$MASTER_LOG"
    echo "Start: $(date)" >> "$MASTER_LOG"
    echo "" >> "$MASTER_LOG"

    echo ""
    echo -e "${CYAN}╔════════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${CYAN}║       GŁÓWNY SKRYPT TESTOWY - KOLEJ LINOWA KRZESEŁKOWA        ║${NC}"
    echo -e "${CYAN}║                                                                ║${NC}"
    echo -e "${CYAN}║   Uruchamia wszystkie zestawy testów po kolei:                ║${NC}"
    echo -e "${CYAN}║     1. test_suite.sh       - Testy funkcjonalne               ║${NC}"
    echo -e "${CYAN}║     2. test_ipc_mechanisms.sh - Testy IPC                     ║${NC}"
    echo -e "${CYAN}║     3. run_deadlock_tests.sh  - Testy deadlocków              ║${NC}"
    echo -e "${CYAN}╚════════════════════════════════════════════════════════════════╝${NC}"
    echo ""

    log_warning "UWAGA: Pełne testy mogą trwać 30-45 minut!"
    echo ""

    # Sprawdź kompilację
    log_master "Sprawdzanie kompilacji..."
    if ! make clean all >/dev/null 2>&1; then
        log_error "Kompilacja nie powiodła się!"
        exit 1
    fi
    log_success "Kompilacja OK"
    echo ""

    # Uruchom testy po kolei
    local start_time=$(date +%s)

    cleanup_all
    run_test_suite || true
    echo ""

    cleanup_all
    run_ipc_tests || true
    echo ""

    cleanup_all
    run_deadlock_tests || true
    echo ""

    local end_time=$(date +%s)
    local duration=$((end_time - start_time))
    local minutes=$((duration / 60))
    local seconds=$((duration % 60))

    log_master "Całkowity czas testów: ${minutes}m ${seconds}s"

    # Generuj raport końcowy
    generate_final_report

    cleanup_all

    if [ $SUITE_FAILED -eq 0 ]; then
        exit 0
    else
        exit 1
    fi
}

# ============================================================
#                    OBSŁUGA ARGUMENTÓW
# ============================================================

trap cleanup_all EXIT INT TERM

if [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
    echo "Główny skrypt testowy - Kolej Linowa Krzesełkowa"
    echo ""
    echo "Użycie: $0 [opcje]"
    echo ""
    echo "Opcje:"
    echo "  -h, --help        Wyświetl tę pomoc"
    echo "  -1, --suite       Uruchom tylko test_suite.sh"
    echo "  -2, --ipc         Uruchom tylko test_ipc_mechanisms.sh"
    echo "  -3, --deadlock    Uruchom tylko run_deadlock_tests.sh"
    echo ""
    echo "Bez opcji uruchamia wszystkie testy po kolei."
    echo ""
    echo "Czas wykonania: ~30-45 minut (wszystkie testy)"
    echo "Wyniki zapisywane w: ${MASTER_RESULTS_DIR}/"
    exit 0
fi

if [ "$1" == "-1" ] || [ "$1" == "--suite" ]; then
    echo "=== TYLKO TEST SUITE ===" > "$MASTER_LOG"
    make clean all >/dev/null 2>&1
    cleanup_all
    run_test_suite
    cleanup_all
    exit $?
fi

if [ "$1" == "-2" ] || [ "$1" == "--ipc" ]; then
    echo "=== TYLKO TEST IPC ===" > "$MASTER_LOG"
    make clean all >/dev/null 2>&1
    cleanup_all
    run_ipc_tests
    cleanup_all
    exit $?
fi

if [ "$1" == "-3" ] || [ "$1" == "--deadlock" ]; then
    echo "=== TYLKO DEADLOCK TESTS ===" > "$MASTER_LOG"
    make clean all >/dev/null 2>&1
    cleanup_all
    run_deadlock_tests
    cleanup_all
    exit $?
fi

# Domyślnie uruchom wszystko
main
ENDOFSCRIPT