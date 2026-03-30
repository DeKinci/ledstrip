#!/bin/bash
set -e
cd "$(dirname "$0")"

npx esbuild ledstrip-widgets.ts \
  --bundle --minify \
  --outfile=../rsc/microproto-ui.js \
  --format=iife \
  --target=es2020

echo "Built: $(wc -c < ../rsc/microproto-ui.js | tr -d ' ') bytes → rsc/microproto-ui.js"
