#!/usr/bin/env bash
# Met à jour la copie d'ArduinoCore-API embarquée dans le cœur (cores/gr551x/api) :
# récupère une version publiée, y applique nos patchs (patches/arduinocore-api/*.patch), remplace la copie.
#
#   tools/update-arduinocore-api.sh            → dernière version publiée
#   tools/update-arduinocore-api.sh 1.5.2      → version donnée (tag du dépôt arduino/ArduinoCore-API)
#
# Code de sortie : 0 = copie mise à jour, 10 = déjà à jour, autre = erreur (un patch qui ne s'applique plus
# demande une intervention : le rafraîchir sur la nouvelle version).
# Ne compile rien : construire ensuite examples/blink (le workflow arduinocore-api.yml le fait).
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PATCHES=$ROOT/patches/arduinocore-api
API=$ROOT/cores/gr551x/api
URL=https://github.com/arduino/ArduinoCore-API.git

cur=$(cat "$PATCHES/VERSION")
tag=${1:-$(git ls-remote --tags --refs "$URL" | sed 's|.*refs/tags/||' \
           | grep -E '^v?[0-9]+\.[0-9]+\.[0-9]+$' | sort -V | tail -n 1)}
[ -n "$tag" ] || { echo "aucune version trouvée sur $URL"; exit 1; }
if [ "$tag" = "$cur" ]; then echo "ArduinoCore-API $cur : déjà à jour"; exit 10; fi

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
echo "ArduinoCore-API $cur → $tag"
git -c advice.detachedHead=false clone -q --depth 1 -b "$tag" "$URL" "$tmp/src"
cp -r "$tmp/src/api" "$tmp/api"
cp "$tmp/src/LICENSE" "$tmp/api/LICENSE"
for p in "$PATCHES"/*.patch; do
  echo "patch $(basename "$p")"
  (cd "$tmp" && git apply --verbose "$p") || { echo "le patch $(basename "$p") ne s'applique plus sur $tag"; exit 2; }
done

rsync -a --delete "$tmp/api/" "$API/"
echo "$tag" > "$PATCHES/VERSION"
# Version citée par le README (tableau « Layout »)
sed -i "s|\(\[ArduinoCore-API\](https://github.com/arduino/ArduinoCore-API)\) $cur|\1 $tag|" "$ROOT/README.md"
patches=("$PATCHES"/*.patch)
echo "cores/gr551x/api : ArduinoCore-API $tag + ${#patches[@]} patch(s)"
