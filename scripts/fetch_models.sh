#!/usr/bin/env bash
# fetch_models.sh — stage OPTIONAL model assets for the experimental backends
# into models/, which customize.sh copies to /data/adb/clarifae/models/ on flash.
# These are NOT required for the default RNNoise engine. Always exits 0 — missing
# optional models are a warning, not a failure.
set -uo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
MODELS="$HERE/models"
mkdir -p "$MODELS/dtln" "$MODELS/df"

echo "== DTLN (mode 2, experimental) =="
# Pretrained TFLite models live in breizhn/DTLN/pretrained_model/.
DTLN_BASE="https://github.com/breizhn/DTLN/raw/master/pretrained_model"
for f in model_1.tflite model_2.tflite; do
  if [ -f "$MODELS/dtln/$f" ]; then
    echo "  have $f"
  else
    echo "  fetching $f ..."
    if ! curl -fL --retry 3 -o "$MODELS/dtln/$f" "$DTLN_BASE/$f" 2>/dev/null; then
      echo "  !! could not fetch $f — get it manually from https://github.com/breizhn/DTLN"
      rm -f "$MODELS/dtln/$f"
    fi
  fi
done

echo "== DeepFilterNet (mode 3, experimental) =="
echo "  DeepFilterNet ships an exported model with the Python package / releases."
echo "  Place the exported model archive here:"
echo "      $MODELS/df/DeepFilterNet3_onnx.tar.gz"
echo "  and build libdf for Android (see INSTALL.md). Repo: https://github.com/Rikorose/DeepFilterNet"

echo
echo ">> Done. Staged models under: $MODELS (copied to /data/adb/clarifae/models on flash)"
exit 0
