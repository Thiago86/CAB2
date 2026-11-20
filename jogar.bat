@echo off
setlocal enabledelayedexpansion
rem Crazy Taxi 2 (recompilacao estatica) - gera, compila e executa. Uso: jogar.bat [pasta_do_track03.bin]
cd /d "%~dp0"
set "GDI=%~1"
set "CT2_SKIP_TEST=1"
rem jogar.bat            -> gera/compila/executa o jogo (usa o disco que o launcher lembrou em launcher.ini)
rem jogar.bat "pasta"    -> idem, com a pasta do track03.bin indicada
rem jogar.bat launcher   -> abre o launcher (CrazyTaxi2.exe); se nao existir, gera a versao pronta em dist\ antes
if /i "%GDI%"=="launcher" goto OPENLAUNCHER
if "%GDI%"=="" if exist launcher.ini (
  for /f "usebackq tokens=1,* delims==" %%A in ("launcher.ini") do if /i "%%A"=="track" for %%P in ("%%B") do set "GDI=%%~dpP"
  if defined GDI if "!GDI:~-1!"=="\" set "GDI=!GDI:~0,-1!"
)
if "%GDI%"=="" goto OPENLAUNCHER
if not exist "%GDI%\track03.bin" ( echo track03.bin nao encontrado em "%GDI%". Uso: jogar.bat "C:\pasta\do\track03.bin" & call :PAUSE & exit /b 1 )

set "REGEN="
if not exist "recomp\ct2_funcs_00.c" set "REGEN=1"
if not exist "out\extra_usado.txt" set "REGEN=1"
if exist "out\extra_usado.txt" ( fc /b extra_entries.txt out\extra_usado.txt >nul 2>nul || set "REGEN=1" )
if defined REGEN (
  echo [1/3] Gerando o C a partir do disco ^(demora alguns minutos^)...
  powershell -ExecutionPolicy Bypass -File "%~dp0run.ps1" -Gdi "%GDI%"
  if errorlevel 1 ( echo run.ps1 falhou. & call :PAUSE & exit /b 1 )
  copy /y extra_entries.txt out\extra_usado.txt >nul
) else ( echo [1/3] C ja gerado, pulando. )

set "SKIPCC="
if exist ct2.exe powershell -NoProfile -Command "$e=(Get-Item ct2.exe).LastWriteTime; $n=Get-ChildItem recomp -Include *.c,*.h,*.inc -Recurse | Where-Object { $_.LastWriteTime -gt $e }; if($n){exit 1}else{exit 0}" && set "SKIPCC=1"
if defined SKIPCC ( fc /b VERSION.txt ct2.ver >nul 2>nul || set "SKIPCC=" )
if defined SKIPCC ( echo [2/3] ct2.exe ja esta atualizado, pulando a compilacao. & goto RUN )
echo [2/3] Compilando ct2.exe...
set "CC="
where clang >nul 2>nul && set "CC=clang"
if not defined CC ( where gcc >nul 2>nul && set "CC=gcc" )
if not defined CC ( echo Nem clang nem gcc encontrados no PATH. & call :PAUSE & exit /b 1 )
powershell -ExecutionPolicy Bypass -File "%~dp0compile.ps1" -CC %CC%
if errorlevel 1 ( echo A compilacao falhou: copie as mensagens acima e envie. & call :PAUSE & exit /b 1 )
copy /y VERSION.txt ct2.ver >nul

:RUN
if "%CT2_BUILD_ONLY%"=="1" exit /b 0
echo [3/3] Executando ^(Esc fecha a janela^)...
if not exist disc\1ST_READ.BIN ( echo disc\1ST_READ.BIN nao existe. & call :PAUSE & exit /b 1 )
ct2.exe disc\1ST_READ.BIN disc\IP.BIN 0 0 "%GDI%\track03.bin" > saida.txt 2>&1
echo Terminou ^(codigo %errorlevel%^). Ultimas linhas do log:
powershell -NoProfile -Command "Select-String -Path saida.txt -Pattern 'PAROU','endereco:','f_' -SimpleMatch | Select-Object -First 14 | ForEach-Object { $_.Line }"
call :PAUSE
exit /b 0

:OPENLAUNCHER
if exist CrazyTaxi2.exe (
  start "" "%~dp0CrazyTaxi2.exe"
  exit /b 0
)
if not exist launcher\build.bat ( echo CrazyTaxi2.exe nao encontrado. & call :PAUSE & exit /b 1 )
set /p VER=<VERSION.txt
if not exist "dist\CrazyTaxi2_!VER!\CrazyTaxi2.exe" (
  echo Gerando a versao pronta...
  call launcher\build.bat
  if errorlevel 1 ( call :PAUSE & exit /b 1 )
)
start "" "%~dp0dist\CrazyTaxi2_!VER!\CrazyTaxi2.exe"
exit /b 0

:PAUSE
if not "%CT2_BUILD_ONLY%"=="1" pause
exit /b 0
