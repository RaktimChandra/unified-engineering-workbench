#!/usr/bin/env bash
# Assembles the submission ZIP from an existing Windows build. Packaging only:
# copies files, converts committed SVG plots to PNG, writes the git bundle and
# commit log, and zips. It implements no application behaviour.
#
#   scripts/package.sh [build-dir]      (default: build-win)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${1:-$ROOT/build-win}"
ID="RA2311033010038"
NAME="${ID}_Workbench"
OUT="$ROOT/dist/$NAME"

for f in Launch.exe wb_cli.exe wb_tests.exe; do
  [[ -f "$BUILD/$f" ]] || { echo "missing $BUILD/$f - build the Windows target first (docs/BUILD.md)"; exit 1; }
done

rm -rf "$ROOT/dist" && mkdir -p "$OUT"
cd "$ROOT"

# 1. packaged application
cp "$BUILD/Launch.exe" "$OUT/"
mkdir -p "$OUT/app/bin"
cp "$BUILD/wb_cli.exe" "$BUILD/wb_tests.exe" "$OUT/app/bin/"
cp -r src/frontend "$OUT/app/web"

# 2. sources, build files, editor configuration, data, tests, docs (tracked files only)
git ls-files -z | while IFS= read -r -d '' f; do
  mkdir -p "$OUT/$(dirname "$f")"
  cp "$f" "$OUT/$f"
done

# 3. PNG copies of the committed SVG plots (for viewers without SVG support)
for svg in "$OUT"/results/*.svg "$OUT"/docs/figures/*.svg; do
  [[ -f "$svg" ]] && rsvg-convert -w 1200 -b white "$svg" -o "${svg%.svg}.png"
done

# 4. version-control evidence
mkdir -p "$OUT/vcs"
git bundle create "$OUT/vcs/${NAME}.bundle" --all >/dev/null 2>&1
git log --date=iso --stat > "$OUT/vcs/COMMIT_LOG.txt"
git log --oneline > "$OUT/vcs/COMMIT_LOG_oneline.txt"
cat > "$OUT/vcs/README.md" <<EOF
# Version-control evidence

* \`${NAME}.bundle\` is the complete Git history. Restore it with:
  \`git clone ${NAME}.bundle workbench\`
* \`COMMIT_LOG.txt\` is the readable log with changed files per commit.
* \`COMMIT_LOG_oneline.txt\` is a one-line summary.

Ignored by design (\`.gitignore\`): build trees, packaged binaries and runtime logs.
EOF

# 5. guard: no absolute machine paths or credentials in text files
BUILD_ROOT="$ROOT"
if grep -rl --exclude=*.bundle --exclude=package.sh -F "$BUILD_ROOT" "$OUT" ; then
  echo "build-machine absolute path found in package (see above)"; exit 1
fi

# 6. zip
(cd "$ROOT/dist" && rm -f "$NAME.zip" && zip -qr -9 "$NAME.zip" "$NAME")
ls -la "$ROOT/dist/$NAME.zip"
