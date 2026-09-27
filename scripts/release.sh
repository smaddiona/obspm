#!/bin/bash
# Commits all pending changes together with the version bump (message = patch/minor/major/X.Y.Z),
# tags and pushes: the tag triggers the CI release build.
# Usage: scripts/release.sh [patch|minor|major|X.Y.Z]   (default: patch)
set -euo pipefail
cd "$(dirname "$0")/.."

bump="${1:-patch}"
current=$(python3 -c 'import json; print(json.load(open("buildspec.json"))["version"])')
IFS=. read -r major minor patch <<<"${current%%-*}"

case "$bump" in
patch) next="$major.$minor.$((patch + 1))" ;;
minor) next="$major.$((minor + 1)).0" ;;
major) next="$((major + 1)).0.0" ;;
*)
	[[ "$bump" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-(beta|rc)[0-9]*)?$ ]] || { echo "Usage: $0 [patch|minor|major|X.Y.Z]"; exit 1; }
	next="$bump"
	;;
esac

branch=$(git rev-parse --abbrev-ref HEAD)
[[ "$branch" == main ]] || { echo "Releases are made from main (you are on $branch)."; exit 1; }
git fetch --quiet origin main
git merge-base --is-ancestor origin/main HEAD || { echo "main is behind origin/main: pull first."; exit 1; }
! git rev-parse -q --verify "refs/tags/$next" >/dev/null || { echo "Tag $next already exists."; exit 1; }

python3 - "$next" <<'PY'
import json, sys
b = json.load(open("buildspec.json"))
b["version"] = sys.argv[1]
open("buildspec.json", "w").write(json.dumps(b, indent=4) + "\n")
PY

git add -A
git commit --quiet -m "$bump"
git tag -a "$next" -m "Release $next"
git push --quiet origin main "$next"

repo=$(git remote get-url origin | sed -E 's#^.*[:/]([^/]+/[^/]+)$#\1#; s#\.git$##')
echo "Released $current -> $next. CI: https://github.com/$repo/actions"
echo "When it finishes, publish the draft: https://github.com/$repo/releases"
