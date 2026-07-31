function Format-DownloadSize {
    param([long]$Bytes)

    if ($Bytes -ge 1GB) {
        return ('{0:N2} GiB' -f ($Bytes / 1GB))
    }
    if ($Bytes -ge 1MB) {
        return ('{0:N1} MiB' -f ($Bytes / 1MB))
    }
    if ($Bytes -ge 1KB) {
        return ('{0:N1} KiB' -f ($Bytes / 1KB))
    }
    return "$Bytes bytes"
}

function Invoke-ResilientDownload {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)]
        [string]$Uri,

        [Parameter(Mandatory = $true)]
        [string]$Destination,

        [Parameter(Mandatory = $true)]
        [string]$DisplayName,

        [long]$ExpectedSize = 0
    )

    $curl = Get-Command curl.exe -ErrorAction Stop
    $destinationPath = [IO.Path]::GetFullPath($Destination)
    $destinationDirectory = Split-Path -Parent $destinationPath
    New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null

    $existingSize = 0
    if (Test-Path -LiteralPath $destinationPath -PathType Leaf) {
        $existingSize = (Get-Item -LiteralPath $destinationPath).Length
        if ($ExpectedSize -gt 0 -and $existingSize -eq $ExpectedSize) {
            Write-Host "[download] Reusing complete $DisplayName ($(Format-DownloadSize $existingSize))."
            return
        }
        if ($ExpectedSize -gt 0 -and $existingSize -gt $ExpectedSize) {
            Write-Host "[download] Discarding oversized partial file for $DisplayName."
            Remove-Item -LiteralPath $destinationPath -Force
            $existingSize = 0
        }
    }

    $sizeDescription = if ($ExpectedSize -gt 0) {
        Format-DownloadSize $ExpectedSize
    } else {
        'unknown size'
    }
    if ($existingSize -gt 0) {
        Write-Host "[download] Resuming $DisplayName at $(Format-DownloadSize $existingSize) of $sizeDescription."
    } else {
        Write-Host "[download] Starting $DisplayName ($sizeDescription)."
    }

    $curlArguments = @(
        '--fail'
        '--location'
        '--show-error'
        '--progress-bar'
        '--retry', '5'
        '--retry-all-errors'
        '--retry-delay', '5'
        '--retry-max-time', '1800'
        '--connect-timeout', '30'
        '--max-time', '1800'
        '--speed-limit', '1024'
        '--speed-time', '120'
    )
    if ($existingSize -gt 0) {
        $curlArguments += @('--continue-at', '-')
    }
    $curlArguments += @('--output', $destinationPath, $Uri)

    & $curl.Source @curlArguments
    $curlExitCode = $LASTEXITCODE
    if ($curlExitCode -ne 0) {
        throw "Download failed for $DisplayName with curl exit code $curlExitCode"
    }
    if (-not (Test-Path -LiteralPath $destinationPath -PathType Leaf)) {
        throw "Download for $DisplayName did not create $destinationPath"
    }

    $downloadedSize = (Get-Item -LiteralPath $destinationPath).Length
    if ($ExpectedSize -gt 0 -and $downloadedSize -ne $ExpectedSize) {
        throw "Download size mismatch for $DisplayName. Expected $ExpectedSize bytes, got $downloadedSize"
    }
    Write-Host "[download] Completed $DisplayName ($(Format-DownloadSize $downloadedSize))."
}
