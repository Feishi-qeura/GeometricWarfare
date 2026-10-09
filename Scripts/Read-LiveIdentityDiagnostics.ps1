param([string]$Path = '')
$ErrorActionPreference = 'Stop'
if (-not $Path) {
    $identityLogRoot = Join-Path $env:LOCALAPPDATA 'GeometricWarfare/Saved/LivePlatform'
    $identityLog = Get-ChildItem -LiteralPath $identityLogRoot -Filter 'provider-diagnostics-*.jsonl' -File |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $identityLog) { throw 'No provider diagnostic log found.' }
    $Path = $identityLog.FullName
}
$identityEvents = @(foreach ($line in Get-Content -LiteralPath $Path) {
    try { $entry = $line | ConvertFrom-Json } catch { continue }
    if ($entry.stage -ne 'identity_event' -or
        $entry.user_fingerprint -notmatch '^[0-9A-F]{40}$' -or
        $entry.name_fingerprint -notmatch '^([0-9A-F]{40})?$' -or
        $entry.message_fingerprint -notmatch '^[0-9A-F]{40}$' -or
        $entry.op -notin @('live_comment','live_team','live_enter','live_follow','live_gift') -or
        $entry.action -notin @('event','join','number','weapon','rifle','other')) { continue }
    [pscustomobject]@{
        utc = $entry.utc
        event = $entry.op
        action = $entry.action
        user = $entry.user_fingerprint
        name = $entry.name_fingerprint
        message = $entry.message_fingerprint
        normalized = [bool]$entry.id_whitespace_normalized
    }
})
$sameNames = @(foreach ($group in $identityEvents | Where-Object { $_.name } | Group-Object name) {
    $ids = @($group.Group.user | Sort-Object -Unique)
    if ($ids.Count -gt 1) { [pscustomobject]@{ name = $group.Name; users = $ids } }
})
[pscustomobject]@{
    path = [IO.Path]::GetFullPath($Path)
    identity_event_count = $identityEvents.Count
    same_display_name_multiple_ids = $sameNames
    events = $identityEvents
    note = 'Compare within one authenticated room session only. Matching names do not prove matching accounts; platform identity equivalence still needs verification.'
} | ConvertTo-Json -Depth 6
