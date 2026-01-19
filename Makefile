CC = gcc
CFLAGS = -Wall -Wextra -g -I./include
LDFLAGS = -lpthread -lrt

SRC_DIR = src
OBJ_DIR = obj
BIN_DIR = bin

SOURCES = $(wildcard $(SRC_DIR)/*.c)
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
EXECUTABLE = $(BIN_DIR)/kolej_linowa

.PHONY: all clean directories

all: directories $(EXECUTABLE)

directories:
	@mkdir -p $(OBJ_DIR) $(BIN_DIR) logs

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) logs/*.txt
	ipcrm -a 2>/dev/null || true

run: all
	./$(EXECUTABLE)

# Czyszczenie zasobów IPC w razie problemów
clean-ipc:
	ipcrm -a 2>/dev/null || true
	rm -f /dev/shm/sem_*