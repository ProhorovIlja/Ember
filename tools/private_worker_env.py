"""One-shot environment scrubbing for local model executables.

Keep system/GPU/DLL search paths; never pass incidental cloud/API credentials
from a developer's shell to external native inference binaries. Offline flags
are defense-in-depth only, NOT an OS network sandbox.
"""
import os


def private_worker_environment(source=None):
    environment = dict(os.environ if source is None else source)
    for name in list(environment):
        upper = name.upper()
        if (any(fragment in upper for fragment in
                ("TOKEN", "SECRET", "PASSWORD", "CREDENTIAL", "API_KEY",
                 "PRIVATE_KEY")) or
                upper.startswith(("AWS_", "GITHUB_", "OPENAI_",
                                  "GOOGLE_", "AZURE_"))):
            environment.pop(name, None)
    environment["SPEECH_NO_UPDATE_CHECK"] = "1"
    environment["HF_HUB_OFFLINE"] = "1"
    environment["TRANSFORMERS_OFFLINE"] = "1"
    return environment


def self_test():
    incoming = {
        "PATH": "keep-system-binaries", "SystemRoot": "keep-windows",
        "CUDA_PATH": "keep-gpu", "VK_ICD_FILENAMES": "keep-vulkan",
        "GITHUB_TOKEN": "secret", "HF_TOKEN": "secret",
        "GOOGLE_APPLICATION_CREDENTIALS": "secret",
        "OPENAI_API_KEY": "secret", "AWS_SECRET_ACCESS_KEY": "secret",
        "AZURE_CLIENT_SECRET": "secret", "a_private_key": "secret",
        "my_password": "secret",
        "SPEECH_NO_UPDATE_CHECK": "0",
    }
    cleaned = private_worker_environment(incoming)
    for name in ("PATH", "SystemRoot", "CUDA_PATH", "VK_ICD_FILENAMES"):
        assert cleaned[name] == incoming[name]
    for name in incoming:
        if name not in ("PATH", "SystemRoot", "CUDA_PATH", "VK_ICD_FILENAMES",
                        "SPEECH_NO_UPDATE_CHECK"):
            assert name not in cleaned
    assert cleaned["SPEECH_NO_UPDATE_CHECK"] == "1"
    assert cleaned["HF_HUB_OFFLINE"] == "1"
    assert cleaned["TRANSFORMERS_OFFLINE"] == "1"
    assert incoming["GITHUB_TOKEN"] == "secret"  # never mutate caller's mapping
    print("Private local worker environment tests passed")


if __name__ == "__main__":
    self_test()
