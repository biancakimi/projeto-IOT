# ======================================================================================
# SCRIPT DE AUTOMACAO DE GOVERNANCA GIT (Hooks Locais + Protecao de Branch no GitHub)
# ======================================================================================
# O que este script faz:
#   1. Ativa os Git Hooks locais (.githooks/pre-commit e pre-push) para impedir commits
#      e pushes diretos nas branches 'main' e 'develop'.
#   2. Usa a credencial do proprio Git (git credential fill) via API REST nativa do
#      PowerShell (sem precisar instalar o GitHub CLI 'gh') para ativar o Branch
#      Protection de 'develop' e 'main' no GitHub exigindo:
#      - Pull Request obrigatorio a partir de uma branch separada
#      - Status check 'Automated Code Review & Validation' aprovado
#      - Pelo menos 1 aprovacao (Approve) de outro integrante do grupo
# ======================================================================================

$ErrorActionPreference = "Stop"

Write-Host "1. Ativando Git Hooks locais (.githooks)..." -ForegroundColor Cyan
git config core.hooksPath .githooks
Write-Host "   [OK] Hooks locais ativados! Commits e pushes diretos em 'main' e 'develop' estao bloqueados." -ForegroundColor Green

Write-Host "`n2. Verificando permissao no GitHub para ativar Branch Protection (develop e main)..." -ForegroundColor Cyan

try {
    $credInput = "protocol=https`nhost=github.com`n`n"
    $credOutput = $credInput | git credential fill
    $tokenLine = ($credOutput -split "`n") | Where-Object { $_ -match "^password=" }
    $token = ($tokenLine -replace "^password=", "").Trim()

    if (-not $token) {
        throw "Token nao encontrado no git credential helper."
    }

    $headers = @{
        Authorization          = "Bearer $token"
        Accept                 = "application/vnd.github+json"
        "X-GitHub-Api-Version" = "2022-11-28"
    }

    $repoInfo = Invoke-RestMethod -Uri "https://api.github.com/repos/biancakimi/projeto-IOT" -Headers $headers
    $userInfo = Invoke-RestMethod -Uri "https://api.github.com/user" -Headers $headers

    Write-Host "   Usuario autenticado no Git: @$($userInfo.login) (Admin no repo: $($repoInfo.permissions.admin))" -ForegroundColor Gray

    if (-not $repoInfo.permissions.admin) {
        Write-Host "   [AVISO] O usuario @$($userInfo.login) tem permissao de escrita (Push), mas nao de Admin no repositorio 'biancakimi/projeto-IOT'." -ForegroundColor Yellow
        Write-Host "   -> Para ativar o bloqueio remoto no GitHub, peca para a dona do repo (@biancakimi) rodar este script './scripts/configure-branch-protection.ps1' ou conceder permissao Admin ao grupo em Settings > Collaborators." -ForegroundColor Yellow
        exit 0
    }

    $protectionBody = @{
        required_status_checks = @{
            strict   = $true
            contexts = @("Automated Code Review & Validation")
        }
        enforce_admins = $true
        required_pull_request_reviews = @{
            dismiss_stale_reviews           = $true
            require_code_owner_reviews      = $false
            required_approving_review_count = 1
            require_last_push_approval      = $true
        }
        restrictions       = $null
        allow_force_pushes = $false
        allow_deletions    = $false
    } | ConvertTo-Json -Depth 5

    foreach ($branch in @("develop", "main")) {
        Invoke-RestMethod -Method Put `
            -Uri "https://api.github.com/repos/biancakimi/projeto-IOT/branches/$branch/protection" `
            -Headers $headers `
            -Body $protectionBody `
            -ContentType "application/json" | Out-Null
        Write-Host "   [OK] Protecao de PR + Code Review + 1 Aprovacao ativada no GitHub para '$branch'!" -ForegroundColor Green
    }
} catch {
    Write-Host "   [AVISO] Nao foi possivel aplicar a protecao remota via API: $($_.Exception.Message)" -ForegroundColor Yellow
}
