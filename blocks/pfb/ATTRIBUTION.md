# Attribution

This module includes code ported and adapted from GNU Radio 3 (GNU Radio) under the GNU GPL.

Upstream project:
- GNU Radio (GR3)
- Source repository: https://github.com/gnuradio/gnuradio (historical GR3 codebase)

Ported/adapted files:
- `PfbArbResamplerKernel.hpp`  ← based on GR3 `pfb_arb_resampler.cc` / `pfb_arb_resampler.h`
- `PfbArbResamplerTaps.hpp`    ← based on GR3 `pfb.py` (`arb_resampler_*::create_taps`)
- `PfbFirdes.hpp`              ← based on GR3 `firdes.cc` / `firdes.h` (low_pass_2 + helpers)
- `PfbWindow.hpp`              ← based on GR3 `window.cc` / `window.h` (Blackman‑Harris window)
- `PfbOptfir.hpp`              ← based on GR3 `optfir.py` (low_pass, remezord, lporder)
- `PfbRemez.hpp`               ← based on GR3 `pm_remez.cc` / `pm_remez.h`
- `qa_PfbArbResampler.cpp`      ← based on GR3 `qa_pfb_arb_resampler.py`

Notes:
- All code in `blocks/pfb/` is GPL‑3.0‑or‑later, except `PfbRemez.hpp`, which is GPL‑2.0‑or‑later (compatible with GPLv3).
- Modifications include: GR4 block wrapper, templating, C++‑only tap generation, and removal of VOLK usage.
