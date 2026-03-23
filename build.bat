@echo off
:: Pfad für VS 2022 Professional
set "VS_ENV=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%VS_ENV%" (
    echo [ERROR] vcvars64.bat nicht gefunden unter %VS_ENV%
    pause
    exit
)

call "%VS_ENV%"

echo [BASTION] Kompiliere ai_music.cpp...
cl.exe /EHsc /std:c++17 ai_music.cpp /link /OUT:AI_Audio.exe user32.lib gdi32.lib comdlg32.lib shlwapi.lib shell32.lib ole32.lib

if %ERRORLEVEL% EQU 0 (
    echo [SUCCESS] AI_Audio.exe wurde erfolgreich geschmiedet!
) else (
    echo [FAIL] Fehler beim Kompilieren.
)
pause