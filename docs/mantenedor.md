# Guia do mantenedor

## Configuração única no GitHub

1. **Repositório público**, com a licença MIT que já está no repo.
2. **Ruleset da branch `main`** (Settings → Rules → Rulesets → New branch ruleset):
   - Target: `main`.
   - Require a pull request before merging, com 1 aprovação e *Require review from Code Owners*.
   - Require status checks to pass: `check` (CI). Block force pushes. Restrict deletions.
   - Bypass list: vazia, ou só você, se quiser poder ajustar algo em emergência.
3. **Ruleset de tags** (target: tags, padrão `v*`): *Restrict creations*, com bypass só para você.
   Assim só você dispara releases.
4. **Environment `release`** (Settings → Environments): *Required reviewers* = você.
   O job que publica a release fica parado até você aprovar.
5. Settings → Actions → General: *Workflow permissions* = Read repository contents;
   *Fork pull request workflows* = require approval for first-time contributors.
6. Troque `@OWNER_GITHUB_USER` em `.github/CODEOWNERS` pelo seu usuário.

## Fazer uma release

1. Faça o merge dos PRs na `main`.
2. `git switch main`, `git pull`, `git tag v1.0.0`, `git push origin v1.0.0`.
3. Na aba Actions, revise o build e clique em **Review deployments → Approve**.
4. A release aparece com o Kit de Build e o `SHA256SUMS.txt`.
