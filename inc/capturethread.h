//capturethread.h
#pragma once

#include <QThread>
#include <QImage>
#include <atomic>
#include <QObject>

// POSIX headers required for mmap / MAP_FAILED etc.
#include <sys/mman.h>
#include <linux/videodev2.h>

extern "C" {
#include "gpio_buttons.h" // your existing C header
}

class CaptureThread : public QThread
{
    Q_OBJECT
public:
    explicit CaptureThread(std::atomic<int> &palette_atomic, QObject *parent = nullptr);
    ~CaptureThread() override;

    void run() override;
    void stopAndWait();

    static const char* paletteName(int idx);

    // expose palette atomic if needed
    std::atomic<int> &paletteAtomic() { return m_palette_atomic; }

public slots:
    // slot/entry point invoked via QMetaObject::invokeMethod from C callback
    Q_INVOKABLE void handleGpioEvent(unsigned int offset, int value);
    Q_INVOKABLE void setPaletteIndex(int idx);


signals:
    void frameReady(const QImage &img);
    void paletteIndexChanged(int idx);
    void gpioPressed(unsigned int offset, int value);

private:
    void cleanup();
    bool m_running;
    std::atomic<int> &m_palette_atomic;

    // V4L2 locals
    int v4l2_fd = -1;
    struct v4l2_format fmt;
    struct v4l2_buffer buf;
    void *video_buffer = nullptr; // initialized to nullptr (mmap result stored as MAP_FAILED in cpp)
    size_t buf_length = 0;

    // GPIO helper
    struct gpio_buttons *gb = nullptr;
};
