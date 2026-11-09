#include <windows.h>
#include <setupapi.h>
#include <winusb.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <limits>
#include <vector>

#include <QBuffer>
#include <QPainter>
#include <QTransform>

#include "WonderVisionDevice.h"

namespace
{
    constexpr int DEFAULT_WIDTH = 2400;
    constexpr int DEFAULT_HEIGHT = 1080;
    constexpr unsigned int USB_PACKET_SIZE = 512;
    constexpr unsigned int MAGIC = 0x78563412;
    constexpr unsigned int JPEG_COMMAND = 2;

    const GUID USB_DEVICE_INTERFACE_GUID =
    {
        0xA5DCBF10, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED }
    };

    void WriteLe32(char* destination, unsigned int value)
    {
        destination[0] = static_cast<char>(value & 0xFF);
        destination[1] = static_cast<char>((value >> 8) & 0xFF);
        destination[2] = static_cast<char>((value >> 16) & 0xFF);
        destination[3] = static_cast<char>((value >> 24) & 0xFF);
    }

    std::wstring Lowercase(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return value;
    }
}

WonderVisionDevice::WonderVisionDevice()
    : stop_requested(false), status(QStringLiteral("Wonder Vision is disconnected.")),
      width(DEFAULT_WIDTH), height(DEFAULT_HEIGHT),
      file_handle(nullptr), usb_handle(nullptr), pipe_in(0), pipe_out(0)
{
}

WonderVisionDevice::~WonderVisionDevice()
{
    Stop();
}

bool WonderVisionDevice::Start()
{
    Stop();
    stop_requested = false;
    SetStatus(QStringLiteral("Connecting to Wonder Vision..."));
    worker = std::thread(&WonderVisionDevice::Run, this);
    return true;
}

void WonderVisionDevice::Stop()
{
    stop_requested = true;
    frame_cv.notify_all();
    if(worker.joinable())
    {
        worker.join();
    }
    Close();
    SetStatus(QStringLiteral("Screen streaming stopped."));
}

void WonderVisionDevice::SetFrame(const QImage& frame)
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        latest_frame = frame;
    }
    frame_cv.notify_one();
}

QString WonderVisionDevice::Status() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return status;
}

QSize WonderVisionDevice::Resolution() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return QSize(width, height);
}

void WonderVisionDevice::SetStatus(const QString& new_status)
{
    std::lock_guard<std::mutex> lock(mutex);
    status = new_status;
}

bool WonderVisionDevice::Open()
{
    HDEVINFO devices = SetupDiGetClassDevsW(&USB_DEVICE_INTERFACE_GUID, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if(devices == INVALID_HANDLE_VALUE)
    {
        SetStatus(QStringLiteral("Could not scan USB devices (Windows error %1)." ).arg(GetLastError()));
        return false;
    }

    bool found = false;
    QString failure = QStringLiteral("Wonder Vision USB device 87AD:70DB was not found.");

    for(DWORD index = 0; ; ++index)
    {
        SP_DEVICE_INTERFACE_DATA interface_data{};
        interface_data.cbSize = sizeof(interface_data);
        if(!SetupDiEnumDeviceInterfaces(devices, nullptr, &USB_DEVICE_INTERFACE_GUID, index, &interface_data))
        {
            break;
        }

        DWORD required = 0;
        SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, nullptr, 0, &required, nullptr);
        if(required < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W))
        {
            continue;
        }

        std::vector<unsigned char> detail_storage(required, 0);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detail_storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if(!SetupDiGetDeviceInterfaceDetailW(devices, &interface_data, detail, required, nullptr, nullptr))
        {
            continue;
        }

        if(Lowercase(detail->DevicePath).find(L"vid_87ad&pid_70db") == std::wstring::npos)
        {
            continue;
        }

        HANDLE file = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
        if(file == INVALID_HANDLE_VALUE)
        {
            failure = QStringLiteral("Could not open Wonder Vision (Windows error %1). Try running as administrator.").arg(static_cast<unsigned int>(GetLastError()));
            continue;
        }

        WINUSB_INTERFACE_HANDLE usb = nullptr;
        if(!WinUsb_Initialize(file, &usb))
        {
            failure = QStringLiteral("WinUSB could not initialize Wonder Vision (Windows error %1)." ).arg(static_cast<unsigned int>(GetLastError()));
            CloseHandle(file);
            continue;
        }

        USB_INTERFACE_DESCRIPTOR descriptor{};
        if(!WinUsb_QueryInterfaceSettings(usb, 0, &descriptor))
        {
            failure = QStringLiteral("Could not read Wonder Vision USB endpoints (Windows error %1)." ).arg(static_cast<unsigned int>(GetLastError()));
            WinUsb_Free(usb);
            CloseHandle(file);
            continue;
        }

        UCHAR input = 0;
        UCHAR output = 0;
        for(UCHAR pipe_index = 0; pipe_index < descriptor.bNumEndpoints; ++pipe_index)
        {
            WINUSB_PIPE_INFORMATION pipe{};
            if(!WinUsb_QueryPipe(usb, 0, pipe_index, &pipe) || pipe.PipeType != UsbdPipeTypeBulk)
            {
                continue;
            }
            ULONG timeout_ms = 2500;
            WinUsb_SetPipePolicy(usb, pipe.PipeId, PIPE_TRANSFER_TIMEOUT, sizeof(timeout_ms), &timeout_ms);
            if(pipe.PipeId & 0x80) input = pipe.PipeId;
            else output = pipe.PipeId;
        }

        if(input == 0 || output == 0)
        {
            failure = QStringLiteral("Wonder Vision opened, but no bulk input/output endpoint pair was found.");
            WinUsb_Free(usb);
            CloseHandle(file);
            continue;
        }

        file_handle = file;
        usb_handle = usb;
        pipe_in = input;
        pipe_out = output;
        found = true;
        break;
    }

    SetupDiDestroyDeviceInfoList(devices);
    if(!found)
    {
        SetStatus(failure);
        return false;
    }

    UCHAR handshake[64]{};
    handshake[0] = 0x12;
    handshake[1] = 0x34;
    handshake[2] = 0x56;
    handshake[3] = 0x78;
    handshake[56] = 1;
    ULONG transferred = 0;
    auto usb = static_cast<WINUSB_INTERFACE_HANDLE>(usb_handle);
    if(!WinUsb_WritePipe(usb, pipe_out, handshake, sizeof(handshake), &transferred, nullptr)
       || transferred != sizeof(handshake))
    {
        SetStatus(QStringLiteral("Wonder Vision handshake write failed (Windows error %1)." ).arg(static_cast<unsigned int>(GetLastError())));
        Close();
        return false;
    }

    UCHAR response[1024]{};
    transferred = 0;
    if(!WinUsb_ReadPipe(usb, pipe_in, response, sizeof(response), &transferred, nullptr)
       || transferred < 37 || response[24] == 0)
    {
        SetStatus(QStringLiteral("Wonder Vision did not return a valid USB handshake (Windows error %1)." ).arg(static_cast<unsigned int>(GetLastError())));
        Close();
        return false;
    }

    const int detected_width = response[36] == 0x20 ? 1600 : DEFAULT_WIDTH;
    const int detected_height = response[36] == 0x20 ? 720 : DEFAULT_HEIGHT;
    {
        std::lock_guard<std::mutex> lock(mutex);
        width = detected_width;
        height = detected_height;
        status = QStringLiteral("Connected: %1 x %2, firmware SUB 0x%3.")
            .arg(width).arg(height).arg(response[36], 2, 16, QLatin1Char('0')).toUpper();
    }
    return true;
}

void WonderVisionDevice::Close()
{
    if(usb_handle != nullptr)
    {
        WinUsb_Free(static_cast<WINUSB_INTERFACE_HANDLE>(usb_handle));
        usb_handle = nullptr;
    }
    if(file_handle != nullptr && file_handle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(static_cast<HANDLE>(file_handle));
        file_handle = nullptr;
    }
    pipe_in = 0;
    pipe_out = 0;
}

bool WonderVisionDevice::Send(const QImage& source)
{
    if(usb_handle == nullptr || source.isNull())
    {
        return false;
    }

    const qint64 frame_key = source.cacheKey();
    if(cached_packet.isEmpty() || frame_key != cached_frame_key)
    {
        QSize size = Resolution();
        // The 2400x1080 Wonder Vision panel is normally driven with a
        // 1600x720 JPEG render, which the display scales to its native panel.
        // Keeping the submitted frame at native size sends 2.25x as many
        // pixels and can overrun the panel's image-update path.
        if(size == QSize(2400, 1080))
        {
            size = QSize(1600, 720);
        }
        QImage frame = source;
        if(frame.size() != size)
        {
            frame = frame.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QImage canvas(size, QImage::Format_RGB32);
            canvas.fill(QColor(4, 8, 15));
            QPainter painter(&canvas);
            painter.drawImage((size.width() - frame.width()) / 2, (size.height() - frame.height()) / 2, frame);
            frame = canvas;
        }
        frame = frame.transformed(QTransform().rotate(180), Qt::FastTransformation);

        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        if(!buffer.open(QIODevice::WriteOnly) || !frame.save(&buffer, "JPEG", 82) || jpeg.isEmpty())
        {
            return false;
        }

        cached_packet = QByteArray(64, '\0');
        WriteLe32(cached_packet.data(), MAGIC);
        WriteLe32(cached_packet.data() + 4, JPEG_COMMAND);
        WriteLe32(cached_packet.data() + 8, static_cast<unsigned int>(size.width()));
        WriteLe32(cached_packet.data() + 12, static_cast<unsigned int>(size.height()));
        WriteLe32(cached_packet.data() + 56, 2);
        WriteLe32(cached_packet.data() + 60, static_cast<unsigned int>(jpeg.size()));
        cached_packet.append(jpeg);
        cached_frame_key = frame_key;
    }

    auto usb = static_cast<WINUSB_INTERFACE_HANDLE>(usb_handle);
    // 87AD:70DB expects each encoded frame in a single bulk transfer. Splitting
    // it into multiple application-level writes can make the panel expose
    // partial frame updates while it is still receiving the rest of the JPEG.
    if(cached_packet.size() > static_cast<qint64>((std::numeric_limits<ULONG>::max)()))
    {
        return false;
    }
    ULONG written = 0;
    const ULONG packet_size = static_cast<ULONG>(cached_packet.size());
    if(!WinUsb_WritePipe(usb, pipe_out, reinterpret_cast<PUCHAR>(cached_packet.data()), packet_size, &written, nullptr)
       || written != packet_size)
    {
        return false;
    }
    if(cached_packet.size() % USB_PACKET_SIZE == 0)
    {
        UCHAR byte = 0;
        ULONG written = 0;
        if(!WinUsb_WritePipe(usb, pipe_out, &byte, 0, &written, nullptr))
        {
            return false;
        }
    }
    return true;
}

void WonderVisionDevice::Run()
{
    if(!Open())
    {
        return;
    }

    // The Wonder Vision stays stable with 1600x720 frames. Refresh at up to
    // 10 FPS so animated media and sensor overlays update smoothly without
    // returning to the much heavier native-resolution transfer.
    constexpr auto FRAME_COOLDOWN = std::chrono::milliseconds(100);
    while(!stop_requested)
    {
        QImage frame;
        {
            std::unique_lock<std::mutex> lock(mutex);
            frame_cv.wait(lock, [this]()
            {
                return stop_requested || !latest_frame.isNull();
            });
            if(stop_requested) break;
            frame = latest_frame;
        }
        if(!frame.isNull() && !Send(frame))
        {
            SetStatus(QStringLiteral("USB frame write failed (Windows error %1)." ).arg(static_cast<unsigned int>(GetLastError())));
            break;
        }
        std::unique_lock<std::mutex> lock(mutex);
        frame_cv.wait_for(lock, FRAME_COOLDOWN, [this]() { return stop_requested.load(); });
    }
    Close();
}
