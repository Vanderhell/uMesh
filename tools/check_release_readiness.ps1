param(
    [string]$RequestedTag = 'v1.5.0',
    [string]$ExpectedPackageVersion = '1.4.0'
)

$ErrorActionPreference = 'Stop'

function Fail([string]$Message) {
    throw $Message
}

function Get-FileText([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        Fail "missing required file: $Path"
    }
    return Get-Content -LiteralPath $Path -Raw
}

function Get-DefinedVersion([string]$Path) {
    $text = Get-FileText $Path
    $match = [regex]::Match($text, '(?m)^\s*#define\s+UMESH_VERSION_MAJOR\s+(\d+)\s*$.*?^\s*#define\s+UMESH_VERSION_MINOR\s+(\d+)\s*$.*?^\s*#define\s+UMESH_VERSION_PATCH\s+(\d+)\s*$', [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $match.Success) {
        Fail "could not parse UMESH_VERSION_* from $Path"
    }
    return ('{0}.{1}.{2}' -f $match.Groups[1].Value, $match.Groups[2].Value, $match.Groups[3].Value)
}

function Get-LibraryVersion([string]$Path) {
    $text = Get-FileText $Path
    $match = [regex]::Match($text, '(?m)^version=(\d+\.\d+\.\d+)\s*$')
    if (-not $match.Success) {
        Fail "could not parse version= from $Path"
    }
    return $match.Groups[1].Value
}

function Assert-Contains([string]$Text, [string]$Needle, [string]$Label) {
    if ($Text -notlike "*$Needle*") {
        Fail "$Label is missing required text: $Needle"
    }
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $repoRoot

$dirty = @(git status --short)
if ($dirty.Count -gt 0) {
    Fail "worktree must be clean before release readiness checks"
}

$tags = @(git tag --list --sort=version:refname)
$latestTag = if ($tags.Count -gt 0) { $tags[-1] } else { '' }
Write-Host "latest tag: $latestTag"

if ($tags -contains $RequestedTag) {
    Fail "requested release tag already exists: $RequestedTag"
}

if ($latestTag -and ($latestTag.TrimStart('v') -as [version]) -gt ($RequestedTag.TrimStart('v') -as [version])) {
    Fail "latest tag $latestTag is newer than requested tag $RequestedTag"
}

$defsVersion = Get-DefinedVersion (Join-Path $repoRoot 'src/common/defs.h')
$docVersion = Get-DefinedVersion (Join-Path $repoRoot 'docs/IMPLEMENTATION.md')
$pkgVersion = Get-LibraryVersion (Join-Path $repoRoot 'library.properties')

if ($defsVersion -ne $ExpectedPackageVersion) {
    Fail "src/common/defs.h version mismatch: expected $ExpectedPackageVersion, got $defsVersion"
}

if ($docVersion -ne $ExpectedPackageVersion) {
    Fail "docs/IMPLEMENTATION.md version mismatch: expected $ExpectedPackageVersion, got $docVersion"
}

if ($pkgVersion -ne $ExpectedPackageVersion) {
    Fail "library.properties version mismatch: expected $ExpectedPackageVersion, got $pkgVersion"
}

$readme = Get-FileText (Join-Path $repoRoot 'README.md')
Assert-Contains $readme 'Next project release target' 'README.md'
Assert-Contains $readme $RequestedTag 'README.md'
Assert-Contains $readme 'Wire protocol version' 'README.md'

$verification = Get-FileText (Join-Path $repoRoot 'VERIFICATION.md')
Assert-Contains $verification 'build-debug' 'VERIFICATION.md'
Assert-Contains $verification 'build-release' 'VERIFICATION.md'
Assert-Contains $verification '100% tests passed' 'VERIFICATION.md'

Write-Host "package version: $pkgVersion"
Write-Host "wire version: defined separately in include/umesh.h"
Write-Host "release readiness checks passed for requested tag $RequestedTag"
