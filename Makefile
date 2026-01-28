CC = gcc
CFLAGS = -Wall -Wextra -g -I./include
LDFLAGS = -lpthread -lrt

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin
INC_DIR = include

# Wspólne źródła
COMMON_SRC = $(SRC_DIR)/ipc_utils.c $(SRC_DIR)/logger.c $(SRC_DIR)/pipe_comm.c
COMMON_OBJ = $(OBJ_DIR)/ipc_utils.o $(OBJ_DIR)/logger.o $(OBJ_DIR)/pipe_comm.o

# Programy wykonywalne
MAIN = $(BIN_DIR)/kolej_linowa
KASJER = $(BIN_DIR)/kasjer
PRACOWNIK1 = $(BIN_DIR)/pracownik1
PRACOWNIK2 = $(BIN_DIR)/pracownik2
TURYSTA = $(BIN_DIR)/turysta

ALL_BINS = $(MAIN) $(KASJER) $(PRACOWNIK1) $(PRACOWNIK2) $(TURYSTA)

.PHONY: all clean directories run clean-ipc

all: directories $(ALL_BINS)

directories:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) logs

# Kompilacja obiektów wspólnych
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Program główny
$(MAIN): $(OBJ_DIR)/main.o $(COMMON_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

# Kasjer - osobny program
$(KASJER): $(OBJ_DIR)/kasjer.o $(COMMON_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

# Pracownik 1
$(PRACOWNIK1): $(OBJ_DIR)/pracownik1.o $(COMMON_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

# Pracownik 2
$(PRACOWNIK2): $(OBJ_DIR)/pracownik2.o $(COMMON_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

# Turysta
$(TURYSTA): $(OBJ_DIR)/turysta.o $(COMMON_OBJ)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) logs/*.txt logs/*.log

run: all
	./$(MAIN)

# Czyszczenie zasobów IPC
clean-ipc:
	ipcrm -a 2>/dev/null || true
	rm -f /tmp/kolej_* 2>/dev/null || true
	rm -f /dev/shm/sem.kolej* 2>/dev/null || true

# Pełne czyszczenie
distclean: clean clean-ipc