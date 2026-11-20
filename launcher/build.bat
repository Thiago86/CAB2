@echo off
echo Crazy Taxi 2 - criando a versao pronta ^(dist^)...
setlocal
set "RESULT=0"
set "BUNDLE=1"
set "WITHC="
for %%A in (%*) do if /i "%%A"=="nobundle" set "BUNDLE="
if exist recomp\ct2_funcs_00.c set "WITHC=1"
for %%A in (%*) do if /i "%%A"=="public" set "WITHC="
rem Gera a "versao pronta": dist\CrazyTaxi2_<versao>\  com CrazyTaxi2.exe (icone embutido) e so as pastas/arquivos necessarios.
rem Uso: launcher\build.bat          (sem o C gerado do disco - e o que se distribui; ele e gerado no 1o PLAY)
rem      launcher\build.bat nobundle (sem o Python/compilador portateis dentro: o jogo os baixa na 1a vez)
rem      (padrao) inclui o C gerado do disco se existir em recomp\ - so para uso proprio, nao redistribuir; "public" = sem ele
cd /d "%~dp0"
if not exist VERSION.txt cd /d "%~dp0.."
if not exist VERSION.txt ( echo ERRO: nao achei VERSION.txt. Este build.bat precisa ficar na pasta "launcher" do projeto ^(ou na raiz dele^), junto de recomp, tools e launcher\assets. & set "RESULT=1" & goto END )
if not exist launcher\assets\bg.raw ( echo ERRO: nao achei launcher\assets. Extraia o zip inteiro e rode o build.bat que esta dentro da pasta launcher. & set "RESULT=1" & goto END )
echo Pasta do projeto: %CD%
set /p VER=<VERSION.txt
set "D=dist\CrazyTaxi2_%VER%"
set "LCC="
where gcc >nul 2>nul && set "LCC=gcc"
if not defined LCC where clang >nul 2>nul && set "LCC=clang"
if not defined LCC call :GETCC
if not defined LCC ( echo Nao achei nenhum compilador e nao consegui baixar o portatil ^(precisa de internet; veja a mensagem acima^). Voce tambem pode instalar o MSYS2 ou o MinGW-w64 manualmente. & set "RESULT=1" & goto END )
echo Compilador: %LCC%
echo Pasta de saida: %D%
set "RC="
where windres >nul 2>nul && set "RC=windres"
if not defined RC where llvm-windres >nul 2>nul && set "RC=llvm-windres"
if not exist "%D%" mkdir "%D%"
if not exist "%D%" ( echo Nao consegui criar a pasta %D% ^(sem permissao? tente mover o projeto para uma pasta sua, como Documentos^). & set "RESULT=1" & goto END )
for %%F in (run.ps1 compile.ps1 VERSION.txt extra_entries.txt names_manual.csv intro_loading.ppm intro_adx.ppm README.md) do if exist %%F copy /y %%F "%D%\" >nul
if not exist "%D%\launcher" mkdir "%D%\launcher"
copy /y jogar.bat "%D%\launcher\run.bat" >nul
robocopy launcher\assets "%D%\launcher\assets" /E /NFL /NDL /NJH /NJS >nul
if errorlevel 8 ( echo Falha ao copiar launcher\assets & set "RESULT=1" & goto END )
robocopy tools "%D%\tools" /E /XD __pycache__ /NFL /NDL /NJH /NJS >nul
if errorlevel 8 ( echo Falha ao copiar tools & set "RESULT=1" & goto END )
if exist ghidra robocopy ghidra "%D%\ghidra" /E /NFL /NDL /NJH /NJS >nul
if defined WITHC (
  robocopy recomp "%D%\recomp" *.c *.h *.inc Makefile /NFL /NDL /NJH /NJS >nul
) else (
  robocopy recomp "%D%\recomp" *.c *.h *.inc Makefile /XF ct2_funcs_*.c ct2_funcs.h ct2_dispatch.c scan_list.txt /NFL /NDL /NJH /NJS >nul
)
if errorlevel 8 ( echo Falha ao copiar recomp & set "RESULT=1" & goto END )
if defined BUNDLE (
  echo Incluindo o compilador portatil na versao pronta ^(sem baixar nada no PC de quem for jogar^)...
  call :GETCC
  if defined WITHC (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\get_toolchain.ps1" -Dest "%CD%\toolchain" -Only compiler
  ) else (
    echo Sem o C gerado: incluindo tambem o Python portatil ^(baixa 1 vez se ainda nao estiver em toolchain^)...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\get_toolchain.ps1" -Dest "%CD%\toolchain" -Only all
  )
  if exist toolchain robocopy toolchain "%D%\toolchain" /E /XD cache /NFL /NDL /NJH /NJS >nul
)
if defined WITHC (
  echo Incluindo o C gerado do disco ^(uso pessoal: NAO distribua esta pasta; use "build.bat public" para uma versao distribuivel^).
  if not exist "%D%\out" mkdir "%D%\out"
  if exist out\extra_usado.txt copy /y out\extra_usado.txt "%D%\out\" >nul
)
echo Arquivos copiados. Compilando o launcher...
set "RCO="
if defined RC (
  %RC% -I. launcher\src\launcher.rc -O coff -o launcher\icon_res.o
  if exist launcher\icon_res.o set "RCO=launcher\icon_res.o"
)
if not defined RCO echo AVISO: windres nao encontrado - o .exe sai sem o icone.
%LCC% -O2 -w -mwindows launcher\src\launcher.c %RCO% -o "%D%\CrazyTaxi2.exe" -lgdi32 -lmsimg32 -lcomdlg32 -luser32
if errorlevel 1 ( echo A compilacao do launcher falhou: copie as mensagens acima e envie. & set "RESULT=1" & goto END )
if exist launcher\icon_res.o del launcher\icon_res.o
echo.
echo Versao pronta criada em %D%\
echo Abra %D%\CrazyTaxi2.exe  
:END
rem sempre espera uma tecla (a janela nao fecha antes de voce ler), exceto quando chamado pelo jogar.bat
if not defined CT2_NOPAUSE pause
exit /b %RESULT%

:GETCC
if exist "%CD%\toolchain\w64devkit\bin\gcc.exe" set "PATH=%CD%\toolchain\w64devkit\bin;%PATH%"
where gcc >nul 2>nul || powershell -NoProfile -ExecutionPolicy Bypass -File "%CD%\tools\get_toolchain.ps1" -Dest "%CD%\toolchain" -Only compiler
if exist "%CD%\toolchain\w64devkit\bin\gcc.exe" set "PATH=%CD%\toolchain\w64devkit\bin;%PATH%"
where gcc >nul 2>nul && set "LCC=gcc"
exit /b 0
