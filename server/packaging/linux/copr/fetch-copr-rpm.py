#!/usr/bin/env python3
"""Fetch the ArtLight RPMs COPR published for one version.

Called by .github/workflows/attach-fedora-packages.yml to copy the packages the
Fedora build *already* produced onto the matching GitHub release, so the same
bytes serve both `dnf` and a hand download. Nothing is compiled here.

The chroot list comes from the COPR API rather than a constant in this file, so
enabling or removing a chroot in COPR stays the only change needed for this side
to follow it.

The version transform mirrors Sunshine.spec's own `%global rpm_version`: strip a
leading `v`, then turn the first `-` into RPM's `~` prerelease separator, except
`-stable`, which becomes `+stable`. Keep the two in step.

Exit status: 0 when every enabled chroot yielded a verified package, 1 otherwise.
"""

from __future__ import annotations

import argparse
import gzip
import json
import sys
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path

OWNER = "onaiaku"
PROJECT = "artlight"
PACKAGE = "artlight"
ARCH = "x86_64"

RESULTS = f"https://download.copr.fedorainfracloud.org/results/{OWNER}/{PROJECT}"
PROJECT_API = (
    "https://copr.fedorainfracloud.org/api_3/project"
    f"?ownername={OWNER}&projectname={PROJECT}"
)

NS = {"c": "http://linux.duke.edu/metadata/common"}


def rpm_version(build_version: str) -> str:
    """Reproduce Sunshine.spec's %global rpm_version exactly.

    The spec tests the six characters after the first hyphen for "stable"
    rather than matching the whole tail, so this does the same.
    """
    version = build_version[1:] if build_version.startswith("v") else build_version
    head, sep, tail = version.partition("-")
    if not sep:
        return version
    return f"{head}+{tail}" if tail.startswith("stable") else f"{head}~{tail}"


def get(url: str, timeout: int = 300) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "artlight-release"})
    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return response.read()
    except urllib.error.HTTPError as error:
        raise SystemExit(f"{url}: HTTP {error.code}") from error
    except urllib.error.URLError as error:
        raise SystemExit(f"{url}: {error.reason}") from error


def enabled_chroots() -> list[str]:
    payload = json.loads(get(PROJECT_API, timeout=120))
    chroots = sorted(payload.get("chroot_repos") or {})
    if not chroots:
        raise SystemExit("COPR reported no enabled chroots for the project.")
    return chroots


def primary_path(chroot: str) -> str:
    """Read repomd.xml for the hashed primary-metadata filename."""
    repomd = get(f"{RESULTS}/{chroot}/repodata/repomd.xml", timeout=120).decode()
    root = ET.fromstring(repomd)
    for data in root:
        if data.get("type") == "primary":
            location = data.find("{http://linux.duke.edu/metadata/repo}location")
            if location is not None:
                return location.get("href", "")
    raise SystemExit(f"{chroot}: repomd.xml declares no primary metadata.")


def packages(chroot: str) -> list[dict[str, str]]:
    """Every binary package the chroot's repo currently publishes."""
    url = f"{RESULTS}/{chroot}/{primary_path(chroot)}"
    root = ET.fromstring(gzip.decompress(get(url)))
    found = []
    for package in root.findall("c:package", NS):
        if package.get("type") != "rpm":
            continue
        version = package.find("c:version", NS)
        location = package.find("c:location", NS)
        if version is None or location is None:
            continue
        found.append(
            {
                "name": package.findtext("c:name", "", NS),
                "arch": package.findtext("c:arch", "", NS),
                "ver": version.get("ver", ""),
                "rel": version.get("rel", ""),
                "href": location.get("href", ""),
            }
        )
    return found


def fetch_for_chroot(chroot: str, want: str, output_dir: Path) -> Path:
    published = packages(chroot)
    matches = [
        package
        for package in published
        if package["name"] == PACKAGE
        and package["arch"] == ARCH
        and package["ver"] == want
    ]
    if len(matches) != 1:
        available = sorted(
            f"{p['name']}-{p['ver']}-{p['rel']}.{p['arch']}" for p in published
        )
        listing = "\n  ".join(available) or "(nothing published)"
        raise SystemExit(
            f"{chroot}: expected exactly one {PACKAGE} {ARCH} build with "
            f"ver={want}, found {len(matches)}.\n  available:\n  {listing}"
        )

    href = matches[0]["href"]
    name = Path(href).name
    destination = output_dir / name
    destination.write_bytes(get(f"{RESULTS}/{chroot}/{href}"))
    print(f"{chroot}\t{name}\t{matches[0]['ver']}-{matches[0]['rel']}", flush=True)
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--version",
        required=True,
        help="Build version as copr-build.sh computes it, for example 1.5.6 or "
        "1.5.6-rc1 (a leading v is accepted and stripped).",
    )
    parser.add_argument("--output-dir", type=Path, help="Where to write the RPMs.")
    parser.add_argument(
        "--print-version",
        action="store_true",
        help="Print the transformed RPM version and exit, fetching nothing.",
    )
    arguments = parser.parse_args()

    want = rpm_version(arguments.version)
    if arguments.print_version:
        print(want)
        return 0
    if arguments.output_dir is None:
        parser.error("--output-dir is required unless --print-version is given")
    arguments.output_dir.mkdir(parents=True, exist_ok=True)

    print(f"build version {arguments.version} -> rpm version {want}", file=sys.stderr)
    for chroot in enabled_chroots():
        fetch_for_chroot(chroot, want, arguments.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
