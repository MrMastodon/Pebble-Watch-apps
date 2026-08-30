#!/usr/bin/env bash
# Builds every app under apps/ and copies the resulting .pbw into
# apps/<app>/dist/<app>.pbw, ready to link to from the README.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for app_dir in "$repo_root"/apps/*/; do
  app_name="$(basename "$app_dir")"

  # Most apps are a Pebble project directly. Apps with a companion phone app
  # keep the watch side in watchapp/ so the two halves can sit side by side.
  if [ -f "$app_dir/package.json" ]; then
    project_dir="$app_dir"
  elif [ -f "$app_dir/watchapp/package.json" ]; then
    project_dir="$app_dir/watchapp"
  else
    echo "==> Skipping $app_name (no Pebble project found)"
    continue
  fi

  echo "==> Building $app_name"
  (cd "$project_dir" && pebble build)
  mkdir -p "$app_dir/dist"
  # waf names the bundle after the project directory, which is not always the
  # app name, so glob for it rather than assuming.
  cp "$project_dir"/build/*.pbw "$app_dir/dist/$app_name.pbw"
done
