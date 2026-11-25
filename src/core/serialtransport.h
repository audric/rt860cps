#pragma once
#include "sync.h"

namespace rt860 {

// Serial port at 115200 baud 8N1, using the operating system directly:
// serialtransport.cpp on Linux, serialtransport_win.cpp on Windows.
class SerialTransport : public Transport {
public:
    explicit SerialTransport(const QString &device);
    ~SerialTransport() override;
    void write(const QByteArray &bytes) override;
    QByteArray read(int timeoutMs) override;
    void discardInput() override;

private:
#ifdef Q_OS_WIN
    qintptr m_handle = -1;   // a Win32 HANDLE
#else
    int m_fd = -1;
#endif
};

// Ports to offer: /dev/ttyUSB* and /dev/ttyACM* on Linux, COM ports on Windows.
QStringList availableSerialPorts();

} // namespace rt860
