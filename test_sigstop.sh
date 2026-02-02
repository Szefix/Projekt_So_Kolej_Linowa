#!/bin/bash
# Test SIGSTOP/SIGCONT dla symulacji kolei linowej
cd "$(dirname "$0")"

echo "=== CZYSZCZENIE ==="
ipcrm -a 2>/dev/null
rm -f logs/*.log 2>/dev/null

echo "=== URUCHAMIANIE SYMULACJI ==="
timeout 40 ./bin/main -n 30 -t 35 > /dev/null 2>&1 &
MAIN_PID=$!
echo "Main PID: $MAIN_PID"

sleep 4

echo ""
echo "=== PRZED ZATRZYMANIEM ==="
BILETY1=$(grep -c "bilet #" logs/kasjer.log 2>/dev/null || echo 0)
echo "Bilety: $BILETY1"

echo ""
echo "=== ZATRZYMUJE TURYSTOW (kill -STOP) ==="
ps aux | grep "./bin/turysta" | grep -v grep | awk '{print $2}' | while read pid; do
    kill -STOP $pid 2>/dev/null
    echo "Zatrzymano PID: $pid"
done

sleep 5

echo ""
echo "=== PO ZATRZYMANIU ==="
BILETY2=$(grep -c "bilet #" logs/kasjer.log 2>/dev/null || echo 0)
echo "Bilety: $BILETY2"

echo ""
echo "=== WZNAWIAM TURYSTOW (kill -CONT) ==="
ps aux | grep "./bin/turysta" | grep -v grep | awk '{print $2}' | while read pid; do
    kill -CONT $pid 2>/dev/null
    echo "Wznowiono PID: $pid"
done

sleep 6

echo ""
echo "=== PO WZNOWIENIU ==="
BILETY3=$(grep -c "bilet #" logs/kasjer.log 2>/dev/null || echo 0)
echo "Bilety: $BILETY3"

echo ""
echo "=== WYNIK ==="
if [ "$BILETY3" -gt "$BILETY2" ]; then
    echo "*** SUKCES: Bilety wzrosly po wznowieniu! ($BILETY2 -> $BILETY3) ***"
else
    echo "Bilety nie wzrosly ($BILETY2 -> $BILETY3)"
    echo "Mozliwe ze turyści juz kupili wszystkie bilety przed wznowieniem"
fi

# Poczekaj na zakonczenie lub zabij
wait $MAIN_PID 2>/dev/null
killall -9 main turysta kasjer pracownik1 pracownik2 2>/dev/null

echo ""
echo "=== KONIEC TESTU ==="
