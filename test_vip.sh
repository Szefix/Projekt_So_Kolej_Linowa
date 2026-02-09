#!/bin/bash
# ============================================================
#    TEST VIP - Weryfikacja trybu wymuszenia VIP
# ============================================================
# Uruchamia symulacje z flaga -V i sprawdza, czy WSZYSCY
# turysci maja status VIP i wchodza bez kolejki.

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

RESULTS_DIR="test_results"
LOG_PREFIX="${RESULTS_DIR}/test_vip_$(date +%Y%m%d_%H%M%S)"

log_test() {
    echo -e "${BLUE}[TEST]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[PASS]${NC} $1"
}

log_error() {
    echo -e "${RED}[FAIL]${NC} $1"
}

cleanup() {
    pkill -9 -f "bin/main" 2>/dev/null || true
    pkill -9 -f "bin/kasjer" 2>/dev/null || true
    pkill -9 -f "bin/pracownik" 2>/dev/null || true
    pkill -9 -f "bin/turysta" 2>/dev/null || true
    make clean-ipc >/dev/null 2>&1 || true
    sleep 1
}

trap cleanup EXIT INT TERM

# Kompilacja
log_test "Kompilacja projektu..."
if ! make clean all >/dev/null 2>&1; then
    log_error "Kompilacja nie powiodla sie!"
    exit 1
fi
log_success "Kompilacja OK"

mkdir -p "$RESULTS_DIR"

# ============================================================
#                    TEST VIP
# ============================================================

test_vip_all_tourists() {
    log_test "TEST VIP: Wymuszenie VIP dla wszystkich turystow (30s, 200 turystow)"
    cleanup

    timeout 45 ./bin/main -t 30 -n 200 -V > "${LOG_PREFIX}_output.log" 2>&1
    local status=$?

    if [ $status -ne 0 ] && [ $status -ne 124 ]; then
        log_error "Symulacja nie zakonczyla sie poprawnie (status: $status)"
        return 1
    fi

    local logfile="logs/wszyscy_turysci.log"

    if [ ! -f "$logfile" ]; then
        log_error "Brak pliku logu: $logfile"
        return 1
    fi

    local passed=0
    local failed=0

    # --- Sprawdzenie 1: Wszyscy turysci maja status VIP ---
    local total_tourists=$(grep -c "Przychodzę" "$logfile" 2>/dev/null || true)
    local non_vip_tourists=$(grep "Przychodzę" "$logfile" 2>/dev/null | grep -c "zwykły" || true)

    log_test "  Sprawdzenie 1: Wszyscy turysci maja status VIP"
    echo "    Laczna liczba turystow: $total_tourists"
    echo "    Turysci nie-VIP: $non_vip_tourists"

    if [ "$total_tourists" -eq 0 ]; then
        log_error "  Brak turystow w logach - test niekonkluzywny"
        return 1
    fi

    if [ "$non_vip_tourists" -gt 0 ]; then
        log_error "  Znaleziono $non_vip_tourists turystow NIE-VIP (powinno byc 0)"
        failed=$((failed + 1))
    else
        log_success "  Wszyscy turysci ($total_tourists) maja status VIP"
        passed=$((passed + 1))
    fi

    # --- Sprawdzenie 2: VIP turysci wchodza bez kolejki ---
    local bez_kolejki=$(grep -c "\[VIP\].*bez kolejki" "$logfile" 2>/dev/null || true)

    log_test "  Sprawdzenie 2: Turysci VIP wchodza bez kolejki"
    echo "    Wpisy 'bez kolejki': $bez_kolejki"

    if [ "$bez_kolejki" -gt 0 ]; then
        log_success "  Znaleziono $bez_kolejki wpisow VIP 'bez kolejki'"
        passed=$((passed + 1))
    else
        log_error "  Brak wpisow VIP 'bez kolejki' w logach"
        failed=$((failed + 1))
    fi

    # --- Sprawdzenie 3: Zaden turysta nie czeka w zwyklej kolejce ---
    local czekam_na_stacje=$(grep -c "Czekam na miejsce na stacji" "$logfile" 2>/dev/null || true)

    log_test "  Sprawdzenie 3: Zaden turysta nie czeka w zwyklej kolejce"
    echo "    Wpisy 'Czekam na miejsce na stacji': $czekam_na_stacje"

    if [ "$czekam_na_stacje" -eq 0 ]; then
        log_success "  Zaden turysta nie uzywa zwyklej kolejki"
        passed=$((passed + 1))
    else
        log_error "  Znaleziono $czekam_na_stacje turystow w zwyklej kolejce (powinno byc 0)"
        failed=$((failed + 1))
    fi

    # --- Podsumowanie ---
    echo ""
    echo "  Wynik: $passed/3 sprawdzen zaliczonych"

    if [ "$failed" -eq 0 ]; then
        log_success "TEST VIP PASSED - Wszystkie sprawdzenia zaliczone"
        return 0
    else
        log_error "TEST VIP FAILED - $failed sprawdzen niezaliczonych"
        return 1
    fi
}

# ============================================================
#                    MAIN
# ============================================================

echo ""
echo "============================================================"
echo "         TEST VIP - KOLEJ LINOWA KRZESEŁKOWA"
echo "============================================================"
echo ""

test_vip_all_tourists
exit $?
