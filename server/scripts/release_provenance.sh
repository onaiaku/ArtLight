#!/usr/bin/env bash
# Rebuild release-provenance.json from the release's own asset digests.
#
# GitHub publishes a sha256 digest for every uploaded asset, so the manifest can
# be rebuilt from what is actually attached without downloading a byte. The
# Windows build only ever sees its own artifacts, which is why a manifest made
# there cannot describe the Arch package or the Fedora RPMs.
#
# Usage: release_provenance.sh <release-tag> [owner/repo]
#        release_provenance.sh 1.6.1
#
# Run it after the last asset is attached, then again after anything else lands.
# It is idempotent: the manifest is replaced, never appended to.
set -euo pipefail

tag=${1:-}
repo=${2:-${GITHUB_REPOSITORY:-}}
[[ -n ${tag} ]] || { echo 'usage: release_provenance.sh <release-tag> [owner/repo]' >&2; exit 2; }
[[ -n ${repo} ]] || { echo "error: no repository given and GITHUB_REPOSITORY is unset" >&2; exit 2; }
command -v gh >/dev/null || { echo 'error: gh is required' >&2; exit 2; }
command -v jq >/dev/null || { echo 'error: jq is required' >&2; exit 2; }

release_json=$(gh api "repos/${repo}/releases/tags/${tag}")
release_id=$(jq -r '.id // empty' <<<"${release_json}")
[[ -n ${release_id} ]] || { echo "error: no release with tag ${tag} in ${repo}" >&2; exit 1; }

assets=$(jq -c '[.assets[]? | select(.name != "release-provenance.json") | {name, size, digest}]' <<<"${release_json}")
if [[ $(jq 'length' <<<"${assets}") == 0 ]]; then
  echo "error: release ${tag} has no assets to record" >&2
  exit 1
fi
if [[ $(jq '[.[] | select((.digest // "") == "")] | length' <<<"${assets}") != 0 ]]; then
  jq -r '.[] | select((.digest // "") == "") | "  no digest: \(.name)"' <<<"${assets}" >&2
  echo "error: GitHub reported no digest for the assets above; refusing to write a partial manifest" >&2
  exit 1
fi

# Preserve the build metadata the original manifest carried, if it is attached.
previous='{}'
if existing_id=$(jq -r '.assets[]? | select(.name == "release-provenance.json") | .id' <<<"${release_json}" | head -1) &&
  [[ -n ${existing_id} ]]; then
  previous=$(gh api "repos/${repo}/releases/assets/${existing_id}" \
    -H 'Accept: application/octet-stream' 2>/dev/null || echo '{}')
  jq -e 'type == "object"' <<<"${previous}" >/dev/null 2>&1 || previous='{}'
fi
release_commit=$(jq -r '.release_commit // empty' <<<"${previous}")
if [[ -z ${release_commit} ]]; then
  release_commit=$(gh api "repos/${repo}/commits/${tag}" --jq '.sha' 2>/dev/null || true)
fi
source_run=$(jq -r '.source_build_run_id // empty' <<<"${previous}")

manifest=$(jq -n \
  --arg tag "${tag}" \
  --arg commit "${release_commit}" \
  --arg run "${source_run}" \
  --argjson assets "${assets}" \
  '{
    schema_version: 1,
    release_tag: $tag,
    release_commit: $commit,
    source_build_run_id: $run,
    assets: ($assets | map({key: .name, value: (.digest | sub("^sha256:"; ""))}) | from_entries)
  }')

work=$(mktemp -d "${TMPDIR:-/tmp}/artlight-provenance.XXXXXXXX")
trap 'rm -rf -- "${work}"' EXIT
printf '%s\n' "${manifest}" >"${work}/release-provenance.json"

gh release upload "${tag}" "${work}/release-provenance.json" --repo "${repo}" --clobber >/dev/null

attached=$(gh api "repos/${repo}/releases/${release_id}/assets" --paginate --jq '.[].name')
recorded=$(jq -r '.assets | keys[]' <<<"${manifest}")
unrecorded=$(comm -23 <(LC_ALL=C sort <<<"${attached}") <(LC_ALL=C sort <<<"${recorded}") | grep -v '^release-provenance.json$' || true)
if [[ -n ${unrecorded} ]]; then
  echo "error: attached but missing from the manifest:" >&2
  printf '  %s\n' ${unrecorded} >&2
  exit 1
fi

jq -r '"recorded \(.assets | length) asset(s) for release \(.release_tag):", (.assets | to_entries[] | "  \(.key)  \(.value[0:16])…")' <<<"${manifest}"
