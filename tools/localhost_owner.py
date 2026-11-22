"""Foreground Windows-only PID verification for private localhost ASR probes.

Never infer trust merely from an HTTP response on port 43822. These helpers
perform no network requests, do not start services and write no output files.
"""
import os
import subprocess


def listening_owner_pids(netstat_output, port):
    if not 1 <= port <= 65535:
        raise ValueError("Invalid local speech TCP port")
    owners = set()
    local = f"127.0.0.1:{port}"
    for line in netstat_output.splitlines():
        fields = line.split()
        # The LISTENING state may be localized. The remote TCP endpoint
        # 0.0.0.0:0 and numeric PID are stable in Windows netstat -ano.
        if (len(fields) >= 5 and fields[0].upper() == "TCP" and
                fields[1] == local and fields[2] == "0.0.0.0:0" and
                fields[-1].isdigit()):
            owners.add(int(fields[-1]))
    return owners


def require_owned_speech_listener(expected_pid, port=43822):
    if os.name != "nt":
        raise ValueError("Private live ASR probes require Windows PID checks")
    if type(expected_pid) is not int or expected_pid <= 0:
        raise ValueError("Specify the exact speech.exe PID with --speech-pid")
    if port != 43822:
        raise ValueError("Private live ASR is restricted to 127.0.0.1:43822")
    done = subprocess.run(
        ["netstat", "-ano", "-p", "tcp"],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL, check=False, timeout=10,
    )
    if done.returncode:
        raise ValueError("Cannot verify Windows localhost speech listener")
    owners = listening_owner_pids(done.stdout.decode("ascii", "ignore"), port)
    if owners != {expected_pid}:
        raise ValueError("Local speech port PID does not match the trusted process")
