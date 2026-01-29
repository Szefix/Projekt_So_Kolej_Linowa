#!/bin/bash
# ============================================================
#     ANALIZATOR DEADLOCKÓW W CZASIE RZECZYWISTYM
# ============================================================
# Monitoruje działający program i wykrywa deadlocki

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

if [ -z "$1" ]; then
    echo "Użycie: $0 <PID_procesu_main>"
    echo ""
    echo "Przykład: $0 12345"
    echo ""
    echo "Ten skrypt monitoruje proces i wykrywa deadlocki w czasie rzeczywistym."
    exit 1
fi

TARGET_PID=$1

if ! kill -0 "$TARGET_PID" 2>/dev/null; then
    echo -e "${RED}Proces $TARGET_PID nie istnieje lub nie masz uprawnień${NC}"
    exit 1
fi

echo -e "${BLUE}╔═══════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║     ANALIZATOR DEADLOCKÓW - MONITORING W CZASIE RZECZ.   ║${NC}"
echo -e "${BLUE}╚═══════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${GREEN}Monitorowany proces: $TARGET_PID${NC}"
echo -e "${YELLOW}Naciśnij Ctrl+C aby zatrzymać monitoring${NC}"
echo ""

last_log_lines=0
stuck_counter=0
last_cpu_time=0
cpu_stuck_counter=0

while kill -0 "$TARGET_PID" 2>/dev/null; do
    clear
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}  DEADLOCK ANALYZER - $(date +%H:%M:%S)${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo ""

    # Sprawdź postęp w logach
    current_log_lines=$(cat logs/*.log 2>/dev/null | wc -l || echo "0")
    log_delta=$((current_log_lines - last_log_lines))

    if [ $log_delta -eq 0 ]; then
        stuck_counter=$((stuck_counter + 1))
        echo -e "${YELLOW}[!] Brak postępu w logach (${stuck_counter} iteracji)${NC}"
    else
        echo -e "${GREEN}[✓] Postęp w logach: +${log_delta} linii${NC}"
        stuck_counter=0
    fi

    last_log_lines=$current_log_lines

    # Sprawdź CPU
    current_cpu_time=$(ps -p "$TARGET_PID" -o cputime --no-headers 2>/dev/null | tr -d ' :' || echo "0")

    if [ "$current_cpu_time" == "$last_cpu_time" ]; then
        cpu_stuck_counter=$((cpu_stuck_counter + 1))
        echo -e "${YELLOW}[!] CPU time się nie zmienia (${cpu_stuck_counter} iteracji)${NC}"
    else
        echo -e "${GREEN}[✓] Proces wykorzystuje CPU${NC}"
        cpu_stuck_counter=0
    fi

    last_cpu_time=$current_cpu_time

    # Statystyki procesu głównego
    echo ""
    echo -e "${BLUE}─── Proces główny ───${NC}"
    ps -p "$TARGET_PID" -o pid,ppid,state,%cpu,%mem,vsz,rss,cmd --no-headers

    # Procesy potomne
    echo ""
    echo -e "${BLUE}─── Procesy potomne ───${NC}"
    local children=$(pgrep -P "$TARGET_PID" 2>/dev/null)
    if [ -n "$children" ]; then
        ps -p "$children" -o pid,ppid,state,%cpu,%mem,cmd --no-headers 2>/dev/null | head -n 10
        local total_children=$(echo "$children" | wc -l)
        if [ $total_children -gt 10 ]; then
            echo -e "${YELLOW}... i jeszcze $((total_children - 10)) procesów${NC}"
        fi
    else
        echo "Brak procesów potomnych"
    fi

    # Procesy zablokowane
    echo ""
    echo -e "${BLUE}─── Procesy zablokowane (stan D) ───${NC}"
    local blocked=$(ps -eo pid,state,wchan:20,cmd | grep -E "kasjer|pracownik|turysta" | grep "D " || echo "")
    if [ -n "$blocked" ]; then
        echo -e "${RED}$blocked${NC}"
    else
        echo -e "${GREEN}Brak zablokowanych procesów${NC}"
    fi

    # Procesy zombie
    echo ""
    echo -e "${BLUE}─── Procesy zombie (stan Z) ───${NC}"
    local zombies=$(ps -eo pid,state,cmd | grep -E "kasjer|pracownik|turysta" | grep "Z " || echo "")
    if [ -n "$zombies" ]; then
        echo -e "${RED}$zombies${NC}"
    else
        echo -e "${GREEN}Brak procesów zombie${NC}"
    fi

    # Semafory
    echo ""
    echo -e "${BLUE}─── Semafory IPC ───${NC}"
    local sem_count=$(ipcs -s 2>/dev/null | grep "^0x" | wc -l || echo "0")
    echo "Liczba semaforów: $sem_count"

    local sem_waiting=$(ipcs -s 2>/dev/null | awk 'NR>3 && $7 > 0 {count++} END {print count+0}')
    if [ "$sem_waiting" -gt 0 ]; then
        echo -e "${YELLOW}Semafory z oczekującymi procesami: $sem_waiting${NC}"
    fi

    # Kolejki komunikatów
    echo ""
    echo -e "${BLUE}─── Kolejki komunikatów ───${NC}"
    local msgq_count=$(ipcs -q 2>/dev/null | grep "^0x" | wc -l || echo "0")
    echo "Liczba kolejek: $msgq_count"

    local full_queues=$(ipcs -q 2>/dev/null | awk 'NR>3 && $5 > 1000 {count++} END {print count+0}')
    if [ "$full_queues" -gt 0 ]; then
        echo -e "${YELLOW}Kolejki prawie pełne: $full_queues${NC}"
    fi

    # DETEKCJA DEADLOCKA
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"

    if [ $stuck_counter -ge 6 ] && [ $cpu_stuck_counter -ge 3 ]; then
        echo -e "${RED}╔═══════════════════════════════════════════════════════╗${NC}"
        echo -e "${RED}║  ⚠⚠⚠  PRAWDOPODOBNY DEADLOCK WYKRYTY!  ⚠⚠⚠        ║${NC}"
        echo -e "${RED}╚═══════════════════════════════════════════════════════╝${NC}"
        echo ""
        echo -e "${YELLOW}Zalecenia:${NC}"
        echo "1. Przerwij proces: kill -9 $TARGET_PID"
        echo "2. Sprawdź logi w: logs/"
        echo "3. Przeanalizuj synchronizację w kodzie"
        echo "4. Użyj: ipcs -s aby zobaczyć semafory"
        echo ""
    elif [ $stuck_counter -ge 3 ]; then
        echo -e "${YELLOW}[!] OSTRZEŻENIE: Podejrzenie deadlocka (brak postępu przez $(($stuck_counter * 3))s)${NC}"
    else
        echo -e "${GREEN}[✓] System działa normalnie${NC}"
    fi

    sleep 3
done

echo ""
echo -e "${RED}Proces $TARGET_PID zakończył się${NC}"
