$JsonPath = Join-Path "$PSScriptRoot" "openxr-api-layer.json"
Start-Process -FilePath powershell.exe -Verb RunAs -Wait -ArgumentList @"
	& {
		Remove-ItemProperty -Path HKLM:\Software\Khronos\OpenXR\1\ApiLayers\Implicit -Name '$jsonPath' -Force | Out-Null
	}
"@
