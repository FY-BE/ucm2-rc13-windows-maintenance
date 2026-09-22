$root = Split-Path -Parent $PSScriptRoot
Copy-Item -LiteralPath (Join-Path $root 'CLAUDE.md') -Destination (Join-Path $root 'AGENTS.md') -Force
