# Modificando com IA

## Prompt inicial sugerido

Cole no seu assistente de código, na raiz do projeto:

```text
Este projeto gera um patch portátil para Akuma — Demon Spawn (Windows).
Leia README.md e docs/widescreen.md primeiro.
Regras: não crie nem versione arquivos do jogo (BIN, EXE, DLL, PCX, ZIPs);
mantenha o estilo do código existente; faça a menor mudança possível.
Tarefa: <descreva o comportamento desejado no jogo>.
Ao terminar, diga quais arquivos mudou, rode build.ps1 se possível e
explique como eu testo no jogo.
```

## Exemplos de tarefas

- "Adicione ao launcher uma opção para lembrar a última resolução."
  Arquivo: `Launcher.cs`.
- "Troque o atalho que mostra e esconde o minimapa de F9 para M."
  Arquivos: `AkumaHud.c` e a aba de referência do launcher.
- "Revise as traduções em português das falas do primeiro mapa."
  Arquivo: `dialogues.json`. Peça para preservar as chaves (hashes) e o formato.

## Cuidados

- O build depende da sua mídia e de ferramentas externas (Zig, cnc-ddraw).
  A IA normalmente não roda o build completo sem os caminhos certos:
  passe-os no prompt.
- Endereços em `patch_akuma.py` valem só para a revisão do executável conferida
  pelo build. Não deixe a IA "chutar" endereços: exija evidência.
- Imagens do jogo não podem ir para o repositório, nem como exemplo.
