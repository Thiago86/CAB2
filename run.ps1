<#
  Crazy Taxi 2 (DC) - pipeline local de engenharia reversa e recompilacao estatica SH-4 -> C.
  Uso:  powershell -ExecutionPolicy Bypass -File .\run.ps1 -Gdi "C:\pasta\com\track03.bin"
  Obrigatorio: Python 3.   Opcional: gcc ou clang (compila e testa o C gerado);  Ghidra + JDK (pseudocodigo C).
  Nada e instalado nem baixado. O C gerado (recomp\ct2_funcs_*.c) deriva do seu dump: nao o redistribua.
  Os passos 1-3 foram executados em Linux; os passos 4-5 (Windows) NAO foram testados aqui.
#>
param([Parameter(Mandatory=$true)][string]$Gdi)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot; $disc = Join-Path $root "disc"; $out = Join-Path $root "out"; $rc = Join-Path $root "recomp"
New-Item -ItemType Directory -Force $disc, $out | Out-Null
$py = (Get-Command python -ErrorAction SilentlyContinue), (Get-Command py -ErrorAction SilentlyContinue) | Where-Object { $_ } | Select-Object -First 1
if (-not $py) { Write-Host "Python 3 nao encontrado no PATH." -ForegroundColor Red; exit 1 }
function Py { & $py.Source -I @args; if ($LASTEXITCODE -ne 0) { throw "falhou: python $args" } }
$track = Join-Path $Gdi "track03.bin"
if (-not (Test-Path $track)) { Write-Host "track03.bin nao encontrado em $Gdi" -ForegroundColor Red; exit 1 }
$bin = Join-Path $disc "1ST_READ.BIN"; $t = Join-Path $root "tools"

if ($env:CT2_SKIP_EXTRACT -eq "1" -and (Test-Path $bin) -and (Test-Path (Join-Path $disc "IP.BIN"))) { Write-Host "[1/5] Disco ja extraido pelo launcher." }
else {
Write-Host "[1/5] Extraindo o disco..."
Py (Join-Path $t "gdextract.py") $track $disc | Set-Content (Join-Path $out "disc_filelist.txt")
}
$sha = (Get-FileHash $bin -Algorithm SHA1).Hash.ToLower()
if ($sha -ne "bcff06e9defcd454d99ada84b9d26757e8ab4221") { Write-Host "AVISO: 1ST_READ.BIN difere da versao analisada (US v1.004): $sha" -ForegroundColor Yellow }

Write-Host "[2/5] Mapeando e classificando funcoes..."
Py (Join-Path $t "analyze.py") $bin $out $t
Py (Join-Path $t "classify.py") $bin $out

Write-Host "[3/5] Traduzindo SH-4 -> C..."
Py (Join-Path $t "sh4c.py") $bin $out $rc 8
Py (Join-Path $t "make_scan_list.py") $out $rc

Write-Host "[4/5] Compilando e testando o C gerado..."
$cc = (Get-Command gcc -ErrorAction SilentlyContinue), (Get-Command clang -ErrorAction SilentlyContinue) | Where-Object { $_ } | Select-Object -First 1
if ($env:CT2_SKIP_TEST -eq "1") { Write-Host "(teste do C gerado pulado: o jogo nao precisa dele)" }
elseif ($cc) {
  $srcs = @("test_ct2.c") + (Get-ChildItem $rc -Filter "ct2_funcs_*.c" | ForEach-Object { $_.Name }) + @("ct2_dispatch.c","sh4rt.c")
  Push-Location $rc
  try {
    & $cc.Source -O1 -w -I. @srcs -o test_ct2.exe
    if ($LASTEXITCODE -eq 0) { & .\test_ct2.exe $bin scan_list.txt | Tee-Object (Join-Path $out "test_log.txt") }
    else { Write-Host "Compilacao falhou (veja a mensagem acima)." -ForegroundColor Yellow }
  } finally { Pop-Location }
} else { Write-Host "gcc/clang nao encontrado: passo pulado (o C ja esta gerado em recomp\)." -ForegroundColor Yellow }

Write-Host "[5/5] Ghidra headless (opcional)..."
$gh = $env:GHIDRA_INSTALL_DIR
$headless = if ($gh) { Join-Path $gh "support\analyzeHeadless.bat" } else { $null }
if (-not $headless -or -not (Test-Path $headless)) { Write-Host "Ghidra nao configurado: passo pulado." -ForegroundColor Yellow; exit 0 }
$proj = Join-Path $root "ghidra_project"; New-Item -ItemType Directory -Force $proj | Out-Null
& $headless $proj ct2 -import $bin -overwrite -loader BinaryLoader -loader-baseAddr 0x0C010000 -processor "SuperH4:LE:32:default" -cspec default `
  -scriptPath (Join-Path $root "ghidra") -preScript SeedFunctions.java (Join-Path $out "functions.csv") -postScript ExportCt2.java $out
Write-Host "Pronto. Resultados em $out"
