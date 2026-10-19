# ======================================================================================
# SCRIPT DE AUTOMAÇÃO DE GOVERNANÇA GIT (Hooks Locais + Proteção de Branch no GitHub)
# ======================================================================================
# O que este script faz:
#   1. Ativa os Git Hooks locais (.githooks/pre-commit e pre-push) para impedir commits
#      e pushes diretos nas branches 'main' e 'develop'.
#   2. Caso o GitHub CLI (gh) esteja autenticado, configura via API a proteção oficial
#      das branches 'develop' e 'main' no GitHub exigindo:
#      - Pull Request obrigatório a partir de uma branch separada
#      - Status check 'Automated Code Review & Validation' aprovado
#      - Pelo menos 1 aprovação (Approve) de outro integrante do grupo
# ======================================================================================

$ErrorActionPreference = "Stop"

Write-Host "1. Ativando Git Hooks locais (.githooks)..." -ForegroundColor Cyan
git config core.hooksPath .githooks
Write-Host "   ✅ Hooks locais ativados! Commits diretos em 'main' e 'develop' estão bloqueados." -ForegroundColor Green

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
            Write-Host "   ✅ Proteção de PR + Code Review + 1 Aprovação aplicada em '$branch'!" -ForegroundColor Green
        } catch {
            Write-Host "   ⚠️ Não foi possível aplicar via API na branch '$branch' (verifique permissões de admin no repositório ou se a branch já foi enviada ao remoto)." -ForegroundColor Yellow
        }
    }
} else {
    Write-Host "`nℹ️ GitHub CLI (gh) não encontrado localmente. O workflow '.github/workflows/enforce-branch-protection.yml' cuidará da proteção no GitHub." -ForegroundColor Yellow
}
