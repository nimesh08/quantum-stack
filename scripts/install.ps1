# scripts/install.ps1
#
# One-line install for the Heisenberg CLI binaries on Windows x86_64.
# Detects host, downloads the release tarball from the docs site,
# verifies provenance via `gh attestation verify` (with a SHA256
# fallback), and installs into %LOCALAPPDATA%\heisenberg\.
#
# Usage:
#   iwr -useb https://nimesh08.github.io/quantum-stack/install.ps1 | iex
#
# Override version:
#   $env:HEISENBERG_VERSION = 'v0.1.0'
#   iwr -useb .../install.ps1 | iex

$ErrorActionPreference = 'Stop'

$Version  = if ($env:HEISENBERG_VERSION) { $env:HEISENBERG_VERSION } else { 'latest' }
$Verify   = -not ($env:HEISENBERG_NOVERIFY -eq '1')
$Prefix   = if ($env:HEISENBERG_PREFIX) { $env:HEISENBERG_PREFIX } else { "$env:LOCALAPPDATA\heisenberg" }
$DocsBase = 'https://nimesh08.github.io/quantum-stack/downloads'
$GhRepo   = 'nimesh08/quantum-stack'

Write-Host "host triple: windows-x86_64"
Write-Host "version:     $Version"
Write-Host "install:     $Prefix"

# Resolve 'latest' via GitHub API.
if ($Version -eq 'latest') {
    try {
        $rel = Invoke-RestMethod "https://api.github.com/repos/$GhRepo/releases/latest"
        $Version = $rel.tag_name
    } catch {
        Write-Error "could not resolve 'latest' version: $_"
        exit 1
    }
    Write-Host "resolved:    $Version"
}

$Artifact = "heisenberg-cli-$Version-windows-x86_64.tar.gz"
$Url      = "$DocsBase/$Artifact"
$Tmp      = New-TemporaryFile
$TmpDir   = Split-Path $Tmp -Parent

Write-Host "downloading $Url"
Invoke-WebRequest -UseBasicParsing -Uri $Url      -OutFile (Join-Path $TmpDir $Artifact)
Invoke-WebRequest -UseBasicParsing -Uri "$Url.sha256" `
    -OutFile (Join-Path $TmpDir "$Artifact.sha256") -ErrorAction SilentlyContinue

if ($Verify) {
    if (Get-Command gh -ErrorAction SilentlyContinue) {
        Write-Host "verifying provenance via gh attestation verify..."
        & gh attestation verify (Join-Path $TmpDir $Artifact) --repo $GhRepo
        if ($LASTEXITCODE -ne 0) {
            Write-Error "provenance verification FAILED. Aborting."
            exit 1
        }
        Write-Host "  ok"
    } else {
        Write-Host "'gh' not found; falling back to SHA256 check"
        $shaFile = Join-Path $TmpDir "$Artifact.sha256"
        if (Test-Path $shaFile) {
            $expected = (Get-Content $shaFile).Split(' ')[0].Trim()
            $actual   = (Get-FileHash -Algorithm SHA256 (Join-Path $TmpDir $Artifact)).Hash.ToLower()
            if ($expected -ne $actual) {
                Write-Error "SHA256 mismatch. Aborting."
                exit 1
            }
            Write-Host "  ok"
        } else {
            Write-Error "no checksum file and no 'gh' to verify. Re-run with `$env:HEISENBERG_NOVERIFY = '1' to skip (NOT recommended)."
            exit 1
        }
    }
}

# Extract using tar (Windows 10+ ships tar.exe).
New-Item -ItemType Directory -Path $Prefix -Force | Out-Null
& tar -xzf (Join-Path $TmpDir $Artifact) -C $Prefix --strip-components=1

# PATH hint.
$BinDir = Join-Path $Prefix 'bin'
$path = [Environment]::GetEnvironmentVariable('PATH','User')
if ($path -notlike "*$BinDir*") {
    Write-Host ""
    Write-Host "Add $BinDir to your PATH (User):"
    Write-Host "  setx PATH `"$BinDir;%PATH%`""
}

Write-Host ""
Write-Host "Installed Heisenberg CLI $Version (windows-x86_64)."
Write-Host "Try:  & '$BinDir\photonc.exe' version"
