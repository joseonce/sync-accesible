@echo off
setlocal
cd /d "%~dp0"

echo Compilando recursos...
windres src/resource.rc -O coff -o resource.res
if errorlevel 1 (
    echo Error compilando recursos.
    exit /b 1
)

echo Compilando ejecutable nativo C++...
g++ -std=c++17 -O2 -s -mwindows -municode ^
    src/main.cpp ^
    src/gui.cpp ^
    src/dialogs.cpp ^
    src/sync_engine.cpp ^
    src/scheduler.cpp ^
    src/speech.cpp ^
    src/logger.cpp ^
    resource.res ^
    -o SyncAccesible.exe ^
    -lcomctl32 -lshlwapi -lole32 -loleaut32 -luuid -lshell32

if errorlevel 1 (
    echo Error en la compilacion de C++.
    exit /b 1
)

echo [OK] Compilacion completada con exito: SyncAccesible.exe
dir SyncAccesible.exe

