# Ember — Part 53: offline Windows package integrity and preflight

Date: 2026-11-24. Repo: https://github.com/ProhorovIlja/Ember
Branch: `embermind-gguf-thinking-compat`
Starting head from Part 52: `1dedea218d50ef7a243a400666489da099ca1b97`

## What changed

1. **Portable ZIP64 content integrity:** previous `tools/portable_zip.py` examined only ZIP entry names after packaging; it could not detect a corrupt model or DLL copied after initial SHA verification. Revised `archive()` writes each source file in 1-MiB blocks, computes a SHA-256 of bytes streamed, compares any model/engine/license again with the existing signed-off `offline-assets.json` (human approval, NOT cryptographic signature), and reopens every ZIP member to check decompressed bytes, CRC, size and SHA-256. It hashes the whole ZIP before promoting the temporary archive to the final path.
2. **Source mutation/size checks:** source file stat/size/mtime/inode are checked around the copy, and asset bytes must match the approved manifest even if a source changed after the first verify. The packager refuses archive output under its own source directory and does not include sensitive files.
3. **Reproducibility:** fixed ZIP64 member timestamps and normal file mode, stable order and no redundant compression of .gguf/.onnx/.bin. Source modification times no longer change ZIP byte output on the same Python/zlib version. Different compressor versions may still differ.
4. **Early Windows collision rejection:** `tools/offline_bundle.py` rejects case-folded file/directory aliases in both actual source inventory and human-reviewed manifests before copying gigabytes. Added synthetic self-tests for same-case and file/parent-folder collisions.
5. **Real failure tests in source:** `tools/portable_zip.py` now contains regression fixtures that build the same ZIP twice after source mtime change, validate identical SHA, deliberately change an archived GGUF byte and require rejection, and refuse ZIP output inside input directory. Existing integrity and private-data cases remain.
6. **Foreground suite:** added `tools/packaging_preflight.py` to run existing/new offline asset, PE import, ZIP64 and disk-spanned Inno tests without models, GPU or Internet. Python optimization (which disables legacy asserts) causes immediate failure. Both user-initiated `release/build-complete-windows.ps1` and standalone `release/build-complete-offline.ps1` invoke it before expensive model copying/build.

## Independent checks in current engineering container

- Live Python 3.13 synthetic reproduction of ZIP64 streaming writes with `ZipInfo`, fixed member date, `force_zip64`, stored GGUF, compressed .exe, deterministic archive SHA across two builds, per-member SHA replay and CRC rejection after one changed model byte: PASS.
- Pure Python synthetic Windows case-folded file/directory namespace collision cases: PASS.
- Python documentation confirms cross-version `ZipFile(..., compresslevel=6)` and `open(..., force_zip64=True)`; an early use of unsupported `ZipInfo.compresslevel` was corrected in a subsequent GitHub commit before finishing.
- Connected GitHub API wrote and reread source changes. This environment has no Qt6, MSVC, Inno Setup, full Windows repo checkout or approved Ember model weights; the committed `tools/packaging_preflight.py` four-suite source tests were **NOT** directly executed here. The manual synthetic reproduction is not a substitute for that check.
- No real LLM/TTS/ASR weights were downloaded, merged, run, or redistributed. No native installer, public release, CI workflow execution, scheduled task or monitoring was created.

## Sequential code commits

- `401d09a177f964050ef7b6aca09b85c131a0909c`: full portable model SHA and ZIP member replay, fixed member metadata.
- `89baaacb425cdf84174e74d178e0cfde12e21bf4`: cross-version Python ZipFile compression level fix.
- `6df7f08bfa2770d253ad430eeab437264387fad9`: deterministic ZIP, corrupted-byte and in-tree output regression tests.
- `6c7ecc21322a9be1b6484c3f5b7cbb2fc37679cb`: early case-folded Windows asset namespace collisions.
- `8dd490e216ada9e31cd9bf40ce235c9e64822756`: compute global SHA before final output promotion.
- `0bac505bad8f5d54e39075f8672699d984a2f6aa`: add manual offline packaging preflight.
- `fa3ea8e7fa80a07c3fed052add1d1f430fac99fa`: run one-shot preflight before native Windows Qt compilation.
- `c4541315950d05133accae975cf56dbd7e10d983`: run one-shot preflight in standalone offline packaging.
- `f2d4330d71e5f69dde52702200c07b5bf7892561`: updated release gates.
- `0d42255cbf0401f79dfdbe155d41ec41f26458dd`: updated offline distribution guide.

## Remaining full-application blockers

A verified full Windows Qt6/MSVC release build, tested clean installation/uninstallation on Windows 10/11, actual licensable model weights and engine dependencies, combined RTX 4070 12GB LLM+TTS+ASR validation, high quality English female speech acceptance, real RU/EN dictation and accurate speech recognition, long conversation coding/research/creative quality, source-consent checks and genuine offline/no-VPN first-run on target device. The project is still in development; do not treat these source hardening changes as a finished user-installable Ember or a trained EmberMind/Voice checkpoint.

All work here was immediate/foreground to the user's request. Absolutely no automation or scheduled background task was created.
