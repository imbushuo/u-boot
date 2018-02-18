# Windows only - bridges build tasks for Visual Studio
# This script translates Windows path to WSL/Linux path, then invoke WSL for build tasks.

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
    $BuildThreads = 4
)

Function Convert-WslPath
{
	Param
	(
		[Parameter(Mandatory=$True)]
		[string]
		$Path
	)

	if ($Path -eq $null)
	{
		return $null
	}

	$WorkspaceRootPartial = $Path.Split("\\")

	# Force convert disk label
	$DiskLabel = $WorkspaceRootPartial[0]
	$DiskLabel = $DiskLabel.ToLower().Substring(0, $DiskLabel.IndexOf(":"))

	$ConvertedPath = "/mnt/$($DiskLabel)"

	for ($i = 1; $i -lt $WorkspaceRootPartial.Length; $i++)
	{
		$ConvertedPath = "$($ConvertedPath)/$($WorkspaceRootPartial[$i])"
	}

	return $ConvertedPath

}

$workspaceRoot = Convert-WslPath -Path $WorkspaceRoot
$scriptPath = "$($workspaceRoot)/scripts/qualcomm-release.ps1"

if ($Clean)
{
	C:\Windows\SysNative\wsl.exe $scriptPath "--WorkspaceRoot" "$($workspaceRoot)" "--ConfigurationName" "$($ConfigurationName)" "--BuildThreads" "$($BuildThreads)" "--Clean" "--WslBridged"
}
else
{
	C:\Windows\SysNative\wsl.exe $scriptPath "--WorkspaceRoot" "$($workspaceRoot)" "--ConfigurationName" "$($ConfigurationName)" "--BuildThreads" "$($BuildThreads)" "--WslBridged"
}

return $?
