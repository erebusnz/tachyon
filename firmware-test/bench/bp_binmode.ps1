# Drive the Bus Pirate terminal through: VT100 prompt -> ASCII, then `binmode`,
# capturing the menu so we can pick the BPIO2 entry. Keeps the port open for the
# whole exchange and reacts to prompts as they arrive. Uses bare CR line endings.
param(
  [string]$Port = 'COM14',
  [int]$Baud = 115200,
  [string]$Answer = ''   # optional: text to send when the binmode menu prompt appears
)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 150
$sp.WriteTimeout = 1000
$sp.DtrEnable = $true
$sp.RtsEnable = $true

function ReadFor([int]$ms) {
  $sb = New-Object System.Text.StringBuilder
  $deadline = [DateTime]::UtcNow.AddMilliseconds($ms)
  while ([DateTime]::UtcNow -lt $deadline) {
    $n = $sp.BytesToRead
    if ($n -gt 0) {
      $buf = New-Object byte[] $n
      $r = $sp.Read($buf, 0, $n)
      [void]$sb.Append([System.Text.Encoding]::ASCII.GetString($buf, 0, $r))
    } else { Start-Sleep -Milliseconds 20 }
  }
  return $sb.ToString()
}
function Send([string]$s) { $sp.Write($s + "`r") }

try {
  $sp.Open()
  $log = New-Object System.Text.StringBuilder

  # Step through up to a handful of reactive rounds.
  for ($round = 0; $round -lt 8; $round++) {
    $chunk = ReadFor 700
    if ($chunk) { [void]$log.Append($chunk) }
    $tail = $log.ToString()

    if ($tail -match '\(Y/n\)') {
      Send 'n'                      # decline VT100 color mode
      [void]$log.Append("`n<<sent: n>>`n")
      Start-Sleep -Milliseconds 300
      continue
    }
    if ($tail -match 'binmode' -and $tail -match '\d\s*[-.)]' -and $Answer -ne '') {
      Send $Answer                  # pick the BPIO2 menu entry
      [void]$log.Append("`n<<sent: $Answer>>`n")
      Start-Sleep -Milliseconds 400
      continue
    }
    # Once at a clean prompt with nothing pending, fire `binmode` to reveal menu.
    if ($chunk -eq '' -and $tail -notmatch 'binmode') {
      Send 'binmode'
      [void]$log.Append("`n<<sent: binmode>>`n")
      Start-Sleep -Milliseconds 400
      continue
    }
    if ($chunk -eq '') { break }   # settled, nothing more coming
  }
  # Final drain.
  [void]$log.Append((ReadFor 800))
  Write-Output $log.ToString()
}
finally {
  if ($sp.IsOpen) { $sp.Close() }
  $sp.Dispose()
}
