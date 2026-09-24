<p align="center">
  <img src="assets/logo-256.png" alt="Audio Watchdog" width="180">
</p>

<h1 align="center">Audio Watchdog</h1>

<p align="center">
  Serviço do Windows open source que mantém os dispositivos de áudio fora do <b>modo exclusivo</b><br>
  e, opcionalmente, padroniza a taxa de amostragem e a profundidade de bits.
</p>

<p align="center">
  <img alt="Versão" src="https://img.shields.io/badge/vers%C3%A3o-1.1.0-1f6feb">
  <img alt="Windows 10/11" src="https://img.shields.io/badge/Windows-10%20%7C%2011-0078d4">
  <img alt="Licença" src="https://img.shields.io/badge/licen%C3%A7a-AGPL--3.0-555">
</p>

---

## O que é

O **Audio Watchdog** é um programa nativo para Windows (C++20 / Win32) que roda como
**serviço do Windows** e vigia continuamente todos os dispositivos de áudio de reprodução e de
captura. Sempre que um dispositivo volta a permitir o **modo exclusivo** — por causa de um
aplicativo, de uma atualização de driver, do próprio Windows ou de alguém mexendo no painel de
Som — o Audio Watchdog desliga o modo exclusivo de novo, confere se a mudança ficou gravada e
continua monitorando.

Uma pequena aplicação na **bandeja do sistema** mostra o estado e permite pausar, retomar e
encerrar a proteção.

## O problema que resolve

O Windows permite que um aplicativo tome o **controle exclusivo** de uma placa de som
(opção *"Permitir que aplicativos assumam o controle exclusivo deste dispositivo"*). Enquanto isso
acontece, todos os outros programas perdem o acesso ao dispositivo: o navegador fica mudo, a
chamada no Zoom/Teams cai, o OBS para de capturar, a automação de rádio perde a saída.

Desligar a opção manualmente não basta: drivers e atualizações do Windows costumam religá-la em
silêncio. O Audio Watchdog trata essa configuração como uma política e a **reaplica sempre** que
ela muda.

## Como funciona

```
Dispositivo detectado / alterado / verificação periódica
                    │
                    ▼
     Modo exclusivo permitido? ── não ──▶ ok
                    │ sim
                    ▼
        Desligar ──▶ Reler e confirmar ──▶ registrar no log

        (opcional) Formato padrão configurado?
                    │
                    ▼
     O driver suporta a taxa/profundidade pedida?
          │ sim                        │ não
          ▼                            ▼
   Aplicar e confirmar        Registrar no log e pular
```

- **Notificações em tempo real** (`IMMNotificationClient`): dispositivo conectado, removido,
  ativado/desativado, endpoint recriado ou propriedade alterada disparam uma verificação em
  segundos.
- **Verificação periódica** (padrão: a cada 60 s) como rede de segurança.
- **Nunca grava às cegas**: se o estado não pode ser lido, nada é alterado; toda escrita é
  relida para confirmar.
- **Sem loops**: se um driver rejeitar uma alteração, as verificações disparadas por
  notificação deixam aquele dispositivo em espera por 30 s; a verificação periódica tenta de novo.

## Funcionalidades

- Monitoramento contínuo dos dispositivos de áudio
- Remoção automática do modo exclusivo (obrigatória — é a função principal)
- Monitoramento de dispositivos de **reprodução** (playback)
- Monitoramento de dispositivos de **captura** (microfones, entradas de linha)
- Detecção de dispositivos novos, removidos, reconectados e recriados
- **Serviço do Windows** (`AudioWatchdog`), início automático, funciona sem usuário logado
- **Recuperação automática**: o serviço é reiniciado pelo Windows em caso de falha
- **Bandeja do sistema** com status, pausar/retomar, janela de status e encerrar
- **Pausa/retomada**: pausado, nenhum dispositivo é alterado; ao retomar é feita uma verificação
  completa
- **Padronização opcional de taxa de amostragem** (44100 Hz ou 48000 Hz)
- **Padronização opcional de profundidade de bits** (16, 24 ou 32 bits)
- Verificação dos formatos realmente suportados pelo driver antes de qualquer alteração
- **Logs** na pasta de instalação e eventos importantes no *Visualizador de Eventos*
- Instalador e desinstalador próprios (interface gráfica ou modo silencioso)

## Interface

### Bandeja do sistema

O ícone do Audio Watchdog fica na área de notificações. Ao passar o mouse, o tooltip mostra:

> O Audio Watchdog está em execução

Clique com o botão direito para abrir o menu:

```
Audio Watchdog
─────────────────────
Status: Em execução
─────────────────────
Pausar monitoramento      (vira "Retomar monitoramento" quando pausado)
─────────────────────
Abrir
─────────────────────
Encerrar
```

| Item | O que faz |
|---|---|
| **Pausar monitoramento** | Suspende as correções. O serviço continua instalado e em execução, eventos continuam chegando, mas nenhum dispositivo é alterado. O ícone fica cinza e o status mostra *Pausado* (também visível no `services.msc`). |
| **Retomar monitoramento** | Sai da pausa, relê a configuração e executa uma verificação completa, corrigindo o que for necessário. Também inicia o serviço se ele estiver parado. |
| **Abrir** | Janela de status: versão, estado, as duas funcionalidades (ativadas/desativadas e formato alvo), modo de execução e atalho para a pasta de logs. Clicar no ícone com o botão esquerdo faz o mesmo. |
| **Encerrar** | Pede confirmação, para o serviço de forma limpa (sem acionar a recuperação automática) e fecha o ícone. A proteção volta na próxima inicialização do Windows ou ao abrir o Audio Watchdog pelo Menu Iniciar. |

O ícone cinza indica que a proteção está pausada ou parada.

### Instalador

```
Instalação do Audio Watchdog

☑ Remover modo exclusivo dos dispositivos de áudio          (obrigatório)
☐ Padronizar taxa de amostragem e profundidade de bits dos dispositivos de áudio
      Taxa de amostragem:   [48000 Hz ▼]
      Profundidade de bits: [24-bit   ▼]

Pasta de instalação: [C:\Program Files\Audio Watchdog] [Procurar...]
☑ Iniciar o Audio Watchdog ao concluir
```

A primeira opção vem marcada e não pode ser desmarcada. Os campos de formato só ficam
habilitados quando a segunda opção está marcada.

## Instalação

1. Baixe `AudioWatchdog-Setup.exe` na página de [Releases](https://github.com/caio-kenai/Audio-Watchdog/releases).
2. Execute e aceite o pedido de administrador (UAC).
3. Escolha as opções e clique em **Instalar**.

O instalador:

- copia `AudioWatchdog.exe` e `Uninstall.exe` para a pasta escolhida e cria a subpasta `logs\`;
- grava a configuração em `C:\ProgramData\Audio Watchdog\config.ini` (preservando os demais
  ajustes em uma atualização);
- instala (ou atualiza) o serviço `AudioWatchdog`: início automático, conta LocalSystem,
  recuperação *Reiniciar o serviço* na 1ª, 2ª e demais falhas;
- registra a aplicação da bandeja para iniciar no logon de todos os usuários;
- cria a pasta **Audio Watchdog** no Menu Iniciar (aplicativo, logs e desinstalador) e a entrada
  em *Configurações → Aplicativos*;
- inicia o serviço e o ícone da bandeja.

### Instalação silenciosa

```bat
AudioWatchdog-Setup.exe /S                        :: mantém a configuração existente
AudioWatchdog-Setup.exe /S /format=48000:24       :: ativa a padronização (44100|48000 : 16|24|32)
AudioWatchdog-Setup.exe /S /noformat              :: desativa a padronização
AudioWatchdog-Setup.exe /S /dir="D:\Apps\Audio Watchdog" /notray
```

### Desinstalação

Use *Configurações → Aplicativos → Audio Watchdog → Desinstalar*, o atalho
**Desinstalar Audio Watchdog** no Menu Iniciar ou `Uninstall.exe` na pasta de instalação.

São removidos: serviço, executáveis, desinstalador, inicialização automática, atalhos, entrada
em *Aplicativos* e a origem do Log de Eventos.

**Logs e configuração são mantidos por padrão** (úteis para diagnóstico e para reinstalar com
as mesmas opções). Para removê-los, marque *"Remover também os logs e a configuração"* no
desinstalador, ou use `Uninstall.exe /S /removedata`. As configurações de áudio já aplicadas aos
dispositivos não são revertidas.

## Padronização de taxa de amostragem e profundidade de bits

Funcionalidade **opcional** e **independente** da proteção contra modo exclusivo:

```
Exclusive Mode Protection:     ENABLED
Audio Format Standardization:  ENABLED
Target:                        48000 Hz / 24-bit
```

Antes de alterar qualquer dispositivo, o Audio Watchdog pergunta **ao próprio driver** (pino de
streaming KS, via `IKsFormatSupport`) se o formato é suportado. Nada é forçado:

```
Requested:       48000 Hz / 24-bit
Device supports: 44100 Hz / 16-bit, 48000 Hz / 16-bit, 48000 Hz / 24-bit
Result:          48000 Hz / 24-bit aplicado
```

```
[Webcam 1 (NDI Webcam Audio)] format standardization skipped. Requested: 48000 Hz / 24-bit.
Device supports: 44100 Hz / 16-bit, 48000 Hz / 16-bit. Current: 44100 Hz / 16-bit.
Reason: requested format is not supported.
```

- Não existe *fallback* silencioso para outro formato: se não houver suporte, o dispositivo é
  pulado e o motivo vai para o log (uma vez por dispositivo, até ele mudar).
- 24 bits é aplicado como 24/24 ou 24-em-32, conforme o que o driver aceitar; 32 bits como
  inteiro ou ponto flutuante.
- Aplicado apenas a dispositivos ativos, na instalação/primeira execução, ao iniciar o serviço,
  quando um dispositivo é conectado ou recriado, em alterações relevantes, nas verificações
  periódicas e ao retomar o monitoramento.
- A mudança é aplicada pela mesma interface usada pelo painel de Som do Windows, então o motor
  de áudio passa a usar o novo formato imediatamente.
- Funciona com qualquer endpoint reconhecido pelo Windows (Realtek, USB, interfaces
  profissionais, HDMI/DisplayPort, Bluetooth, dispositivos virtuais). Endpoints cujo driver não
  informa os formatos suportados são pulados com o motivo no log.

## Configuração

`C:\ProgramData\Audio Watchdog\config.ini` (criado pelo instalador; somente administradores
podem alterá-lo):

```ini
[Features]
ExclusiveModeProtection=true   ; função principal
FormatStandardization=false    ; padronização de formato (opcional)
SampleRate=48000               ; 44100 | 48000
BitDepth=24                    ; 16 | 24 | 32

[Monitor]
Playback=true                  ; dispositivos de reprodução
Capture=true                   ; dispositivos de captura
CheckIntervalSeconds=60        ; verificação periódica (1..86400)

[Logging]
Enable=true
Level=INFO                     ; DEBUG | INFO | WARN | ERROR

[Behavior]
Enforce=true                   ; false = apenas relatar, nunca alterar
```

As alterações valem ao reiniciar o serviço, ao **retomar** o monitoramento ou com
`sc control AudioWatchdog paramchange`. Valores inválidos voltam ao padrão com aviso no log.

## Logs

| Arquivo | Conteúdo |
|---|---|
| `<pasta de instalação>\logs\audiowatchdog.log` | Serviço: início/parada, dispositivos adicionados/removidos, correções, formatos, erros |
| `<pasta de instalação>\logs\tray.log` | Aplicação da bandeja: pausar, retomar, encerrar |
| `<pasta de instalação>\logs\setup.log` | Instalações, atualizações e desinstalações |

Rotação automática a cada 5 MB (5 arquivos antigos). Correções e falhas graves também vão para o
**Visualizador de Eventos → Logs do Windows → Aplicativo**, origem *Audio Watchdog*. A pasta de
logs pode ser aberta pela janela **Abrir** ou pelo atalho no Menu Iniciar.

## Linha de comando

`AudioWatchdog.exe` sem argumentos abre a aplicação da bandeja. Comandos:

| Comando | Descrição |
|---|---|
| `status` | Estado do serviço, PID, controles aceitos, tipo de início |
| `pause` / `resume` | Pausa / retoma o monitoramento (sem precisar de administrador) |
| `start` / `stop` / `restart` | Ciclo de vida do serviço (administrador) |
| `install` / `uninstall` | Registra / remove apenas o serviço (administrador) |
| `scan` | Executa uma verificação agora, neste processo |
| `devices` | Lista os endpoints com estado do modo exclusivo, formato atual e formatos suportados |
| `diagnose [--probe-exclusive]` | Autodiagnóstico: SO, endpoints, leitura/escrita, sonda WASAPI |
| `--foreground` | Executa o watchdog no console (depuração) |
| `version` / `help` | Versão / ajuda |

## Arquitetura

```
Windows
 │
 ├── Serviço "AudioWatchdog"  (AudioWatchdog.exe --service, LocalSystem, sessão 0)
 │     │
 │     ├── WatchdogEngine ........ estado RUNNING / PAUSED / STOPPING / STOPPED,
 │     │                            verificações completas, periódicas e por evento
 │     ├── AudioDeviceWatcher .... IMMNotificationClient (adicionado, removido, estado, propriedade)
 │     ├── CoreAudioDeviceLister . enumeração MMDevice (reprodução + captura)
 │     ├── ExclusiveModeManager .. política do modo exclusivo ──▶ ExclusiveModeStore (IPropertyStore)
 │     ├── FormatManager ......... política de formato ──▶ AudioFormatStore
 │     │                            (IKsFormatSupport para suporte, IPolicyConfig para aplicar)
 │     ├── Config ................ config.ini em ProgramData
 │     └── Logger / EventLog ..... logs\audiowatchdog.log + Log de Eventos
 │
 └── Aplicação da bandeja  (AudioWatchdog.exe, uma por sessão de usuário)
       │
       ├── Status ................ QueryServiceStatus (atualizado a cada 2 s)
       ├── Pausar / Retomar ...... SERVICE_CONTROL_PAUSE / SERVICE_CONTROL_CONTINUE
       ├── Abrir ................. janela de status
       └── Encerrar .............. parada limpa do serviço (código 0 → sem reinício automático)
```

- **Um único executável**: bandeja (padrão), serviço (`--service`) e CLI.
- A bandeja **não precisa de administrador**: o instalador concede aos usuários interativos
  apenas os direitos de iniciar, parar e pausar/retomar *este* serviço.
- **Modo portátil**: se o serviço não estiver instalado, a bandeja executa o monitoramento no
  próprio processo (alterar dispositivos exige executar como administrador).
- A camada de áudio é baseada em interfaces (`IAudioDeviceLister`, `IExclusiveModeStore`,
  `IAudioFormatStore`), então a lógica é testada com *mocks* em memória.

### Estrutura do repositório

```
src/
  main.cpp            ponto de entrada (bandeja / serviço / CLI)
  app/                aplicação da bandeja (Shell_NotifyIcon, menu, janela de status)
  service/            serviço do Windows (SCM, pausa/continuação, recarga de configuração)
  core/               WatchdogEngine (estados, verificações, debounce, backoff)
  audio/              enumeração, watcher, modo exclusivo, formatos
  config/             config.ini
  logging/            logger com rotação, Log de Eventos
  cli/                comandos e diagnóstico
  util/               RAII de COM, strings, caminhos/ACLs, SCM
installer/            instalador/desinstalador Win32 (payload RCDATA)
resources/            ícones, recursos de versão, manifesto
assets/               logo (original, PNG para documentação)
tests/                testes unitários (44) com mocks
scripts/build.ps1     build + testes + dist\
```

## Como o modo exclusivo é controlado

- A opção *"Permitir que aplicativos assumam o controle exclusivo"* é a propriedade
  `{B3F8FA53-0004-438E-9003-51A46E139BFC},3` da *property store* de cada endpoint
  (`,4` = *"dar prioridade aos aplicativos no modo exclusivo"*), `VT_UI4`, `0` = bloqueado.
- A gravação usa a API pública `IMMDevice::OpenPropertyStore(STGM_READWRITE)` →
  `IPropertyStore::SetValue` → `Commit()`, seguida de releitura.
- Propriedade ausente significa o padrão do Windows (**permitido**) e é corrigida.
- Validado por comportamento: com o valor `0`, `IAudioClient::Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE)`
  retorna `AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED (0x8889000E)`.

## Tecnologias utilizadas

- **C++20** (MSVC, Visual Studio 2022 Build Tools)
- **Win32 API** (janelas, menus, diálogos, registro, ACLs, processos)
- **Windows Core Audio API**: **MMDevice API** (`IMMDeviceEnumerator`, `IMMDevice`,
  `IMMNotificationClient`, `IPropertyStore`), **WASAPI** (`IAudioClient`, usado no diagnóstico),
  **DeviceTopology API** (`IDeviceTopology`, `IConnector`, `IPart`, `IKsFormatSupport`)
- **IPolicyConfig** (interface COM não documentada do painel de Som) para aplicar o formato padrão
- **COM** (ponteiros RAII próprios, sem WIL/ATL)
- **Windows Service API** (SCM, pausa/continuação, ações de recuperação, DACL do serviço)
- **Shell API** (`Shell_NotifyIcon`, atalhos `IShellLink`, `IFileOpenDialog`, `TaskDialog`)
- **Windows Event Log**
- **CMake** (≥ 3.20) e **Ninja**; recursos com **rc.exe**
- Python + Pillow apenas para gerar os ícones a partir da logo (não é necessário para compilar)

Sem dependências externas em tempo de execução.

## Compilação

Requisitos: Windows 10/11 x64, Visual Studio 2022 (workload *Desktop development with C++*),
CMake ≥ 3.20.

```bat
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release
```

Ou, com Ninja (Prompt de Comando do Desenvolvedor x64):

```bat
cmake -G Ninja -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`scripts\build.ps1` localiza o Visual Studio, compila, roda os testes e copia o instalador para
`dist\`.

Saídas em `build\bin\`: `AudioWatchdog.exe`, `AudioWatchdog-Setup.exe` (instalador com o
executável embutido) e `audiowatchdog_tests.exe`.

> Binários nativos sem assinatura digital que usam o SCM podem ser marcados por heurísticas do
> Windows Defender/SmartScreen. Assine os executáveis para distribuição em larga escala.

## Compatibilidade

Windows 10 e Windows 11, x64. O serviço funciona sem usuário conectado; a bandeja roda em cada
sessão interativa (inclusive Área de Trabalho Remota).

## Licença

AGPL-3.0 — veja [`LICENSE`](LICENSE).
