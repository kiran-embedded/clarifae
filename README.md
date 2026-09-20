<div align="center">
  <h1>✨ Clarifae (Redmi Note 10 Edition) ✨</h1>
  <p><strong>Real-Time AI Microphone Clarity & Noise Cancellation</strong></p>
  <p><i>A premium fork featuring RNNoise integration, hardware-specific mic fixes, and a live Web UI.</i></p>

  <p>
    <a href="https://github.com/kiran-embedded/clarifae/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/kiran-embedded/clarifae?style=for-the-badge&color=success"></a>
    <img alt="Platform" src="https://img.shields.io/badge/Platform-Android_KernelSU-blue?style=for-the-badge">
    <img alt="License" src="https://img.shields.io/badge/License-MIT-purple?style=for-the-badge">
  </p>
</div>

---

## 🌟 Overview

Clarifae is an advanced, lightweight Android audio pre-processing module that uses Artificial Intelligence (RNNoise) to completely isolate your voice and eliminate background noise during phone calls, VoIP calls (WhatsApp, Discord), and voice recordings. 

This specific fork is highly optimized and **hardware-patched for the Redmi Note 10 (and similar Xiaomi devices)** to fix the notorious "bottom mic echo / top mic swap" issue, delivering crystal clear, loud, and echo-free audio.

## 🛠️ Key Fixes & Features in this Fork

* 📱 **Redmi Note 10 Hardware Mic Fix:** Standard Android ROMs often invert the stereo input arrays `[Ch0, Ch1]` for the top and bottom microphones on Redmi devices. This caused the AI to accidentally isolate the top microphone (background noise) instead of the bottom microphone (your voice), leading to severe echoing. We intercept the audio buffers in C and perfectly swap the channels milliseconds before inference. Your voice is now 100% isolated and crystal clear.
* ⚡ **Live-Streaming Web UI Logs:** Completely rewrote the KernelSU Web UI logging system. Logs now auto-refresh and stream live in real-time without crashing or freezing. 
* 🔔 **Clear Activation Indicators:** The module now explicitly prints high-level warning logs (`▶ Clarifae Activated: Call/Recording started`) to the logcat the exact second you receive a call, so you never have to guess if the AI is actively running in the background.
* 🎨 **Premium Flashing UI:** Upgraded the Magisk/KernelSU flashing animation with sleek Unicode UI elements.

## 📦 Downloads (v1.0-Initial)

We offer two different builds depending on your needs. Check the **[Releases](https://github.com/kiran-embedded/clarifae/releases)** page to download them!

1. **Clarifae Pro (40MB)** - *(Recommended)*
   Contains the massive, uncompressed, official RNNoise AI training dataset. It provides the absolute highest quality noise-cancellation and voice isolation. 
2. **Clarifae Lite (15MB)** - *(Battery Saver)*
   Uses a stripped-down, compressed AI model. Slightly lower voice quality, but uses less storage and less CPU for extended VoIP gaming sessions.

## 🚀 How to Install

1. Download the latest `.zip` release from the Releases tab.
2. Open **KernelSU**, **Magisk**, or **APatch**.
3. Go to the Modules tab and click **Install from storage**.
4. Select the `.zip` file and wait for the premium flashing animation to complete.
5. **Reboot your phone.**
6. Open your root manager, click on Clarifae, and open the Web UI to configure the AI!

## 🙏 Credits & Acknowledgments

* **Modded & Fixed By:** [@kiran-embedded](https://github.com/kiran-embedded)
* **Original Creator:** [@Profazia](https://github.com/Profazia) — Massive credit to Profazia for creating the original Clarifae foundation.
* **AI Engine:** [RNNoise by Xiph.org](https://jmvalin.ca/demo/rnnoise/)

---
<div align="center">
  <i>If this module helped fix your microphone, please leave a ⭐ on the repository!</i>
</div>
