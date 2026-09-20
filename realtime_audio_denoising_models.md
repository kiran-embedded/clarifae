# Open-Source Real-Time AI Audio Denoising Models

This document covers three practical open-source model families for real-time speech enhancement and noise suppression: RNNoise, DeepFilterNet, and DTLN.[cite:16][cite:21] Each can be used as a progressively stronger mode for increasing clarity, moving from very low compute and low latency toward higher quality and more configurable enhancement.[cite:16][cite:21]

## Overview

The three model families differ mainly in architecture, deployment stack, latency budget, sample-rate assumptions, and ease of customization.[cite:16][cite:21] RNNoise is a compact recurrent neural network library in C aimed at real-time noise reduction, DeepFilterNet is a full-band speech-enhancement framework built around deep filtering with real-time and plugin paths, and DTLN is a TensorFlow-based dual-signal-transformation LSTM model with pretrained SavedModel, TF Lite, and ONNX export paths.[cite:16][cite:21][cite:26]

## Three clarity modes

A practical product mapping is to expose the three projects as quality modes instead of as raw model names.[cite:16][cite:21]

| Mode | Model family | Best use | Why it fits |
|---|---|---|---|
| Mode 1: Low latency | RNNoise | Live calls, embedded systems, CPU-constrained devices | Small C library, RNN-based, designed for real-time suppression.[cite:16] |
| Mode 2: Balanced | DTLN | Apps that need better denoising with portable inference formats | TensorFlow implementation with TF Lite, ONNX, and real-time processing support.[cite:21][cite:26] |
| Mode 3: Maximum clarity | DeepFilterNet | Desktop, Linux audio chains, higher-quality speech enhancement | Full-band 48 kHz enhancement, pretrained models, real-time version, LADSPA plugin, Python and Rust tooling.[cite:1] |

This ordering is based on deployment complexity and likely enhancement strength rather than on a universal benchmark hierarchy, because the repositories emphasize different trade-offs and target environments.[cite:16][cite:1][cite:21]

## RNNoise

### What it is

RNNoise is an open-source recurrent neural network library for audio noise reduction maintained by Xiph.[cite:16] The repository describes it as a C-based project for real-time audio noise reduction and distributes it under the BSD-3-Clause license.[cite:16][cite:19]

- Repository: [xiph/rnnoise](https://github.com/xiph/rnnoise) [cite:16]
- License: BSD-3-Clause [cite:16]
- Primary language: C, with additional Python and build files in the repository.[cite:16]
- Release noted in the repository view: RNNoise 0.2, dated April 15, 2024.[cite:1]

### Why it is useful

RNNoise is the easiest of the three to embed directly into native audio pipelines because it is small, C-based, and focused specifically on real-time suppression.[cite:16] It is a strong fit for VoIP, streaming microphones, Android native layers, desktop plugins, and WebRTC-adjacent audio preprocessing where deterministic low overhead matters more than squeezing out the last bit of enhancement quality.[cite:16][cite:18]

### Build and integration

The repository is a source project on GitHub and is commonly integrated as a native library in applications or exposed through plugins that wrap the core denoiser.[cite:16][cite:18] One well-known plugin wrapper is `werman/noise-suppression-for-voice`, which provides a real-time voice plugin based on Xiph's RNNoise for user-facing audio applications.[cite:18]

Typical integration paths:

- Native C/C++ application or service that links RNNoise directly.[cite:16]
- Audio plugin or filter chain via wrappers built on top of RNNoise.[cite:18]
- Mobile integration through JNI or NDK if building Android audio capture pipelines.[cite:16]

### Technical profile

RNNoise uses a recurrent neural network architecture for suppression, which is much lighter than many modern full-band enhancement networks.[cite:16] In practice, that usually makes it the safest choice when CPU headroom is tight or when latency spikes are unacceptable.[cite:16]

### Strengths

- Small native footprint and simple deployment path.[cite:16]
- Mature and widely reused in voice-noise-suppression projects.[cite:16][cite:18]
- Permissive BSD-3-Clause license suitable for commercial integration.[cite:16]

### Limitations

- The repository description is concise and exposes fewer training and evaluation details than DeepFilterNet or DTLN.[cite:16]
- It is best understood as a practical voice denoiser rather than a full research framework for broad speech-enhancement experimentation.[cite:16]
- Quality may be lower than heavier modern enhancement models in difficult noise conditions, especially when compared with newer full-band or multi-stage approaches.[cite:1][cite:21]

## DeepFilterNet

### What it is

DeepFilterNet is a low-complexity speech-enhancement framework for full-band audio at 48 kHz, based on deep filtering.[cite:1] The repository includes training, evaluation, pretrained model weights, Rust components, Python wrappers, and a LADSPA plugin for real-time noise suppression.[cite:1]

- Repository: [Rikorose/DeepFilterNet](https://github.com/Rikorose/DeepFilterNet) [cite:1]
- License: dual MIT or Apache-2.0, at the user's option.[cite:1]
- Primary languages: Python and Rust.[cite:1]
- Designed for Linux, macOS, and Windows; training is noted as tested under Linux.[cite:1]

### Model family and variants

The project documents multiple generations, including DeepFilterNet, DeepFilterNet2, and a later real-time perceptually motivated version referenced in the repository news section.[cite:1] The repository states that the default pretrained model loaded by the Python tooling is DeepFilterNet2 unless another model is specified.[cite:1]

### Framework layout

The repository is structured into several subcomponents.[cite:1]

- `libDF`: Rust code for data loading and augmentation.[cite:1]
- `DeepFilterNet`: training, evaluation, visualization, and pretrained model code.[cite:1]
- `pyDF`: Python wrapper for the libDF STFT/ISTFT loop.[cite:1]
- `pyDF-data`: Python wrapper for dataset functionality and a PyTorch data loader.[cite:1]
- `ladspa`: real-time LADSPA plugin for noise suppression.[cite:1]
- `models`: pretrained models usable by the Python and Rust toolchains.[cite:1]

### Real-time and offline usage

The project provides a precompiled `deep-filter` binary for suppressing noise in WAV files and also exposes a LADSPA plugin with PipeWire filter-chain integration for real-time microphone cleanup.[cite:1] The repository notes that the `deep-filter` binary currently supports WAV files sampled at 48 kHz.[cite:1]

Example CLI usage from the repository:

```bash
deep-filter audio-file.wav
```

Relevant options include model selection, delay compensation, output directory selection, and an optional postfilter.[cite:1]

### Python install

The documented Python install path uses PyTorch first, then the package itself.[cite:1]

```bash
pip install torch torchaudio -f https://download.pytorch.org/whl/cpu/torch_stable.html
pip install deepfilternet
```

For training support on Linux, the repository documents an extra install target.[cite:1]

```bash
pip install deepfilternet[train]
```

### Manual build

For source builds, the repository documents Rust, maturin, poetry, and PyTorch as the main requirements.[cite:1]

```bash
cd path/to/DeepFilterNet/
pip install torch torchaudio -f https://download.pytorch.org/whl/cpu/torch_stable.html
pip install maturin poetry
poetry -C DeepFilterNet install -E train -E eval
maturin develop --release -m pyDF/Cargo.toml
```

Optional dataset tooling is built through `pyDF-data`, with HDF5 development headers needed in some cases.[cite:1]

### Python API

The repository exposes a simple Python entry point for enhancement.[cite:1]

```python
from df import enhance, init_df

model, df_state, _ = init_df()
enhanced_audio = enhance(model, df_state, noisy_audio)
```

This makes DeepFilterNet attractive when a Python-based inference service or research pipeline is already in place.[cite:1]

### Training pipeline

Training starts from `DeepFilterNet/df/train.py` and expects HDF5 datasets plus a dataset configuration JSON file.[cite:1] The repository also documents a helper script for preparing speech, noise, and RIR datasets at 48 kHz into HDF5 format before training.[cite:1]

Example data preparation command:

```bash
python df/scripts/prepare_data.py --sr 48000 speech training_set.txt TRAIN_SET_SPEECH.hdf5
```

Example train command:

```bash
python df/train.py path/to/dataset.cfg path/to/data_dir/ path/to/base_dir/
```

### Strengths

- Strong documentation and a full training-to-deployment framework.[cite:1]
- Full-band 48 kHz focus, which is attractive when preserving more audio detail matters.[cite:1]
- Multiple deployment paths: Python, Rust, precompiled binary, LADSPA plugin, and PipeWire integration.[cite:1]
- Permissive licensing suitable for product use.[cite:1]

### Limitations

- Heavier build and runtime stack than RNNoise, especially if using PyTorch or source builds.[cite:1]
- The documented `deep-filter` binary currently targets 48 kHz WAV input, so resampling or pipeline normalization may be needed in products that ingest mixed sample rates.[cite:1]
- More operational complexity than DTLN if the goal is just exporting a compact TF Lite model to mobile hardware.[cite:1][cite:21]

## DTLN

### What it is

DTLN is a TensorFlow 2.x implementation of the DTLN real-time speech-denoising model, and the repository explicitly states that it includes TF Lite, ONNX, and real-time audio-processing support.[cite:21][cite:26] This makes it the most straightforward of the three when the deployment target prefers TensorFlow-family runtimes or ONNX-based inference.[cite:21][cite:26]

- Repository: [breizhn/DTLN](https://github.com/breizhn/DTLN) [cite:21]
- Web wrapper: [sapphi-red/DTLN-web](https://github.com/sapphi-red/DTLN-web) [cite:13]
- Core stack: TensorFlow 2.x.[cite:21]
- Export/deployment paths: SavedModel, TF Lite, and ONNX.[cite:21]

### Why it is useful

DTLN is especially attractive for shipping to browsers, mobile, or portable runtimes because its repository explicitly includes pretrained models and real-time processing paths beyond raw training code.[cite:21] The `DTLN-web` project shows that the model family has already been adapted for web deployment, including real-time speech denoising in browser environments.[cite:13]

### Build and runtime profile

The repository description emphasizes training, inference, and serving in Python, plus pretrained models in SavedModel, TF Lite, and ONNX formats.[cite:21] That means the project is useful both as a research base and as a conversion source for product inference targets.[cite:21]

### Real-time paths

The search results specifically reference a `real_time_processing_tf_lite.py` file in the repository, which indicates an implemented TF Lite real-time processing path rather than only an offline notebook or training script.[cite:24] The repository also includes `run_training.py` and the model definition file `DTLN_model.py`, which signals a full train-and-serve workflow.[cite:22][cite:25]

### Browser path

`DTLN-web` packages the model for JavaScript use and documents installation through npm.[cite:13]

```bash
npm i @sapphi-red/dtln-web
```

That web wrapper is a strong indicator that DTLN is viable for low-latency browser-based demos or production-grade front-end denoising experiments.[cite:13]

### Related AEC model

The same model family has a separate DTLN-AEC repository for real-time acoustic echo cancellation.[cite:23] That repository documents three pretrained model sizes: 128 LSTM units per layer at 1.8 million parameters, 256 units at 3.9 million parameters, and 512 units at 10.4 million parameters.[cite:23]

Although DTLN-AEC is a different repository from the denoising model itself, it is relevant if the product goal is live communication rather than pure denoising, because echo cancellation and noise suppression are often paired in the same real-time voice path.[cite:23]

### Strengths

- TensorFlow 2.x training and inference stack with export paths that are practical for deployment.[cite:21]
- TF Lite and ONNX support are explicitly called out by the project description.[cite:21][cite:26]
- Real-time and web usage examples exist through the main repository and DTLN-web wrapper.[cite:24][cite:13]

### Limitations

- The available repository snippets provide less immediate detail about licensing and exact install commands than DeepFilterNet's README does.[cite:21]
- The ecosystem is somewhat split across the main DTLN repo, DTLN-web, and DTLN-AEC, so product teams may need to assemble the exact runtime path themselves.[cite:21][cite:13][cite:23]

## Build matrix

| Area | RNNoise | DTLN | DeepFilterNet |
|---|---|---|---|
| Main repo | [xiph/rnnoise](https://github.com/xiph/rnnoise) [cite:16] | [breizhn/DTLN](https://github.com/breizhn/DTLN) [cite:21] | [Rikorose/DeepFilterNet](https://github.com/Rikorose/DeepFilterNet) [cite:1] |
| Main language | C [cite:16] | TensorFlow/Python [cite:21] | Python + Rust [cite:1] |
| Real-time focus | Yes [cite:16] | Yes [cite:21][cite:24] | Yes [cite:1] |
| Browser path | Indirect or wrapper-based [cite:18] | Yes, via DTLN-web [cite:13] | Not the primary documented path [cite:1] |
| Mobile-friendly export | Requires custom wrapping [cite:16] | TF Lite and ONNX available [cite:21] | Possible, but not the most direct path [cite:1] |
| Training framework | Limited repo-level detail in snippet [cite:16] | Yes [cite:21][cite:25] | Yes, with documented data prep and train entry points [cite:1] |
| License | BSD-3-Clause [cite:16] | Check repository before shipping; snippet does not confirm it [cite:21] | MIT or Apache-2.0 [cite:1] |

## Recommended implementation strategy

### Mode 1: RNNoise

Use this when latency, power draw, and implementation simplicity dominate quality requirements.[cite:16] This is the safest baseline for telephony-style cleanup, embedded voice capture, and always-on microphone preprocessing.[cite:16][cite:18]

### Mode 2: DTLN

Use this when the product needs stronger denoising than RNNoise while keeping an export path to TF Lite or ONNX.[cite:21][cite:26] This is a practical middle mode for Android apps, browser experiments, and products that already have TensorFlow or ONNX runtimes in place.[cite:13][cite:21]

### Mode 3: DeepFilterNet

Use this when perceived clarity is the priority and the system can afford a somewhat richer runtime stack.[cite:1] It is especially compelling on Linux desktops, creator tools, or applications that can exploit the project's PipeWire and LADSPA real-time ecosystem.[cite:1]

## Build prerequisites by stack

### RNNoise

- C toolchain for native builds.[cite:16]
- Application-side audio I/O and buffering integration.[cite:16]
- Optional plugin wrappers if building a user-facing DAW or desktop effect path.[cite:18]

### DTLN

- Python environment for training or conversion tasks.[cite:21]
- TensorFlow 2.x runtime.[cite:21]
- Optional TF Lite or ONNX inference target depending on deployment environment.[cite:21][cite:26]
- Optional npm and browser audio integration if using DTLN-web.[cite:13]

### DeepFilterNet

- Python environment.[cite:1]
- PyTorch and torchaudio.[cite:1]
- Rust toolchain through rustup/cargo for source builds and Rust components.[cite:1]
- `maturin` and `poetry` for source installation.[cite:1]
- Optional HDF5 development headers for dataset tooling.[cite:1]

## What to choose

For a production system exposing three clarity modes, the cleanest mapping is RNNoise for ultra-low-latency mode, DTLN for balanced mode, and DeepFilterNet for high-clarity mode.[cite:16][cite:21][cite:1] That structure gives clear progression in quality and deployment cost while staying inside open-source model families that already have practical real-time usage paths.[cite:16][cite:21][cite:1]

## Repo links

- RNNoise: [https://github.com/xiph/rnnoise](https://github.com/xiph/rnnoise) [cite:16]
- DeepFilterNet: [https://github.com/Rikorose/DeepFilterNet](https://github.com/Rikorose/DeepFilterNet) [cite:1]
- DTLN: [https://github.com/breizhn/DTLN](https://github.com/breizhn/DTLN) [cite:21]
- DTLN-web: [https://github.com/sapphi-red/DTLN-web](https://github.com/sapphi-red/DTLN-web) [cite:13]
- RNNoise voice plugin: [https://github.com/werman/noise-suppression-for-voice](https://github.com/werman/noise-suppression-for-voice) [cite:18]
- DTLN-AEC: [https://github.com/breizhn/DTLN-aec](https://github.com/breizhn/DTLN-aec) [cite:23]

## Build checklists

### RNNoise checklist

1. Clone the repository and inspect the build instructions in the repo root.[cite:16]
2. Build the native library with a C toolchain suitable for the target OS.[cite:16]
3. Add frame buffering, model state management, and streaming audio glue in the host application.[cite:16]
4. Benchmark CPU cost under worst-case concurrent audio load before shipping.[cite:16]

### DTLN checklist

1. Clone the DTLN repository and install its TensorFlow-based dependencies.[cite:21]
2. Choose a deployment target: Python runtime, TF Lite, ONNX, or browser via DTLN-web.[cite:21][cite:13]
3. Test the real-time TF Lite processing path and validate end-to-end latency with actual microphone chunk sizes.[cite:24]
4. If needed, extend the model pipeline with echo cancellation using DTLN-AEC.[cite:23]

### DeepFilterNet checklist

1. Decide whether to use the prebuilt binary, Python wheel, or source build.[cite:1]
2. Normalize incoming audio to the expected 48 kHz path if using the documented binary workflow.[cite:1]
3. For Python-based integration, install PyTorch and `deepfilternet`, then validate enhancement quality with the default DeepFilterNet2 model.[cite:1]
4. For Linux realtime mic cleanup, evaluate the LADSPA and PipeWire filter-chain path.[cite:1]
5. If custom training is required, prepare HDF5 speech, noise, and RIR datasets and train through `df/train.py`.[cite:1]
