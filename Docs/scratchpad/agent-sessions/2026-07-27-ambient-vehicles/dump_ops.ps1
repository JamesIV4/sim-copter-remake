$ops = @(
  @(12,'0x4ca940'), @(25,'0x4cb550'), @(30,'0x4cbfd0'), @(32,'0x4cc290'), @(35,'0x4cc410'),
  @(37,'0x4cc530'), @(44,'0x4cc6a0'), @(46,'0x4cc7d0'), @(47,'0x4cc8d0'), @(48,'0x4cc900'),
  @(50,'0x4cc980'), @(51,'0x4cca00'), @(53,'0x4cca60'), @(54,'0x4ccb40'), @(55,'0x4ccb80'),
  @(56,'0x4ccc40'), @(58,'0x4cccd0'), @(60,'0x4cced0'), @(61,'0x4ccef0'), @(62,'0x4ca700'),
  @(63,'0x4ca6f0'), @(66,'0x4cbbc0'), @(67,'0x4cbb80'), @(68,'0x4cb730'), @(69,'0x4cbb60'),
  @(71,'0x4cbaa0'), @(72,'0x4cb770'), @(73,'0x4cb9c0'), @(74,'0x4cba70'), @(75,'0x4cba10'),
  @(76,'0x4cba40'), @(77,'0x4cb9e0'), @(78,'0x4cb830'), @(79,'0x4cb7d0'), @(80,'0x4cb790'),
  @(82,'0x4ccad0'), @(83,'0x4cc130'), @(84,'0x4cc830'), @(87,'0x4cce50')
)
$out = "C:\Users\james\AppData\Local\Temp\claude\s--Repos-sim-copter-remake\1e7b7a5f-851b-45a0-b6e6-fec686c5c5aa\scratchpad\unported_ops.txt"
Remove-Item $out -ErrorAction SilentlyContinue
foreach ($o in $ops) {
  Add-Content $out "`n##################### OPCODE $($o[0])  ($($o[1])) #####################"
  $t = & "S:\Repos\sim-copter-remake\Tools\re-agent\.venv\Scripts\ghidra-bridge.exe" decompile $o[1] 2>&1
  Add-Content $out ($t | Where-Object { $_ -ne "" })
}
Write-Output "done"
