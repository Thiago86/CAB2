# Compila em paralelo, guardando os objetos: so refaz o que mudou. Uso: compile.ps1 -CC clang
param([string]$CC="clang")
$ErrorActionPreference="Stop"
Set-Location (Join-Path $PSScriptRoot "recomp")
New-Item -ItemType Directory -Force obj | Out-Null
# chave: compilador + cabecalhos; se mudar, refaz tudo
$hs = (Get-ChildItem *.h | Sort-Object Name | ForEach-Object { (Get-FileHash $_.FullName -Algorithm SHA1).Hash }) -join ""
$key = "$CC|$hs"
$kf = "obj\key.txt"
if(-not (Test-Path $kf) -or (Get-Content $kf -Raw).Trim() -ne $key){ Remove-Item obj\*.o,obj\*.hash -ErrorAction SilentlyContinue; Set-Content $kf $key }
$ih = (Get-ChildItem *.inc,*.h | Sort-Object Name | ForEach-Object { (Get-FileHash $_.FullName -Algorithm SHA1).Hash }) -join ""
$jobs=@()
$funcs = Get-ChildItem ct2_funcs_*.c | Select-Object -ExpandProperty Name
$small = @("boot.c","win.c","aica.c","ct2_dispatch.c","sh4rt.c","ta.c")
foreach($f in ($funcs + $small)){
  $o = "obj\" + [IO.Path]::GetFileNameWithoutExtension($f) + ".o"
  # decide pelo CONTEUDO (hash), nao pela data do arquivo: o zip pode trazer datas/fusos que enganam
  $hf = $o + ".hash"; $h = (Get-FileHash $f -Algorithm SHA1).Hash; if($small -contains $f){ $h += $ih }
  $need = (-not (Test-Path $o)) -or (-not (Test-Path $hf)) -or ((Get-Content $hf -Raw).Trim() -ne $h)
  if($need){ Set-Content $hf $h }
  if($need){
    $opt = if($f -eq "ta.c"){"-O3"}else{"-O1"}
    $ca = @($opt,"-w","-DCT2_WINDOW","-I.","-c",$f,"-o",$o)
    Write-Host "compilando $f"
    $p = Start-Process -FilePath $CC -ArgumentList $ca -NoNewWindow -PassThru; $null = $p.Handle   # (sem isso o ExitCode volta vazio)
    $jobs += [pscustomobject]@{N=$f; P=$p; O=$o}
    # no maximo (nucleos) compilacoes ao mesmo tempo
    while(@($jobs | Where-Object { -not $_.P.HasExited }).Count -ge [Math]::Max(2,[Environment]::ProcessorCount-1)){ Start-Sleep -Milliseconds 200 }
  }
}
$bad=0; foreach($j in $jobs){ $j.P.WaitForExit(); $ok = (Test-Path $j.O); if($j.P.ExitCode -ne $null -and $j.P.ExitCode -ne 0){ $ok=$false }; if(-not $ok){ Write-Host "FALHOU: $($j.N)"; $bad=1; Remove-Item $j.O,($j.O+".hash") -ErrorAction SilentlyContinue } }
if($bad){ exit 1 }
Write-Host "ligando ct2.exe..."
& $CC (Get-ChildItem obj\*.o | ForEach-Object { $_.FullName }) -o ..\ct2.exe -luser32 -lgdi32 -lshell32
if($LASTEXITCODE -ne 0){ exit 1 }
exit 0
