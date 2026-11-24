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

## Part 54 — installed-vs-portable private data lifecycle (24 Nov 2026)

Audit after the Part 53 packaging hardening exposed a user-data integrity risk:
the native `privateStorageFolder()` used `QCoreApplication::applicationDirPath()`
for **both** ordinary installations and portable releases. A per-user
Inno installation to a protected program directory could lack write access;
more importantly, an app-side chat database is tied to binaries and their
location, making uninstall/upgrade/data recovery fragile.

Actual GitHub source changes:
1. `src/Core.h/.cpp`: a pure `privateStorageRoot` policy and a
   filesystem-backed `detectWindowsStorageRoot` that reads a real
   `ember-installed.marker` in the EXE directory. Portable/no marker uses
   app-side storage; fresh Inno-installed marker uses Qt's current-user
   `AppLocalDataLocation`. An existing user profile wins; a legacy
   app-side chat DB/settings/library is preserved and used if no per-user
   profile exists. There is no implicit file copy, merge or deletion.
2. `src/EmberBackend.cpp`: `privateStorageFolder` retains an explicit
   `--smoke-test` ephemeral-root override, but installed Windows mode
   otherwise calls the shared detector. The root is cached ONCE per process
   so settings, SQLite, image Library, microphone scratch and recovery
   cannot split if another instance changes the alternative profile later.
   On non-Windows platforms source development retains existing app-side
   behavior.
3. `release/ember-installed.marker` (new) +
   `release/ember.iss`: Inno alone installs this small mode marker. It is
   NOT copied into the portable runtime payload. Inno source-relative
   `Source:` behavior was checked against official documentation.
4. `tools/offline_bundle.py` rejects a marker in the portable Qt
   application tree and adds a negative private-data fixture;
   `tools/portable_zip.py` also rejects it, with negative test.
   The developer cannot accidentally distribute an "installed" portable
   ZIP by first staging an Inno-installed directory.
5. `release/build-dev-installer.ps1` validates the Inno marker source
   before building and the marker's presence after a test installation;
   existing real uninstall and complete model-integrity smoke remains.
6. `src/EmberBackend.h/.cpp`: read-only
   `privateDataLocation()` reports the actual `data/` path. Native
   `qml/Main.qml` Settings displays it in a compact local storage card
   with a hover tooltip, so users can back up the right location.
   The UI explicitly explains the installed/portable difference.
7. `tests/CoreTests.cpp`: QtTest source cases cover all six policy
   combinations (portable, new installed, legacy, existing per-user,
   both profiles and empty per-user root), plus isolated real temporary
   directories with a marker file, an old SQLite file and a user-profile
   INI. They assert state precedence and no legacy file erasure.
8. `README.md`, `docs/SETUP.md`, `docs/OFFLINE-DISTRIBUTION.md`
   and `docs/RELEASE-GATES.md`: updated user migration/backup
   semantics and real Windows acceptance tests.

Important limitations and honest status:
- This tool environment does not contain a Qt6 SDK, Windows MSVC or
  Inno Setup. Source files have been written via the authenticated
  GitHub connector and read back; **C++ QtTest, real QML, Inno and actual
  Windows startup have NOT been executed** here.
- Even an installed profile is still local PLAINTEXT SQLite/config;
  no encryption or background cloud sync is introduced. The app
  uses manual user-triggered backup/restore; neither schema nor
  files are silently migrated.
- If BOTH an existing per-user profile and old app-side data exist,
  the per-user profile wins; the old app-side archive remains on disk
  and can be restored manually. This behavior avoids arbitrary mixing
  of distinct personal chats and is shown in the real path UI.
- A new protected Program Files deployment cannot be installed
  non-elevated with Inno `PrivilegesRequired=lowest`; acceptance
  distinguishes a legitimate per-user Inno install and a separately
  authorized protected-path runtime launch without elevation.
- Actual portable movement between SSDs and clean Win10/11 uninstall/
  reinstall retention still require on-device acceptance.
- There is NO downloadable finished native Ember build, verified local
  English female voice quality, genuine bundled model weights, or
  benchmarked RTX4070 LLM+TTS+ASR release at this time.
- No scheduled automation, background monitor, GitHub Actions run,
  public Release or user-file deletion was performed.

Part 54 main GitHub commits:
79982d17f857fb0853a732fb78c65f379455e499
c506dfd82fd613a2ecdbe89b8886fd80efee48a9
4986001d0dfbe814cf977affff287b11cdf72118
1f4a55fbf2c7b5e5f3d1ee211e75aa0ba93d3bb7
3874823ad7ea1dc2cc94f0e1810717c6c00dcfa6
79a3c931f3dd432da783b504b76d8fe05e93a967
ff5c895afd71481d78ca74c11ee621b78e71350a
fbc45dfd45cce1b979fbd51870d4141e187c52b7
0c53e9e98ef2cc1dc8ad3ae5607d2d7c4fa48362
ab9c14a00ba831b2909e36d5b6e6e19dc74269d4
d309d338a539c56481b108368024b61f6e5e4bb8
523de8ff51fcb206b484d499da8f88b8827f1e69
6eae60417bc1fc0ce2940cf85752f3eddf9a37f8
9c5473ef5bd036045c731fae562d4fd28c1e5437
39698e7ad00de3de19faf20deafed23306a9fc9a
537162efe5832c6c0e0bed897aab5add06a7b8fa
07f69bc63ca6a2450373be1dfabb0c868f27c03e
a54b18c34d8ef846386973437df598bd855859f9
146c82d06a88b1365e52816f9ecf627cfc42a1ff
2d55675a560b076e21336da63d7c25432ee12c3
171e9f3acb9a8ab690a83d766b99e26f61860c23
99f45b231cd1881fc856f17c47ff0c7272e16f42
5116c662c76b6d339dedd0e63a7add2c90dcd7b7
ad885f9b57e39728fda35968c70e7de8745aa638

Continue by running real Windows/Qt6, reviewing model provenance
and constructing a working offline Ember binary with all native
licensed weights and 12 GB simultaneous text/voice/ASR acceptance,
not by assuming source commits equal a finished product.
