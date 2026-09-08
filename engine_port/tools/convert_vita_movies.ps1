param(
    [string]$DataRoot = (Join-Path $PSScriptRoot '..\vita_kit\ux0_data\farcry'),
    [string]$Ffmpeg = '',
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$resolvedRoot = (Resolve-Path -LiteralPath $DataRoot).Path

if (-not $Ffmpeg) {
    $command = Get-Command ffmpeg.exe -ErrorAction SilentlyContinue
    if ($command) {
        $Ffmpeg = $command.Source
    } else {
        $wingetRoot = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages\Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe'
        $candidate = Get-ChildItem -LiteralPath $wingetRoot -Recurse -File -Filter ffmpeg.exe -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($candidate) {
            $Ffmpeg = $candidate.FullName
        }
    }
}

if (-not $Ffmpeg -or -not (Test-Path -LiteralPath $Ffmpeg -PathType Leaf)) {
    throw 'A host FFmpeg executable is required to convert the user-supplied Bink movies.'
}

$movies = @(Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Filter *.bik |
    Sort-Object FullName)
if (-not $movies.Count) {
    throw "No Bink movies were found below $resolvedRoot"
}

foreach ($movie in $movies) {
    $output = [IO.Path]::ChangeExtension($movie.FullName, '.mp4')
    if (-not $Force -and (Test-Path -LiteralPath $output) -and
        (Get-Item -LiteralPath $output).LastWriteTimeUtc -ge $movie.LastWriteTimeUtc) {
        Write-Host "up-to-date: $output"
        continue
    }

    $temporary = "$output.tmp"
    if (Test-Path -LiteralPath $temporary) {
        Remove-Item -LiteralPath $temporary -Force
    }

    Write-Host "converting: $($movie.FullName)"
    & $Ffmpeg -hide_banner -loglevel warning -stats -nostdin -y `
        -i $movie.FullName `
        -map '0:v:0' -map '0:a:0?' `
        -vf "scale=720:408:force_original_aspect_ratio=decrease:force_divisible_by=2,pad=720:408:(ow-iw)/2:(oh-ih)/2:black" `
        -c:v libx264 -preset slow -crf 22 -profile:v baseline -level:v 3.1 `
        -pix_fmt yuv420p -maxrate 3000k -bufsize 6000k `
        -c:a aac -b:a 128k -ar 48000 -ac 2 `
        -movflags +faststart -f mp4 $temporary
    if ($LASTEXITCODE -ne 0) {
        if (Test-Path -LiteralPath $temporary) {
            Remove-Item -LiteralPath $temporary -Force
        }
        throw "FFmpeg failed for $($movie.FullName) (exit $LASTEXITCODE)"
    }
    Move-Item -LiteralPath $temporary -Destination $output -Force
}

$outputs = @(Get-ChildItem -LiteralPath $resolvedRoot -Recurse -File -Filter *.mp4)
$totalBytes = ($outputs | Measure-Object -Property Length -Sum).Sum
Write-Host ("ready: {0} movies, {1:N1} MiB" -f $outputs.Count, ($totalBytes / 1MB))
