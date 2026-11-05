#pragma once
#include <QFileInfo>
#include <QDir>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

// QFileInfo::canonicalFilePath alone does not resolve every Windows junction.
// Resolve an opened filesystem handle, following reparse points, before checking
// that an existing file or parent directory lies inside an allowed root.
inline QString resolvedFileSystemPath(const QString &path) {
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED);
    if (!required || required > 32768) { CloseHandle(handle); return {}; }
    QString resolved(int(required + 1), QChar());
    const DWORD size = GetFinalPathNameByHandleW(handle, reinterpret_cast<LPWSTR>(resolved.data()), required + 1, FILE_NAME_NORMALIZED);
    CloseHandle(handle);
    if (!size || size > required) return {};
    resolved.truncate(int(size));
    if (resolved.startsWith("\\\\?\\UNC\\")) resolved = "\\\\" + resolved.mid(8);
    else if (resolved.startsWith("\\\\?\\")) resolved.remove(0, 4);
    return QDir::cleanPath(QDir::fromNativeSeparators(resolved));
#else
    return QFileInfo(path).canonicalFilePath();
#endif
}
