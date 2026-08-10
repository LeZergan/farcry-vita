# Vita: real, reusable build+deploy for the ported engine (see
# engine_port/CMakeLists.txt and vita_sources.txt for what actually gets
# compiled/why). No build system existed for engine_port before this --
# only vita_bringup/CMakeLists.txt did, for an unrelated standalone triangle
# demo. This is the missing piece: one command, from source to a real
# eboot.bin deployed where Vita3K already expects it (matches the FCRY00002
# layout already proven working -- this script only replaces eboot.bin,
# never touches fcdata/languages/etc, which are the real, large, gitignored
# game assets already deployed there).
#
# Usage: pwsh -File engine_port/build_vita.ps1

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $PSScriptRoot "build"
$DeployDir = "$env:APPDATA\Vita3K\Vita3K\ux0\app\FCRY00002"

if (-not $env:VITASDK) {
	Write-Error "VITASDK environment variable is not set."
	exit 1
}

if (-not (Test-Path $BuildDir)) {
	New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

Push-Location $BuildDir
try {
	& cmake -G Ninja .. 2>&1 | Tee-Object -Variable configureOutput
	if ($LASTEXITCODE -ne 0) {
		Write-Error "cmake configure failed."
		exit 1
	}

	& ninja -j8 2>&1 | Tee-Object -Variable buildOutput
	if ($LASTEXITCODE -ne 0) {
		Write-Error "Build failed -- see output above."
		exit 1
	}
}
finally {
	Pop-Location
}

$SelfPath = Join-Path $BuildDir "farcry_vita.self"
if (-not (Test-Path $SelfPath)) {
	Write-Error "Build reported success but $SelfPath is missing."
	exit 1
}

if (-not (Test-Path $DeployDir)) {
	Write-Error "Deploy target $DeployDir doesn't exist -- expected the already-installed FCRY00002 app (real fcdata/etc, see engine_port/compat/README.md's project memory notes). Not creating it from scratch here."
	exit 1
}

Copy-Item $SelfPath (Join-Path $DeployDir "eboot.bin") -Force
Write-Host "Deployed $SelfPath -> $DeployDir\eboot.bin"
