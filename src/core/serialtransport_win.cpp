// Windows side of SerialTransport. The Linux side is serialtransport.cpp.
#include "serialtransport.h"
#include <QSettings>
#include <algorithm>
#include <windows.h>

namespace rt860 {

static RadioError osError(const QString &what) {
    const DWORD code = ::GetLastError();
    wchar_t *text = nullptr;
    ::FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                     nullptr, code, 0, reinterpret_cast<wchar_t *>(&text), 0, nullptr);
    const QString why = text ? QString::fromWCharArray(text).trimmed() : QStringLiteral("error %1").arg(code);
    if (text) ::LocalFree(text);
    return RadioError(QStringLiteral("%1: %2").arg(what, why));
}

static HANDLE handleOf(qintptr h) { return reinterpret_cast<HANDLE>(h); }

SerialTransport::SerialTransport(const QString &device) {
    // "COM10" and above open only with the \\.\ prefix; it is harmless for the others.
    const QString path = device.startsWith(QLatin1String("\\\\")) ? device : QStringLiteral("\\\\.\\") + device;
    const HANDLE h = ::CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
                                   0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw osError(QStringLiteral("Cannot open %1").arg(device));
    const auto fail = [&](const QString &what) {
        const RadioError e = osError(what);
        ::CloseHandle(h);
        return e;
    };
    DCB dcb{};
    dcb.DCBlength = sizeof dcb;
    if (!::GetCommState(h, &dcb)) throw fail(QStringLiteral("%1 is not a serial port").arg(device));
    dcb.BaudRate = CBR_115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;   // the CPS keeps DTR asserted
    dcb.fRtsControl = RTS_CONTROL_ENABLE;   // as on Linux, where opening the port asserts RTS
    if (!::SetCommState(h, &dcb)) throw fail(QStringLiteral("Cannot configure %1").arg(device));
    ::PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    m_handle = reinterpret_cast<qintptr>(h);
}

SerialTransport::~SerialTransport() {
    if (m_handle != -1) ::CloseHandle(handleOf(m_handle));
}

// Reads return as soon as any byte is there, or after `readMs` with nothing.
static bool setTimeouts(HANDLE h, int readMs) {
    COMMTIMEOUTS t{};
    t.ReadIntervalTimeout = MAXDWORD;
    if (readMs > 0) {
        t.ReadTotalTimeoutMultiplier = MAXDWORD;
        t.ReadTotalTimeoutConstant = DWORD(readMs);
    }
    t.WriteTotalTimeoutConstant = 2000;
    return ::SetCommTimeouts(h, &t);
}

void SerialTransport::write(const QByteArray &bytes) {
    const HANDLE h = handleOf(m_handle);
    if (!setTimeouts(h, 0)) throw osError(QStringLiteral("Serial port failed"));
    int sent = 0;
    while (sent < bytes.size()) {
        DWORD n = 0;
        if (!::WriteFile(h, bytes.constData() + sent, DWORD(bytes.size() - sent), &n, nullptr))
            throw osError(QStringLiteral("Serial port write failed"));
        if (n == 0) throw RadioError(QStringLiteral("Timed out sending to the serial port."));
        sent += int(n);
    }
}

QByteArray SerialTransport::read(int timeoutMs) {
    const HANDLE h = handleOf(m_handle);
    if (!setTimeouts(h, std::max(timeoutMs, 0))) throw RadioError(QStringLiteral("The serial port was disconnected."));
    char buf[2048];
    DWORD n = 0;
    if (!::ReadFile(h, buf, sizeof buf, &n, nullptr)) throw RadioError(QStringLiteral("The serial port was disconnected."));
    return QByteArray(buf, int(n));
}

void SerialTransport::discardInput() { ::PurgeComm(handleOf(m_handle), PURGE_RXCLEAR); }

QStringList availableSerialPorts() {
    // Windows lists the serial ports that are present right now under this key.
    const QSettings map(QStringLiteral("HKEY_LOCAL_MACHINE\\HARDWARE\\DEVICEMAP\\SERIALCOMM"), QSettings::NativeFormat);
    QStringList ports;
    for (const QString &key : map.allKeys()) {
        const QString name = map.value(key).toString();
        if (!name.isEmpty() && !ports.contains(name)) ports << name;
    }
    const auto number = [](const QString &name) { return name.mid(3).toInt(); };
    std::sort(ports.begin(), ports.end(), [&](const QString &a, const QString &b) { return number(a) < number(b); });
    return ports;
}

} // namespace rt860
