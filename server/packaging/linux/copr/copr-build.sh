#! /bin/sh -x

# Adapted for ArtLight from Sunshine's packaging/linux/fedora/copr-build.sh
# (upstream commit 15b91a1ad, "build(fedora)!: add copr build (#3239)").
#
# This file is NOT used directly by the repo. Its text is pasted into the COPR
# project as a "Custom (script)" source, and COPR runs it to produce the sources
# and the spec that its SRPM step then packs into a .src.rpm.
# Re-paste it into COPR whenever this file changes.
#
# Why it exists: the spec declares `Source0: tarball.tar.gz` and its globals carry
# the comment "use sed to replace these values". Nothing in the repo did either of
# those, and rpkg cannot invent them — a plain SCM source build fails with
#   error: Bad file: .../tarball.tar.gz: No such file or directory
# This script is the thing that creates the tarball and does the sed.
#
# COPR configuration this needs (docs: "Custom source method"):
#   script    : the text of this file
#   chroot    : fedora-44-x86_64, or leave the default (fedora-latest-x86_64)
#   builddeps : git
#     COPR runs this as the unprivileged 'mockbuild' user and cannot install
#     packages from inside the script, so anything it shells out to must be
#     declared up front. git is the only extra one needed; tar and gzip are in
#     the base chroot.

set -e
# COPR exports COPR_RESULTDIR. The docs define resultdir as "current working
# directory" by default, so fall back to that if the variable is absent.
resultdir="${COPR_RESULTDIR:-.}"

git clone "https://github.com/onaiaku/ArtLight.git" --depth 1
cd ArtLight

# A depth-1 clone may not carry tags, and the version comes from the tag.
git fetch --tags --depth 1 origin || true

# The build needs the submodule trees (FFmpeg among them).
git submodule update --init --recursive

# The tag of this commit, if it has one.
TAG=$(git tag --points-at HEAD | head -n1)
if [ -z "$TAG" ]; then
  TAG="0.0.0"
fi
TAG=$(echo "$TAG" | sed 's/^v//')  # drop the v prefix
echo "TAG=$TAG"

COMMIT=$(git rev-parse HEAD)
echo "COMMIT=$COMMIT"

BRANCH=$(git rev-parse --abbrev-ref HEAD)
echo "BRANCH=$BRANCH"

# The spec has to sit where COPR expects to find it.
mv server/packaging/linux/copr/*.spec "${resultdir}"

# "use sed to replace these values" — the spec's own instruction, fulfilled here.
sed -i "s|%global build_version 0|%global build_version ${TAG}|" "${resultdir}"/*.spec
sed -i "s|%global branch 0|%global branch ${BRANCH}|" "${resultdir}"/*.spec
sed -i "s|%global commit 0|%global commit ${COMMIT}|" "${resultdir}"/*.spec

# The file Source0 asks for.
tar -czf "${resultdir}/tarball.tar.gz" .
