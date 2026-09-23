# Changelog

## [1.1.0] - 2026-09-23

### Novidades
- Serviço do Windows como modo principal: início automático, conta LocalSystem, funciona sem
  usuário logado, recuperação *Reiniciar o serviço* na 1ª, 2ª e demais falhas (também em
  paradas com erro).
- Estado explícito `RUNNING / PAUSED / STOPPING / STOPPED`; pausa e continuação integradas ao
  SCM (visíveis no `services.msc`). Ao retomar, verificação completa.
- Nova aplicação da bandeja: tooltip "O Audio Watchdog está em execução", menu com status,
  Pausar/Retomar monitoramento, Abrir (janela de status) e Encerrar (parada limpa, sem reinício
  automático). Ícone cinza quando pausado/parado. Funciona sem administrador.
- Padronização opcional de taxa de amostragem (44100/48000 Hz) e profundidade de bits
  (16/24/32 bits), com verificação dos formatos suportados pelo driver antes de qualquer
  alteração e registro no log quando não há suporte.
- Novo instalador gráfico: opção obrigatória de remoção do modo exclusivo, opção de
  padronização de formato com seleção de taxa e profundidade, escolha da pasta, modo
  silencioso (`/S`, `/format=`, `/noformat`, `/dir=`, `/notray`), atualização in-place.
- Desinstalador (`Uninstall.exe`, Aplicativos e Menu Iniciar), com opção de remover logs e
  configuração; modo silencioso `/S [/removedata]`.
- Logs na pasta de instalação (`logs\`), com `audiowatchdog.log`, `tray.log` e `setup.log`;
  origem registrada no Log de Eventos.
- Comandos `pause`, `resume`; `devices` mostra formato atual e formatos suportados.
- Nova logo aplicada ao executável, ícone da bandeja, instalador e README.

### Correções
- Endpoints sem a propriedade de modo exclusivo (padrão do Windows = permitido) eram tratados
  como bloqueados e nunca corrigidos.
- A ACL da pasta de logs nunca era aplicada (direito SDDL `M` inválido).
- O menu de contexto da bandeja não abria de forma confiável (`NOTIFYICON_VERSION_4` entrega o
  evento em `LOWORD(lParam)`).
- `Behavior.ExclusiveModeDisabled` era lido mas ignorado.
- Comentários no fim da linha do `config.ini` invalidavam o valor.
- Alterações rejeitadas por um driver podiam gerar um ciclo notificação → escrita →
  notificação; agora há debounce e espera de 30 s por dispositivo.
- Ausência de endpoints era registrada como erro a cada verificação.
- `TerminateThread` removido do desligamento; threads encerram de forma limpa.
- Estado do serviço atualizado de forma thread-safe, com checkpoints nos estados pendentes.
- `IMMNotificationClient::Release` podia apagar um objeto alocado na pilha.
- Rotação de log tentava renomear a cada linha quando o arquivo estava em uso.
- Dependência desnecessária do serviço `Tcpip` removida.
- Testes não dependem mais de caminhos fixos da máquina de desenvolvimento.

## [1.0.0]
- Versão inicial: aplicação na bandeja, serviço opcional, CLI e instalador simples.
