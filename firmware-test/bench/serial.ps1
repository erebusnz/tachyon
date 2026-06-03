# Minimal serial helper for Tachyon bench bring-up.
# Opens a COM port, optionally sends one or more lines (with a read pause
# between each), then drains the input buffer for ReadMs and prints what came back.
#
#   powershell -File serial.ps1 -Port COM13 -Send "id" -ReadMs 1500
#   powershell -File serial.ps1 -Port COM14 -Send "binmode","2","y" -StepMs 600
#   powershell -File serial.ps1 -Port COM13            # just read whatever is there
#
param(
  [Parameter(Mandatory=$true)][string]$Port,
  [int]$Baud = 115200,
  [string[]]$Send = @(),
  [int]$ReadMs = 1500,   # final drain window after the last send
  [int]$StepMs = 400,    # read window after each intermediate send
  [string]$Eol = "`r`n"  # line terminator appended to each -Send line
)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout  = 200
$sp.WriteTimeout = 1000
$sp.NewLine = $Eol
$sp.DtrEnable = $true   # many USB-CDC stacks gate TX on DTR
$sp.RtsEnable = $true

function Drain([int]$ms) {
  $sb = New-Object System.Text.StringBuilder
  $deadline = [DateTime]::UtcNow.AddMilliseconds($ms)
  while ([DateTime]::UtcNow -lt $deadline) {
    try {
      $n = $sp.BytesToRead
      if ($n -gt 0) {
        $buf = New-Object byte[] $n
        $read = $sp.Read($buf, 0, $n)
        [void]$sb.Append([System.Text.Encoding]::ASCII.GetString($buf, 0, $read))
      } else {
        Start-Sleep -Milliseconds 25
      }
    } catch { Start-Sleep -Milliseconds 25 }
  }
  return $sb.ToString()
}

try {
  $sp.Open()
  Start-Sleep -Milliseconds 150
  # Print anything already waiting (banners, prompts).
  $pre = Drain 250
  if ($pre.Length -gt 0) { Write-Output $pre }

  for ($i = 0; $i -lt $Send.Count; $i++) {
    $sp.Write($Send[$i] + $Eol)
    $isLast = ($i -eq $Send.Count - 1)
    $out = Drain ($(if ($isLast) { $ReadMs } else { $StepMs }))
    if ($out.Length -gt 0) { Write-Output $out }
  }

  if ($Send.Count -eq 0) {
    Write-Output (Drain $ReadMs)
  }
}
finally {
  if ($sp.IsOpen) { $sp.Close() }
  $sp.Dispose()
}
