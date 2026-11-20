<#
  Baixa, so se faltar, um Python portatil e um compilador C portatil para a pasta toolchain\ (nada e instalado no Windows).
  Uso: get_toolchain.ps1 -Dest C:\pasta\toolchain [-Only all|python|compiler]
  Fontes oficiais: python.org (Python embeddable) e github.com/skeeto/w64devkit (GCC + binutils). Precisa de internet so nessa vez.
#>
param([Parameter(Mandatory=$true)][string]$Dest, [ValidateSet("all","python","compiler")][string]$Only="all")
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}
$PyVer  = "3.13.13"
$PyUrl  = "https://www.python.org/ftp/python/$PyVer/python-$PyVer-embeddable-amd64.zip"
$GccVer = "2.4.0"
$GccUrl = "https://github.com/skeeto/w64devkit/releases/download/v$GccVer/w64devkit-x64-$GccVer.7z.exe"
New-Item -ItemType Directory -Force $Dest | Out-Null

function Have-Python {
  $p = Join-Path $Dest "python\python.exe"; if (Test-Path $p) { return $true }
  foreach ($n in "python","py") { $c = Get-Command $n -ErrorAction SilentlyContinue; if ($c) { try { $o = & $c.Source -c "import sys;print(sys.version_info[0])" 2>$null; if ($LASTEXITCODE -eq 0 -and "$o".Trim() -eq "3") { return $true } } catch {} } }
  return $false
}
function Have-Compiler {
  if (Test-Path (Join-Path $Dest "w64devkit\bin\gcc.exe")) { return $true }
  foreach ($n in "gcc","clang") { if (Get-Command $n -ErrorAction SilentlyContinue) { return $true } }
  foreach ($d in "C:\msys64\ucrt64\bin","C:\msys64\mingw64\bin","C:\mingw64\bin","C:\MinGW\bin","C:\Program Files\LLVM\bin") { if ((Test-Path "$d\gcc.exe") -or (Test-Path "$d\clang.exe")) { return $true } }
  return $false
}
function Get-File($url, $out) {
  for ($i = 1; $i -le 3; $i++) {
    try { Invoke-WebRequest -Uri $url -OutFile $out -UseBasicParsing; if ((Get-Item $out).Length -gt 1MB) { return } } catch { Write-Host "[toolchain] tentativa $i falhou: $($_.Exception.Message)" }
    Start-Sleep -Seconds 2
  }
  throw "nao consegui baixar $url (sem internet, proxy ou bloqueio?)"
}

try {
  if (($Only -eq "all" -or $Only -eq "python") -and -not (Have-Python)) {
    Write-Host "[toolchain] Baixando o Python portatil ($PyVer, ~11 MB)..."
    $z = Join-Path $env:TEMP "ct2_python.zip"; $cz = Join-Path $Dest "cache\python-$PyVer-embeddable-amd64.zip"
    if (Test-Path $cz) { Copy-Item $cz $z -Force } else { Get-File $PyUrl $z }
    $pd = Join-Path $Dest "python"; if (Test-Path $pd) { Remove-Item $pd -Recurse -Force }
    Expand-Archive -Path $z -DestinationPath $pd -Force; Remove-Item $z -Force
    if (-not (Test-Path (Join-Path $pd "python.exe"))) { throw "python.exe nao apareceu apos extrair" }
    Write-Host "[toolchain] Python pronto em $pd"
  }
  if (($Only -eq "all" -or $Only -eq "compiler") -and -not (Have-Compiler)) {
    Write-Host "[toolchain] Baixando o compilador portatil (w64devkit $GccVer, ~40 MB; so na primeira vez)..."
    $x = Join-Path $env:TEMP "ct2_w64devkit.exe"; $cx = Join-Path $Dest "cache\w64devkit-x64-$GccVer.7z.exe"
    if (Test-Path $cx) { Write-Host "[toolchain] Usando o compilador que ja veio junto (sem baixar)..."; Copy-Item $cx $x -Force } else { Get-File $GccUrl $x }
    $gd = Join-Path $Dest "w64devkit"; if (Test-Path $gd) { Remove-Item $gd -Recurse -Force }
    Write-Host "[toolchain] Extraindo o compilador..."
    $p = Start-Process -FilePath $x -ArgumentList @("-y", "-o`"$Dest`"") -Wait -PassThru -WindowStyle Hidden
    Remove-Item $x -Force -ErrorAction SilentlyContinue
    if (-not (Test-Path (Join-Path $gd "bin\gcc.exe"))) {
      $g = Get-ChildItem $Dest -Recurse -Filter gcc.exe -ErrorAction SilentlyContinue | Select-Object -First 1
      if ($g) { $root = $g.Directory.Parent.FullName; if ($root -ne $gd -and $root.StartsWith($Dest)) { Rename-Item $root "w64devkit" -ErrorAction SilentlyContinue } }
    }
    if (-not (Test-Path (Join-Path $gd "bin\gcc.exe"))) { throw "gcc.exe nao apareceu apos extrair (codigo $($p.ExitCode))" }
    Write-Host "[toolchain] Compilador pronto em $gd"
  }
  exit 0
} catch {
  Write-Host "[toolchain] ERRO: $($_.Exception.Message)"
  exit 1
}
