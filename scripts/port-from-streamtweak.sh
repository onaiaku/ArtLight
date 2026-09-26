#!/usr/bin/env bash
#
# port-from-streamtweak.sh — bring FoggyBytes/StreamTweak changes into ArtLight Control.
#
# ⚠️ THIS DOES NOT HAND-PORT. GitHub does the three-way merge; this script only (a) maps his
# paths onto ours and (b) applies the rebrand to the merged result. If a file conflicts, the
# conflict markers are written into our file and the script says so — resolve by hand, never by
# eyeballing his diff.
#
# Why a three-way merge is possible at all: `git merge-base HEAD 1650906` in
# /projects/artlight-work returns 1650906 itself — his v8.2.0 tag is a genuine ancestor of our
# tree. Our StreamTweak lineage is a real fork, not a re-import. Do not lose that property.
#
# Usage:
#   ./port-from-streamtweak.sh <his-path> [<his-path> ...]
#   ./port-from-streamtweak.sh --dry-run <his-path> ...
#
# Paths are HIS repo paths, e.g. StreamTweak.Core/ClipboardShare.cs

set -uo pipefail

OURS=/projects/artlight-work
UPSTREAM=/home/rias/.hermes/cache/scratch/upstream-streamtweak
FORK=v8.2.0
DRY=0
[ "${1:-}" = "--dry-run" ] && { DRY=1; shift; }

[ -d "$UPSTREAM/.git" ] || { echo "no upstream clone at $UPSTREAM" >&2; exit 1; }
[ -d "$OURS/.git" ]        || { echo "no ours at $OURS" >&2; exit 1; }

# ── his path -> our path ────────────────────────────────────────────────────────────────
map_path() {
  local p="$1"
  # File renames the rebrand did — these must come BEFORE the directory substitutions.
  p="${p/#StreamTweak.Core\/StreamTweakBridge.cs/control/ArtLightControl.Core/ArtLightBridge.cs}"
  p="${p/#StreamTweakUI\/StreamTweakUI.csproj/control/ArtLightControl/ArtLightControl.csproj}"
  p="${p/#StreamTweakUI\/StreamTweakUI.sln/control/ArtLightControl.sln}"
  p="${p/#StreamTweak.Core\//control/ArtLightControl.Core/}"
  p="${p/#StreamTweakUI\//control/ArtLightControl/}"
  p="${p/#StreamTweakService\//control/ArtLightControlService/}"
  p="${p/#StreamTweak.sln/control/ArtLightControl.sln}"
  p="${p/#Installer.iss/control/Installer.iss}"
  p="${p/#CodeDependencies.iss/control/CodeDependencies.iss}"
  p="${p/#changelog.txt/control/changelog.txt}"
  p="${p/#installer\//control/installer/}"
  p="${p/#tools\//control/tools/}"
  printf '%s' "$p"
}

# ── his brand -> ours. ORDER MATTERS. ───────────────────────────────────────────────────
# FoggyBytes.ClipboardSync is PROTECTED: his host registers it and our client already
# registers it (ArtMoon app/streaming/clipboardsync.cpp:33). It is the handshake between the
# two halves, not branding. Rebranding it breaks the feature silently.
rebrand() {
  sed \
    -e 's/FoggyBytes\.ClipboardSync/@@CLIPHANDSHAKE@@/g' \
    -e 's/StreamTweakBridge/ArtLightBridge/g' \
    -e 's/StreamTweak\.Core/ArtLightControl.Core/g' \
    -e 's/StreamTweakUI/ArtLightControl/g' \
    -e 's/StreamTweakService/ArtLightControlService/g' \
    -e 's/StreamTweak\.sln/ArtLightControl.sln/g' \
    -e 's/StreamTweak/ArtLightControl/g' \
    -e 's/StreamLight/ArtMoon/g' \
    -e 's/@@CLIPHANDSHAKE@@/FoggyBytes.ClipboardSync/g'
}

port_one() {
  local his="$1"
  local our; our="$(map_path "$his")"
  local out="$OURS/$our"

  if ! git -C "$UPSTREAM" cat-file -e "HEAD:$his" 2>/dev/null; then
    echo "  SKIP    $his (not in his tree at HEAD)"; return 0
  fi

  if git -C "$UPSTREAM" cat-file -e "$FORK:$his" 2>/dev/null; then
    # He had it at the fork => ours exists too => real three-way merge.
    if [ ! -f "$out" ]; then
      echo "  ⚠️ BAD   $his existed at fork but $our is missing here"; return 1
    fi
    local base theirs merged rc
    base=$(mktemp); theirs=$(mktemp); merged=$(mktemp)
    git -C "$UPSTREAM" show "$FORK:$his" > "$base"
    git -C "$UPSTREAM" show "HEAD:$his" > "$theirs"
    git merge-file -p "$out" "$base" "$theirs" > "$merged" 2>/dev/null
    rc=$?
    if [ "$DRY" = 1 ]; then
      echo "  merge   $his -> $our  ($(grep -c '^<<<<<<<' "$merged" 2>/dev/null || echo 0) conflicts)"
    else
      rebrand < "$merged" > "$out"
      if [ "$rc" -ne 0 ]; then
        echo "  ⚠️ CONFLICT $our — $rc hunk(s) marked, resolve by hand"
      else
        echo "  merged  $his -> $our"
      fi
    fi
    rm -f "$base" "$theirs" "$merged"
  else
    # New file upstream => we do not have it.
    if [ "$DRY" = 1 ]; then
      echo "  add     $his -> $our  ($(git -C "$UPSTREAM" show "HEAD:$his" | wc -l) lines)"
    else
      mkdir -p "$(dirname "$out")"
      git -C "$UPSTREAM" show "HEAD:$his" | rebrand > "$out"
      echo "  added   $his -> $our"
    fi
  fi
}

echo "porting from $UPSTREAM@HEAD (fork $FORK, $(git -C "$UPSTREAM" rev-list --count "$FORK..HEAD") commits behind)"

# ── completeness guard ──────────────────────────────────────────────────────────────────
# Added after a real miss: the first run of this script ported 30 files and silently skipped
# Installer.iss, changelog.txt, README.md and .badges/downloads.svg. Skipping Installer.iss
# meant the Windows App SDK runtime call site never came across while the csproj's
# PackageReference did — an installer that lays down the wrong runtime. Nothing failed; the
# merge was clean. So: never trust a port you haven't proved covered everything.
#
# The test is "would merging his changes into ours still change anything?" — NOT "is our file
# equal to his after rebranding". The second test flags every file we ever edited ourselves,
# which is most of the fork, and an alarm that always fires is not an alarm.
check_complete() {
  local f our base theirs out rc sync=0 conflict=0 absent=0 documented=0
  local tmp; tmp=$(mktemp -d)
  echo
  echo "── completeness vs his tree ──────────────────────────────────────────"
  for f in $(git -C "$UPSTREAM" diff --name-only "$FORK" HEAD); do
    our=$(map_path "$f")
    if [ ! -f "$OURS/$our" ]; then
      echo "  ABSENT   $f   (-> $our) — his change not here at all"; absent=$((absent+1)); continue
    fi
    git -C "$UPSTREAM" show "$FORK:$f" > "$tmp/base"   2>/dev/null || continue
    git -C "$UPSTREAM" show "HEAD:$f"  > "$tmp/theirs" 2>/dev/null || continue
    git merge-file -p "$OURS/$our" "$tmp/base" "$tmp/theirs" > "$tmp/out" 2>/dev/null
    rc=$?
    if [ "$rc" -ne 0 ]; then
      # A conflict alone proves nothing — re-merging always conflicts wherever both sides
      # touched one line, which is most of the fork. What matters is whether his ADDED lines
      # actually reached our file. This is the check that would have caught the real miss:
      # his three uninstaller lines were absent while Installer.iss read as "conflict".
      local missing=0 total=0 relin
      while IFS= read -r relin; do
        total=$((total+1))
        grep -qF -- "$relin" "$OURS/$our" || { missing=$((missing+1)); }
      done < <(diff "$tmp/base" "$tmp/theirs" 2>/dev/null \
                 | grep '^> ' | sed 's/^> //' | rebrand | grep -v '^[[:space:]]*$')
      if [ "$missing" -eq 0 ]; then
        echo "  ok       $f   (-> $our) — all $total of his added lines are present"
        sync=$((sync+1))
      elif grep -qF -- "$our" "$OURS/scripts/port-divergences.txt" 2>/dev/null; then
        echo "  kept     $f   (-> $our) — $missing of $total absent, documented in port-divergences.txt"
        documented=$((documented+1))
      else
        echo "  MISSING  $f   (-> $our) — $missing of $total of his added lines absent"
        echo "           ↳ UNDOCUMENTED. Port it, or add a reason to scripts/port-divergences.txt."
        absent=$((absent+1))
      fi
    elif ! cmp -s "$tmp/out" "$OURS/$our"; then
      echo "  STALE    $f   (-> $our) — his change would still land; port it"
      absent=$((absent+1))
    else
      sync=$((sync+1))
    fi
  done
  for f in $(git -C "$UPSTREAM" diff --name-only --diff-filter=A "$FORK" HEAD); do
    our=$(map_path "$f")
    [ -f "$OURS/$our" ] || { echo "  ABSENT   $f   (-> $our, new upstream)"; absent=$((absent+1)); }
  done
  rm -rf "$tmp"
  echo "  ── in sync: $sync   kept on purpose: $documented   outstanding: $absent"
  echo
  if [ "$absent" -eq 0 ]; then
    echo "  ✓ port is complete. Every line of his that is not here has a reason in"
    echo "    scripts/port-divergences.txt."
  else
    echo "  ⚠️  $absent file(s) outstanding with no recorded reason — port them, or add a"
    echo "    row to scripts/port-divergences.txt."
  fi
  return 0
}

if [ "${1:-}" = "--check" ]; then check_complete; exit 0; fi

rc=0
for f in "$@"; do port_one "$f" || rc=1; done
check_complete
exit $rc
