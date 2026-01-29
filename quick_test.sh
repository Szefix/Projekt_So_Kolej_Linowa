#!/bin/bash
# ============================================================
#              SZYBKI TEST DEADLOCKÓW (5 min)
# ============================================================

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║         SZYBKI TEST DEADLOCKÓW (~5 minut)                ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""

cleanup() {
    pkill -9 -f "bin/main" 2>/dev/null || true
    pkill -9 -f "bin/kasjer" 2>/dev/null || true
    pkill -9 -f "bin/pracownik" 2>/dev/null || true
    pkill -9 -f "bin/turysta" 2>/dev/null || true
    make clean-ipc >/dev/null 2>&1 || true
}

trap cleanup EXIT INT TERM

# Kompilacja
echo -e "${BLUE}[1/4]${NC} Kompilacja..."
if ! make clean all >/dev/null 2>&1; then
    echo -e "${RED}Błąd kompilacji!${NC}"
    exit 1
fi
echo -e "${GREEN}✓ OK${NC}"
echo ""

# Test 1: Podstawowy
echo -e "${BLUE}[2/4]${NC} Test podstawowy (30s, 50 turystów)..."
cleanup
timeout 40 ./bin/main -t 30 -n 50 >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ PASSED${NC}"
else
    echo -e "${RED}✗ FAILED lub TIMEOUT${NC}"
fi
echo ""

# Test 2: Średni
echo -e "${BLUE}[3/4]${NC} Test średni (40s, 100 turystów)..."
cleanup
timeout 50 ./bin/main -t 40 -n 100 >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ PASSED${NC}"
else
    echo -e "${RED}✗ FAILED lub TIMEOUT${NC}"
fi
echo ""

# Test 3: Wysoki
echo -e "${BLUE}[4/4]${NC} Test wysoki (50s, 200 turystów)..."
cleanup
timeout 65 ./bin/main -t 50 -n 200 >/dev/null 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✓ PASSED${NC}"
else
    echo -e "${RED}✗ FAILED lub TIMEOUT${NC}"
fi
echo ""

cleanup

echo -e "${GREEN}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  Szybki test zakończony!                                 ║${NC}"
echo -e "${GREEN}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""
echo "Aby wykonać pełne testy deadlocków (15-20 min):"
echo "  ./deadlock_tests/run_deadlock_tests.sh"
