param(
    [Parameter(Mandatory = $true)]
    [string]$StageDir,
    [Parameter(Mandatory = $true)]
    [string]$ArtifactName,
    [Parameter(Mandatory = $true)]
    [string]$DistDir
)

$ErrorActionPreference = "Stop"

$binDir = Join-Path $StageDir "bin"
$skewerRender = Join-Path $binDir "skewer-render.exe"
$loom = Join-Path $binDir "loom.exe"

if (!(Test-Path $skewerRender)) {
    throw "Missing $skewerRender"
}

if (!(Test-Path $loom)) {
    throw "Missing $loom"
}

New-Item -ItemType Directory -Force -Path $DistDir | Out-Null

@"
Skewer portable command-line bundle

Run from this extracted directory:

  .\bin\skewer-render.exe --help
  .\bin\loom.exe --help

The bin directory contains the runtime DLLs needed by these binaries.
"@ | Set-Content -Path (Join-Path $StageDir "README.txt") -Encoding UTF8

$systemDlls = @(
    "advapi32.dll",
    "api-ms-win-core-console-l1-1-0.dll",
    "api-ms-win-core-console-l1-2-0.dll",
    "api-ms-win-core-datetime-l1-1-0.dll",
    "api-ms-win-core-debug-l1-1-0.dll",
    "api-ms-win-core-errorhandling-l1-1-0.dll",
    "api-ms-win-core-fibers-l1-1-0.dll",
    "api-ms-win-core-file-l1-1-0.dll",
    "api-ms-win-core-file-l1-2-0.dll",
    "api-ms-win-core-file-l2-1-0.dll",
    "api-ms-win-core-handle-l1-1-0.dll",
    "api-ms-win-core-heap-l1-1-0.dll",
    "api-ms-win-core-interlocked-l1-1-0.dll",
    "api-ms-win-core-libraryloader-l1-1-0.dll",
    "api-ms-win-core-localization-l1-2-0.dll",
    "api-ms-win-core-memory-l1-1-0.dll",
    "api-ms-win-core-namedpipe-l1-1-0.dll",
    "api-ms-win-core-processenvironment-l1-1-0.dll",
    "api-ms-win-core-processthreads-l1-1-0.dll",
    "api-ms-win-core-processthreads-l1-1-1.dll",
    "api-ms-win-core-profile-l1-1-0.dll",
    "api-ms-win-core-rtlsupport-l1-1-0.dll",
    "api-ms-win-core-string-l1-1-0.dll",
    "api-ms-win-core-synch-l1-1-0.dll",
    "api-ms-win-core-synch-l1-2-0.dll",
    "api-ms-win-core-sysinfo-l1-1-0.dll",
    "api-ms-win-core-timezone-l1-1-0.dll",
    "api-ms-win-core-util-l1-1-0.dll",
    "api-ms-win-crt-conio-l1-1-0.dll",
    "api-ms-win-crt-convert-l1-1-0.dll",
    "api-ms-win-crt-environment-l1-1-0.dll",
    "api-ms-win-crt-filesystem-l1-1-0.dll",
    "api-ms-win-crt-heap-l1-1-0.dll",
    "api-ms-win-crt-locale-l1-1-0.dll",
    "api-ms-win-crt-math-l1-1-0.dll",
    "api-ms-win-crt-multibyte-l1-1-0.dll",
    "api-ms-win-crt-private-l1-1-0.dll",
    "api-ms-win-crt-process-l1-1-0.dll",
    "api-ms-win-crt-runtime-l1-1-0.dll",
    "api-ms-win-crt-stdio-l1-1-0.dll",
    "api-ms-win-crt-string-l1-1-0.dll",
    "api-ms-win-crt-time-l1-1-0.dll",
    "api-ms-win-crt-utility-l1-1-0.dll",
    "bcrypt.dll",
    "crypt32.dll",
    "dnsapi.dll",
    "gdi32.dll",
    "imm32.dll",
    "iphlpapi.dll",
    "kernel32.dll",
    "ncrypt.dll",
    "normaliz.dll",
    "ole32.dll",
    "oleaut32.dll",
    "rpcrt4.dll",
    "secur32.dll",
    "shell32.dll",
    "shlwapi.dll",
    "ucrtbase.dll",
    "user32.dll",
    "version.dll",
    "winmm.dll",
    "ws2_32.dll"
)

$systemDllSet = @{}
foreach ($name in $systemDlls) {
    $systemDllSet[$name.ToLowerInvariant()] = $true
}

function Get-Dependents {
    param([string]$Path)

    $lines = & dumpbin /DEPENDENTS $Path 2>$null
    foreach ($line in $lines) {
        if ($line -match "^\s*([A-Za-z0-9_.+-]+\.dll)\s*$") {
            $matches[1]
        }
    }
}

function Resolve-Dll {
    param(
        [string]$Name,
        [string[]]$SearchDirs
    )

    foreach ($dir in $SearchDirs) {
        if ([string]::IsNullOrWhiteSpace($dir) -or !(Test-Path $dir)) {
            continue
        }

        $candidate = Join-Path $dir $Name
        if (Test-Path $candidate) {
            return (Resolve-Path $candidate).Path
        }
    }

    if ($env:VCToolsRedistDir -and (Test-Path $env:VCToolsRedistDir)) {
        $match = Get-ChildItem -Path $env:VCToolsRedistDir -Recurse -Filter $Name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($match) {
            return $match.FullName
        }
    }

    return $null
}

$searchDirs = @($binDir)

if ($env:VCPKG_ROOT) {
    $searchDirs += Join-Path $env:VCPKG_ROOT "installed\x64-windows\bin"
}

$searchDirs += ($env:PATH -split ";")

$queue = New-Object System.Collections.Queue
$seen = @{}

foreach ($exe in @($skewerRender, $loom)) {
    $queue.Enqueue($exe)
    $seen[(Resolve-Path $exe).Path.ToLowerInvariant()] = $true
}

while ($queue.Count -gt 0) {
    $current = [string]$queue.Dequeue()

    foreach ($dll in Get-Dependents -Path $current) {
        $dllKey = $dll.ToLowerInvariant()
        if ($systemDllSet.ContainsKey($dllKey) -or $dllKey.StartsWith("api-ms-") -or $dllKey.StartsWith("ext-ms-")) {
            continue
        }

        $target = Join-Path $binDir $dll
        if (Test-Path $target) {
            $resolvedTarget = (Resolve-Path $target).Path.ToLowerInvariant()
            if (!$seen.ContainsKey($resolvedTarget)) {
                $seen[$resolvedTarget] = $true
                $queue.Enqueue($target)
            }
            continue
        }

        $resolved = Resolve-Dll -Name $dll -SearchDirs $searchDirs
        if (!$resolved) {
            throw "Could not resolve runtime dependency $dll required by $current"
        }

        Copy-Item -Path $resolved -Destination $target -Force
        $resolvedTarget = (Resolve-Path $target).Path.ToLowerInvariant()
        if (!$seen.ContainsKey($resolvedTarget)) {
            $seen[$resolvedTarget] = $true
            $queue.Enqueue($target)
        }
    }
}

& $skewerRender --help | Out-Null
& $loom --help | Out-Null

$archive = Join-Path $DistDir "$ArtifactName.zip"
if (Test-Path $archive) {
    Remove-Item $archive -Force
}

Compress-Archive -Path $StageDir -DestinationPath $archive -Force
Get-FileHash -Algorithm SHA256 $archive | ForEach-Object {
    "$($_.Hash.ToLowerInvariant())  $(Split-Path -Leaf $archive)"
} | Set-Content -Path "$archive.sha256" -Encoding ASCII

Write-Host "Created $archive"
