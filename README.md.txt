# Kolej Linowa Krzesełkowa - Symulacja Systemu

## Opis projektu

Symulacja wieloprocesowego systemu obsługi kolei linowej krzesełkowej. 
Projekt demonstruje użycie mechanizmów IPC (Inter-Process Communication) w systemie UNIX/Linux:
- **Semafory POSIX** - synchronizacja dostępu i kontrola przepustowości
- **Pamięć współdzielona System V** - wspólny stan systemu
- **Kolejki komunikatów System V** - komunikacja między procesami
- **Sygnały UNIX** - zatrzymywanie/wznawianie kolei

## Architektura systemu