param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$CsvPath,

    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$requiredColumns = @('sample', 'anchor_id', 'true_mm', 'measured_mm')

if (-not (Test-Path -LiteralPath $CsvPath -PathType Leaf)) {
    throw "Calibration CSV not found: $CsvPath"
}

$rows = @(Import-Csv -LiteralPath $CsvPath)
if ($rows.Count -eq 0) {
    throw 'Calibration CSV has no measurement rows.'
}

foreach ($column in $requiredColumns) {
    if (-not ($rows[0].PSObject.Properties.Name -contains $column)) {
        throw "Missing CSV column: $column"
    }
}

$culture = [Globalization.CultureInfo]::InvariantCulture
$parsed = foreach ($row in $rows) {
    $anchorId = 0
    $trueMm = 0.0
    $measuredMm = 0.0
    if (-not [int]::TryParse($row.anchor_id, [ref]$anchorId) -or
        $anchorId -lt 0 -or $anchorId -gt 1) {
        throw "Invalid anchor_id '$($row.anchor_id)' in sample '$($row.sample)'."
    }
    if (-not [double]::TryParse(
            $row.true_mm,
            [Globalization.NumberStyles]::Float,
            $culture,
            [ref]$trueMm) -or $trueMm -le 0.0) {
        throw "Invalid true_mm '$($row.true_mm)' in sample '$($row.sample)'."
    }
    if (-not [double]::TryParse(
            $row.measured_mm,
            [Globalization.NumberStyles]::Float,
            $culture,
            [ref]$measuredMm) -or $measuredMm -le 0.0) {
        throw "Invalid measured_mm '$($row.measured_mm)' in sample '$($row.sample)'."
    }

    [pscustomobject]@{
        Sample = $row.sample
        AnchorId = $anchorId
        TrueMm = $trueMm
        MeasuredMm = $measuredMm
        RequiredOffsetMm = $trueMm - $measuredMm
    }
}

$results = @()
$configLines = @()
foreach ($anchorId in 0..1) {
    $anchorRows = @($parsed | Where-Object AnchorId -eq $anchorId)
    if ($anchorRows.Count -lt 3) {
        throw "Anchor $anchorId needs at least 3 samples; found $($anchorRows.Count)."
    }

    $offset = [Math]::Round(
        ($anchorRows | Measure-Object RequiredOffsetMm -Average).Average,
        0,
        [MidpointRounding]::AwayFromZero)
    $correctedErrors = @(
        $anchorRows | ForEach-Object {
            $_.MeasuredMm + $offset - $_.TrueMm
        }
    )
    $absoluteErrors = @($correctedErrors | ForEach-Object { [Math]::Abs($_) })
    $mae = ($absoluteErrors | Measure-Object -Average).Average
    $maximum = ($absoluteErrors | Measure-Object -Maximum).Maximum
    $mean = ($correctedErrors | Measure-Object -Average).Average
    $sumSquares = 0.0
    foreach ($errorValue in $correctedErrors) {
        $sumSquares += [Math]::Pow($errorValue - $mean, 2.0)
    }
    $standardDeviation = [Math]::Sqrt($sumSquares / $correctedErrors.Count)

    $results += [pscustomobject]@{
        Anchor = $anchorId
        Samples = $anchorRows.Count
        OffsetMm = [int]$offset
        CorrectedMaeMm = [Math]::Round($mae, 1)
        CorrectedMaxMm = [Math]::Round($maximum, 1)
        CorrectedStdMm = [Math]::Round($standardDeviation, 1)
    }
    $configLines += ('CONFIG_C_KEY_ANCHOR{0}_OFFSET_MM={1}' -f $anchorId, [int]$offset)
}

$results | Format-Table -AutoSize
''
'Suggested ESP-IDF configuration:'
$configLines

if ($OutputPath) {
    $resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
    [IO.File]::WriteAllLines($resolvedOutput, $configLines, [Text.UTF8Encoding]::new($false))
    ''
    "Wrote configuration to: $resolvedOutput"
}
