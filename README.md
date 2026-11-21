# Crazy Taxi 2 — Windows-native static recompilation (Dreamcast, SH-4 → C)

**English** · [Português](#português)

A fan project that statically recompiles the SH-4 code of *Crazy Taxi 2* (Dreamcast, **US v1.004**) to C and runs it natively on Windows, with a Direct3D 11 renderer, audio, controller support and some extras.

> **Legal / no game data.** This repository contains **no game data and no code derived from the game**. Everything (extracted disc files and the generated C, ~13 MB) is produced locally from **your own legitimate dump**. Do not redistribute the generated files. Target: `1ST_READ.BIN`, SHA-1 `bcff06e9defcd454d99ada84b9d26757e8ab4221`.

## Features
- Static SH-4 → C translation (99.8% of 2,865 functions fully translated), with a minimal Dreamcast hardware model (PVR/TA, AICA audio + ADX, GD-ROM, Maple controllers, VMU).
- **Direct3D 11 renderer** with GPU-side anti-aliasing (BASIC / FXAA / SSAA) and direct presentation (4K capable); automatic software-renderer fallback.
- Resolutions up to 3840×2160, windowed / fullscreen (F11), 4:3 / 16:9 stretched / pillarbox / widescreen (side objects are no longer culled).
- XInput and generic gamepads (PlayStation, Switch Pro, generic), keyboard.
- Native extra menu entries (CHEATS, PLAY AS YOU LIKE, TYPE C controls, resolution, aspect, upscale, anti-aliasing).
- Launcher that validates your disc, extracts it and builds the game.

## Requirements
- Windows 10/11 and a GPU with Direct3D 11.
- Your own *Crazy Taxi 2* US dump (`.gdi` / `track03.bin`).
- Python 3 and a C compiler (gcc or clang; `tools/get_toolchain.ps1` can fetch one). The first run generates and compiles the C (takes a few minutes).

## How to run
1. Build or open the launcher (`launcher\build.bat`, or `jogar.bat launcher`), click **SELECT FILES** and pick your `.gdi` / `track03.bin`.
2. Click **PLAY**. Or from a terminal: `jogar.bat "C:\path\to\folder\with\track03.bin"`.
3. After updating the project, delete `recomp\obj`, `ct2.exe` and any old `dist` folder so everything is rebuilt.

Hotkeys: F4 renderer (GPU/CPU), F11 window/fullscreen, F5/F6/F7 graphics diagnostics, F9 freeze timer, F12 dump a frame.
Direct GPU presentation can be disabled with a `no_direct.txt` file next to the exe, or `CT2_GPUDIRECT=0`.

## Status
Most of the game is playable. Windows-specific parts are developed without access to a Windows machine and verified by the author's testing; each changelog entry states what is **not** verified. Known pending items: HUD anchoring in widescreen, free camera on the right stick, some audio/voice details.

## Documentation
- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) — full history and technical notes (currently in Portuguese).
- [`docs/LAUNCHER_PLAN.md`](docs/LAUNCHER_PLAN.md) — launcher design.
- `tools/` — disassembly/analysis/translation scripts; `recomp/` — runtime, hardware model, renderer.

Not affiliated with or endorsed by SEGA. *Crazy Taxi* is a trademark of its owner.

---

## Português

Projeto de fã que recompila estaticamente o código SH-4 do *Crazy Taxi 2* (Dreamcast, **US v1.004**) para C e o executa nativamente no Windows, com renderizador Direct3D 11, áudio, suporte a controles e alguns extras.

> **Aviso legal / sem dados do jogo.** Este repositório **não contém dados do jogo nem código derivado dele**. Tudo (arquivos extraídos do disco e o C gerado, ~13 MB) é produzido localmente a partir do **SEU dump legítimo**. Não redistribua os arquivos gerados. Alvo: `1ST_READ.BIN`, SHA-1 `bcff06e9defcd454d99ada84b9d26757e8ab4221`.

### Recursos
- Tradução estática SH-4 → C (99,8% das 2.865 funções traduzidas por completo), com um modelo mínimo do hardware do Dreamcast (PVR/TA, áudio AICA + ADX, GD-ROM, controles Maple, VMU).
- **Renderizador Direct3D 11** com antialiasing na GPU (BASIC / FXAA / SSAA) e apresentação direta (roda em 4K); volta sozinho ao renderizador por software se necessário.
- Resoluções até 3840×2160, janela / tela cheia (F11), 4:3 / 16:9 esticado / barras laterais / widescreen (objetos laterais não somem mais).
- Controles XInput e genéricos (PlayStation, Switch Pro, genérico) e teclado.
- Opções extras nativas nos menus (CHEATS, PLAY AS YOU LIKE, controle TYPE C, resolução, aspecto, upscale, antialiasing).
- Launcher que valida o disco, extrai e compila o jogo.

### Requisitos
- Windows 10/11 e GPU com Direct3D 11.
- Seu próprio dump do *Crazy Taxi 2* US (`.gdi` / `track03.bin`).
- Python 3 e um compilador C (gcc ou clang; `tools/get_toolchain.ps1` pode baixar um). A primeira execução gera e compila o C (leva alguns minutos).

### Como rodar
1. Compile ou abra o launcher (`launcher\build.bat`, ou `jogar.bat launcher`), clique em **SELECT FILES** e escolha seu `.gdi` / `track03.bin`.
2. Clique em **PLAY**. Ou pelo terminal: `jogar.bat "C:\pasta\com\track03.bin"`.
3. Ao atualizar o projeto, apague `recomp\obj`, `ct2.exe` e a pasta `dist` antiga para tudo ser recompilado.

Teclas: F4 renderizador (GPU/CPU), F11 janela/tela cheia, F5/F6/F7 diagnóstico gráfico, F9 congela o cronômetro, F12 salva um quadro.
A apresentação direta pela GPU se desliga com um arquivo `no_direct.txt` ao lado do exe, ou `CT2_GPUDIRECT=0`.

### Estado
A maior parte do jogo é jogável. As partes específicas do Windows são desenvolvidas sem acesso a uma máquina Windows e validadas nos testes do autor; cada entrada do changelog diz o que **não** foi verificado. Pendências conhecidas: HUD ancorado no widescreen, câmera livre no analógico direito, alguns detalhes de áudio/voz.

### Documentação
- [`docs/CHANGELOG.md`](docs/CHANGELOG.md) — histórico completo e notas técnicas (em português).
- [`docs/LAUNCHER_PLAN.md`](docs/LAUNCHER_PLAN.md) — projeto do launcher.
- `tools/` — scripts de desassembly/análise/tradução; `recomp/` — runtime, modelo de hardware e renderizador.

Sem afiliação nem aprovação da SEGA. *Crazy Taxi* é marca de seu titular.
