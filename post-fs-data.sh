#!/system/bin/sh
# post-fs-data.sh — early boot. Ensure the Clarifae data dir exists with safe
# perms before anything reads it. Keep this lightweight (runs before full boot).
MODDIR=${0%/*}
DATADIR=/data/adb/clarifae

mkdir -p "$DATADIR/models/dtln" "$DATADIR/models/df"
chmod 0700 "$DATADIR" 2>/dev/null

# Keep copies of the helper library and the CLI in the data dir so they are
# reachable by absolute path (the WebUI calls clarifae-ctl this way, since the
# system/bin overlay isn't reliably on the WebUI shell's PATH).
[ -f "$MODDIR/common/functions.sh" ] && cp -f "$MODDIR/common/functions.sh" "$DATADIR/functions.sh"
[ -f "$MODDIR/common/clarifae-ctl" ] && { cp -f "$MODDIR/common/clarifae-ctl" "$DATADIR/clarifae-ctl"; chmod 0755 "$DATADIR/clarifae-ctl"; }
