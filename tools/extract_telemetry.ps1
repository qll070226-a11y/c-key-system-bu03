param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$InputLog,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$OutputCsv
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $InputLog -PathType Leaf)) {
    throw "Input log not found: $InputLog"
}

$marker = 'C_KEY_CSV,'
$header = 'timestamp_ms,key_id,accepted_id,uwb_link,valid_mask,a0_mm,a1_mm,a2_mm,pose_valid,x_m,y_m,boundary_m,angle_deg,residual_m,state,events'
$rows = [Collections.Generic.List[string]]::new()

foreach ($logLine in [IO.File]::ReadLines([IO.Path]::GetFullPath($InputLog))) {
    $markerIndex = $logLine.IndexOf($marker, [StringComparison]::Ordinal)
    if ($markerIndex -lt 0) {
        continue
    }
    $payload = $logLine.Substring($markerIndex + $marker.Length)
    $payload = [regex]::Replace($payload, "$([char]27)\[[0-9;]*m", '').Trim()
    if (($payload.ToCharArray() | Where-Object { $_ -eq ',' }).Count -ne 15) {
        throw "Malformed telemetry row: $payload"
    }
    $rows.Add($payload)
}

if ($rows.Count -eq 0) {
    throw 'No C_KEY_CSV telemetry rows were found.'
}

$lines = [Collections.Generic.List[string]]::new()
$lines.Add($header)
$lines.AddRange($rows)
$resolvedOutput = [IO.Path]::GetFullPath($OutputCsv)
[IO.File]::WriteAllLines($resolvedOutput, $lines, [Text.UTF8Encoding]::new($false))
"Extracted $($rows.Count) telemetry rows to: $resolvedOutput"
