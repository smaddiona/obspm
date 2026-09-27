#!/bin/bash
# Resets obspm to a first-install state: plugin config + obspm-h/obspm-v profiles and scene collections.
# Recordings and <workdir>/obspm.json are NOT touched.
set -euo pipefail
OBS="$HOME/Library/Application Support/obs-studio"
INI="$OBS/user.ini"

if pgrep -x OBS >/dev/null; then echo "Close OBS first."; exit 1; fi

# OBS strips "-" from directory/file names: obspm-h -> obspmh
rm -rf "$OBS/plugin_config/obspm"
rm -rf "$OBS/basic/profiles/"{obspmh,obspmv,obspm-h,obspm-v}
rm -f "$OBS/basic/scenes/"{obspmh,obspmv,obspm-h,obspm-v}.json*

# If OBS was left on an obspm profile/collection, point it back to another existing one.
if grep -q '^ProfileDir=obspm' "$INI"; then
	dir=$(ls "$OBS/basic/profiles" | head -1)
	name=$(sed -n 's/^Name=//p' "$OBS/basic/profiles/$dir/basic.ini" | head -1)
	sed -i '' -e "s/^Profile=obspm.*/Profile=$name/" -e "s/^ProfileDir=obspm.*/ProfileDir=$dir/" "$INI"
fi
if grep -q '^SceneCollectionFile=obspm' "$INI"; then
	file=$(ls "$OBS/basic/scenes" | grep '\.json$' | head -1)
	name=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["name"])' "$OBS/basic/scenes/$file")
	sed -i '' -e "s/^SceneCollection=obspm.*/SceneCollection=$name/" -e "s/^SceneCollectionFile=obspm.*/SceneCollectionFile=$file/" "$INI"
fi
echo "obspm reset. Start OBS to see the first-run setup again."
