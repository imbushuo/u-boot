#!/usr/bin/pwsh
# Automated build script for U-Boot

Param
(
    [Parameter(Mandatory=$True)]
    [string]
    $WorkspaceRoot,
    
    [switch]
    $Clean,

    [string]
    $ConfigurationName = "dragonboard410c_defconfig",

    [int]
    $BuildThreads = 4,

	[switch]
	$WslBridged
)

Write-Host "U-Boot automated PowerShell builder for Qualcomm LK"
Write-Host "Configuration: $($ConfigurationName), clean build: $($Clean)"

if ($WslBridged)
{
	# This is a WSL workaround (because it doesn't load environment when launched externally)
	$env:PATH = "/opt/skales:/opt/gcc-linaro-6.4.1-2017.11-x86_64_aarch64-elf/bin:$($env:PATH)"
}

# Set of required tools. Assume we have full Linaro toolchain if we have GCC
$requiredTools = 
@(
    "autoconf",
    "aarch64-elf-gcc",
    "dtbTool",
    "make",
    "mkbootimg"
)

Function Get-Toolchain
{
    Param
    (
        [Parameter(Mandatory=$True)]
        [string]
        $Executable
    )

    $query = Get-Command -Name $Executable -ErrorAction SilentlyContinue

    # Assume presence if we found command
    return $query -ne $null
}

# Check workspace root
if ($WorkspaceRoot -eq $null)
{
    Write-Warning -Message "Environment Check: Workspace root path is not set."
    return -1

    if ((Test-Path -Path $WorkspaceRoot -ErrorAction SilentlyContinue) -eq $false)
    {
        Write-Warning -Message "Environment Check: Workspace root path is not found."
        return -1
    }
}

# Check tools
foreach ($tool in $requiredTools)
{
    if ((Get-Toolchain -Executable $tool) -eq $false)
    {
        Write-Error "Environment Check: Mandatory tool $($tool) is not found. Please verify your installation."
        return -1
    }
    else
    {
        Write-Host "Environment Check: Mandatory tool $($tool) is found."
    }
}

# Set environment variables
Write-Host "Pre Configuration: Setting environment variables."
$env:ARCH = "aarch64"
$env:CROSS_COMPILE = "aarch64-elf-"

# Pre-clean target if required
if ($Clean)
{
    Write-Host "Clean: Enter target."

    $currLocation = Get-Location
    Set-Location -Path $WorkspaceRoot

    # Invoke native method
    make clean
    if (-not $?)
    {
        Write-Error "Clean: Target failed."
        return $?
    }
    # Remove dt.img
    if (Test-Path -Path "dt.img")
    {
        Remove-Item -Path "dt.img" -ErrorAction Stop
    }
    # Remove u-boot.img
    if (Test-Path -Path "u-boot.img")
    {
        Remove-Item -Path "u-boot.img" -ErrorAction Stop
    }

    Set-Location $currLocation
    Write-Host "Clean: Exit target."
}

# Configure target
Write-Host "Configuration: Enter target $($ConfigurationName)."

$currLocation = Get-Location
Set-Location -Path $WorkspaceRoot

# Invoke configuration
make $ConfigurationName
if (-not $?)
{
    Write-Error "Configuration: Target failed."
    return $?
}

Set-Location $currLocation
Write-Host "Configuration: Exit target."

# Build U-Boot
Write-Host "Build: Enter GCC target, using $($BuildThreads) thread(s)."

$currLocation = Get-Location
Set-Location -Path $WorkspaceRoot

# Invoke build
make "-j$($BuildThreads)"
if (-not $?)
{
    Write-Error "Build: Target failed."
    return $?
}

Set-Location $currLocation
Write-Host "Build: Exit GCC target."

# Build Qualcomm
Write-Host "Build: Enter Qualcomm target."

$currLocation = Get-Location
Set-Location -Path $WorkspaceRoot

# Ramdisk
touch rd
dtbTool -o dt.img arch/arm/dts
mkbootimg --kernel=u-boot-dtb.bin --output=u-boot.img --dt=dt.img --pagesize 2048 --base 0x80000000 --ramdisk=rd --cmdline=""
if (-not $?)
{
    Write-Error "Build: Target failed."
    return $?
}

Set-Location $currLocation
Write-Host "Build: Exit Qualcomm target."
