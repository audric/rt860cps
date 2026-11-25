#include "serialtransport.h"
#include <QDir>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace rt860 {

static RadioError osError(const QString &what) {
    return RadioError(QStringLiteral("%1: %2").arg(what, QString::fromLocal8Bit(std::strerror(errno))));
}

SerialTransport::SerialTransport(const QString &device) {
    m_fd = ::open(device.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_fd < 0) throw osError(QStringLiteral("Cannot open %1").arg(device));
    if (::ioctl(m_fd, TIOCEXCL) < 0) { /* not fatal */ }
    termios tio{};
    if (::tcgetattr(m_fd, &tio) < 0) {
        const RadioError e = osError(QStringLiteral("%1 is not a serial port").arg(device));
        ::close(m_fd);
        m_fd = -1;
        throw e;
    }
    ::cfmakeraw(&tio);
    tio.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
    tio.c_cflag |= CS8 | CLOCAL | CREAD;
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    ::cfsetispeed(&tio, B115200);
    ::cfsetospeed(&tio, B115200);
    if (::tcsetattr(m_fd, TCSANOW, &tio) < 0) {
        const RadioError e = osError(QStringLiteral("Cannot configure %1").arg(device));
        ::close(m_fd);
        m_fd = -1;
        throw e;
    }
    int bits = TIOCM_DTR;
    ::ioctl(m_fd, TIOCMBIS, &bits);   // the CPS keeps DTR asserted
    ::tcflush(m_fd, TCIOFLUSH);
}

SerialTransport::~SerialTransport() {
    if (m_fd >= 0) ::close(m_fd);
}

void SerialTransport::write(const QByteArray &bytes) {
    int sent = 0;
    while (sent < bytes.size()) {
        pollfd p{m_fd, POLLOUT, 0};
        if (::poll(&p, 1, 2000) <= 0) throw RadioError(QStringLiteral("Timed out sending to the serial port."));
        const ssize_t n = ::write(m_fd, bytes.constData() + sent, size_t(bytes.size() - sent));
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) continue;
            throw osError(QStringLiteral("Serial port write failed"));
        }
        sent += int(n);
    }
}

QByteArray SerialTransport::read(int timeoutMs) {
    pollfd p{m_fd, POLLIN, 0};
    const int ready = ::poll(&p, 1, timeoutMs);
    if (ready < 0 && errno != EINTR) throw osError(QStringLiteral("Serial port failed"));
    if (ready <= 0) return {};
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) throw RadioError(QStringLiteral("The serial port was disconnected."));
    char buf[2048];
    const ssize_t n = ::read(m_fd, buf, sizeof buf);
    if (n < 0) {
        if (errno == EAGAIN || errno == EINTR) return {};
        throw osError(QStringLiteral("Serial port read failed"));
    }
    return QByteArray(buf, int(n));
}

void SerialTransport::discardInput() { ::tcflush(m_fd, TCIFLUSH); }

QStringList availableSerialPorts() {
    QStringList ports;
    const QDir dev(QStringLiteral("/dev"));
    for (const QString &name : dev.entryList({QStringLiteral("ttyUSB*"), QStringLiteral("ttyACM*")}, QDir::System, QDir::Name))
        ports << dev.absoluteFilePath(name);
    return ports;
}

} // namespace rt860
