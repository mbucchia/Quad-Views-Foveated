$JsonPath = Join-Path "$PSScriptRoot" "openxr-api-layer-32.json"
Start-Process -FilePath powershell.exe -Verb RunAs -Wait -ArgumentList @"
	& {
		Remove-ItemProperty -Path HKLM:\Software\WOW6432Node\Khronos\OpenXR\1\ApiLayers\Implicit -Name '$jsonPath' -Force | Out-Null
	}
"@
