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

The Chat composer includes **Google Drive**: Connect, List folder,
Read one text/Google Doc, and Disconnect. Results appear in the same
review-before-chat dialog used for GitHub and Wikipedia. A dedicated Qt
`EmberDrive::Client` uses Google Desktop OAuth with system-browser
consent, IPv4 loopback callback, PKCE S256, CSRF state and strict callback
Host validation. Reading document contents now requires an explicitly
granted `drive.readonly` scope; the prior metadata-only Windows credential
is separated by a distinct credential target and never silently upgraded.
The only outbound API hosts are oauth2.googleapis.com and
www.googleapis.com. An explicit folder request returns at most 20 metadata
entries. An explicit file-ID request reads metadata first, requires a
supported text/*, JSON, XML, YAML or Google Docs MIME type, rejects files
over 96 KiB when size is advertised, and caps response size and the text
preview. Google Docs are exported as text/plain; non-native text uses
Drive files.get alt=media. UTF-8 is validated and NUL rejected. File
contents are **untrusted** and are not sent to the model until reviewed.
No uploads, edits, automatic search, cloud history sync, polling, scheduled
tasks or agent-wide Drive browsing are implemented. Google Cloud Desktop
OAuth credentials and an appropriate consent screen/test user are needed;
the broader read-only scope may require Google verification.
**Real sign-in, network behavior, and target Windows acceptance remain
unverified**; regression tests use mock data and URL contracts.
The sign-in uses the system browser, not WebView2 or an embedded OAuth page.


### 22 Nov 2026 — Google OAuth account continuity and callback hardening

The loopback callback now has a separate unit-tested HTTP/1.1 validator: exactly one canonical IPv4 Host header matching the assigned port, a single valid CSRF state and authorization code, no duplicate parameters, and no embedded control characters. Requests that only contain a forged Host substring are rejected. Consent listener expires after 180 seconds (no polling or scheduled account actions). A cancelled, timed-out or unsuccessful new account connection preserves the prior Windows Credential Manager refresh credential and in-session token. A successful new consent replaces the old credential or removes it when no new refresh token is supplied. This is an implementation safeguard, **not** proof of a real Google OAuth flow or Drive API access. Keep real user authorization and on-device QA as release blockers.

### 22 Nov 2026 — one-shot local source project map

The existing Project tools button now offers two manual actions after a directory picker: bounded literal text search (preserved) or a filename-only source project map. The latter traverses no symlink/junction, ignores sensitive dotfiles, key filenames and common dependency/build directories, and does not read file bodies. Output is a compact, capped project tree appended to an editable draft only after a user click, never automatically sent to a model or external service. Windows Qt regression tests cover path listing, ignored sources, missing directories, large drafts and absence of file content. This improves local repository orientation, but does not implement gitignore-complete indexing, semantic search, autonomous patching, command execution or model-driven tool invocation.


### Manual local code editing (deadline-critical foundation)

The Chat workspace adds a native **Edit source** action: choose one permitted local UTF-8 source file, enter unique exact old text and proposed replacement, inspect a full editable-looking *read-only* change preview, then independently choose Apply or Cancel. The preview itself never edits anything. Apply rejects changed/symlinked/secret/binary targets, limits source and output to 512 KiB, verifies original SHA-256, writes a private original-byte backup under Windows AppLocalDataLocation/edit-backups, uses QSaveFile atomic replacement (no direct-write fallback) and reads back the resulting SHA. On every failure the original remains unchanged or a backup remains available. Qt regression tests validate Unicode/CRLF preservation, duplicate-match rejection, no-change before approval, backup fidelity, and stale-edit rejection. This is a deliberately manual foundation for future *permission-gated* model-proposed coding actions; it does **not** execute the model's commands, build code automatically or perform arbitrary unified-diff application.


### Responsive review UI and complete local edit preview

The source/tool action strip now uses a native QML Flow with a height derived from positioned children, allowing the seven file, project, GitHub, Wikipedia and image actions to wrap on narrow content widths without moving the send/voice row. The code-edit preview displays the **entire** old/new text within existing 32K/64K input bounds rather than silently clipping at 12K. A user sees the complete proposed change before applying it. This UI change is not a claim of actual on-device visual acceptance; run clean Windows 10 layout checks before Release.

### One-block code assistant bridge (model suggestion is not authority)

The one-file Edit source flow now accepts exactly one model-proposed Aider-style `<<<<<<< SEARCH` / `=======` / `>>>>>>> REPLACE` block from the last completed local assistant answer, or manual old/new input. Parsing is bounded, rejects missing/multiple/identical edits, and the user still picks the real source file through the operating system. The parser never trusts model-supplied filesystem paths and never applies a change automatically. The standard byte-accurate SHA-256, confirmation, backup and atomic-write gates remain mandatory. This is an original minimal format parser inspired by the public Aider edit-format documentation (https://aider.chat/docs/more/edit-formats.html), not a vendored copy of Aider. A model without reliable edit-format compliance will still require manual mode. A real autonomous test/retry tool loop remains unimplemented.


### One-click RU/EN dictation: edit or send

The text workspace provides **Dictate · edit** and **Dictate · send**. Click to begin recording and click again to stop; neither requires holding a mouse button. Both use the same native microphone and local chunked ASR pipeline with no fixed 30-second duration cap. Edit always inserts recognized text into the editable composer. Send inserts the transcript first, then emits `dictationAutoSendReady` and submits it through the existing loopback ownership gate **only** when recognition finishes normally on an intact recorded input; interrupted or partially recovered dictation is never auto-submitted. The model may be offline; in that case the editable draft remains present. Voice conversation still uses the existing turn-based speech model. No background, scheduled, or always-on microphone service is created.


### Explicit GitHub Issues write: separate, scoped permission

The **GitHub · Issues** action offers manual publication of one issue to a user-chosen repository. A dedicated fine-grained GitHub PAT requiring Issues (write) for selected repos is stored **separately** from Ember's existing read-only GitHub PAT under a different Windows Credential Manager target. The complete Markdown body is edited/reviewed in a native dialog; a separate default-No confirmation is required before a single POST to the exact `https://api.github.com/repos/{owner}/{repo}/issues` endpoint. Response status 201, selected repository identity and issue URL are validated. Network redirects are disabled, request and response sizes bounded. An optional draft from the last local model reply is treated as untrusted text and cannot publish without manual review. The write API never uses the read-only token. The user must check repo state before retrying ambiguous network failures to avoid duplicates. This is not a tool-calling agent, automated GitHub issue/PR management, or scheduled action.


### Additional public/connected GitHub read: branches, forks, Actions runs

The one-shot **GitHub · Read** picker now supports **branches**, **forks** and recent **GitHub Actions workflow runs** in addition to repository overview, Issues, PRs and one source file. Public access does not send a token. Connected reads use only the original read-only Windows Credential Manager token; private Actions may additionally require Actions (read) in the fine-grained PAT. Endpoints are strictly generated under api.github.com for a validated owner/repository, responses are bounded JSON, text is sanitized and previewed for user approval. Workflow run links are constructed from the verified owner/repository and numeric run IDs, **not** from an untrusted response URL. No Actions run is dispatched, rerun, monitored or scheduled; this is a static on-demand view. Search across arbitrary repository forks and diff reviews are still future work.


### First actual local model-driven tool loop: read-only project investigator

The **Investigate** action in the text workspace is a deliberately scoped on-demand agent: user types a research/debugging goal and explicitly chooses one local project root. The managed, kernel-ownership-verified localhost `llama-server` (needs `--jinja` and a tool-capable model/template) receives exactly two typed OpenAI-style read-only tools: `project_map()` and `search_text(query)`. A bounded state machine validates each model tool call, executes at most three sequential calls, provides capped source excerpts back as UNTRUSTED tool results, then asks for a final grounded answer. Only a single model call may run at a time, with 60-second per-request timeout, 64 KiB response cap, fixed localhost listener verification on every call, and no shell/network/Git/write tools. If the tool parser rejects a hallucinated/unsupported/privileged call, or the model hits its token limit, the task stops. Results open in a copyable review dialog and never modify local project files or silently enter SQLite history. Cancel aborts the active reply. The existing code search ignores common secret filenames, symlinks and dependency/build directories, but is NOT a universal secret scanner; only approve folders you trust. This is the FIRST limited agent cycle, not a general web/GitHub/Drive coding agent or whole-repository semantic index. It does not create any scheduled or background automation.

Compatibility requirement: start `llama-server` with `--jinja` to enable OpenAI-compatible tools. Model-specific chat templates may still be necessary. The library's typed tool contract is unit-tested; end-to-end inference with an actual GGUF must be tested on target hardware.


### Agent source-inspection tool

The read-only investigator also offers typed `read_source(path,start_line)` to retrieve up to 65 numbered lines from one user-root-contained UTF-8 source file (128 KiB on-disk cap). It validates every path segment, rejects traversal, symlinks and junctions, hidden/secret/dependency paths, binary/non-UTF8 data and unsafe file types; canonical root containment is enforced again after resolution. This enables a three-step map/search/read workflow without shell execution or code writes. User approval of the root is necessary; this is not a universal secret classifier.


### Larger source files within fixed resource limits

The local investigator scans text files up to 256 KiB each, while the existing per-search safety limits remain 4 MiB total, 600 source files, 3,000 directory entries, and 20 displayed matches. `read_source` supports UTF-8 files up to 512 KiB on disk but returns no more than 65 numbered source lines, about 4.7K characters locally, and a 2.5K-character model tool result. This allows examining Ember's own large C++ files, without loading the entire file into the active LLM context. It is not a universal large-repository semantic index.


### Native model-driven GitHub research within one approved repository

The text workspace's **GitHub agent** action now runs the same bounded local GGUF function-calling harness in *GitHubRepository* scope. The user types a goal, selects exactly one validated owner/repository, chooses public or existing read-only Windows Credential Manager token and independently approves at most three API GETs. The local model may select only `github_read(view,path?)`, where view is one of overview/open Issues/open PRs/branches/forks/Actions run results/a single small source file. The selected repository and credential are NEVER accepted as model arguments, and all HTTP endpoints are generated under official api.github.com. Redirects are disabled, responses are size- and schema-validated with the existing sanitizer, any GitHub text is delivered as untrusted local model context, and the result opens in a read-only review dialog. No write token, workflow run/rerun, issue publication, command execution, repository cloning, scheduled task or continuous monitoring is available through this agent. Private repo support depends on the user-connected read token and GitHub permissions. It is NOT global cross-repository research or autonomous development yet. Actual GGUF tool compliance and network access require end-to-end user hardware testing.


### Model-driven GitHub details and on-demand failed Actions step summaries

Within the same **explicitly approved single GitHub repository** and three-call task budget, the model can now invoke `github_detail(kind,number)` with kind `issue`, `pull` or `run_jobs`. The application issues a single exact HTTPS GET to `/issues/N`, `/pulls/N` or `/actions/runs/N/jobs`, verifies the issue/PR repository identity and numeric ID, and extracts a bounded, plain-text issue description, PR head/base state or CI job and failed-step summary. It never takes API endpoints from the model, follows redirects, downloads logs, reruns Actions or uses a write credential. These detail calls compose with list discovery (`github_read`) in the SAME finite call budget; this remains read-only, not full code patch/test/retry. GitHub REST docs: https://docs.github.com/en/rest/issues/issues , https://docs.github.com/en/rest/pulls/pulls , https://docs.github.com/en/rest/actions/workflow-jobs .


### GitHub large-source excerpt inspection

The `github_read(view=file,path,start_line?)` model tool now supports a selected starting source line within an explicitly approved repository. The native REST Content API response is validated as base64 UTF-8 text (file <=160 KiB; entire response <=256 KiB), then the system extracts at most 65 numbered lines (~4.2K characters before the 2.5K model-context cap) rather than sending the entire file to the local model. Default start_line=1; later source sections can be requested explicitly. No arbitrary branch/host/URL control, content writes or follow-on workflow execution is added. This makes larger GitHub files inspectable without increasing the active model context dramatically; files above this cap still require local cloning/import and appropriate consent.


### Long chat archives: on-demand pagination, no silent 80/200-session browsing limit

The SQLite archive keeps all persisted sessions and messages without a total-retention cap. The native history sidebar now has **Load older conversations** (80 per deliberate click) and chat timeline **Load earlier messages** (100 per click after the initial newest 300). Both use bounded SQL LIMIT/OFFSET reads and never upload, erase, or summarize the original archive. Existing local backup/export still covers ALL saved messages. During subsequent live conversation the visible window can return to recent entries, but old content remains recoverable by manual paging or full search. The local model's active context remains physically finite; this change improves history browsing, not memory/reasoning parity with cloud models. No scheduled synchronization or ongoing background activity is created.


### Agent-driven, one-file source repair proposal with mandatory review

The native text workspace now offers **Propose fix** in addition to read-only **Investigate**. The user first chooses a project root. Only in Propose fix does the local model receive the typed `propose_patch(path,old_text,new_text)` tool, alongside the existing bounded project-map/source-search/numbered-source-reader tools. The proposal tool **does not write**. It validates exactly one source path and exact before/after snippets (8K/12K chars max), verifies every directory component is a real nonhidden nondependency directory without links or junctions, checks the canonical target is contained in the user-approved root, and reuses Ember's existing single-match SHA-256 `planExactReplace` staging/preview mechanism. On a successful proposal, the model gets a final explanation without further tools. The same full-diff review modal is opened; accepting it executes the independently implemented original-byte backup and atomic single-file replacement. Cancel/decline/failure never changes the file. Model-generated patches never get automatic command execution, compilation, testing, Git commit, GitHub publication, cross-file writes, or persistent scheduled behavior. Exact hardware/model performance is still untested. Tests exercise parser scope denial, refused malicious parameters, immutable staging, verified backup and stale-state rejection.


### Real GGUF function-call acceptance harness

The existing offline-only Python text benchmark now accepts manual `--tool-verify` to test a pre-existing managed local llama-server (127.0.0.1:43821) with a four-call *synthetic* tool sequence, followed by a text answer. It checks assistant/tool call JSON syntax, exact call ordering and typed argument values, finite response size, and excludes proxies/redirects/remote hosts. No real tool body is executed or patch applied; output is metrics-only. The existing `--self-test` exercises the validation parser with synthetic malformed responses and is also run by Windows CI via the already-existing text benchmark step; it does not launch any model. A successful offline handshake on target RTX 4070 is a necessary but insufficient acceptance gate for automatic coding.


### One-shot, separately approved native project test execution

The native chat tools now offer **Run tests**. The user explicitly chooses ONE trusted local project folder, then one fixed preset: CTest in an existing real `build/` directory, Cargo `test --locked` with a real Cargo.toml, or Python `-m unittest discover -v` with a real tests/ folder. The pure C++ planner verifies the paths, selected build/test marker and installed runner, and passes fixed arguments through QProcess **without a shell**. The native UI displays the exact program, arguments and root and requires a separate default-No confirmation; no local LLM can call this command or select arbitrary arguments. The live process has a 120-second timeout, bounded 24 KiB in-RAM stdout/stderr capture, interrupt/cancel and a user-reviewed result preview. Common credential environment variables are removed but this is NOT a security sandbox: arbitrary project tests may access files/network and launch child processes, which could outlive cancellation. Only run projects you trust. No schedules, periodic monitoring or automatic retry, writes, Git commits or PR publication are created. Windows CI exercises deterministic planner tests, not the real execution of arbitrary user projects. Next integration: explicit opt-in for model-guided error triage and multi-file/CI workflows with per-action approval.
