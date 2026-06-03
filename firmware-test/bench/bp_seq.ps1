# Linear scripted exchange with the Bus Pirate terminal. Opens once, sends each
# step with a read window after it. Bare-CR line endings. Use -Steps to pass the
# ordered keystrokes, e.g.  -Steps n,binmode   or   -Steps n,binmode,2,y
param(
  [string]$Port = 'COM14',
  [int]$Baud = 115200,
  [string[]]$Steps = @(),
  [int]$StepMs = 900,
  [int]$InitMs = 900
)
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 150; $sp.WriteTimeout = 1000
$sp.DtrEnable = $true; $sp.RtsEnable = $true
function ReadFor([int]$ms) {
  $sb = New-Object System.Text.StringBuilder
  $deadline = [DateTime]::UtcNow.AddMilliseconds($ms)
  while ([DateTime]::UtcNow -lt $deadline) {
    $n = $sp.BytesToRead
    if ($n -gt 0) {
      $buf = New-Object byte[] $n
      $r = $sp.Read($buf,0,$n)
      [void]$sb.Append([System.Text.Encoding]::ASCII.GetString($buf,0,$r))
    } else { Start-Sleep -Milliseconds 20 }
  }
  return $sb.ToString()
}
try {
  $sp.Open()
  Write-Output "--- banner ---"
  Write-Output (ReadFor $InitMs)
  foreach ($s in $Steps) {
    $sp.Write($s + "`r")
    Write-Output "--- after [$s] ---"
    Write-Output (ReadFor $StepMs)
  }
}
finally { if ($sp.IsOpen) { $sp.Close() }; $sp.Dispose() }
