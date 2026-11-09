#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <QImage>
#include <QByteArray>
#include <QString>
#include <QSize>

class WonderVisionDevice
{
public:
    WonderVisionDevice();
    ~WonderVisionDevice();

    bool Start();
    void Stop();
    void SetFrame(const QImage& frame);
    QString Status() const;
    QSize Resolution() const;

private:
    void Run();
    bool Open();
    void Close();
    bool Send(const QImage& frame);
    void SetStatus(const QString& status);

    std::atomic<bool> stop_requested;
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable frame_cv;
    QImage latest_frame;
    QByteArray cached_packet;
    qint64 cached_frame_key = 0;
    QString status;
    int width;
    int height;
    void* file_handle;
    void* usb_handle;
    unsigned char pipe_in;
    unsigned char pipe_out;
};
