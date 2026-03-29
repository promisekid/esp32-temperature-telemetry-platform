#include "serial_link.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace telemetry_platform::serial_link {

namespace {

#ifdef _WIN32
std::string win32_error_message(DWORD error_code) {
    LPSTR buffer = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error_code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&buffer),
        0,
        nullptr
    );

    std::string message;
    if (length != 0 && buffer != nullptr) {
        message.assign(buffer, buffer + length);
        LocalFree(buffer);
    } else {
        message = "unknown Win32 error";
    }
    return message;
}

std::string normalize_port_name(const std::string &port_name) {
    if (port_name.rfind("\\\\.\\", 0) == 0) {
        return port_name;
    }

    if (port_name.size() > 3 && (port_name.rfind("COM", 0) == 0 || port_name.rfind("com", 0) == 0)) {
        try {
            const auto port_index = std::stoi(port_name.substr(3));
            if (port_index > 8) {
                return "\\\\.\\" + port_name;
            }
        } catch (...) {
        }
    }

    return port_name;
}
#else
speed_t baud_to_native(std::uint32_t baud_rate) {
    switch (baud_rate) {
        case 9600:
            return B9600;
        case 19200:
            return B19200;
        case 38400:
            return B38400;
        case 57600:
            return B57600;
        case 115200:
            return B115200;
        default:
            return B115200;
    }
}
#endif

}  // namespace

struct SerialPort::Impl {
#ifdef _WIN32
    HANDLE handle {INVALID_HANDLE_VALUE};
#else
    int fd {-1};
#endif
};

SerialPort::SerialPort() : impl_(new Impl()) {}

SerialPort::~SerialPort() {
    close();
    delete impl_;
}

bool SerialPort::open(const SerialPortConfig &config, std::string *error_message) {
    close();
    config_ = config;

#ifdef _WIN32
    const auto native_name = normalize_port_name(config.port_name);
    impl_->handle = CreateFileA(
        native_name.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (impl_->handle == INVALID_HANDLE_VALUE) {
        if (error_message != nullptr) {
            *error_message = "无法打开串口 " + config.port_name + ": " + win32_error_message(GetLastError());
        }
        return false;
    }

    DCB dcb {};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(impl_->handle, &dcb)) {
        if (error_message != nullptr) {
            *error_message = "读取串口参数失败: " + win32_error_message(GetLastError());
        }
        close();
        return false;
    }

    dcb.BaudRate = config.baud_rate;
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
    dcb.fAbortOnError = FALSE;
    dcb.fDtrControl = config.assert_dtr ? DTR_CONTROL_ENABLE : DTR_CONTROL_DISABLE;
    dcb.fRtsControl = config.assert_rts ? RTS_CONTROL_ENABLE : RTS_CONTROL_DISABLE;

    if (!SetCommState(impl_->handle, &dcb)) {
        if (error_message != nullptr) {
            *error_message = "设置串口参数失败: " + win32_error_message(GetLastError());
        }
        close();
        return false;
    }

    COMMTIMEOUTS timeouts {};
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = config.read_timeout_ms;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = config.read_timeout_ms;
    timeouts.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(impl_->handle, &timeouts)) {
        if (error_message != nullptr) {
            *error_message = "设置串口超时失败: " + win32_error_message(GetLastError());
        }
        close();
        return false;
    }

    SetCommMask(impl_->handle, EV_ERR);
    SetupComm(impl_->handle, 4096, 4096);
    DWORD flags = 0;
    COMSTAT status {};
    ClearCommError(impl_->handle, &flags, &status);
    PurgeComm(impl_->handle, PURGE_TXCLEAR | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_RXABORT);
    EscapeCommFunction(impl_->handle, config.assert_dtr ? SETDTR : CLRDTR);
    EscapeCommFunction(impl_->handle, config.assert_rts ? SETRTS : CLRRTS);
    return true;
#else
    impl_->fd = ::open(config.port_name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (impl_->fd < 0) {
        if (error_message != nullptr) {
            *error_message = "无法打开串口 " + config.port_name + ": " + std::strerror(errno);
        }
        return false;
    }

    termios options {};
    if (tcgetattr(impl_->fd, &options) != 0) {
        if (error_message != nullptr) {
            *error_message = "读取串口参数失败: " + std::string(std::strerror(errno));
        }
        close();
        return false;
    }

    cfmakeraw(&options);
    cfsetispeed(&options, baud_to_native(config.baud_rate));
    cfsetospeed(&options, baud_to_native(config.baud_rate));
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = static_cast<cc_t>(config.read_timeout_ms / 100);

    if (tcsetattr(impl_->fd, TCSANOW, &options) != 0) {
        if (error_message != nullptr) {
            *error_message = "设置串口参数失败: " + std::string(std::strerror(errno));
        }
        close();
        return false;
    }

    return true;
#endif
}

void SerialPort::close() {
#ifdef _WIN32
    if (impl_->handle != INVALID_HANDLE_VALUE) {
        CloseHandle(impl_->handle);
        impl_->handle = INVALID_HANDLE_VALUE;
    }
#else
    if (impl_->fd >= 0) {
        ::close(impl_->fd);
        impl_->fd = -1;
    }
#endif
}

bool SerialPort::is_open() const {
#ifdef _WIN32
    return impl_->handle != INVALID_HANDLE_VALUE;
#else
    return impl_->fd >= 0;
#endif
}

std::size_t SerialPort::read_some(std::uint8_t *buffer, std::size_t capacity, std::string *error_message) {
    if (!is_open() || buffer == nullptr || capacity == 0) {
        return 0;
    }

#ifdef _WIN32
    DWORD flags = 0;
    COMSTAT status {};
    if (!ClearCommError(impl_->handle, &flags, &status)) {
        if (error_message != nullptr) {
            *error_message = "读取串口状态失败: " + win32_error_message(GetLastError());
        }
        close();
        return 0;
    }

    if (status.cbInQue == 0) {
        return 0;
    }

    const auto bytes_to_read =
        static_cast<DWORD>(std::min<std::size_t>(capacity, static_cast<std::size_t>(status.cbInQue)));
    DWORD bytes_read = 0;
    if (!ReadFile(impl_->handle, buffer, bytes_to_read, &bytes_read, nullptr)) {
        if (error_message != nullptr) {
            *error_message = "读取串口失败: " + win32_error_message(GetLastError());
        }
        close();
        return 0;
    }
    return static_cast<std::size_t>(bytes_read);
#else
    const auto result = ::read(impl_->fd, buffer, capacity);
    if (result < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return 0;
        }
        if (error_message != nullptr) {
            *error_message = "读取串口失败: " + std::string(std::strerror(errno));
        }
        close();
        return 0;
    }
    return static_cast<std::size_t>(result);
#endif
}

bool SerialPort::write_all(const std::uint8_t *data, std::size_t size, std::string *error_message) {
    if (!is_open() || data == nullptr || size == 0) {
        return false;
    }

#ifdef _WIN32
    std::size_t total_written = 0;
    while (total_written < size) {
        DWORD written = 0;
        const auto chunk = static_cast<DWORD>(std::min<std::size_t>(size - total_written, 4096));
        if (!WriteFile(impl_->handle, data + total_written, chunk, &written, nullptr)) {
            if (error_message != nullptr) {
                *error_message = "write serial failed: " + win32_error_message(GetLastError());
            }
            close();
            return false;
        }
        total_written += static_cast<std::size_t>(written);
    }
    return true;
#else
    std::size_t total_written = 0;
    while (total_written < size) {
        const auto result = ::write(impl_->fd, data + total_written, size - total_written);
        if (result < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            if (error_message != nullptr) {
                *error_message = "write serial failed: " + std::string(std::strerror(errno));
            }
            close();
            return false;
        }
        total_written += static_cast<std::size_t>(result);
    }
    return true;
#endif
}

const SerialPortConfig &SerialPort::config() const {
    return config_;
}

}  // namespace telemetry_platform::serial_link
