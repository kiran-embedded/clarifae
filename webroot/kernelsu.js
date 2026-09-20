/*
 * kernelsu.js — thin bridge over the KernelSU / APatch / MMRL WebUI host API.
 *
 * The host injects a global `ksu` object with:
 *     ksu.exec(command, optionsJson, callbackName)
 *        -> later calls window[callbackName](errno, stdout, stderr)
 *     ksu.toast(message)
 *
 * We wrap exec() in a Promise. When no host is present (e.g. opened in a desktop
 * browser for design work) we fall back to a mock so the page still renders.
 *
 * Exposes window.ClarifaeKSU = { exec, toast, isMock }.
 */
(function () {
  'use strict';

  var hasHost = typeof window.ksu === 'object' && typeof window.ksu.exec === 'function';

  function genCb() {
    return 'clf_cb_' + Math.random().toString(36).slice(2) + '_' + Date.now().toString(36);
  }

  function exec(command, options) {
    options = options || {};
    return new Promise(function (resolve) {
      if (!hasHost) { resolve(mockExec(command)); return; }
      var cb = genCb();
      var done = false;
      window[cb] = function (errno, stdout, stderr) {
        if (done) return;
        done = true;
        try { delete window[cb]; } catch (e) { window[cb] = undefined; }
        resolve({ errno: Number(errno) || 0, stdout: stdout || '', stderr: stderr || '' });
      };
      try {
        window.ksu.exec(command, JSON.stringify(options), cb);
      } catch (e) {
        try { delete window[cb]; } catch (_) {}
        resolve({ errno: -1, stdout: '', stderr: String(e) });
      }
      // safety timeout so the UI never hangs forever
      setTimeout(function () {
        if (!done) { done = true; resolve({ errno: -2, stdout: '', stderr: 'exec timeout' }); }
      }, 8000);
    });
  }

  function toast(msg) {
    if (hasHost && typeof window.ksu.toast === 'function') {
      try { window.ksu.toast(msg); return; } catch (e) { /* fall through */ }
    }
    console.log('[toast] ' + msg);
  }

  // ---- design-preview mock (only used outside a root WebUI host) ----
  function mockExec(command) {
    var out = '';
    if (command.indexOf('status-json') !== -1) {
      out = JSON.stringify({
        enabled: true, mode: 1, mode_name: 'RNNoise (Low latency)',
        strength: 70, atten_db: 24, vad: 50, targets: 'mic,voice_comm',
        highpass: true, autogain: false, loglevel: 1,
        lib32: false, lib64: true, xml_patched: true,
        model_rnnoise: true, model_dtln: false, model_df: false,
        audioserver: true, effect_loaded: false, version: 'v1.0.0 (preview)'
      });
    } else if (command.indexOf('get-json') !== -1) {
      out = JSON.stringify({
        enabled: true, mode: 1, mode_name: 'RNNoise (Low latency)',
        strength: 70, atten_db: 24, vad: 50, targets: 'mic,voice_comm',
        highpass: true, autogain: false, loglevel: 1
      });
    } else if (command.indexOf(' log') !== -1) {
      out = '[preview] no device — clarifae-ctl log unavailable.';
    } else {
      out = 'ok (preview)';
    }
    return Promise.resolve({ errno: 0, stdout: out, stderr: '' });
  }

  window.ClarifaeKSU = { exec: exec, toast: toast, isMock: !hasHost };
})();
