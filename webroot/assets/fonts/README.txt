Clarifae brand font — "Creative Caps"
=====================================

The Clarifae wordmark uses "Brandmark-Creative-Caps" (brand.txt: Font = Creative
Caps). That is a proprietary brandmark font and is NOT bundled here — the
wordmark ships instead as the vector logo (../logo.svg / ../logo-white.svg),
which always renders correctly without the font installed.

The WebUI references the font family "Creative Caps" via @font-face in style.css
for headings, with a system-ui / -apple-system fallback. To enable the real
brand font for the UI text, drop a web font file here named:

    CreativeCaps.woff2

and it will be picked up automatically (no CSS changes needed). Without it, the
UI falls back to the system sans-serif and the logo image is unaffected.
