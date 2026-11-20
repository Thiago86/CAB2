# Crazy Taxi 2 — port nativo para Windows (recompilação estática)

Port de **Crazy Taxi 2 (Dreamcast)** para Windows por **recompilação estática**: o código SH-4 do `1ST_READ.BIN`
do SEU disco é traduzido para C (`tools/sh4c.py`) e compilado junto com um runtime próprio (`recomp/`: CPU/memória,
PowerVR TA → Direct3D 11, AICA → áudio, controles, VMU, menu de opções, widescreen etc.).

> **Este repositório NÃO contém nenhum dado do jogo.** Nada de disco, texturas, código gerado ou binários.
> Você precisa do seu próprio disco (dump GDI com `track03.bin`). Tudo o que deriva do jogo é gerado
> localmente na sua máquina e está no `.gitignore`.

## Aviso importante
- Projeto de preservação/estudo. Crazy Taxi 2 © SEGA. Sem licença de uso definida (todos os direitos reservados ao autor do código); os assets do launcher são arte do autor baseada em material da Sega.
- **Status:** a lógica foi desenvolvida/testada em Linux. **Tudo que é Windows (D3D11, `win.c`, launcher GUI, `build.bat`, ícone/windres) ainda é pouco testado** — pode haver falhas.

## O que você precisa
1. Windows 10/11 64 bits.
2. **Python 3** no PATH.
3. **gcc (MinGW-w64)** ou **clang** no PATH.
4. Dump do seu disco original: pasta com o `.gdi` e o `track03.bin` (o arquivo da trilha de dados precisa se chamar `track03.bin`).

## Como jogar (versão pronta com launcher)
1. Rode `launcher\build.bat` — cria `dist\CrazyTaxi2_<versão>\` (a "versão pronta").
2. Abra `dist\CrazyTaxi2_<versão>\CrazyTaxi2.exe`.
3. **SELECT FILES** → escolha o `.gdi` / `track03.bin`. O launcher valida o disco, extrai os arquivos (barra de progresso) e libera o **PLAY**.
4. **PLAY** gera o código C do seu disco, compila (primeira vez demora) e abre o jogo.

## Modo desenvolvedor
`jogar.bat "C:\caminho\do\dump"` — extrai, analisa, gera C, compila e roda direto.
Apague `recomp\obj` e `ct2.exe` ao atualizar de versão (o `jogar.bat` só recompila se `VERSION.txt` mudou).

## Estrutura
- `tools/` — extrator GD-ROM, análise, classificação, tradutor SH-4→C (`sh4c.py`)
- `recomp/` — runtime (`boot.c`, `sh4rt.*`, `ta.c`/`gpu11.inc` D3D11, `aica.*`, `win.c`, `menux.inc` opções)
- `launcher/` — launcher Win32 (fonte, assets, `build.bat`)
- `ghidra/`, `names_manual.csv`, `extra_entries.txt` — apoio à análise
- `docs/CHANGELOG.md` — histórico por versão; `docs/LAUNCHER_PLAN.md`

## Recursos
Widescreen (com correção de culling nas bordas), controles teclado/XInput com turbo dash, menu de opções, VMU/saves em arquivo, filtro anisotrópico, entre outros — veja o changelog.
