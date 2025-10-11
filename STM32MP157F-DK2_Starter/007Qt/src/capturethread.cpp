// capturethread.cpp
#include "capturethread.h"
#include <QDebug>
#include <QImage>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <time.h>
#include <QMetaObject>

static const int WIDTH = 640;
static const int HEIGHT = 480;
static const char *VIDEO_DEVICE = "/dev/video0";
static const int NUM_PALETTES = 6;

const char *palette_names[] = {
    "IRONBOW", "RAINBOW", "ARCTIC", "LAVA", "BLACKHOT", "WHITEHOT"
};

CaptureThread::CaptureThread(std::atomic<int> &palette_atomic, QObject *parent)
    : QThread(parent),
      m_running(false),
      m_palette_atomic(palette_atomic),
      v4l2_fd(-1),
      video_buffer(MAP_FAILED),
      buf_length(0),
      gb(nullptr)
{
}

CaptureThread::~CaptureThread()
{
    stopAndWait();
}

const char* CaptureThread::paletteName(int idx)
{
    idx = idx % NUM_PALETTES;
    return palette_names[idx];
}

/* Palette conversion logic (same as before) */
static void gray_to_color_palette(uint16_t gray, uint8_t &r, uint8_t &g, uint8_t &b, int mode) {
    uint8_t v = gray >> 8;
    switch (mode) {
        case 0: r = (v < 128) ? v * 2 : 255; g = (v < 128) ? 0 : (v - 128) * 2; b = 0; break;
        case 1:
            if (v < 64) { r=0; g=0; b=4*v; }
            else if (v < 128) { r=0; g=4*(v-64); b=255-4*(v-64); }
            else if (v < 192) { r=4*(v-128); g=255-4*(v-128); b=0; }
            else { r=255; g=4*(v-192); b=4*(v-192); }
            break;
        case 2: r = v / 2; g = v; b = 255; break;
        case 3: r = v; g = v / 4; b = 0; break;
        case 4: r = g = b = 255 - v; break;
        case 5: r = g = b = v; break;
        default: r = g = b = v; break;
    }
}

/*
 * C callback used by gpio_buttons. The 'user' pointer is expected to be a
 * CaptureThread* (passed when registering). This function forwards the
 * event into the CaptureThread via QMetaObject::invokeMethod so the
 * event is delivered in Qt's event/queue system.
 *
 * Note: avoid doing heavy work or Qt calls directly from this C callback.
 */
static void gpio_button_cb(unsigned int offset, int value, void *user)
{
    if (!user) return;
    CaptureThread *ct = reinterpret_cast<CaptureThread*>(user);

    // Forward event into Qt object method which will emit gpioPressed
    // Use QueuedConnection so it's thread-safe and queued to the object's thread.
    QMetaObject::invokeMethod(ct, "handleGpioEvent", Qt::QueuedConnection,
                              Q_ARG(unsigned int, offset),
                              Q_ARG(int, value));
}

void CaptureThread::handleGpioEvent(unsigned int offset, int value)
{
    // This runs in the CaptureThread object's thread context (queued).
    // We only forward the event as a Qt signal for the GUI to handle.
    emit gpioPressed(offset, value);
    // Example:
    float pitch, roll, yaw; // Get data from your sensor
    emit imuDataReady(pitch, roll, yaw);
}

void CaptureThread::run()
{
    m_running = true;

    // Open V4L2 device
    v4l2_fd = open(VIDEO_DEVICE, O_RDWR);
    if (v4l2_fd < 0) {
        qWarning() << "Failed to open video device" << VIDEO_DEVICE << strerror(errno);
        return;
    }

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_Y16;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(v4l2_fd, VIDIOC_S_FMT, &fmt) < 0) {
        qWarning() << "VIDIOC_S_FMT failed:" << strerror(errno);
        cleanup(); return;
    }

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &req) < 0) {
        qWarning() << "VIDIOC_REQBUFS failed:" << strerror(errno);
        cleanup(); return;
    }

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &buf) < 0) {
        qWarning() << "VIDIOC_QUERYBUF failed:" << strerror(errno);
        cleanup(); return;
    }

    buf_length = buf.length;
    video_buffer = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, buf.m.offset);
    if (video_buffer == MAP_FAILED) {
        qWarning() << "mmap video buffer failed:" << strerror(errno);
        cleanup(); return;
    }

    if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
        qWarning() << "VIDIOC_QBUF failed:" << strerror(errno);
        cleanup(); return;
    }

    if (ioctl(v4l2_fd, VIDIOC_STREAMON, &buf.type) < 0) {
        qWarning() << "VIDIOC_STREAMON failed:" << strerror(errno);
        cleanup(); return;
    }

    // Initialize gpio_buttons if available
    // offsets array should match your board mapping; you had {0,1,4,6} earlier
    const unsigned int offsets[] = {0,1,4,6};
    const char *names[] = {"PF0","PF1","PF4","PF6"};
    gb = gpio_buttons_create("/dev/gpiochip5", offsets, names, 4, 200, 20000);
    if (!gb) {
        qWarning() << "Warning: gpio_buttons_create failed — continuing without gpio";
    } else {
        for (int i = 0; i < 4; ++i) {
            // pass 'this' as user pointer so callback can forward events to this object
            gpio_buttons_register_callback(gb, i, gpio_button_cb, this);
        }
        gpio_buttons_start(gb);
    }

    // -------------------------
    // Precompute palette LUTs
    // Each LUT entry is a uint32_t (QRgb as returned by qRgba)
    // This is the key performance improvement.
    // -------------------------
    uint32_t palette_lut[NUM_PALETTES][256];
    for (int p = 0; p < NUM_PALETTES; ++p) {
        for (int v = 0; v < 256; ++v) {
            uint8_t r, g, b;
            /* reconstruct a fake 16-bit gray with top byte = v */
            uint16_t gray16 = (uint16_t)(v << 8);
            gray_to_color_palette(gray16, r, g, b, p);
            // qRgba returns a QRgb (uint32_t) in the correct packed order
            palette_lut[p][v] = qRgba(r, g, b, 0xFF);
        }
    }

    // Main capture loop
    while (m_running) {
        if (ioctl(v4l2_fd, VIDIOC_DQBUF, &buf) < 0) {
            qWarning() << "VIDIOC_DQBUF failed:" << strerror(errno);
            break;
        }

        // Create QImage but fill bits() directly via uint32_t*
        QImage img(WIDTH, HEIGHT, QImage::Format_ARGB32);
        uint8_t *vptr = reinterpret_cast<uint8_t*>(video_buffer);
        int bytesperline = fmt.fmt.pix.bytesperline;
        int mode_idx = m_palette_atomic.load();
        if (mode_idx < 0) mode_idx = 0;
        if (mode_idx >= NUM_PALETTES) mode_idx %= NUM_PALETTES;

        // pointer to LUT row
        uint32_t *lut = palette_lut[mode_idx];

        // Fill image scanlines directly
        for (int y = 0; y < HEIGHT; ++y) {
            uint16_t *row = reinterpret_cast<uint16_t*>(vptr + y * bytesperline);
            uint32_t *dest = reinterpret_cast<uint32_t*>(img.scanLine(y));
            // Unroll a bit for speed (compiler may auto-unroll)
            for (int x = 0; x < WIDTH; ++x) {
                uint8_t v = row[x] >> 8;        // top 8 bits
                dest[x] = lut[v];              // single uint32 write
            }
        }

        // Emit the filled QImage to GUI
        emit frameReady(img);

        if (ioctl(v4l2_fd, VIDIOC_QBUF, &buf) < 0) {
            qWarning() << "VIDIOC_QBUF failed:" << strerror(errno);
            break;
        }

        // ~60 FPS sleep (conservative)
        struct timespec ts = {0, 16666 * 1000};
        nanosleep(&ts, NULL);
    }

    cleanup();
}

void CaptureThread::cleanup()
{
    if (gb) {
        gpio_buttons_stop(gb);
        gpio_buttons_destroy(gb);
        gb = nullptr;
    }

    if (video_buffer != MAP_FAILED && buf_length > 0) {
        munmap(video_buffer, buf_length);
        video_buffer = MAP_FAILED;
    }
    if (v4l2_fd >= 0) {
        close(v4l2_fd);
        v4l2_fd = -1;
    }
}

void CaptureThread::stopAndWait()
{
    m_running = false;
    requestInterruption();
    quit();
    wait();
    cleanup();
}
