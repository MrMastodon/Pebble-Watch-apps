#!/usr/bin/env bash
# Builds every app under apps/ and copies the resulting .pbw into
# apps/<app>/dist/<app>.pbw, ready to link to from the README.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

for app_dir in "$repo_root"/apps/*/; do
  app_name="$(basename "$app_dir")"
  echo "==> Building $app_name"
  (cd "$app_dir" && pebble build)
  mkdir -p "$app_dir/dist"
  cp "$app_dir/build/$app_name.pbw" "$app_dir/dist/$app_name.pbw"
done
