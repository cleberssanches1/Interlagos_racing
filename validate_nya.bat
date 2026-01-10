@echo off
setlocal
set NYA_PATH=%~dp0cd\data\INTLAGOS.NYA
if not exist "%NYA_PATH%" (
  echo NYA not found %NYA_PATH%
  exit /b 1
)
powershell -NoLogo -NoProfile -Command ^
  "$p = '%NYA_PATH%';" ^
  "$data = [IO.File]::ReadAllBytes($p);" ^
  "if($data.Length -lt 12){ Write-Output 'File too small'; exit 1 }" ^
  "$typeBE = [BitConverter]::ToUInt32([byte[]]@($data[0..3]) -as [byte[]],0); $typeBE = ([System.Net.IPAddress]::NetworkToHostOrder([int]$typeBE)) -band 0xFFFFFFFF;" ^
  "$meshBE = [System.Net.IPAddress]::NetworkToHostOrder([int][BitConverter]::ToUInt32($data,4)) -band 0xFFFFFFFF;" ^
  "$texBE  = [System.Net.IPAddress]::NetworkToHostOrder([int][BitConverter]::ToUInt32($data,8)) -band 0xFFFFFFFF;" ^
  "$typeLE = [BitConverter]::ToUInt32($data,0);" ^
  "$meshLE = [BitConverter]::ToUInt32($data,4);" ^
  "$texLE  = [BitConverter]::ToUInt32($data,8);" ^
  "Write-Output ('File size: {0} bytes' -f $data.Length);" ^
  "Write-Output ('Header BE: type={0} meshes={1} textures={2}' -f $typeBE,$meshBE,$texBE);" ^
  "Write-Output ('Header LE: type={0} meshes={1} textures={2}' -f $typeLE,$meshLE,$texLE);" ^
  "$off = 12; if($data.Length -ge $off+8){ $pcountBE = [System.Net.IPAddress]::NetworkToHostOrder([int][BitConverter]::ToUInt32($data,$off)) -band 0xFFFFFFFF; $fcountBE = [System.Net.IPAddress]::NetworkToHostOrder([int][BitConverter]::ToUInt32($data,$off+4)) -band 0xFFFFFFFF; Write-Output ('Mesh0 counts BE: points={0} faces={1}' -f $pcountBE,$fcountBE); $need = $pcountBE*12 + $fcountBE*24; Write-Output ('Mesh0 rough bytes (verts+faces min): {0}' -f $need) } else { Write-Output 'Mesh0 not readable' }"
endlocal
