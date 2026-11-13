param(
  [Parameter(Mandatory = $true)][string]$Source,
  [Parameter(Mandatory = $true)][string]$WikiUrl,
  [Parameter(Mandatory = $true)][string]$Checkout
)

$ErrorActionPreference = 'Stop'
if (-not $env:WIKI_TOKEN) { throw 'Set WIKI_TOKEN to a token with wiki write access.' }
if (Test-Path -LiteralPath $Checkout) { throw "Wiki checkout already exists: $Checkout" }

$pages = @(Get-ChildItem -LiteralPath $Source -Filter '*.md' -File)
if ($pages.Count -eq 0) { throw 'No wiki pages found.' }

$credential = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("x-access-token:$env:WIKI_TOKEN"))
Write-Output "::add-mask::$credential"
$env:GIT_CONFIG_COUNT = '1'
$env:GIT_CONFIG_KEY_0 = 'http.https://github.com/.extraheader'
$env:GIT_CONFIG_VALUE_0 = "AUTHORIZATION: basic $credential"
$env:GIT_TERMINAL_PROMPT = '0'

try {
  git clone --quiet -- $WikiUrl $Checkout
  if ($LASTEXITCODE -ne 0) { throw 'Could not clone the wiki.' }
  foreach ($page in $pages) {
    Copy-Item -LiteralPath $page.FullName -Destination (Join-Path $Checkout $page.Name)
  }
  git -C $Checkout add -- @($pages.Name)
  if ($LASTEXITCODE -ne 0) { throw 'Could not stage wiki pages.' }
  git -C $Checkout diff --cached --quiet
  if ($LASTEXITCODE -eq 0) {
    Write-Output 'Wiki is already current.'
    return
  }
  if ($LASTEXITCODE -ne 1) { throw 'Could not compare wiki pages.' }
  git -C $Checkout -c user.name='github-actions[bot]' -c user.email='41898282+github-actions[bot]@users.noreply.github.com' commit -m 'docs: sync wiki'
  if ($LASTEXITCODE -ne 0) { throw 'Could not commit wiki pages.' }
  git -C $Checkout push
  if ($LASTEXITCODE -ne 0) { throw 'Could not push the wiki; retry if another editor changed it.' }
} finally {
  Remove-Item Env:\GIT_CONFIG_COUNT, Env:\GIT_CONFIG_KEY_0, Env:\GIT_CONFIG_VALUE_0, Env:\GIT_TERMINAL_PROMPT -ErrorAction SilentlyContinue
}
