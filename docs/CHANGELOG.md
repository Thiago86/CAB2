# Crazy Taxi 2 (Dreamcast, US v1.004) - engenharia reversa e recompilacao estatica para port nativo

Alvo: `1ST_READ.BIN` - SHA-1 bcff06e9defcd454d99ada84b9d26757e8ab4221, 1.452.148 bytes, SH-4 little-endian, carga em 0x8C010000.
Este pacote nao contem dados nem codigo do jogo: tudo e gerado localmente a partir do SEU dump.
O C gerado (`recomp/ct2_funcs_*.c`, ~13 MB) deriva do codigo do jogo: nao o redistribua.

## Como rodar (Windows)
    powershell -ExecutionPolicy Bypass -File .\run.ps1 -Gdi "C:\pasta\com\track03.bin"
1. extrai o disco (ISO9660 da trilha 3) para `disc\`
2. mapeia funcoes e classifica -> `out\`
3. traduz SH-4 -> C -> `recomp\`
4. compila e testa (se houver gcc ou clang)
5. Ghidra headless (opcional, exige GHIDRA_INSTALL_DIR e JDK 25)
Exige Python 3. Os passos 1-4 foram executados em Linux, do zero, com resultado identico em duas rodadas;
**no Windows foram executados so os passos 1-2 (confirmado pela sua pasta); 3-5 nao foram testados la**.
O passo 5 nunca rodou (Ghidra nao instalado).

## Resultados verificados (out/test_log.txt)
- Tradutor: 2.860 de 2.865 funcoes traduzidas por completo (99,8%); as 5 parciais sao tratamento de excecao do SDK
  (`rte`, `trapa`, `ssr/spc`) e um byte de dados. Os 8 arquivos C (247.464 instrucoes) compilam sem erro.
- 17 verificacoes de comportamento, 0 falhas: operacoes de vetor (soma, subtracao, escala, produto escalar,
  comprimento, distancia, normalizacao), seno/cosseno de angulo de 16 bits, pilha de matriz do hardware
  (identidade, translacao acumulada, transformacao de ponto e de direcao) e tres rotacoes.
- Oraculo = o significado matematico, nao outra implementacao minha. Teste automatico por hipoteses identificou
  mais 7 funcoes de biblioteca: divisao e resto com e sem sinal (dividendo em r1, divisor em r0), 1/sqrt, abs e uma subtracao.
- Rotacoes: 0x0C073D50 = eixo Y, 0x0C073BA0 = eixo X, 0x0C073E10 = eixo Z; angulo inteiro de 16 bits (65536 = volta),
  sentido positivo de mao direita.
- 35 nomes em `names_manual.csv`; 23 verificados por teste. 58% das chamadas do jogo a biblioteca (1.496 de 2.579)
  vao para funcoes ja nomeadas.

## Mapa do executavel (observado)
- 2.865 funcoes; ponto de entrada 0x0C145AE0 -> 0x0C01B214 (provavel main).
- Codigo do jogo antes de 0x0C06ECF2 (1.330 funcoes); biblioteca de matematica/runtime em 0x0C06ECF2-0x0C092DA8;
  SDK Sega (Shinobi 1.76, Kamui 1.11, CRI ADXT 5.93, NAOMI library 0.8) a partir de 0x0C140000.
- 287-290 funcoes de biblioteca sao chamadas diretamente pelo jogo (`out/port_boundary.csv`).
- Hardware: poucas funcoes tocam registradores (PVR 13, AICA 10, GD-ROM 3, Maple 1, store queue/TA 42).

## Premissas do recompilador (nao verificadas contra hardware)
- `fsca` usa sin/cos em precisao dupla; o hardware usa tabela, entao ha diferenca na ultima casa decimal.
- fmov de 64 bits (SZ=1): endereco+0 vai para o registrador par, endereco+4 para o impar (como em emuladores conhecidos).
  Isso so importa se o codigo misturar acessos de 32 e 64 bits ao mesmo dado.
- Modo de precisao dupla do FPU (PR=1) aborta com mensagem; so SZ=1 e suportado.
- SR: guarda T/Q/M e o restante como valor opaco; troca de banco de registradores (RB) e interrupcoes nao sao modeladas.
- Acesso fora da RAM de 16 MB cai em `rt_hw_r/rt_hw_w` (hoje aborta): e aqui que o hardware (PVR, AICA, GD-ROM, controles)
  precisa ser ligado ao Windows. PR e usado so como valor; retorno de funcao e o `return` de C.

## Boot executado (Linux; `recomp/boot.c`, `tools/discover.py`, `out/boot_log.txt`)
O ponto de entrada 0x8C010000 roda ate o carregador de arquivos. Observado, em ordem:
1. configura o cache (CCR) e a inicializacao do SDK: registradores do chip Holly (DMA), Maple (controles), G1/G2;
2. chama o BIOS duas vezes (vetor 0x8C0000B0, seletores 0 e 3 = inicializacao e identificacao do sistema);
3. confere o ID do PVR: a constante 0x17FD11DB esta no codigo, e a revisao precisa ser >= 17;
4. programa o modo de video do PVR e limpa a VRAM (~2 milhoes de escritas);
5. entra no carregador de arquivos (0x0C01AA64). Uma parada anterior (ponteiro nulo em 0x0C1718CC) era um **bug do
   meu tradutor**, nao do jogo: quando a entrada de uma funcao nao era o menor endereco dela, o C gerado comecava a
   executar pelo menor endereco. Corrigido (`goto` para a entrada);
6. programa o PVR, usa DMA do canal 2 (SH-4 + Holly) para mandar dados ao TA/VRAM e depende de **interrupcoes**:
   o jogo grava seus tratadores numa tabela em VBR+0x200 (VBR=0x8C000000, valor que o BIOS real deixaria);
   0x320 = IRL9 (ASIC normal; mascara 0x5F6930) e 0x360 = IRL11 (mascara 0x5F6920). O tratador recebe o INTEVT em r4;
7. le o disco de verdade pelo BIOS (HLE do GD-ROM: `boot.c`, `b_gdrom`): diretorio, depois, em ordem,
   SPRST.BIN, AICADRV.BIN, SNDDC*.BIN, SONG01.AFS, VOICEADX.AFS, TEXDC0/POLDC0, TEXPSG0/POLPSG0, ... SPRMENU2/SPRMJAP
   (28 arquivos, ~3700 setores) e envia ~44 MB ao TA/VRAM por DMA: ou seja, o jogo chega ao laco principal;
8. o driver de som (AICADRV.BIN) e carregado na RAM de audio; o jogo espera um sinal do ARM7 (aica_ram+0x5C != 0),
   que aqui e **simulado** (nao ha emulacao do ARM7).
Modelo de hardware do `boot.c` — o que e **observado no codigo do jogo** e o que e **premissa minha**:
- observado: IDs do PVR, enderecos de registradores, tabela de tratadores, protocolo dos comandos do GD-ROM
  (24 init, 40 versao, 19 TOC, 17 leitura DMA), cabecalho "KATANA_FLASH____" da flash, mensagem "AICADRV ERR %d".
- premissa (nao verificada contra hardware): numeracao FAD = LBA do GDI + 150; layout/valores da TOC; imagem de flash
  inventada (128 KB; particoes em 0x0/0x2000/0x4000/0x8000/0x10000); retorno 1 = "pronto" do status da unidade;
  fim do DMA do GD-ROM (ISTNRM bit 14) sem reconhecimento; vblank a cada 16 "relogios"; TMU0 com ritmo inventado;
  convencao dos syscalls do BIOS (r7 = funcao) vinda de memoria de documentacao publica, nao deste projeto.
9. **Primeira imagem** (`recomp/ta.c`, `frames/`): o decodificador da lista do TA + rasterizador por software desenha
   a tela de verificacao de controle do proprio jogo ("CONTROLLER / port A..D / socket 1, 2"): o texto aparece certo
   (sprites texturizados), mas texturas de fundo saem com listras (formato/ordem de bits de alguma textura errado:
   premissas de twiddle, VQ, mipmap e paleta ainda nao verificadas). A tela e a que o jogo mostra sem controle:
   o Maple (controles) ainda nao e emulado. Fins de lista do TA geram as interrupcoes (a lista de EOL nao carrega o tipo:
   vale a lista aberta pelo ultimo cabecalho); STARTRENDER dispara o desenho; ~10 mil quadros em 5 s no Linux (sem tempo real).
Proximo marco: emular o Maple (controle conectado) para o jogo passar dessa tela, corrigir texturas, depois janela/som.
Isto e um modelo minimo de hardware (registradores guardam o ultimo valor escrito; VRAM e RAM de audio sao memoria
comum; BIOS devolve 0). Nao ha video, som, disco, controles nem interrupcoes ainda.
Alcance de enderecos pelo salto indireto: `tools/discover.py` acrescenta alvos descobertos a `extra_entries.txt`.
Para repetir (Linux/gcc): `python3 tools/discover.py . disc/1ST_READ.BIN disc/IP.BIN 8`. No Windows o `boot.c` compila
(`clang -O1 -w -I. boot.c ct2_funcs_*.c ct2_dispatch.c sh4rt.c -o boot.exe`) mas sem o temporizador de seguranca:
um laco sem chamadas nem acessos a hardware nao e interrompido (Ctrl+C). Nao testado no Windows.

## Desconhecido
- 1.023 chamadas indiretas sem alvo estatico: resolvidas em tempo de execucao pela tabela `rt_call`
  (busca binaria endereco -> funcao). Codigo alcancado so por ponteiros montados em RAM pode estar fora do mapa e falhara
  em `rt_call` com o endereco exato (e assim que o mapa deve ser completado).
- Eixos e nomes das demais ~2.800 funcoes: os de `symbols.csv` sao rotulos gerados, nao nomes originais.

## Arquivos
- `tools/` discover.py (ciclo de descoberta), gdextract.py, sh4.py (decodificador), analyze.py (mapa), classify.py (regioes/nomes), sh4c.py (SH-4 -> C), make_scan_list.py
- `recomp/` sh4rt.h/.c (runtime), test_ct2.c (testes), boot.c (executor de boot), Makefile; o C gerado aparece aqui apos o passo 3
- `names_manual.csv` nomes com evidencia e confianca (verificado/observado/inferido)
- `out/` symbols.csv, port_boundary.csv, tables.json, functions.csv, callgraph.json, recomp_partial.json, test_log.txt
- `ghidra/` scripts para o passo 5 (nao testados)

## Menus nativos (v15) — CHEATS, PLAY AS YOU LIKE, TYPE C
Implementado em `recomp/menux.inc` (incluido por `ta.c`): o modulo le o fluxo de triangulos do proprio jogo, reconhece a tela
(Options / regras Around-Small Apple / Controller Setting) pelo texto desenhado e injeta linhas novas com a fonte e as caixas do jogo.
A entrada e filtrada por `menux_filter()` (chamada em `win_pad` no Windows e no fim de `start_render` no teste Linux).
- Options: linha **CHEATS** (esquerda/direita = TIMER ON/OFF, A = UNLOCK ALL ON/OFF). Salvo em `opcoes.ini`.
- Regras: linha **PLAY AS YOU LIKE** (cronometro congelado + aviso "stop and pick up customers!" escondido).
- Controller Setting: **TYPE C** (moderno): toque duplo em R = D gear + acelerador; toque duplo em L = R gear + freio.
- Observacao: `recomp/ct2_funcs_00.c` tem um patch manual em `L_0c01dd40` (`if(!ct2_cheat_notimer) ...`, NOP do cronometro). Ao regenerar o C, reaplicar.
- Testes no Linux: `CT2_RENDERALL=1` (renderiza todo quadro), `CT2_MENUXDBG=1` (log de telas/botoes).

## Audio (v15e)
- Correcao: o anel de reverb do DSP (registrador AICA 0x2804) era decodificado errado (RBP = bits 0-11 x 0x800 bytes; RBL = bits 13-14). Antes o DSP sobrescrevia os bancos SNDDC1-3 na RAM de audio, e os comandos de SFX (menus, motor, jogo) eram aceitos pelo driver ARM mas nao tocavam.
- Pendente: musica e voz (ADX, CRI ADXT 5.93) ainda nao tocam; o jogo faz toggles de ADSUSP no DMA G2 (0x5F781C...) mas o DMA em si nao e emulado.
- Debug: CT2_ARAMWR, CT2_ARMWR, CT2_ARMCMD, CT2_ARMLD, CT2_G2LOG, CT2_AICAWR.
- F12 salva glitch_dump.bin (quadro para depurar o glitch do chao).

## v15f
- Pyramid: foto dos personagens e titulo "CRAZY PYRAMID" voltam (sprites do TA usavam o ISP do poligono anterior em vez do proprio cabecalho).
- Pyramid: o salto 0x0c0231ae foi adicionado em extra_entries.txt (o jogo nao encerra mais ao clicar em jogar; jogar.bat regenera o C na primeira execucao).
- PLAY AS YOU LIKE: a mensagem "stop and pick up customers!" some tambem na pausa.
- tools/sh4c.py mantem o patch do cheat Timer ao regenerar.

## v15g
- extra_entries.txt: +344 ponteiros para meio de rotina achados em dados (varredura estatica), para reduzir saltos sem funcao recompilada (ex.: 2a fase do Pyramid).
- ta.c: variaveis de depuracao CT2_LOD0, CT2_NOMX, CT2_DBGTEX, CT2_TRIHASH.

## v15h
- 0c0233e2 em extra_entries.txt (Pyramid fase 2).

- jogar.bat: out/extra_usado.txt agora vem sincronizado (sem regeneracao ao extrair) e a compilacao e pulada se ct2.exe estiver atualizado.

## v15i
- Tabela de manipuladores do Pyramid (0x0c09d0f8): 16 entradas adicionadas (fases 2 a 5).

## v15j
- Tecla F9 liga/desliga o congelamento do cronometro (mesma variavel 0x8c1eadb0 no Arcade e no Pyramid; confirmado no Linux).

## v15k
- Corrige parada em 0x0c1693a8 e 0x0c07e0aa (novos pontos de entrada em extra_entries.txt).
- DMA G2 (ADX -> RAM do AICA) implementado; o PCM do ADX ja chega na RAM de audio.
- Audio ainda em investigacao: o canal PCM e ligado e desligado quase de imediato pelo ARM.
- Glitch grafico: ainda em investigacao.

## v15l
- AUDIO: corrigido o envelope AICA (estagio decay com DL=0 levava o EG ao silencio total; o driver ARM liberava o canal 2 amostras depois). Musica/voz ADX agora audivel.
- Menu Options: "TIMER STOP OFF/ON" (OFF = cronometro normal; ON = cronometro congelado). F9 tambem alterna.

## v15m
- Rasterizador: calculo das bordas/baricentricas em dupla precisao (triangulos cortados no plano proximo, com coordenadas de ate ~1e5, perdiam precisao em float).
- Novos depuradores: CT2_TRIAT=x,y (lista triangulos que cobrem o pixel), CT2_IDCOL=1 (colore cada triangulo por id).

## v15n
- Velocidade/atraso: timeBeginPeriod(1) no limitador de quadros (Sleep(1) dormia ~15 ms no Windows -> jogo lento); AICA renderizada em blocos de 64 amostras (o spinlock era segurado ~5 ms por bloco e travava o SH4); LOD do rasterizador recalculado a cada 4 pixels.
- Menu Regras: barra "PLAY AS YOU LIKE" alargada (o texto nao cabia na barra de MAP).
- Pendente: voz "We're back, it's Crazy Taxi 2" na tela de titulo (o jogo nao emite nenhum comando de som antes do START).

## v15o
- Teclas de diagnostico grafico ao vivo (janela): F5 = LOD/mipmap desligado, F6 = culling desligado, F7 = esconde os triangulos gigantes (cortados no plano proximo, coordenadas > 20000). Cada tecla alterna e mostra o estado na barra de titulo.
- Depuradores: CT2_ZSRC (origem dos vertices z=3.333), CT2_MXVOICE, CT2_FORCEFREE.

## v15p
- Menu de regras (Around Apple): "PLAY AS YOU LIKE" fica acima de MAP (quando existe) e de EXIT, com caixa esticada preservando as bordas.
- Opcoes de cheat sempre iniciam DESATIVADAS (CT2_KEEPOPT=1 mantem as salvas).
- Desempenho: getenv em cache (GENV), cache de LOD, renderizacao pulada quando o quadro esta atrasado (win_behind), timeBeginPeriod(1), audio em blocos de 64 amostras.
- Diagnostico grafico: F5 (LOD off), F6 (sem culling), F7 (esconde triangulos gigantes) - informe qual tecla muda o glitch.

## v15p (2a rodada)
- Options: nova linha "GRAPHICS" (MIPMAP ON/OFF = F5, CULLING ON/OFF = F6; esquerda/direita e A como na linha CHEATS); salva em opcoes.ini. F7 continua so como tecla.
- Titulo da janela mostra fps, ms de desenho, ms de apresentacao e quadros pulados (para diagnosticar lentidao).
- CT2_MAPLEDBG=1 imprime as leituras do controle; CT2_TRIRANGE=a,b lista triangulos (diagnostico do glitch).

## v15q
- Options: a linha GRAPHICS foi removida. F5 (mipmap/LOD) e F6 (culling) continuam valendo so durante o jogo (nao sao salvos).
- Regras: a barra "PLAY AS LONG AS YOU LIKE" agora tem a mesma largura das demais barras e o texto novo.
- START durante o jogo: novos pontos de entrada em extra_entries.txt (0x0c169b16 e 23 funcoes de stdio apos rts+nop). Alem disso, qualquer salto para codigo da imagem sem funcao recompilada nao encerra mais o jogo: avisa, grava o endereco em faltantes.txt e retorna 0 (jogar.bat regenera o C quando extra_entries.txt muda).
- Atraso de comandos: a trava de botoes passou de 4-10 quadros para 3 quadros no total (o START ficava preso e reabria/fechava a pausa).

## v15r
- Teclado: setas agora só acionam o D-pad (antes também moviam o analógico e o menu recebia a direção duas vezes, "pulando" a opção).
- Analógico forte vira D-pad e recentra o eixo analógico (sem direção duplicada).
- Trava de toque: só estende toques com menos de 3 quadros; botão segurado solta na hora (sem atraso de saída).
- Verificado no harness: cada toque de D/A/START (3 quadros, espaçados) gera uma borda de entrada no driver do pad do jogo; o driver em si não perde toques.

## v15s
- Diagnóstico de desempenho/entrada: o jogo grava `perf.txt` na pasta de execução (a cada 60 quadros: tempo da lógica do jogo, desenho, tela e espera, média/máx em ms; e cada mudança de botão e cada evento de entrada consumido pelo jogo, com o número do quadro). Serve para medir atraso real de menus/START.
- v15s (2): `perf.txt` agora registra também `jogo_leu botoes=` (cada mudança de botões lida pelo jogo via Maple, com o quadro). `key()` usa o bit "apertada desde a última consulta" para não perder toques curtos.
- v15s (3): macro de "PLAY AS LONG AS YOU LIKE" mais rapida (sobe a 1a opcao em pulsos de 2 quadros a cada 6 em vez de 3/14; pulso de A em 5 quadros) - o salto visivel do cursor dura ~3x menos.

## v15t
- A/B/X/Y/START passam ao jogo como toque de exatamente 3 quadros, mesmo se a tecla for segurada (o log mostrou START chegando 2x em quadros seguidos na partida -> pausa abre/fecha; A segurado atravessava para a proxima tela). Direcionais continuam seguindo o dedo (repeticao automatica dos menus). Simulado em C: 11 quadros segurados -> 3 ativos; toque de 1 quadro -> 3 ativos.

## v15u
- Perda de toques nos menus (log do usuario: ~metade dos toques de cima/baixo/A nao geravam evento no jogo, sem relacao com a duracao do toque; o harness sem janela nao reproduzia): a borda de botao (0x0C1EA85C, escrita por f_0c01a1d4 a cada leitura do pad) era sobrescrita por uma segunda leitura antes do gerador de eventos (f_0c033b1e) consumi-la. Agora a borda e "sticky" (ct2_edge_step em boot.c): fica ate o gerador ler (ou expira em 3 leituras). Patch aplicado em ct2_funcs_00.c/01.c e em tools/sh4c.py (regeneracao mantem).
- perf.txt: apos cada mudanca de botao registra, por 10 quadros, quantas vezes o jogo chamou atualiza_pad / driver / gerador e os valores de borda/evento.

## v15v
- Causa real dos toques perdidos (confirmada no perf.txt): o jogo chama a atualizacao do pad (f_0c149a1e) 2-4x por quadro, e uma dessas chamadas ocorre DEPOIS do driver (f_0c01a1d4). A borda de botao ficava na estrutura da lib ate a 1a chamada do quadro seguinte, que a apagava antes do driver le-la (~50% dos toques perdidos; mais em gameplay, com 3-4 chamadas por quadro). Agora a borda e acumulada na origem (ct2_lib_edge) e so e limpa quando o driver a le (ct2_lib_take). Patch em ct2_funcs_00.c/06.c e tools/sh4c.py.

## v15w
- Menu de regras com tudo desbloqueado (MAP presente): a barra "PLAY AS LONG AS YOU LIKE" usava a barra MAP (estreita) como molde e ficava pequena. Agora usa a barra "10 MINUTES" como molde (mesma largura), um degrau a direita, entre ela e MAP; MAP e EXIT mantem as posicoes. Verificado por captura de quadro (harness, CT2_UNLOCK=1, tela de regras com 6 linhas).

## v15x
Novas opcoes (Options, com a fonte/caixas do proprio jogo; salvas em opcoes.ini: res, aspect, upscale, fps, sens, view):
- RESOLUTION: 640x480 ate 3840x2160 (inclui HD/FullHD/2K/4K em 16:9), lista de janela e a mesma lista em FULLSCREEN (ex.: "1920X1080 FULLSCREEN"). F11 alterna janela/tela cheia.
- ASPECT: 3 modos 16:9 - esticado, barras laterais, widescreen experimental (3D esticado + HUD 4:3 centralizado; sem mudanca real de FOV).
- UPSCALE: barra igual a MUSIC VOLUME (MIN..MAX), 100% a 200%; so aumenta a resolucao interna do 3D/cenario inteiro renderizado maior.
- FRAMERATE: 60 (padrao) / 30 renderizado; outras taxas (90/ilimitado) ficam para depois, o jogo e logica a 60.
- Pausa durante a partida: SENSIBILITY (barra; padrao = maximo; reduz a curva do analogico) e VIEW (esquerda/direita troca a camera usando as funcoes do controle 3: B painel, Y angulo, X velocimetro, A reset, etc.).
Verificado so no harness Linux: layout/cursor das telas, UPSCALE 150%/186%, canvas widescreen, pausa, VIEW trocando camera. NAO testado no Windows: troca de resolucao/tela cheia, barras, F11, DPI, 30 FPS, analogico real. Pendente: remover a fala "stop and pick up customers" no modo livre, glitch grafico, loop de voz do passageiro.

## v15y
- Fala "stop and pick up customers" no modo livre: AINDA NAO REMOVIDA. Nao consegui provocar o aviso no harness (carro parado, nunca aparece), e sem saber qual amostra de audio e a fala nao da para bloquear sem silenciar outros sons.
- Diagnostico novo: o jogo grava `voz.txt` na pasta de execucao (cada amostra de voz/ADPCM tocada, com quadro, canal, endereco `sa=` e tamanho; e uma linha `>>> aviso na tela` quando o aviso visual aparece).
- Silenciador: variavel de ambiente `CT2_MUTE=endereco,endereco` (hex, o `sa=` do voz.txt) impede essas amostras de tocar. Com o voz.txt do usuario o endereco certo vira padrao fixo.

## v15z
- Fala "stop and pick up customers" no modo livre (voz.txt do usuario): a frase e tocada por 3 amostras de voz disparadas juntas (enderecos de som 0b28f8, 0bb244, 0b16c4) e se repete a cada ~145 quadros. No modo PLAY AS LONG AS YOU LIKE essas 3 amostras agora sao bloqueadas (`ct2_free_mute`); fora do modo livre continuam normais.
- Identificacao por padrao do log (o log nao trouxe a marca "aviso na tela"), nao confirmada de ouvido. Se sobrar um pedaco da fala, usar `CT2_MUTE=0ba388` (amostra que costuma vir logo depois) e avisar.

## v15za
- A v15z (bloqueio de 0b28f8/0bb244/0b16c4 no modo livre) nao resolveu: a fala continuou. O bloqueio foi desligado (podia calar outros sons).
- Novo: tecla **F8** durante o jogo grava `>>> F8 - fala ouvida agora` no `voz.txt` (com o quadro). Aperte F8 assim que ouvir a fala; os sons tocados nos quadros logo antes da marca mostram qual amostra e a fala.

## v15zb
- jogar.bat: a checagem "ct2.exe ja atualizado" comparava so datas de arquivo; ao extrair um zip novo por cima, as datas dos fontes podiam ser MAIS ANTIGAS que o ct2.exe e o jogo nao era recompilado (por isso o F8 da v15za nao aparecia). Agora tambem compara VERSION.txt com ct2.ver (gravado apos compilar); versao diferente = recompila.

## v15zc
- Bug do voz.txt: o log de sons e a marca do F8 usavam dois ponteiros de arquivo diferentes; a marca era gravada no fim e logo sobrescrita pelo log seguinte. Agora os dois usam o mesmo arquivo (a marca F8 aparece no log).

## v15zd
- voz.txt agora registra TODOS os formatos de amostra (antes so ADPCM): o log com F8 mostrou 3 avisos (a cada ~600 quadros) sem nenhuma amostra ADPCM coincidente, entao a fala deve ser PCM 16/8 bits. Coluna nova `fmt=` (0=PCM16, 1=PCM8, 2=ADPCM).

## v15ze
- Fala "stop and pick up customers" (modo PLAY AS LONG AS YOU LIKE): o voz.txt (4 avisos, quadros 3524/4124/4724/5324, a cada 600) mostrou que a fala e um STREAM de voz em PCM16 (buffer circular de 8192 amostras, enderecos 1dbfc0/1cff00/1d7f80) cuja tecla-on acontece no mesmo quadro em que o aviso aparece. Agora, no modo livre, ao surgir o aviso o jogo silencia (volume 0, o canal continua rodando para o driver nao travar) os canais PCM16 de 8192 amostras ligados nos ultimos 4 quadros e nos 8 seguintes. Nao testado ao vivo (o harness nao dispara o aviso).
- Limite: outros streams de voz PCM16 que comecem no mesmo instante tambem seriam silenciados.

## v15zf
- SOMBRAS (volumes modificadores do PowerVR): as listas de volume modificador (tipos 1 e 3), antes descartadas, agora sao lidas (triangulos de 64 bytes em espaco de tela, z = 1/w). Depois das listas opacas e antes das translucidas, cada pixel de superficie que "recebe sombra" (bit 7 do cabecalho do poligono) conta as faces do volume ATRAS da superficie; paridade impar = dentro do volume = escurece. Fator de escurecimento 0,5 (estimado, nao lido do registro FPU_SHAD_SCALE); ajustavel com a variavel CT2_SHAD (ex.: CT2_SHAD=0.6). Verificado so reproduzindo os dumps F12 no Linux (sombra sob o taxi e os carros); nao ha comparacao pixel a pixel com o emulador de referencia.
- VIBRACAO: o Maple agora emula um Jump Pack (Puru Puru, funcao 0x00010000) no soquete 2 da porta A. O leitor de DMA ainda tinha um defeito: palavras de enchimento (0x700) entre descritores o desalinhavam e a consulta ao subdispositivo era perdida; agora ressincroniza. Com o pacote presente, a linha VIBRATION do menu Options deixa de ficar cinza e alterna ON/OFF. No Windows o win.c repassa a forca (0-7) que o jogo comanda ao XInputSetState (piso de 35% para a forca 1 ser perceptivel; sem novo comando por ~0,7 s o motor para). O proprio jogo respeita ON/OFF (OFF => forca 0). CT2_NORUMBLE=1 desliga a emulacao; CT2_VIBLOG=1 imprime os comandos de vibracao.
- Nao testado aqui: o envio ao XInput (so Windows) e a interpretacao exata dos bytes do efeito (ordem controle/potencia/frequencia/incremento e escala).

## v15zg
- Sombras corrigidas: o jogo envia VARIOS volumes modificadores por quadro, cada um terminando num triangulo com instrucao de volume (bits 29-31 do ISP). A v15zf somava a paridade de todos juntos, entao volumes sobrepostos se cancelavam e aparecia lixo dentro dos carros. Agora a paridade e calculada por malha e o resultado e a uniao (OU) das malhas. Verificado so nos dumps F12 (c7d08eff, cd4f53e5).
- Limite conhecido: os para-choques/lanternas do proprio taxi podem ficar escurecidos por estarem dentro do volume; o glitch da calcada NAO foi tratado nesta versao.

## v15zh
- Diagnostico das sombras: o F12 agora grava, alem do glitch_dump.bin, o quadro renderizado ao vivo (glitch_frame.ppm) e glitch_info.txt (resolucao, aspecto, upscale, numero de volumes/malhas, pixels receptores, fator de sombra). No Linux o quadro ao vivo e identico ao replay do dump; se no Windows diferir, essa comparacao localiza a causa. replay.c aceita CT2_RUP / CT2_RASP / CT2_RRES para simular upscale/aspecto/resolucao.

## v15zi
- Sombras: o volume de sombra do taxi e uma caixa extrudada cuja parede traseira fica na mesma profundidade do porta-malas; com o teste de profundidade exato, a traseira/para-lamas/para-choque do proprio taxi ficavam com cunhas escuras serrilhadas (z-fighting). Agora uma face do volume so conta como "atras da superficie" se estiver mais de 5% (em 1/w) atras dela (tolerancia ajustavel por CT2_SHTOL, padrao 0.05). A sombra no chao (comprimento ate ~50 px alem do para-choque) bate com a geometria do volume. Verificado so em replays dos dumps (0eab35fd, d570b673, c7d08eff, cd4f53e5).
- O quadro ao vivo do Windows (glitch_frame.ppm) foi identico ao replay no Linux, exceto o contador "50 game time" (UI injetada), entao as sombras ao vivo seguem a mesma logica.
- NAO resolvido: o erro da calcada. O chao inteiro e UM triangulo gigante cortado no plano proximo (vertices com y ~44000 e invW 3.33); a textura sai esticada/borrada nele.

## v15zj
- Sombras de outros veiculos fora de posicao (ex.: mancha escura na calcada ao lado de um carro na rua): cada volume de sombra termina num triangulo com instrucao "dentro do ultimo polígono" (bits 29-31 do ISP = 1). Esse triangulo NAO e uma face normal do volume; contando-o como face, a paridade ficava errada e a sombra escorregava para a frente do veiculo. Agora ele e ignorado na contagem (CT2_MVMODE=0 volta ao comportamento anterior). Verificado so nos replays dos dumps (0eab35fd, c7d08eff, d570b673): a sombra do carro cinza ficou sob o carro e a mancha na calcada sumiu. A faixa de sombra do taxi ate a borda inferior da tela continua; nao ha como confirmar sem comparar com o Redream no mesmo quadro.

## v15zk
- Sombras (correcao de fundo): a contagem de paridade dos volumes usava as faces ATRAS da superficie ("depth-fail"), que so funciona com volumes fechados por tampas no infinito; os volumes do jogo sao prismas abertos para baixo, entao a sombra escorria para a frente do carro e aparecia como cunhas na carroceria. Agora conta as faces NA FRENTE da superficie ("depth-pass"), que funciona com esses prismas, e o triangulo "dentro do ultimo poligono" volta a ser contado como face normal (a v15zj o ignorava e piorou outros quadros). Tolerancia de 2% em 1/w (CT2_SHTOL); CT2_MVMODE=1 e 0 restauram os modos v15zj e v15zi. Verificado so em replays dos dumps (c99e60cf, 0eab35fd, d570b673, c7d08eff): sombra retangular sob o taxi, sem cunhas na carroceria e sem manchas na calcada.
- Varredura de triangulos: lo/hi limitados a +-1e9 antes do (int) (triangulos gigantes do plano proximo podiam gerar (int) indefinido). Nao mudou nenhum dos dumps testados.
- Erro da calcada: nao tratado nesta versao.

## v15zl
- Erro da calcada/asfalto (faixa branca esticada a esquerda do taxi): causa encontrada. A varredura de triangulos (raster) usava uma tolerancia de borda FIXA de 1e-4 em coordenadas baricentricas. Em triangulos normais isso e uma fracao minuscula de pixel, mas nos triangulos gigantes cortados no plano proximo (vertices a 1e5-1e6 px) o gradiente por pixel e ~1e-6, entao a tolerancia equivalia a mais de 100 px: o triangulo da calcada de telhas invadia a faixa-base do chao. Agora a tolerancia e 1e-3 px, proporcional ao gradiente de cada borda.
- Verificado so por replay (Linux): no dump 0eab35fd a faixa tem agora a mesma largura e forma do print do Redream na mesma posicao; nos 17 dumps de glitch as diferencas ficam so nessas faixas e nao vi rachaduras. Ainda falta confirmar no Windows.
- Depuracao (env, desligadas por padrao): CT2_TRITEX=tcw lista os triangulos de uma textura; CT2_VCLAMP=px limita as coordenadas dos vertices (so experimento).

## v15zm
- Teste do salto (Crazy Hop mais baixo na Crazy Pyramid): CT2_NOSTICKY=1 desliga a "borda sticky" das v15u/v15v (patches em ct2_funcs_00/01/06 que mantem o evento de botao apertado vivo por mais quadros) e devolve a borda crua, como antes. Padrao inalterado (sticky ligado). Serve para confirmar se foi isso que encurtou o salto; se sim, a correcao definitiva vem a seguir.

## v15zn
- Investigacao do salto (Crazy Hop mais baixo/grosseiro que no Redream, Crazy Pyramid 4-3): o codigo da CPU e identico desde a v15r, e NOSTICKY (v15zm) e o ritmo do TMU (CT2_TMU=0.0407) nao mudaram nada. Medido no harness: o relogio de vblank e movido por trabalho (1 relogio a cada 2048 entradas de funcao + escritas em SR, 16 relogios = 1 vblank); nos menus o jogo gasta 16 relogios por quadro, mas em jogo gasta 32, ou seja, o jogo "acha" que roda a 30 fps (2 vblanks por quadro) e o numero de atualizacoes de logica por quadro (atualiza_pad) fica em media > 2. Hipotese: o passo de tempo da fisica usa o numero de vblanks decorridos; com 2 vblanks o passo dobra e o salto fica mais baixo e menos suave.
- Novo: CT2_VBLK=N = relogios por vblank (padrao 16, inalterado). Com CT2_VBLK=32 cada quadro de jogo dura 1 vblank. O audio por relogio e o SPG_STATUS acompanham (nenhum outro temporizador muda). Depuracao: CT2_TICKLOG=1 imprime o histograma de relogios por quadro a cada 600 quadros.
- (corrigido logo apos a primeira entrega: ct2_vper estava dentro do bloco so-Linux e o build do Windows falhava; agora conferido com -fsyntax-only nos dois modos.)
- Verificado so no harness Linux (histograma de relogios por quadro). NAO verificado: o salto em si, velocidade geral do jogo e audio com CT2_VBLK=32 no Windows.

## v15zo
- TYPE C (macros de toque duplo): o salto era menor no TYPE C que no tipo A do jogo (relato do usuario; o Redream usa o tipo A). O TYPE C e o tipo A do jogo mais as macros de toque duplo em R (D+gas) e L (A = re + freio), e a regra antiga disparava a macro com qualquer soltar+apertar em 30 quadros (o 1o toque podia durar qualquer tempo): pisar normalmente no freio/acelerador engatava a re e cortava o gas. Agora: o 1o toque tem de ser curto (<=12 quadros), o 2o comeca ate 18 quadros depois de soltar, e nenhuma macro dispara nem continua com o Y (salto) apertado e nos 25 quadros seguintes. Corrigi so o filtro (menux.inc); nada mais mudou.
- NAO verificado: no Windows, se o salto no TYPE C volta ao normal. Verificado so que compila e o harness Linux roda.

## v15zp
- Diagnostico do salto: F10 arma a gravacao; no primeiro quadro com Y apertado o jogo grava em hop_trace.txt, para cada um dos 45 quadros seguintes, as palavras de 32 bits da RAM que mudaram (endereco, valor antigo, valor novo; ate 40000 por quadro) mais o estado do pad. Serve para achar a velocidade vertical do carro e o codigo que aplica o impulso do Crazy Hop. Nada muda no jogo; so grava. Teste no Linux: CT2_HOPARM=<quadro> arma sozinho.
- Contexto: o salto no nosso jogo e muito mais baixo que no Redream (e o Redream roda mais fluido). Ja descartados (sem mudanca no salto): vibracao, sombras, desempenho, borda sticky (CT2_NOSTICKY), ritmo do TMU (CT2_TMU), relogios por vblank (CT2_VBLK=32), macros do TYPE C (v15zo). O codigo da CPU e igual desde a v15r.

## v15zq
- Salto, achados da v15zp (hop_trace.txt do usuario, Crazy Pyramid 4-3, tipo A): 0x8c173aa0 = altura do carro (0,57 -> pico 8,6 no quadro 18 -> volta a ~0,16 no 31), 0x8c173ab8/0x8c173ac4 = velocidade vertical (ac4 e ab8 + 0,11): o impulso entra no quadro 4 (depois do Y apertado nos quadros 0-2): velocidade -0,0212 -> 1,4587 (impulso 1,4799), depois cai 0,11 por quadro (gravidade); 0x8c173d74 = estado do salto (0 -> 2 quando o jogo recebe o evento do Y, 2 -> 4 quando salta). No Redream o salto e mais alto e mais longo (relato do usuario) => impulso inicial maior (ou gravidade menor).
- Novo: durante a gravacao F10 tambem sao gravadas, em hop_trace.txt, as escritas nesses 4 enderecos com a pilha de funcoes (linhas "W ..."), para achar qual funcao do jogo calcula o impulso. Nada muda no jogo; o vigia so existe durante os 45 quadros da gravacao.

## v15zr
- Crazy Hop (Y): o codigo original calcula a forca do salto como 3,0 x (0,5 + 0,125 x |velocidade|), com teto em 1,0 (confirmado nos dois hop_trace: parado = forca 1,5, altura ~8,6; em velocidade >= 4 = forca 3,0, altura ~41). O salto parado tem ~1/5 da altura do salto em velocidade; o jogo original ja era assim, mas ficava quase impossivel pular entre plataformas do Crazy Pyramid.
- Mudanca (a pedido): o fator e sempre 1,0, ou seja, o salto sai com forca maxima mesmo parado/devagar. Patch em 0x0c010a42 (sh4c.py + ct2_funcs_00.c). CT2_HOPORIG=1 restaura o comportamento original. Nada novo nas Opcoes.

## v15zs
- Diagnostico de ritmo (so grava; nada muda no jogo): o perf.txt ganha (a) a linha "ritmo:" a cada 60 quadros, com o maior intervalo entre quadros, quantos passaram de 20/25/34 ms e quantos desenhos foram pulados; (b) uma linha "quadro_longo" para cada quadro que demorou mais de 25 ms, com a divisao logica/desenho/tela/espera. Serve para ver se o "menos fluido" do port vem de quadros atrasados. Medicoes no Linux mostraram que a fisica do salto e o relogio do jogo (60 quadros = 1 s) estao corretos.
- Salto: segurar Y >= ~8 quadros da um impulso ~3,6 contra ~2,9 do toque (codigo original). O patch de forca maxima (v15zr) so afeta o carro parado/lento; continua ligado, CT2_HOPORIG=1 volta ao original.

## v15zt
- CAUSA do Crazy Hop baixo (achada comparando com a v15m, onde o salto estava certo): desde a v15t a janela transformava A/B/X/Y/START em um toque de EXATAMENTE 3 quadros, mesmo com o botao segurado. O jogo carrega o Crazy Hop conforme o tempo que Y fica apertado (>= ~8 quadros da um impulso ~3,6 contra ~2,9 do toque; ~50% mais alto), e o toque fixo de 3 quadros tirava essa carga. Na v15m o botao seguia o dedo.
- Correcao: em jogo (ta_ingame_play) A/B/X/Y seguem o dedo (minimo de 3 quadros por toque, sem limite maximo); o limite de 3 quadros continua nos menus e no START (que fazia a pausa abrir/fechar). Em win.c.
- O patch de forca maxima da v15zr agora vem DESLIGADO (salto original); CT2_HOPMAX=1 o liga. O diagnostico de ritmo da v15zs foi retirado.

## v15zu
- Vibracao desativada por padrao (a pedido): o Jump Pack nao e mais anunciado ao jogo no Maple, entao o jogo nem manda efeitos. CT2_RUMBLE=1 liga de novo. NAO verificado no Windows (XInput).
- EXIT do menu principal agora fecha o jogo (A na ultima linha do menu de modos = encerramento limpo, como fechar a janela). Verificado so que compila; NAO testado no Windows.
- Sensibilidade da direcao: novo log `sens.log` (quadro, em_jogo, nivel 0-7, analogico X bruto -> enviado ao jogo, motivo). Achado no harness: o ajuste so age quando o jogo detecta "em jogo" (HUD amarelo "game time" visivel, sem pausa) e o nivel e < 7; fora disso o log diz "fora de jogo: sem curva". Se no seu PC o ajuste continuar sem efeito, envie o sens.log: a coluna "motivo" mostra se foi deteccao de HUD, nivel ou curva fraca (a curva original e x^(1+(7-nivel)*0,17), suave no minimo).
- OPTIONS: a linha FRAMERATE foi trocada por ANTIALIASING com 4 modos: OFF, BASIC (suavizacao barata de 4 vizinhos so onde ha contraste forte; serve para placas antigas), FXAA (mistura 3x3 proporcional ao contraste local) e SSAA 2X (desenha num quadro interno ate 2x maior e reduz por media de caixa; pesado, limitado a 1707x960 internos). Salvo em opcoes.ini (chave `aa=`); CT2_AA=0..3 forca. O modo 30 FPS foi removido. Verificado no harness (imagens geradas nos 4 modos); NAO medido o desempenho no Windows.
- Memory card: VMU emulado na porta A, slot 1 (128 KB em `vmu_a1.bin`, criado e formatado no primeiro uso; CT2_NOVMU=1 desliga). Implementados: info do dispositivo, GetMediaInfo, BlockRead (512 bytes), BlockWrite (4 fases de 128 bytes, grava no arquivo) e confirmacao; LCD/relogio/buzzer so respondem ack. O arquivo `vmu.log` lista os comandos Maple distintos vistos no slot 1. LIMITACAO: no harness o jogo reconhece o VMU (passa a escrever no LCD) mas, ate o quadro ~3500 (tela de aviso de memory card e titulo), ainda nao enviou nenhum comando de armazenamento (GetMediaInfo/BlockRead) e a tela de aviso continua dizendo que nao ha blocos livres; nao consegui chegar a uma tela de SAVE no harness. Ou seja: salvar progresso NAO esta confirmado. Se nao salvar, envie o vmu.log.
- Sombra no corpo do carro: nao consegui reproduzir a cunha escura no harness (nos quadros de jogo alcancaveis a sombra nao aparece), entao nao alterei o sombreamento; iluminacao descartada a pedido. Se a cunha aparecer, aperte F12 no momento e envie glitch_dump.bin/glitch_frame.ppm/glitch_info.txt para eu ajustar.

## v15zv
- Desempenho do renderizador (relato: ANTIALIASING e escala de resolucao ficavam lentos em jogo num Ryzen 9 5950X): o render por software dividia o quadro em no maximo 8 faixas fixas (desequilibradas). Agora o quadro e dividido em faixas pequenas (3 por thread) que as threads puxam de uma fila (contador atomico), e o filtro de antialiasing (BASIC/FXAA/reducao do SSAA) tambem roda em paralelo. Padrao: ate 6 threads (PCs fracos usam todos os nucleos que tiverem); CT2_THREADS=N muda (ate 32). A imagem renderizada e identica com 1 ou 2 threads (conferido por hash no harness).
- Novo modo SSAA 1.5X (meio-termo mais leve que o SSAA 2X, que renderiza 4x os pixels). Modos: OFF, BASIC, FXAA, SSAA 1.5X, SSAA 2X. Para PCs fracos: OFF ou BASIC; a escala de resolucao em 100% e o mais leve.
- NAO medido no Windows: o ganho real de velocidade. O harness Linux tem 2 nucleos, entao so verifiquei que a saida nao muda e que compila (-fsyntax-only do Windows ok).

## v15zw
- NOVO renderizador Direct3D 11 (recomp/gpu11.inc), ligado por padrao: a CPU continua emulando o console e decodificando texturas; a GPU desenha os triangulos (texturas com mipmaps, 3 filtros/enderecamentos, mistura do translucido com os fatores do TSP, recorte por alfa, Z por modo ISP, volumes de sombra por stencil). O quadro volta para fb[] (leitura da GPU) e o resto (antialiasing, escala/resolucao, menus, pausa, writeback) segue igual. Base de hardware: Intel HD 4000 (3a geracao): feature level 10_0, shaders SM 4.0, D24S8, sem compute; o compilador de shaders (d3dcompiler_47/46/43.dll) e carregado em tempo de execucao, sem libs extras de link.
- Se o D3D11 nao iniciar (sem dll, sem GPU compativel, shader recusado) o jogo cai sozinho no renderizador por software; `gpu.log` explica (adaptador, feature level, erros de shader, "primeiro quadro desenhado na GPU"). F4 alterna GPU/SOFTWARE em jogo (salvo em opcoes.ini, chave gpu=; CT2_GPU=0 forca software). O titulo da janela mostra [D3D11] ou [SOFTWARE]. Escala de resolucao e SSAA agora custam GPU em vez de CPU.
- NAO verificado: tudo isso e codigo escrito sem poder rodar Direct3D aqui (so compilei contra headers de teste). Pode haver erro de build (cole a mensagem) ou diferencas visuais. Diferencas conhecidas/premissas: o translucido 100% opaco (corpo dos carros) nao grava Z na GPU; empates de profundidade e bordas de triangulo seguem as regras da GPU e nao a tolerancia do software; as sombras usam paridade por malha (uniao das malhas) so em pixels receptores. DX12 e Vulkan: ainda nao implementados (nao aparecem no menu).

## v15zx
- Corrige o erro de build da v15zw (clang: `IUnknown_Release` nao declarado, pois windows.h entrava antes de COBJMACROS). O gpu11.inc agora chama os metodos COM direto pela vtable (`->lpVtbl->Release`) e ta.c define COBJMACROS antes de windows.h. Continua NAO testado em Direct3D real.

## v15zy
- GPU (D3D11): veiculos transparentes e falha na roda (relato). Causa: o corpo dos carros e desenhado na lista translucida com alfa 1; o software grava Z nesses pixels (alfa >= 0,99) e a v15zw/zx nao gravava, entao a roda/interior aparecia por cima. Agora cada lote translucido ganha uma segunda passada so de profundidade (pixel shader descarta alfa < 0,99; sem cor). Custa um desenho extra dos translucidos. Nao testado em D3D real; F4 volta ao software se ainda houver falha.

## v15zz
- Resolucao 2K/4K: o quadro interno agora acompanha a resolucao escolhida (altura/480), ate 3840x2160 com GPU (D3D11); no software continua limitado a 1707x960. UPSCALE e SSAA usam o mesmo limite.
- Menu de pausa: nova linha BRIGHT (esquerda/direita, 0-10, 5 = normal), salva em opcoes.ini (bright=). Aplicada como pos-processamento no quadro final.
- SENSIBILITY: alem da curva, agora limita a deflexao maxima e a velocidade de giro, entao tambem vale para teclado/digital (0/255). Nivel maximo = resposta original.
- VIEW: ANGLE renomeado para PERSPECTIVE; a troca de camera e enviada ~6 quadros depois de voltar ao jogo (antes ~30). Ajuste do ponto de vista do painel (esquerda/atras) NAO foi feito: precisa de teste no jogo.
- Sombra no porta-malas: nao corrigida (o dump enviado e de outra cena; precisa de F12 na cena com a cunha).
- Memory card: nao alterado nesta versao.
- F12 agora grava glitch_dump_<quadro>.bin (nome novo a cada vez; erro em dump_erro.txt), porque o glitch_dump.bin antigo nao era sobrescrito.
- VMU: testadas 24 variantes de resposta do dispositivo (ordem/endianness das definicoes de funcao); em nenhuma o jogo passou a ler/gravar blocos. Memory card continua pendente.
- Compilacao (jogar.bat -> compile.ps1): agora em paralelo e com cache de objetos em recomp\obj. Os ct2_funcs_*.c (gigantes e que nao mudam) so sao compilados na primeira vez; a cada versao so boot/win/ta/etc. sao refeitos. Se mudar o compilador ou algum .h, refaz tudo.
- Sombra no porta-malas (cunha escura): corrigida no renderizador por software. Causa: a tolerancia de profundidade dos volumes de sombra (2%) ficava no meio de uma camada fina de volume (z 0.0291/0.0292) a ~2% a frente da carroceria, contando so uma das duas faces e escurecendo o carro. Tolerancia agora 0.0001 (CT2_SHTOL ainda sobrescreve). Verificado com glitch_dump_3628.bin: com a sombra desligada o resultado e igual ao do emulador; com o ajuste a cunha some e a sombra no chao e mantida. O caminho D3D11 usa teste de profundidade exato e ja nao tinha essa tolerancia.
- Debug: CT2_MVDBG / CT2_MVSKIP=<malha> / CT2_MVPIX=x,y (lista volumes de sombra), CT2_TRIHASH agora inclui cores e flag de sombra.
- compile.ps1: corrigido falso "FALHOU" (ExitCode vazio no Start-Process sem Handle); agora tambem confere se o .o foi gerado.
- VIEW: selecao absoluta da camera (A primeiro, depois o botao da opcao, pulsos de 3 quadros).
- VIEW: a opcao ANGLE (botao Y do controle virtual) e a camera baixa, a altura da roda (glitch_dump_11290.bin). Nome voltou a ser ANGLE.
- VMU: testadas tambem as 6 ordens das definicoes de funcao e 8 valores para a definicao de memoria (CT2_VMUMEM=hex); o jogo continua sem enviar comandos de armazenamento. Pendente.
- VIEW agora tem 5 opcoes: NORMAL (START+A), DASHBOARD (START+B), ANGLE (Y, camera da roda), SPEEDOMETER (START+X) e PERSPECTIVE (START+Y, a panoramica). Se alguma abrir a camera errada, trocar os botoes em menux.inc (bt[]/st[]).
- VIEW: ANGLE volta a START+Y (Y sozinho nao fazia nada). PERSPECTIVE = START+Y duas vezes (premissa, a verificar no jogo).
- VIEW: botoes trocados conforme teste no jogo: B = camera da roda (ANGLE), Y = painel (DASHBOARD).
- VIEW: nao e mais lido de opcoes.ini; o menu sempre comeca em NORMAL (o jogo abre na camera normal).
- VIEW (teste do usuario): START+Y = camera de replay (nao serve como DASHBOARD). Agora DASHBOARD=START+X, ANGLE=START+B, SPEEDOMETER=START+X 2x (premissa), PERSPECTIVE=START+Y (replay, a rever).
- VIEW (testes do usuario): X abre PERSPECTIVE, B abre ANGLE+DASHBOARD juntos, START+X 2x = SPEEDOMETER (as vezes normal). Mapa novo: A normal; B 1x ANGLE; B 2x DASHBOARD; X 1x PERSPECTIVE; X 2x SPEEDOMETER, com A antes e pulsos espacados.
- VIEW (6 opcoes): NORMAL (START+A), ANGLE (START+B 1x, camera da roda), BACK (START+B 2x, camera do glitch_dump_4536.bin), PERSPECTIVE (START+X 1x), DASHBOARD (START+B 3x, palpite), SPEEDOMETER (START+X 3x, palpite). START+Y abre o replay (nao usado).
- VIEW (medido no emulador com CT2_P3 / CT2_VIEWSET): A = camera normal; B cicla ANGLE -> DASHBOARD -> BACK -> ANGLE; X cicla PERSPECTIVE -> SPEEDOMETER; Y = replay (nao usado). O jogo ignora toques a menos de ~40 quadros um do outro e nao registra pulsos de 3 quadros; agora: A, depois N toques de 6 quadros com 40 de intervalo (CT2_VSPACE). Opcoes: NORMAL, DASHBOARD (B 2x), ANGLE (B 1x), SPEEDOMETER (X 2x), PERSPECTIVE (X 1x), BACK (B 3x). Verificado: ANGLE, DASHBOARD, BACK, PERSPECTIVE, SPEEDOMETER.
- Teste: CT2_VIEWSET="opcao:quadro,..." escolhe a camera sem passar pelo menu.
- VIEW: toques contados por quadro real do jogo (nao pelo menu), espera ~0,8 s depois da pausa, A, e o botao N vezes (6 quadros, 40 de intervalo). No emulador: DASHBOARD, ANGLE e PERSPECTIVE saem certos; BACK (B 3x) e SPEEDOMETER (X 2x) falharam em parte dos testes. Ajustar com CT2_VSPACE.

- v15zz (camera VIEW): a sequencia de toques agora roda UMA vez (piscadas curtas de pausa nao a reiniciam; so aborta apos 45 quadros pausado) e grava `view.log` ao lado do jogo com a selecao, cada toque (botao e quadro) e o fim. Se a camera ainda sair errada, envie o `view.log`.
- compile.ps1 agora decide o que recompilar pelo hash do conteudo (antes: data do arquivo, que podia manter um .o antigo apos extrair o zip).

- v15zz (camera VIEW, laco fechado): o jogo guarda o modo de camera em 0x8c175e58 (0 normal, 9 ANGLE, 1 DASHBOARD, 2 BACK, 10 PERSPECTIVE/SPEEDOMETER). O menu agora le esse valor e toca START+botao (B cicla ANGLE>DASHBOARD>BACK, X entra no modo 10, A = normal) ate o jogo chegar na camera pedida, em vez de contar toques as cegas. SPEEDOMETER nao tem variavel propria: e o 2o toque de X (modo continua 10). `CT2_CAMLOG=1` mostra as trocas de modo; `view.log` registra os toques.

- v15zz (correcao): a leitura do modo de camera (0x8c175e58) estava num trecho que o jogo em janela nao executa (retorna antes), entao no PC o menu nunca via a camera mudar e apertava B ate o limite. Movida para antes do retorno. Novo `camera.log` (sempre gravado): cada mudanca de modo com o numero do quadro; junto com `view.log` mostra o que o menu pediu e o que o jogo fez.

- v15zz: o toque do VIEW e solto assim que o modo de camera muda (um passo por toque) e a sequencia para exatamente no alvo, sem passar dele.

- v15zz: VIEW so termina quando a camera pedida se mantem por 12 quadros (o jogo as vezes da um 2o passo no quadro seguinte, visto no camera.log); se sair do alvo, toca de novo.

- v15zz: PERSPECTIVE/SPEEDOMETER partindo do modo 10 voltam antes ao normal (A) e contam os toques do X desde zero, pois o modo 10 nao diz em qual dos 2 estados do X a camera esta (1o toque = camera tipo replay com cortes; 2o = outra).

- v15zz: toque do VIEW de 3 quadros (antes 6) para o jogo nao andar 2 passos por toque no PC (camera.log mostrava 9->1 e 2->9 em quadros seguidos); CT2_PLEN muda isso. Testes (harness) de X1..X4 mostram a mesma camera externa com cortes: PERSPECTIVE e SPEEDOMETER ainda nao distinguiveis.

- v15zz: toque de 6 quadros de volta (o de 3 piorou DASHBOARD/ANGLE no PC); so o toque que pousa em BACK e curto (2). Testado: escrever o modo direto na RAM (CT2_CAMFORCE) NAO serve: a camera fica errada (o botao muda mais variaveis alem de 0x8c175e58).

- v15zz: comparado com v15zc..v15zl (enviadas pelo usuario): nelas SPEEDOMETER era 1 toque de START+X, sem PERSPECTIVE. Agora SPEEDOMETER = 1o toque do X, PERSPECTIVE = 2o toque (antes estava invertido: PERSPECTIVE=1, SPEEDOMETER=2).

- v15zz: PERSPECTIVE = START+Y (modo 7 no jogo): a camera parada na rua enquanto o carro anda, que era a que funcionava nas primeiras versoes (la o botao Y era usado). SPEEDOMETER = START+X (modo 10). Testado no emulador: Y, B, X, Y em sequencia chegam ao modo pedido.

- v15zz: BACK usa toques a cada 24 quadros (antes 40) para passar menos tempo por ANGLE/DASHBOARD; as demais cameras inalteradas.

- v15zz: barra de UPSCALE do menu Options: o molde era o MUSIC VOLUME, que so desenha os blocos acesos; mudar o volume mudava a barra. Agora o bloco e o passo sao guardados (atualizados a cada quadro enquanto ha 2+ blocos) e a barra de UPSCALE sempre tem 8 blocos.
- v15zz (VMU, investigacao): o botao SAVE & LOAD do menu fica apagado porque o jogo nunca chama a biblioteca de backup. Ela depende de threads cooperativas do proprio jogo (criadas em 0c0948a4, trocadas por 0c094c76) que o runtime ainda nao escalona; o LCD do VMU ja e reconhecido (comando 12 chega). Salvar no VMU continua PENDENTE. Diagnosticos novos (inertes por padrao): CT2_WWATCH=<offset hex na RAM> imprime mudancas de uma palavra; CT2_WATCH agora imprime tambem as chamadas 32, 64, 128...
- v15zz: filtro anisotropico nativo 16x nas texturas filtradas do backend D3D11 (so custo de GPU; NAO testado no Windows).
- v15zz: threads da CPU: agora usa ate 90% dos processadores logicos (antes ate 6). Com a GPU pronta o desenho dos triangulos fica na GPU; a CPU so faz o que sobra (pos-processamento/AA). CT2_THREADS=N continua valendo. NAO testado no Windows.
- v15zz: DUAS RESOLUCOES (renderizador por software): menus e HUD sao desenhados sempre na escala maxima (1280x960; 1707x960 em tela larga), nitidos como a 200%, e o slider UPSCALE passou a mudar so a parte 3D (que e ampliada com bilinear e recebe o HUD por cima). Quadros que sao so menu/tela de resultado (sem lista opaca) saem inteiros na escala maxima e sem AA (texto nao borra). Custo no 3D: o mesmo de antes + uma ampliacao paralela. CT2_NODUAL=1 volta ao comportamento antigo. Backend GPU inalterado.
- v15zz: ANTIALIASING padrao agora FXAA (so vale quando nao ha opcoes.ini com aa=; quem ja tem o arquivo mantem a escolha). No modo de duas resolucoes o FXAA atua so no 3D.
- v15zz: diagnostico CT2_UIDBG=<quadro> imprime a classificacao de triangulos UI/3D.
- v15zz: corrigida a linha vertical clara nas emendas das caixas dos menus (ponta arredondada x meio): quadros planos (mesma profundidade em todos os vertices: menus, HUD, sprites) com UV dentro de 0..1 agora tem as coordenadas limitadas ao retangulo da textura; antes a amostragem bilinear dava a volta na textura e misturava a coluna do lado oposto. Quadro de jogo (3D) identico ao anterior. CT2_NOCLAMPUI=1 desliga. Diagnosticos: CT2_PIXLOG=x,y (quais triangulos pintam o pixel), CT2_TEXONE=<tcw hex>.
- v15zz: **MEMORY CARD (VMU) AGORA SALVA E CARREGA.** Causa raiz achada (eram 3 pecas que faltavam, todas na biblioteca de backup do jogo): (1) o jogo agenda as threads do backup pela interrupcao "fim do DMA Maple" (ISTNRM bit 12), que o emulador nunca gerava -> agora e gerada ao fim de cada DMA Maple (CT2_NOMAPLEIRQ=1 desliga); (2) essas threads usam setjmp/longjmp proprios (0c095ccc/0c095c58) com troca de pilha, impossivel no codigo recompilado que usa a pilha do hospedeiro -> em sh4rt.c viraram COROTINAS do hospedeiro (ucontext no Linux, Fibers no Windows; CT2_NOCORO=1 desliga); (3) acessores de 16 bits da resposta do cartao (0c16857c/86/8e/96) e copia de palavras (0c16856c) sao chamados por ponteiro de funcao e nao tinham sido recompilados (liam 0 -> a validacao da midia falhava) -> implementados em boot.c. A resposta GetMediaInfo (cmd 10) passou a usar campos de 16 bits little-endian como o hardware (CT2_VMUBS=1 volta ao formato antigo). Resultado: SAVE & LOAD habilitado no Mode Selection; a tela mostra "199 free blocks", pede confirmacao e grava o arquivo C_TAXI02.SYS (20 blocos) em vmu_a1.bin ("179 free blocks"); LOAD le os 20 blocos de volta. Diagnosticos: CT2_VMUTRACE=1 (cada comando de armazenamento), CT2_RANGE=lo,hi,quadro (1a chamada de cada funcao), CT2_RETW=lista (retorno de funcoes chamadas por ponteiro), CT2_WHOP=addr (vigia de escrita). NAO testado no Windows: as corotinas usam Fibers (CreateFiber/SwitchToFiber).
- v15zz: TECLADO CONFIGURAVEL. Em Options > Controller Setting aparece o campo "Y KEYBOARD SETUP" (Y no controle / tecla V por padrao). Abre um painel com 11 acoes (cima, baixo, esquerda, direita, acelerar R, freio L, A, B, X, Y, start): cima/baixo escolhem, A (Z) troca, depois e so apertar a tecla desejada (Esc cancela), RESET DEFAULT volta ao padrao, B sai. Se a tecla ja era usada por outra acao, as duas trocam de lugar. Salvo em `opcoes.ini` (linha kmap=). Padrao novo: W/S/A/D direcional, Espaco acelerar, Shift esq. freio, Z=A, C=B, X=X, V=Y, Enter=Start (setas/P/Tab seguem valendo ate haver mapa proprio). Quem joga no teclado usa o controle moderno automaticamente: tocar duas vezes em ACELERAR = turbo dash (e duas em FREIO = marcha re + freio). Painel testado no emulador (harness, tecla simulada); a captura real de tecla (win.c) NAO foi testada no Windows.
- v15zz: "distance view" descartado a pedido. EXIT do menu principal: agora (no Windows) pede o fechamento da janela em vez de usar o longjmp, que podia nao sair da fibra; A ou Start em EXIT fecham. Testado so no harness (Linux).
- v15zz: PAINEL DE CONTROLES UNIFICADO. Em Options > Controller Setting o campo agora diz "PAD Y OR KEY ENTER TO CUSTOMIZE CONTROLS" (Y no controle, Enter no teclado). O painel tem duas colunas, KEYBOARD e CONTROLLER (XInput): cima/baixo escolhem a acao, esquerda/direita escolhem a coluna, A troca e depois e so apertar o botao/tecla (Esc cancela). Botoes do XInput: D-pad, Start, Back, L3/R3, LB/RB, A/B/X/Y, LT/RT. Salvo em opcoes.ini (kmap= e pmap=). Padrao do controle identico ao antigo (LB/RB e gatilhos analogicos so valem sem mapa proprio). Controle generico (winmm, sem XInput) nao e remapeavel. Painel testado no harness; captura de tecla/botao reais so existe em win.c (NAO testado no Windows).
- v15zz: WIDESCREEN EXP: quadros sem 3D (menus, intro, logos, NOW LOADING) agora ficam em 4:3 centralizado com barras laterais (CT2_WIDEMENU=1 volta ao antigo). Dentro do jogo o 3D continua ESTICADO horizontalmente para preencher (nao ha FOV real: a geometria ja vem projetada em 4:3 do jogo); use ASPECT = SIDE BARS para nao distorcer.
- v15zz: ABERTURA: duas telas novas do porte, `intro_loading.ppm` (NOW LOADING) e `intro_adx.ppm` (distribuidora/CRI ADX), exibidas antes do jogo (2,6 s e 3,2 s; qualquer tecla/botao pula; CT2_NOSPLASH=1 desliga). O jogo em si nunca desenhava essas telas neste fluxo (verificado: nenhum quadro com poucos triangulos). Tambem: durante a abertura (SEGA/Hitmaker), apertar Start de novo (>=2 s depois do Start do memory card) avanca em alta velocidade ate o menu principal (so desenha 1 quadro a cada 6).
- v15zzb: FOV REAL NO WIDESCREEN EXP: o 3D deixa de ser esticado; a cena e desenhada em 4:3 de proporcao centralizada e o jogo ja entrega geometria alem das bordas (testado no harness: predios, grama e rua preenchem a tela 16:9 sem achatar o taxi). Risco: objetos que o jogo recorta pelo campo 4:3 podem "aparecer do nada" perto das bordas laterais. CT2_STRETCH3D=1 volta ao esticado. Quadros com menu (mx_screen) tambem ficam em 4:3 com barras. EXIT do menu principal: no Windows o menux_filter roda dentro de win_pad (win.c), nao no boot.c; o pedido de saida agora e tratado la (corrige o EXIT que so voltava a tela de titulo). Dica do campo Controller Setting reescrita: "PAD PRESS Y / KEYBOARD PRESS ENTER". Atencao: com A/D como esquerda/direita, apertar a tecla A ou D na tela Controller Setting muda o TYPE (B->C), como o D-pad.
- v15zzc: o painel de mapeamento (teclado + controle juntos) e seu campo agora so aparecem no TYPE B do Controller Setting; TYPE A e C ficam como eram (sem o campo extra).
- v15zzd: removido o avanco rapido (Start pula intro). Tela ADX/CRI agora aparece DEPOIS dos logos SEGA/Hitmaker (detectados por >=100 quadros seguidos com poucos triangulos) e antes do press start, pausando o jogo 3,2 s (qualquer tecla pula); NOW LOADING continua antes do jogo. Windows: NAO testado (so o codigo do harness compila aqui).
- v15zze: telas NOW LOADING e ADX/CRI ficam em 4:3 centralizado (barras laterais) em SIDE BARS e WIDESCREEN EXP; so esticam no modo STRETCH.
- v15zzf: barras laterais SEMPRE pretas nos menus/carregamento/logos em 4:3 e widescreen (antes cinza 0x20 no fundo e cor do canto da imagem nas telas ADX/NOW LOADING). Verificado no harness (pixels das bordas = 0,0,0); caminho D3D11 mudou so a cor do clear (nao testado no Windows).
- v15zzg: padrao do teclado agora = SETAS para direcional (menus e gameplay); WASD so se o jogador atribuir. Painel de controles: cada tecla/botao aparece como CAIXA selecionavel, suporte a MOUSE (passar por cima seleciona, clicar atribui), e corrigido o bug em que a tecla/botao ja segurado ao abrir a captura (ex.: Z/Enter/A usados para confirmar) era atribuido na hora. Harness: painel/caixas conferidos; mouse e captura reais so em win.c (NAO testado no Windows).
- v15zzh: teclado: acelerar/frear/marchas agora viram os botoes certos do tipo do jogo (no tipo B: A acelera, B freia, R=D, L=R), entao o toque duplo em ACELERAR faz turbo dash tambem no tipo B e some o conflito de teclas em jogo; mapa W/A/S/D antigo salvo no opcoes.ini volta para as setas; tipo do controle (A/B) fica salvo
- v15zzi: WIDESCREEN: corrigido o sumico de predios/carros/pedestres nas bordas laterais quando a camera gira. Causa: o jogo descarta objetos inteiros cujo centro projeta alem de uma extensao horizontal (0x8c2bde50) calculada para 4:3; em widescreen a extensao agora e multiplicada (16:9 = 3x). Testado no harness dirigindo: objetos que faltavam (ex.: onibus na borda direita) passam a aparecer. CT2_NOCULLFIX=1 desliga.
- v15zzj: LAUNCHER (launcher.c + launcher_core.h + launcher/*.raw): tela com fundo do usuario, SELECT FILES (.gdi ou track03.bin) -> verifica volume e SHA-1 do 1ST_READ.BIN -> extrai o disco para disc\ com barra de progresso -> libera PLAY (preto e branco ate la). jogar.bat compila e abre o launcher; PLAY segue para gerar/compilar/rodar. Lembra o disco em launcher.ini. Pular: jogar.bat "pasta" ou CT2_NOLAUNCHER=1.
- v15zzk: ESC nao fecha mais o jogo: vira o Start (abre/fecha o menu de pausa na gameplay); launcher reorganizado em launcher\ (src, assets, build.bat -> CrazyTaxi2.exe com icone embutido via windres); PLAY do launcher abre o jogar.bat sozinho; jogar.bat sem argumento so abre o launcher.
- v15zzl: build do launcher (launcher\build.bat) agora cria a 'versao pronta' em dist\CrazyTaxi2_<versao>\ (CrazyTaxi2.exe com icone + so as pastas necessarias, sem o C derivado do disco; 'build.bat withc' inclui); textos do launcher em ingles.
- v15zzm: jogar.bat sem argumento volta a gerar/compilar/executar o jogo (usa o disco lembrado em launcher.ini); o launcher abre com 'jogar.bat launcher' ou pelo CrazyTaxi2.exe.
- v15zzn: tools/sh4c.py agora gera o ct2_dispatch.c completo (hooks hle_setjmp/hle_longjmp do VMU + depuracao CT2_RETW); antes eram editados a mao e uma regeneracao (primeiro PLAY da versao pronta) os perderia; jogar.bat sem disco conhecido abre o launcher; sem caminho pessoal fixo.
- v15zzp: o launcher agora, ao terminar de extrair o disco, gera o C e compila o jogo (jogar.bat em modo CT2_BUILD_ONLY, janela oculta, log em build.log) com barra de progresso, e so entao libera o PLAY; ao abrir com disco ja extraido mas sem ct2.exe/versao antiga, refaz o build sozinho; o passo de teste do C (compilacao duplicada) e pulado; launcher\build.bat agora e CRLF, nao fecha a janela em caso de erro (duplo clique) e avisa se nao conseguir criar dist.
