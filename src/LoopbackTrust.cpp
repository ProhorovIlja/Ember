#include "Core.h"

// Ember owns exactly two managed IPv4 loopback servers. A 200 response from
// localhost does not prove it came from the process Ember started: another
// program can preempt the port. On Windows, check the kernel TCP listener
// table before marking either model ready or sending private data.
#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <vector>
#endif

namespace EmberCore {
bool ownedLoopbackTcpListener(quint16 port, quint64 processId) {
    // The caller supplies Ember's fixed port. This helper accepts any
    // nonzero port so that a real temporary QTcpServer can test the guard.
    if (port == 0 || processId == 0 || processId > 0xffffffffULL)
        return false;
#ifdef Q_OS_WIN
    // Query only IPv4 listeners. Ember intentionally binds --host 127.0.0.1;
    // a wildcard 0.0.0.0 binding is never acceptable even if the PID matches.
    ULONG bytes = 0;
    DWORD status = GetExtendedTcpTable(nullptr, &bytes, FALSE, AF_INET,
                                       TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (status != ERROR_INSUFFICIENT_BUFFER || bytes == 0 ||
        bytes > 2 * 1024 * 1024) return false;

    // A listener can appear between the size probe and the data read.
    for (int attempt = 0; attempt < 2; ++attempt) {
        std::vector<unsigned char> storage(bytes);
        ULONG actual = bytes;
        status = GetExtendedTcpTable(storage.data(), &actual, FALSE, AF_INET,
                                     TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (status == ERROR_INSUFFICIENT_BUFFER &&
            actual > bytes && actual <= 2 * 1024 * 1024) {
            bytes = actual;
            continue;
        }
        if (status != NO_ERROR || actual < sizeof(DWORD)) return false;
        const auto *table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID *>(
            storage.data());
        const quint64 necessary = sizeof(DWORD) +
            quint64(table->dwNumEntries) * sizeof(MIB_TCPROW_OWNER_PID);
        if (necessary > actual) return false;
        const DWORD loopback = htonl(INADDR_LOOPBACK);
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            const auto &row = table->table[i];
            if (row.dwLocalAddr == loopback &&
                ntohs(static_cast<u_short>(row.dwLocalPort)) == port &&
                row.dwOwningPid == DWORD(processId))
                return true;
        }
        return false;
    }
#endif
    // No platform-dependent best-effort fallback: fail closed.
    return false;
}
}
