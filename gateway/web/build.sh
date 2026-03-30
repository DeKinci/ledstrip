#!/bin/bash
set -e
cd "$(dirname "$0")"

# Sync shared JS client from source
cp ../../rsc/microproto-client.js ../internal/web/static/microproto-client.js

npx esbuild gateway-widgets.ts \
  --bundle --minify \
  --outfile=../internal/web/static/microproto-ui.js \
  --format=iife \
  --target=es2020

echo "Built: $(wc -c < ../internal/web/static/microproto-ui.js | tr -d ' ') bytes → gateway/internal/web/static/microproto-ui.js"
