# ============================================================
#                    KOLEJ LINOWA KRZESEŁKOWA
#                         Makefile
# ============================================================

CC = gcc
CFLAGS = -Wall -Wextra -pedantic -std=c11 -g -D_GNU_SOURCE
LDFLAGS = -pthread

# Katalogi
SRC_DIR = src
INC_DIR = include
BIN_DIR = bin
LOG_DIR = logs

# Pliki źródłowe
COMMON_SRC = $(SRC_DIR)/ipc_utils.c $(SRC_DIR)/pipe_comm.c $(SRC_DIR)/logger.c

# Programy
PROGRAMS = $(BIN_DIR)/main $(BIN_DIR)/kasjer $(BIN_DIR)/pracownik1 \
           $(BIN_DIR)/pracownik2 $(BIN_DIR)/turysta

# ============================================================
#                      REGUŁY GŁÓWNE
# ============================================================

.PHONY: all clean clean-ipc clean-all run help

all: dirs $(PROGRAMS)
	@echo "=========================================="
	@echo "  Kompilacja zakończona pomyślnie!"
	@echo "  Uruchom: make run"
	@echo "=========================================="

dirs:
	@mkdir -p $(BIN_DIR) $(LOG_DIR)

# ============================================================
#                    KOMPILACJA PROGRAMÓW
# ============================================================

$(BIN_DIR)/main: $(SRC_DIR)/main.c $(COMMON_SRC)
	$(CC) $(CFLAGS) -I$(INC_DIR) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/kasjer: $(SRC_DIR)/kasjer.c $(COMMON_SRC)
	$(CC) $(CFLAGS) -I$(INC_DIR) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/pracownik1: $(SRC_DIR)/pracownik1.c $(COMMON_SRC)
	$(CC) $(CFLAGS) -I$(INC_DIR) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/pracownik2: $(SRC_DIR)/pracownik2.c $(COMMON_SRC)
	$(CC) $(CFLAGS) -I$(INC_DIR) $^ -o $@ $(LDFLAGS)

$(BIN_DIR)/turysta: $(SRC_DIR)/turysta.c $(COMMON_SRC)
	$(CC) $(CFLAGS) -I$(INC_DIR) $^ -o $@ $(LDFLAGS)

# ============================================================
#                    URUCHAMIANIE
# ============================================================

run: all
	@echo "Uruchamianie symulacji kolei linowej..."
	@./$(BIN_DIR)/main

run-short: all
	@echo "Uruchamianie krótkiej symulacji (30s)..."
	@./$(BIN_DIR)/main -t 30 -n 20

run-long: all
	@echo "Uruchamianie długiej symulacji (120s)..."
	@./$(BIN_DIR)/main -t 120 -n 100

# ============================================================
#                    CZYSZCZENIE
# ============================================================

clean:
	rm -rf $(BIN_DIR)
	rm -rf $(LOG_DIR)/*.log $(LOG_DIR)/*.txt
	@echo "Usunięto pliki binarne i logi"

clean-ipc:
	@echo "Czyszczenie zasobów IPC..."
	-ipcrm -a 2>/dev/null || true
	-rm -f /tmp/kolej_* 2>/dev/null || true
	@echo "Zasoby IPC wyczyszczone"

clean-all: clean clean-ipc
	@echo "Wszystko wyczyszczone"

# ============================================================
#                      POMOC
# ============================================================

help:
	@echo "=============================================="
	@echo "  KOLEJ LINOWA KRZESEŁKOWA - Makefile"
	@echo "=============================================="
	@echo ""
	@echo "Cele:"
	@echo "  all        - Kompilacja wszystkich programów"
	@echo "  run        - Uruchomienie symulacji (domyślne parametry)"
	@echo "  run-short  - Krótka symulacja (30s, 20 turystów)"
	@echo "  run-long   - Długa symulacja (120s, 100 turystów)"
	@echo "  clean      - Usunięcie plików binarnych i logów"
	@echo "  clean-ipc  - Czyszczenie zasobów IPC"
	@echo "  clean-all  - Pełne czyszczenie"
	@echo "  help       - Ta pomoc"
	@echo ""
	@echo "Parametry programu:"
	@echo "  ./bin/main -t <czas> -n <liczba_turystow>"
	@echo "  -t czas    Czas symulacji (10-3600 sekund)"
	@echo "  -n liczba  Max turystów (1-500)"
	@echo ""
