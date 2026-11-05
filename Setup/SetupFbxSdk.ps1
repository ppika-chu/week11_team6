# Windows PowerShell 5.1에서 실행한다. 시스템 설치/영구 환경 변수 변경 없이 SDK를 준비한다.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version Latest

$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$SdkVersion = '2020.2'
$SdkRoot = Join-Path $RepoRoot "Source\ThirdParty\FBX\$SdkVersion"
$CacheRoot = Join-Path $RepoRoot 'Intermediate\Dependencies'
$InstallerName = 'fbx20202_fbxsdk_vs2017_win.exe'
$InstallerUrl = 'https://damassets.autodesk.net/content/dam/autodesk/www/adn/fbx/2020-2/fbx20202_fbxsdk_vs2017_win.exe'
$InstallerSha256 = '551A80E1FD88731416A935F68A8A03ECB04C2B89ABD895602099BCB4816583DA'
$RequiredFiles = @('include\fbxsdk.h', 'include\fbxsdk\fbxsdk_version.h')
foreach ($config in @('debug', 'release')) {
    foreach ($library in @('libfbxsdk-md.lib', 'libxml2-md.lib', 'zlib-md.lib')) {
        $RequiredFiles += "lib\vs2017\x64\$config\$library"
    }
}

function Test-Sdk([string] $Root) {
    foreach ($relative in $RequiredFiles) {
        $file = Join-Path $Root $relative
        if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { return $false }
        if ((Get-Item -LiteralPath $file).Length -eq 0) { return $false }
    }
    $versionHeader = [IO.File]::ReadAllText((Join-Path $Root 'include\fbxsdk\fbxsdk_version.h'))
    return ($versionHeader -match '(?m)^#define\s+FBXSDK_VERSION_MAJOR\s+2020\b' -and
            $versionHeader -match '(?m)^#define\s+FBXSDK_VERSION_MINOR\s+2\b')
}

function Get-Sha256([string] $Path) {
    # 별도 PowerShell 모듈 설치 상태와 무관하게 기본 .NET API로 검증한다.
    $stream = [IO.File]::OpenRead($Path)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($algorithm.ComputeHash($stream)).Replace('-', '') }
    finally { $algorithm.Dispose(); $stream.Dispose() }
}

function Assert-ProjectPath([string] $Path) {
    $full = [IO.Path]::GetFullPath($Path)
    $prefix = $RepoRoot.TrimEnd('\') + '\'
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Setup path is outside the project: $full"
    }
    # 다운로드/설치/삭제 대상이 junction을 통해 프로젝트 밖으로 향하지 않게 한다.
    $current = $full
    while ($current -and -not $current.Equals($RepoRoot, [StringComparison]::OrdinalIgnoreCase)) {
        if (Test-Path -LiteralPath $current) {
            if ((Get-Item -LiteralPath $current -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) {
                throw "Setup path must not contain a junction or symbolic link: $current"
            }
        }
        $current = [IO.Path]::GetDirectoryName($current)
    }
}

function Remove-SetupDirectory([string] $Path) {
    Assert-ProjectPath $Path
    $name = [IO.Path]::GetFileName($Path)
    if (-not ($name.StartsWith('.fbx-install-') -or $name.StartsWith('.fbx-package-'))) {
        throw "Refusing to delete an unrecognized setup directory: $Path"
    }
    if (Test-Path -LiteralPath $Path) {
        $links = Get-ChildItem -LiteralPath $Path -Recurse -Force |
            Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint } | Select-Object -First 1
        if ($links) { throw "Refusing to clean a setup directory containing links: $Path" }
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

$InstallStage = $null
$PackageStage = $null
$DownloadStage = $null
try {
    # 사용자가 명시한 외부 SDK 경로는 기존과 같이 허용한다.
    if (-not [string]::IsNullOrWhiteSpace($env:FBX_SDK_ROOT)) {
        if (-not (Test-Sdk $env:FBX_SDK_ROOT)) {
            throw "FBX_SDK_ROOT must contain FBX SDK $SdkVersion with VS2017 x64 Debug/Release MD libraries: $env:FBX_SDK_ROOT"
        }
        Write-Host "Using FBX SDK from FBX_SDK_ROOT: $env:FBX_SDK_ROOT"
        exit 0
    }

    Assert-ProjectPath $SdkRoot
    if (Test-Sdk $SdkRoot) {
        Write-Host "FBX SDK $SdkVersion is ready: $SdkRoot"
        exit 0
    }
    if (Test-Path -LiteralPath $SdkRoot) {
        throw "An incomplete or different SDK already exists at $SdkRoot. Move it aside before running setup again."
    }

    Assert-ProjectPath $CacheRoot
    New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
    $installer = Join-Path $CacheRoot $InstallerName
    $cached = (Test-Path -LiteralPath $installer -PathType Leaf) -and
        ((Get-Sha256 $installer) -eq $InstallerSha256)
    if (-not $cached) {
        Write-Host "Downloading Autodesk FBX SDK $SdkVersion (about 158 MiB)..."
        $DownloadStage = $installer + '.' + [Guid]::NewGuid().ToString('N') + '.download'
        Assert-ProjectPath $DownloadStage
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -UseBasicParsing -Uri $InstallerUrl -OutFile $DownloadStage -TimeoutSec 180
        if ((Get-Sha256 $DownloadStage) -ne $InstallerSha256) {
            throw 'FBX SDK download failed SHA256 verification. The existing SDK has not been changed.'
        }
        Move-Item -LiteralPath $DownloadStage -Destination $installer -Force
        $DownloadStage = $null
    }

    $token = [Guid]::NewGuid().ToString('N')
    $InstallStage = Join-Path $CacheRoot ('.fbx-install-' + $token)
    $sdkParent = [IO.Path]::GetDirectoryName($SdkRoot)
    $PackageStage = Join-Path $sdkParent ('.fbx-package-' + $token)
    Assert-ProjectPath $InstallStage
    Assert-ProjectPath $PackageStage

    Write-Host "Preparing FBX SDK $SdkVersion without an installer window..."
    # Autodesk의 NSIS 설치기는 /D를 마지막 인수로 받고 경로에 공백이 있어도 따옴표 없이 처리한다.
    $process = Start-Process -FilePath $installer -ArgumentList ('/S /D=' + $InstallStage) -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(180000)) {
        Stop-Process -Id $process.Id -Force
        throw 'FBX SDK extraction timed out.'
    }
    $process.Refresh()
    if ($process.ExitCode -ne 0) { throw "FBX SDK installer failed with exit code $($process.ExitCode)." }
    if (-not (Test-Sdk $InstallStage)) { throw 'The downloaded SDK does not contain the expected headers and libraries.' }

    # 전체 SDK의 x86/MT/DLL/예제 대신 현재 빌드에 필요한 헤더와 x64 MD 라이브러리만 배치한다.
    New-Item -ItemType Directory -Force -Path $PackageStage | Out-Null
    Copy-Item -LiteralPath (Join-Path $InstallStage 'include') -Destination $PackageStage -Recurse
    foreach ($relative in $RequiredFiles | Where-Object { $_.StartsWith('lib\') }) {
        $destination = Join-Path $PackageStage $relative
        New-Item -ItemType Directory -Force -Path ([IO.Path]::GetDirectoryName($destination)) | Out-Null
        Copy-Item -LiteralPath (Join-Path $InstallStage $relative) -Destination $destination
    }
    # 배포물에 동봉된 라이선스/안내 문서도 함께 보존한다.
    Get-ChildItem -LiteralPath $InstallStage -File | Where-Object { $_.Extension -ne '.exe' } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $PackageStage }
    if (-not (Test-Sdk $PackageStage)) { throw 'The staged SDK is incomplete.' }
    [IO.File]::WriteAllText((Join-Path $PackageStage '.setup-source.txt'), "$InstallerUrl`nSHA256=$InstallerSha256`n")

    # 검증된 폴더만 최종 위치로 옮긴다. 기존 SDK 폴더는 덮어쓰지 않는다.
    Assert-ProjectPath $SdkRoot
    Assert-ProjectPath $PackageStage
    [IO.Directory]::Move($PackageStage, $SdkRoot)
    $PackageStage = $null
    Write-Host "FBX SDK $SdkVersion is ready: $SdkRoot"
}
catch {
    [Console]::Error.WriteLine("FBX SDK setup failed: " + $_.Exception.Message)
    exit 1
}
finally {
    foreach ($stage in @($InstallStage, $PackageStage)) {
        if ($stage) {
            try { Remove-SetupDirectory $stage }
            catch { [Console]::Error.WriteLine("Temporary setup files retained at ${stage}: " + $_.Exception.Message) }
        }
    }
    if ($DownloadStage -and (Test-Path -LiteralPath $DownloadStage)) {
        Assert-ProjectPath $DownloadStage
        Remove-Item -LiteralPath $DownloadStage -Force
    }
}
