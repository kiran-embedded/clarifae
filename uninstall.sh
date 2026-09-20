#!/system/bin/sh
# uninstall.sh — runs when the module is removed.
#
# The module overlay (the patched audio_effects.xml, the soundfx library, and
# system/bin/clarifae-ctl) is unmounted/removed automatically with the module,
# which fully reverts the audio_effects change (the original device file is
# untouched — we only shadowed it). Here we just clean our /data state.
CLARIFAE_DIR=/data/adb/clarifae

# keep a backup of the user's config for convenience
[ -f "$CLARIFAE_DIR/config.conf" ] && cp -f "$CLARIFAE_DIR/config.conf" /data/adb/clarifae-config.bak 2>/dev/null

rm -rf "$CLARIFAE_DIR"

# clear persisted properties
for k in enabled in_enabled out_enabled mode strength out_strength atten_db vad targets out_targets highpass autogain loglevel; do
  if command -v resetprop >/dev/null 2>&1; then
    resetprop --delete "persist.clarifae.$k" 2>/dev/null
  fi
done
