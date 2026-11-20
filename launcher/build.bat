@echo off
setlocal
set "RESULT=0"
rem Gera a "versao pronta": dist\CrazyTaxi2_<versao>\  com CrazyTaxi2.exe (icone embutido) e so as pastas/arquivos necessarios.
rem Uso: launcher\build.bat          (sem o C gerado do disco - e o que se distribui; ele e gerado no 1o PLAY)
rem      launcher\build.bat withc    (inclui recomp\ct2_funcs_*.c - so para uso proprio, nao redistribuir)
cd /d "%~dp0.."
set /p VER=<VERSION.txt
set "D=dist\CrazyTaxi2_%VER%"
set "LCC="
where gcc >nul 2>nul && set "LCC=gcc"
if not defined LCC where clang >nul 2>nul && set "LCC=clang"
if not defined LCC ( echo Nem gcc nem clang encontrados no PATH. Instale o MinGW-w64 ^(gcc^) e tente de novo. & set "RESULT=1" & goto END )
echo Compilador: %LCC%
echo Pasta de saida: %D%
set "RC="
where windres >nul 2>nul && set "RC=windres"
if not defined RC where llvm-windres >nul 2>nul && set "RC=llvm-windres"
if not exist "%D%" mkdir "%D%"
if not exist "%D%" ( echo Nao consegui criar a pasta %D% ^(sem permissao? tente mover o projeto para uma pasta sua, como Documentos^). & set "RESULT=1" & goto END )
set "RCO="
if defined RC (
  %RC% -I. launcher\src\launcher.rc -O coff -o launcher\icon_res.o
  if exist launcher\icon_res.o set "RCO=launcher\icon_res.o"
)
if not defined RCO echo AVISO: windres nao encontrado - o .exe sai sem o icone.
%LCC% -O2 -w -mwindows launcher\src\launcher.c %RCO% -o "%D%\CrazyTaxi2.exe" -lgdi32 -lmsimg32 -lcomdlg32 -luser32
if errorlevel 1 ( echo A compilacao do launcher falhou: copie as mensagens acima e envie. & set "RESULT=1" & goto END )
if exist launcher\icon_res.o del launcher\icon_res.o
for %%F in (jogar.bat run.ps1 compile.ps1 VERSION.txt extra_entries.txt names_manual.csv intro_loading.ppm intro_adx.ppm README.md) do if exist %%F copy /y %%F "%D%\" >nul
robocopy launcher\assets "%D%\launcher\assets" /E /NFL /NDL /NJH /NJS >nul
if errorlevel 8 ( echo Falha ao copiar launcher\assets & set "RESULT=1" & goto END )
robocopy tools "%D%\tools" /E /XD __pycache__ /NFL /NDL /NJH /NJS >nul
if errorlevel 8 ( echo Falha ao copiar tools & set "RESULT=1" & goto END )
if exist ghidra robocopy ghidra "%D%\ghidra" /E /NFL /NDL /NJH /NJS >nul
if /i "%~1"=="withc" (
  robocopy recomp "%D%\recomp" *.c *.h *.inc Makefile /NFL /NDL /NJH /NJS >nul
) else (
  robocopy recomp "%D%\recomp" *.c *.h *.inc Makefile /XF ct2_funcs_*.c ct2_funcs.h ct2_dispatch.c scan_list.txt /NFL /NDL /NJH /NJS >nul
)
if errorlevel 8 ( echo Falha ao copiar recomp & set "RESULT=1" & goto END )
echo.
echo Versao pronta criada em %D%\
echo Abra %D%\CrazyTaxi2.exe  (ou use jogar.bat: gera/compila/executa direto)
:END
rem se foi aberto com duplo clique, nao fecha a janela antes de voce ler
echo %cmdcmdline% | find /i "%~nx0" >nul && pause
exit /b %RESULT%
