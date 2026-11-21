# Guia do mantenedor

## Configuração única no GitHub

1. **Repositório público**, com a licença MIT que já está no repo.
2. **Ruleset da branch `main`** (Settings → Rules → Rulesets → New branch ruleset):
   - Target: `main`.
   - Require a pull request before merging, com 1 aprovação e *Require review from Code Owners*.
   - Require status checks to pass: `check` (CI). Block force pushes. Restrict deletions.
   - Bypass list: vazia, ou só você, se quiser poder ajustar algo em emergência.
3. **Ruleset de tags** (target: tags, padrão `v*`): *Restrict creations*, com bypass só para você.
4. Settings → Actions → General: *Workflow permissions* = Read repository contents;
   *Fork pull request workflows* = require approval for first-time contributors.
5. Troque `@OWNER_GITHUB_USER` em `.github/CODEOWNERS` pelo seu usuário.
6. Não dê permissão de *Write* a ninguém: colaboradores contribuem por fork + PR.
   Só quem tem Write cria releases, então as releases ficam só com você.

## O que vai para onde

| Onde | O quê |
| --- | --- |
| Discord | `Akuma-Original.zip`: cópia fiel da pasta do jogo extraída do CD |
| GitHub (repositório) | Só código, traduções e docs. O CI recusa EXE/DLL/ZIP |
| GitHub (Releases) | `Akuma-Patch.zip`, `SHA256SUMS.txt` |

## Gerar uma release

Pré-requisitos: o BIN/CUE, Zig, cnc-ddraw v7.1.0.0 e a arte do Slayers (veja o README).

```powershell
# 1. Cópia fiel (a mesma que vai para o Discord)
python extract_cd.py AKUMA.bin .work\orig

# 2. Build completo
.\build.ps1 -Image .\AKUMA.bin -CncDdraw .\.work\wrapper -Zig <caminho\zig.exe> -Output .\dist\Akuma-Portatil.zip

# 3. Patch: só o que difere da cópia fiel
.\make_overlay.ps1 -Original .\.work\orig -Built .\dist\Akuma-Portatil.zip -Output .\dist\Akuma-Patch.zip

# 4. Hash para publicar junto
(Get-FileHash .\dist\Akuma-Patch.zip).Hash.ToLower() + '  Akuma-Patch.zip' | Out-File .\dist\SHA256SUMS.txt -Encoding ascii
```

Depois, no GitHub: faça o merge dos PRs na `main`, atualize-a localmente,
crie a tag (`git tag v1.0.0`, `git push origin v1.0.0`), vá em
Releases → Draft a new release, escolha a tag, anexe `Akuma-Patch.zip` e
`SHA256SUMS.txt` e publique.

Antes de publicar, teste: extraia `Akuma-Original.zip` numa pasta limpa, extraia
o patch por cima e abra `Jogar.exe`.

Para a cópia fiel do Discord, compacte `.work\orig` numa pasta chamada `Akuma`.
