# ======================================================================================
# SCRIPT DE AUTOMACAO DE GOVERNANCA GIT (Hooks Locais + Protecao de Branch no GitHub)
# ======================================================================================
# O que este script faz:
#   1. Ativa os Git Hooks locais (.githooks/pre-commit e pre-push) para impedir commits
#      e pushes diretos nas branches 'main' e 'develop'.
#   2. Caso o GitHub CLI (gh) esteja autenticado, configura via API a protecao oficial
#      das branches 'develop' e 'main' no GitHub exigindo:
#      - Pull Request obrigatorio a partir de uma branch separada
#      - Status check 'Automated Code Review & Validation' aprovado
#      - Pelo menos 1 aprovacao (Approve) de outro integrante do grupo
# ======================================================================================

$ErrorActionPreference = "Stop"

Write-Host "1. Ativando Git Hooks locais (.githooks)..." -ForegroundColor Cyan
git config core.hooksPath .githooks
Write-Host "   [OK] Hooks locais ativados! Commits diretos em 'main' e 'develop' estao bloqueados." -ForegroundColor Green

if (Get-Command gh -ErrorAction SilentlyContinue) {
    Write-Host "`n2. Configurando Branch Protection no GitHub (develop e main)..." -ForegroundColor Cyan
    $protectionPayload = @'
{
  "required_status_checks": {
    "strict": true,
    "contexts": ["Automated Code Review & Validation"]
  },
  "enforce_admins": true,
  "required_pull_request_reviews": {
    "dismiss_stale_reviews": true,
    "require_code_owner_reviews": false,
    "required_approving_review_count": 1,
    "require_last_push_approval": true
  },
  "restrictions": null,
  "allow_force_pushes": false,
  "allow_deletions": false
}
'@

    foreach ($branch in @("develop", "main")) {
        try {
            $protectionPayload | gh api -X PUT "repos/biancakimi/projeto-IOT/branches/$branch/protection" `
                -H "Accept: application/vnd.github+json" --input - | Out-Null
            Write-Host "   [OK] Protecao de PR + Code Review + 1 Aprovacao aplicada em '$branch'!" -ForegroundColor Green
        } catch {
            Write-Host "   [AVISO] Nao foi possivel aplicar via API na branch '$branch' (verifique permissoes de admin no repositorio)." -ForegroundColor Yellow
        }
    }
} else {
    Write-Host "`n[INFO] GitHub CLI (gh) nao encontrado localmente. O workflow '.github/workflows/enforce-branch-protection.yml' cuidara da protecao no GitHub." -ForegroundColor Yellow
}
