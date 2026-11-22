# Ember: private tools and local-agent roadmap (no implicit permissions)

## Scope

Long-term goal is to handle real software development, debugging, research,
GitHub repositories, Google Drive documents, and local files on Windows 10
without needing a cloud-hosted language model. **The model runs offline, but
cloud services cannot be accessed without a network connection and permission.**
The app must continue answering from locally available knowledge without cloud
accounts, subscription, VPN, scheduled checks or automatic model downloads.

## Implemented first step: explicit local text file import

The Chat toolbar now has **Add text file**. The native Windows file picker
selects a single small text/code file. Ember reads at most 32 KiB of verified
UTF-8, checks file type and secrets-risk filenames, and adds the complete
content to the visible, editable composer only if its resulting draft stays
below 8,192 characters. It does not truncate without warning. The user
must explicitly press Send before anything reaches the managed local
llama-server. There is no background scanning, command execution, file
writing or cloud sync. The local SQLite chat archive is currently plaintext:
do not import sensitive files without redacting them. This is not yet a
reliable large-codebase retrieval or a real tool-calling agent.

## Implemented second step: explicit bounded source-folder search

The Chat toolbar now also offers **Find in folder**. It asks the user for a
root folder and a literal string, performs one bounded read-only search, and
puts matching `relative-file:line: text` excerpts into the editable draft.
It excludes symlinks, common build/dependency folders and credentials,
enforces canonical-path containment, scans no more than 600 small UTF-8 source
files / 4 MiB / 3,000 entries, and returns up to 20 matching lines or a
clearly marked capped result. There is no directory watcher, persistent
index, global drive search, autonomous tool call or Git execution. Larger
projects need a separate authorized retrieval/indexing design.

## Implemented third step: explicitly selected public GitHub read

The **GitHub · Read** toolbar action asks which public owner/repository to
inspect and whether to fetch its overview, open issues, open pull requests
or one source file. The native Qt client reads only
`https://api.github.com/repos/...` with strict path validation, no personal
token, redirect following, remote code execution, polling or scheduled jobs.
It limits each JSON response to 256 KiB and displays a bounded, copyable
read-only preview in a review dialog before the user may insert it into a
local chat draft. The model is never granted network access automatically.
Private repositories, authentication, issue details, diff/commit writing,
Drive and general web search are **not implemented**. GitHub access itself
requires an Internet connection and may be unavailable in some regions.

## Implemented fourth step: optional private GitHub read with Windows credentials

Within **GitHub · Read**, choose **Connect / replace GitHub read token** and
enter a GitHub fine-grained personal access token configured with the minimum
repository **read** permissions required for your selected private resources:
Contents (read-only), Issues (read-only), Pull requests (read-only). Ember
stores the token in Windows Credential Manager as a user-scoped generic
credential, never in INI, SQLite or a Git repo. Users can disconnect it from
the same dialog. Choose a **Connected** read operation to attach the token
to an HTTPS GET sent exclusively to validated `api.github.com` endpoints.
Selecting **Public** never uses the token. The app has no write API calls,
does not automatically access a repo, and never shares the token with the
local model. Read results still require review before entering the draft.
This is manual PAT integration, NOT a GitHub OAuth login, nor Google Drive
access. No browser login, VPN or external packaged account service needed,
but GitHub connectivity itself is required.

## Implemented fifth step: explicit EN/RU Wikipedia topic search

The **Wikipedia** button permits a one-shot English or Russian Wikipedia
article search. It uses Wikimedia's documented modern MediaWiki REST
`/w/rest.php/v1/search/page` endpoint rather than the older
`/api/rest_v1/page/summary` path, which Wikimedia began deprecating.
Only selected `en.wikipedia.org` and `ru.wikipedia.org` are contacted
over HTTPS, with manual redirects disabled, a 12-second network timeout
and a 128 KiB response limit. A strict parser strips HTML highlighting
from up to five returned excerpts, attaches per-article source URLs and
labels them as search snippets **not full verified articles**. The
same native preview dialog requires user confirmation before adding
research to the local prompt. No automatic background search, general
web search, site crawler, remote code action, or cloud LLM calls exist.

## Next integrations — NOT YET IMPLEMENTED

1. **Local codebase retrieval/indexing:** expand the implemented one-shot
   bounded literal search to explicit approval scopes, gitignore rules,
   token-aware context selection, repository maps and semantic indexing
   without persistent scans or automatic activity.
2. **Expanded Internet research:** initial cited EN/RU Wikipedia topic
   search is now implemented, with strict one-shot browsing and user review.
   Broader verified pages, full articles, time-aware citations and independent
   web search providers require separate opt-in integrations. Remote source
   text must remain untrusted and offline mode must never fabricate results.
3. **Authenticated GitHub write operations:** private read-only access
   is now available with a user-connected fine-grained PAT protected by
   Windows Credential Manager. Separate write permissions and visible
   diff/approval for future file changes, commits and PRs are not implemented.
   Do not let a web page or model prompt invent permissions.
4. **Google Drive:** registered Desktop OAuth application, minimum
   necessary Drive scopes, system-browser consent (the authorization step
   is not an embedded insecure credential form), refresh token in OS secure
   storage, account and destination folder explicitly selected. Read only
   on user command first; any upload or overwrite needs explicit approval.
   Keep compact local backups independent of Drive connectivity.
5. **Development actions:** show proposed commands/patches first. Run only
   explicitly approved, scoped commands, with exit status and captured
   stdout/stderr; no blanket shell execution or arbitrary background jobs.
6. **Quality:** evaluate current official open-weight thinking/coding models
   against actual source patches, tests and diagnostics on RTX 4070 12GB.
   Run Qwen3 8B GGUF and suitable Qwen3.5 9B variants as baselines; record
   throughput, RAM/VRAM, first-token time, regression fix success and
   missed-tool-call rates. Avoid asserting GPT-6 High parity.

## Threat model and resource rules

Treat code comments, web pages, model outputs, repository README files and
Drive document text as untrusted data. Tool results are not instructions.
No user data is sent to a cloud model: only explicitly approved tool
requests leave the PC. Keep the LLM on an exact process-owned loopback
listener. Do not automatically follow links, execute installers or mutate
repositories. Never make automatic scheduled tasks or background monitors.

Use a single quantized LLM on GPU when feasible, plus CPU or low-memory
speech models; load multimodal modules on demand. Memory and context-window
limits must be measured on target hardware. The existing native Qt front
end remains independent of any cloud API.


## Initial native Google Drive read-only OAuth workflow

The Chat composer now includes **Google Drive**: Connect, List folder, or
Disconnect. Results appear in the same read-only review dialog used for
GitHub and Wikipedia. This is metadata only: users explicitly choose the
folder ID; no file contents are downloaded. A dedicated Qt
`EmberDrive::Client` implements Google Desktop OAuth through
the operating system's default browser and IPv4 loopback callback, with
random PKCE S256 verifier, CSRF state and exact redirect/host checks. It
asks only for `drive.metadata.readonly`; no file contents or modification
scopes are requested. Long-lived refresh credentials are saved using the
Windows Credential Manager, never plaintext INI or SQLite. It supports
explicit user-triggered metadata listing of one folder (20 entries max),
account-email verification through Drive about, and local disconnect.
Token exchanges go only to oauth2.googleapis.com; API metadata GETs go
only to www.googleapis.com. There is no scheduled sync, polling or
credential transmission to the local LLM. A Desktop OAuth application ID
created in Google Cloud and corresponding Google consent are necessary.
The native UI is wired, but **no real user Google OAuth sign-in or Drive
API response has yet been verified**, and a registered Desktop OAuth client
and relevant consent screen/test-user configuration are prerequisites.
The Google sign-in step uses the system browser per Google's native-app
OAuth security guidance, not an embedded WebView2 login. CI only covers
pure URL/token validation, mock JSON and isolated Windows credential tests.


### 22 Nov 2026 — Google OAuth account continuity and callback hardening

The loopback callback now has a separate unit-tested HTTP/1.1 validator: exactly one canonical IPv4 Host header matching the assigned port, a single valid CSRF state and authorization code, no duplicate parameters, and no embedded control characters. Requests that only contain a forged Host substring are rejected. Consent listener expires after 180 seconds (no polling or scheduled account actions). A cancelled, timed-out or unsuccessful new account connection preserves the prior Windows Credential Manager refresh credential and in-session token. A successful new consent replaces the old credential or removes it when no new refresh token is supplied. This is an implementation safeguard, **not** proof of a real Google OAuth flow or Drive API access. Keep real user authorization and on-device QA as release blockers.

### 22 Nov 2026 — one-shot local source project map

The existing Project tools button now offers two manual actions after a directory picker: bounded literal text search (preserved) or a filename-only source project map. The latter traverses no symlink/junction, ignores sensitive dotfiles, key filenames and common dependency/build directories, and does not read file bodies. Output is a compact, capped project tree appended to an editable draft only after a user click, never automatically sent to a model or external service. Windows Qt regression tests cover path listing, ignored sources, missing directories, large drafts and absence of file content. This improves local repository orientation, but does not implement gitignore-complete indexing, semantic search, autonomous patching, command execution or model-driven tool invocation.
