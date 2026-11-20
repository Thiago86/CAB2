# Launcher (implementado na v15zzj)

- launcher/src (launcher.c Win32/GDI, launcher_core.h portavel testado no Linux, launcher.rc), launcher/assets (artes .raw + icon.ico), launcher/build.bat -> dist/CrazyTaxi2_<versao>/CrazyTaxi2.exe (versao pronta).
- Fluxo: SELECT FILES (.gdi ou track03.bin) -> valida volume CRAZY_TAXI_2 e SHA-1 do 1ST_READ.BIN (US v1.004) -> extrai para disc\ com barra de progresso -> libera PLAY.
- PLAY grava out\gdi_dir.txt e sai com codigo 0; o jogar.bat continua (gerar C se preciso, compilar, rodar).
- launcher.ini guarda o disco ja extraido (abre direto com PLAY liberado).
- Pendente/ideias: icone .exe, hover nos botoes, idioma, escolher a pasta de destino, empacotar ct2.exe pre-compilado (a geracao do C a partir do disco ainda roda no primeiro PLAY e pede Python).
