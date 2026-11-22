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

## Next integrations — NOT YET IMPLEMENTED

1. **Local codebase retrieval/indexing:** expand the implemented one-shot
   bounded literal search to explicit approval scopes, gitignore rules,
   token-aware context selection, repository maps and semantic indexing
   without persistent scans or automatic activity.
2. **Cited Internet/Wikipedia research:** explicit user request, isolated
   HTTP client, content-length / MIME / timeout limits, direct URL citations,
   injected page text treated as untrusted data; network off means clear
   offline error, not a fabricated search result.
3. **Authenticated GitHub:** extend public read-only access with a user
   connected fine-grained token with narrow repository permissions, stored
   via Windows Credential Manager/DPAPI rather than plaintext settings.
   Read private issues/PRs/files only after account consent. Editing
   files, commits or PRs requires a visible diff and explicit confirmation.
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
