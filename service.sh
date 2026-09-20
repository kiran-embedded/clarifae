#!/system/bin/sh
# service.sh — late_start service. Apply persisted config once boot completes so
# the effect always sees the user's latest settings.
MODDIR=${0%/*}

# wait for boot
i=0
while [ "$(getprop sys.boot_completed)" != "1" ] && [ $i -lt 120 ]; do
  sleep 2; i=$((i + 1))
done

if [ -f "$MODDIR/common/functions.sh" ]; then
  . "$MODDIR/common/functions.sh"
elif [ -f /data/adb/clarifae/functions.sh ]; then
  . /data/adb/clarifae/functions.sh
fi

conf_init
prop_sync
clarifae_log "service.sh: applied config at boot (mode=$(conf_get mode) enabled=$(conf_get enabled))"
