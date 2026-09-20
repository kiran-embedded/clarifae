#!/usr/bin/env bash
# fetch_deps.sh — make native/third_party/rnnoise ready to build.
#
# Two sources, in order of preference:
#   1. A local RNNoise RELEASE tarball (rnnoise-*.tar.gz) you dropped in the
#      module dir or third_party/. Release tarballs bundle the trained weights
#      (src/rnnoise_data.c), so this avoids the large model download entirely.
#      Get one from: https://github.com/xiph/rnnoise/releases
#   2. Otherwise: git clone xiph/rnnoise + a resumable, self-verifying download
#      of the model weights.
#
# Idempotent. Re-run with --force to refresh.
set -euo pipefail

HERE="$(cd "$(dirname "$0")/.." && pwd)"
TP="$HERE/native/third_party"
RNN="$TP/rnnoise"
FORCE="${1:-}"
mkdir -p "$TP"

ensure_os_support() {
  # RNNoise release tarballs omit src/os_support.h (it lives in the repo). The
  # NEON/SSE path #includes it; common.h already provides OPUS_INLINE/OPUS_CLEAR,
  # so a tiny shim is enough. (Skipped when the real header is present.)
  [ -f "$RNN/src/os_support.h" ] && return 0
  echo ">> Adding os_support.h shim (release tarball omits it)"
  cat > "$RNN/src/os_support.h" <<'EOF'
/* os_support.h — minimal shim for RNNoise release tarballs that omit it.
 * common.h already defines OPUS_INLINE/OPUS_CLEAR; this just satisfies the
 * #include and the few extra Opus helpers the SIMD path references. */
#ifndef OS_SUPPORT_H
#define OS_SUPPORT_H
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifndef OPUS_GNUC_PREREQ
# if defined(__GNUC__) && defined(__GNUC_MINOR__)
#  define OPUS_GNUC_PREREQ(maj,min) ((__GNUC__<<16)+__GNUC_MINOR__ >= ((maj)<<16)+(min))
# else
#  define OPUS_GNUC_PREREQ(maj,min) 0
# endif
#endif
#ifndef OPUS_INLINE
# define OPUS_INLINE inline
#endif
#ifndef OPUS_CLEAR
# define OPUS_CLEAR(dst,n) (memset((dst),0,(n)*sizeof(*(dst))))
#endif
#ifndef OPUS_COPY
# define OPUS_COPY(dst,src,n) (memcpy((dst),(src),(n)*sizeof(*(dst))))
#endif
#ifndef OPUS_MOVE
# define OPUS_MOVE(dst,src,n) (memmove((dst),(src),(n)*sizeof(*(dst))))
#endif
static OPUS_INLINE void *opus_alloc(size_t size){return malloc(size);}
static OPUS_INLINE void opus_free(void *ptr){free(ptr);}
#endif
EOF
}

# resumable, self-verifying weights download (model_version == sha256 of tarball)
fetch_weights() {
  [ -f "$RNN/model_version" ] || { ( cd "$RNN" && sh download_model.sh ); return; }
  hash="$(cat "$RNN/model_version")"
  model="rnnoise_data-$hash.tar.gz"
  url="https://media.xiph.org/rnnoise/models/$model"
  ( cd "$RNN" || exit 1
    prev=0; ok=0; i=0
    while [ "$i" -lt 12 ]; do
      i=$((i + 1))
      echo "   download attempt $i ..."
      if command -v wget >/dev/null 2>&1; then
        wget -c -t 2 --timeout=90 -O "$model" "$url" || true
      else
        curl -fL -C - --retry 2 --connect-timeout 60 --max-time 1800 -o "$model" "$url" || true
      fi
      now=$(stat -c%s "$model" 2>/dev/null || echo 0)
      if command -v sha256sum >/dev/null 2>&1; then
        sum=$(sha256sum "$model" 2>/dev/null | awk '{print $1}')
        [ "$sum" = "$hash" ] && { ok=1; break; }
        if [ "$now" -le "$prev" ]; then echo "   checksum bad, no progress -> restarting fresh"; rm -f "$model"; now=0; fi
      else
        tar tzf "$model" >/dev/null 2>&1 && { ok=1; break; }
      fi
      prev=$now
      sleep 3
    done
    if [ "$ok" = 1 ]; then
      tar xzof "$model" && echo ">> Verified + extracted RNNoise weights."
    else
      echo "!! Could not fully download/verify $model (network)." >&2
      echo "   Tip: download a release tarball from github.com/xiph/rnnoise/releases" >&2
      echo "   into this module dir and re-run — it bundles the weights." >&2
      return 1
    fi
  )
}

# --- 1. local release tarball? ---
REL="$(ls "$HERE"/rnnoise-*.tar.gz "$TP"/rnnoise-*.tar.gz 2>/dev/null | head -1 || true)"
if [ -n "$REL" ] && { [ ! -f "$RNN/src/rnnoise_data.c" ] || [ "$FORCE" = "--force" ]; }; then
  echo ">> Using local RNNoise release tarball: $REL"
  rm -rf "$RNN"
  rm -rf "$TP"/rnnoise-*/
  tar xzf "$REL" -C "$TP"
  d="$(ls -d "$TP"/rnnoise-*/ 2>/dev/null | head -1 || true)"
  [ -n "$d" ] && [ "$d" != "$RNN/" ] && mv "$d" "$RNN"
fi

# --- 2. else clone the repo ---
if [ ! -f "$RNN/include/rnnoise.h" ]; then
  echo ">> Cloning xiph/rnnoise ..."
  rm -rf "$RNN"
  git clone --depth 1 https://github.com/xiph/rnnoise.git "$RNN"
fi

[ -f "$RNN/include/rnnoise.h" ] || { echo "!! RNNoise source not available." >&2; exit 1; }

# --- 3. weights (skip if already bundled by a release tarball) ---
if [ ! -f "$RNN/src/rnnoise_data.c" ] || [ "$FORCE" = "--force" ]; then
  echo ">> Fetching RNNoise model weights (resumable)..."
  [ "$FORCE" = "--force" ] && rm -f "$RNN"/rnnoise_data-*.tar.gz
  fetch_weights || true
fi

# --- 4. header shim (release tarballs omit os_support.h) ---
ensure_os_support

if [ -f "$RNN/src/rnnoise_data.c" ]; then
  echo ">> RNNoise ready: $RNN (weights present)"
else
  echo "!! WARNING: rnnoise_data.c missing — the link step will fail until weights are fetched." >&2
fi
