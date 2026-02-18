$ErrorActionPreference='Stop'
function BE16($b,[int]$o){ (($b[$o]-shl 8) -bor $b[$o+1]) }
function BE32($b,[int]$o){ (($b[$o]-shl 24) -bor ($b[$o+1]-shl 16) -bor ($b[$o+2]-shl 8) -bor $b[$o+3]) }
function KnownMode([int]$m){ return @(0,2,4,5,6) -contains $m }
function Parse-Nya($path){
  [byte[]]$b=[IO.File]::ReadAllBytes($path)
  $type=BE32 $b 0; $mc=BE32 $b 4; $tc=BE32 $b 8; $off=12
  $faces=@(); $tex=@()
  for($mi=0;$mi -lt $mc;$mi++){
    $pts=BE32 $b $off; $pol=BE32 $b ($off+4); $off+=8
    $attrOff=$off + $pts*12 + $pol*20
    for($fi=0;$fi -lt $pol;$fi++){
      $fo=$attrOff+$fi*8
      $fl=$b[$fo]; $tid=[int](BE32 $b ($fo+4))
      if(($fl-band 0x80) -ne 0){ $faces += [pscustomobject]@{mesh=$mi;face=$fi;tid=$tid;flags=$fl} }
    }
    if($type -eq 1){ $off += $pts*12 + $pol*20 + $pol*8 + $pts*12 } else { $off += $pts*12 + $pol*20 + $pol*8 }
  }
  for($ti=0;$ti -lt $tc;$ti++){
    if($off+4 -gt $b.Length){ break }
    $w=BE16 $b $off; $h=BE16 $b ($off+2); $off+=4
    $mode=0; $pal=0
    if($off+4 -le $b.Length -and (BE32 $b $off) -eq 0x4E595458){
      if($off+12 -gt $b.Length){ break }
      $modeNew=$b[$off+6]; $modeLegacy=$b[$off+5]
      if(KnownMode $modeNew){ $mode=$modeNew; $pal=BE16 $b ($off+10) } else { $mode=$modeLegacy; $pal=BE16 $b ($off+8) }
      $off += 12 + ($pal*2)
    }
    $px=$w*$h
    $bytes = if($mode -eq 2){ [int]($px/2) } elseif(@(4,5,6) -contains $mode){ [int]$px } else { [int]($px*2) }
    $tex += [pscustomobject]@{id=$ti;w=$w;h=$h;mode=$mode;pal=$pal;bytes=$bytes}
    if($off+$bytes -gt $b.Length){ break }
    $off += $bytes
  }
  $bad = $faces | ? { $_.tid -lt 0 -or $_.tid -ge $tc }
  [pscustomobject]@{texCount=$tc;texturedFaces=$faces.Count;badFaces=$bad.Count;faceTids=$faces;tex=$tex}
}
$r1=Parse-Nya 'cd/data/SEG_001.NYA'
$r2=Parse-Nya 'cd/data/SEG_002.NYA'
Write-Output 'SEG_001'; Write-Output ("texCount={0} texturedFaces={1} badFaces={2}" -f $r1.texCount,$r1.texturedFaces,$r1.badFaces)
$r1.faceTids | Group-Object tid | Sort-Object {[int]$_.Name} | % { "tid {0}: {1}" -f $_.Name,$_.Count }
Write-Output 'SEG_002'; Write-Output ("texCount={0} texturedFaces={1} badFaces={2}" -f $r2.texCount,$r2.texturedFaces,$r2.badFaces)
$r2.faceTids | Group-Object tid | Sort-Object {[int]$_.Name} | % { "tid {0}: {1}" -f $_.Name,$_.Count }
Write-Output 'SEG_001 TEX'; $r1.tex | Select-Object -First 10 | % { "id={0} {1}x{2} mode={3} pal={4}" -f $_.id,$_.w,$_.h,$_.mode,$_.pal }
Write-Output 'SEG_002 TEX'; $r2.tex | Select-Object -First 10 | % { "id={0} {1}x{2} mode={3} pal={4}" -f $_.id,$_.w,$_.h,$_.mode,$_.pal }
