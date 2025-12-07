# moba

A simple terminal-based MOBA-style mini game.

## Build

### Linux/macOS
```
gcc -o moba main.c -lm -lpthread
```

### Windows (MinGW)
```
gcc -o moba.exe main.c
```

The Windows build uses the bundled Win32 compatibility shims for threads,
mutexes, sleep, and keyboard input. Gameplay logic remains unchanged across
platforms.
