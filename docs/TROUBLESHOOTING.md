# Clarifae troubleshooting

Start with a snapshot:

```bash
su -c clarifae-ctl status
su -c clarifae-ctl log
```

## The effect doesn't load (`effect: not detected`)

1. **Library present?** `clarifae-ctl status` should show `lib64: installed`.
   If not, the build didn't stage the lib for your ABI — rebuild: `./build.sh` then re-flash.
   Check at runtime: `ls -l /system/lib64/soundfx/libclarifae.so`.
2. **audio_effects patched?** Needs `xml: patched`. Verify the live file references us:
   ```bash
   su -c 'grep -n clarifae /vendor/etc/audio_effects.xml /system/etc/audio_effects.xml 2>/dev/null'
   ```
   If empty, auto-patch failed (non-standard vendor file). Patch manually — see below.
3. **SELinux denial?** `su -c 'dmesg | grep -i "avc.*audioserver" | tail'`. The bundled
   `sepolicy.rule` covers the common cases; some devices need extra rules.
4. **Reload the chain:** effects attach when a capture session *opens*. Open a fresh
   recorder/call, or `su -c clarifae-ctl restart-audio`.

## No audible difference

- Confirm it's enabled and the right source is selected:
  `clarifae-ctl get` → `enabled=1`, and `targets` includes the source your app uses
  (a phone call/VoIP app uses `voice_comm`; a recorder usually uses `mic`).
- Some apps request the **`unprocessed`/raw** source or run their own DSP and bypass
  system pre-processing — those can't be cleaned this way.
- Raise effect strength: `clarifae-ctl set strength 90` (applies to new buffers).
- A handful of devices only bind capture effects at session start — reboot after changing
  `targets`, since the `<preprocess>` routing is fixed at install time.

## Crackling, robotic artefacts, or latency

- Lower the mode to **1 (RNNoise)** — it's the lightest and lowest-latency.
- Reduce `strength` and/or `atten_db` (e.g. `set strength 50`, `set atten_db 12`).
- Turn **auto-gain off** (`set autogain 0`) if you hear pumping in noisy rooms.
- On weak CPUs, modes 2/3 may not keep up in real time; prefer mode 1.

## DTLN / DeepFilterNet "not used" (falls back to RNNoise)

- `clarifae-ctl status-json` shows `model_dtln`/`model_df`. If `false`, the model or runtime
  isn't installed and Clarifae falls back to RNNoise **by design**.
- DTLN needs `libtensorflowlite_c.so` linked at build time (`CLARIFAE_WITH_DTLN=ON`) and
  `model_1.tflite` + `model_2.tflite` in `/data/adb/clarifae/models/dtln/`.
- DeepFilterNet needs `libdf` linked (`CLARIFAE_WITH_DF=ON`) and a model in
  `/data/adb/clarifae/models/df/`. See [INSTALL.md](../INSTALL.md).
- Check `logcat -s Clarifae` for `create failed (...)` messages explaining the fallback.

## WebUI can't run commands

- The WebUI calls `clarifae-ctl` through the root shell. Ensure it's on PATH:
  `su -c 'which clarifae-ctl'` → `/system/bin/clarifae-ctl`.
- On **Magisk**, the stock manager has no WebUI host — use **MMRL**. KernelSU/APatch have it
  built in (Modules → Clarifae → Open).
- If the page shows a **"preview"** badge, it's running without a root WebUI host (e.g. a
  desktop browser) and is using mock data.

## Manually attaching the effect

If auto-patch failed, edit your device's `audio_effects.xml` (back it up first) and add the
fragments from `common/audio_effects.clarifae.xml`:

```xml
<!-- in <libraries> -->   <library name="clarifae" path="libclarifae.so"/>
<!-- in <effects> -->     <effect name="clarifae_ns" library="clarifae" uuid="c1a71fae-0001-4a5e-9b10-43578768aaf3"/>
<!-- in <preprocess> -->  <stream type="voice_communication"><apply effect="clarifae_ns"/></stream>
                          <stream type="mic"><apply effect="clarifae_ns"/></stream>
```

Place the edited copy in the module overlay so it shadows the original, e.g.
`$MODPATH/system/vendor/etc/audio_effects.xml`, then reboot.

## Restoring / removing

- Remove the module in your root manager and reboot — the overlay is dropped and the stock
  `audio_effects.xml` returns automatically (it was never edited in place).
- A backup of your last config is saved to `/data/adb/clarifae-config.bak` on uninstall.

## Collecting logs for a bug report

```bash
su -c clarifae-ctl status
su -c 'logcat -d -s Clarifae'
su -c 'cat /data/adb/clarifae/clarifae.log'
su -c 'cat /data/adb/clarifae/config.conf'
su -c 'getprop | grep clarifae'
```
