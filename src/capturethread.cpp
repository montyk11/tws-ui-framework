// capturethread.cpp (defensive, ready-to-paste)
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
#include <jpeglib.h>
#include <atomic>

// GUI global visible to this translation unit (declared in mainwindow.cpp)
extern std::atomic<int> palette_mode_global;

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
      video_buffer(nullptr),
      buf_length(0),
      gb(nullptr)
{
    qDebug() << "[CaptureThread] constructed this=" << this << " palette_atomic addr=" << &m_palette_atomic;
}

CaptureThread::~CaptureThread()
{
    stopAndWait();
}

const char* CaptureThread::paletteName(int idx)
{
    if (idx < 0) idx = 0;
    idx = idx % NUM_PALETTES;
    return palette_names[idx];
}

void CaptureThread::setPaletteIndex(int idx)
{
    qDebug() << "[CaptureThread::setPaletteIndex] called with idx=" << idx;
    if (idx < 0) idx = 0;
    if (idx >= NUM_PALETTES) idx = idx % NUM_PALETTES;
    m_palette_atomic.store(idx);
    palette_mode_global.store(idx);
    emit paletteIndexChanged(idx);
    qDebug() << "[CaptureThread::setPaletteIndex] stored idx =" << idx << "(" << paletteName(idx) << ")";
}

/* Palette conversion helper */
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

/* gpio callback -> forwards into thread object */
static void gpio_button_cb(unsigned int offset, int value, void *user)
{
    if (!user) return;
    CaptureThread *ct = reinterpret_cast<CaptureThread*>(user);
    QMetaObject::invokeMethod(ct, "handleGpioEvent", Qt::QueuedConnection,
                              Q_ARG(unsigned int, offset),
                              Q_ARG(int, value));
}

void CaptureThread::handleGpioEvent(unsigned int offset, int value)
{
    emit gpioPressed(offset, value);
}

/* MJPEG decompress helper (uses libjpeg) */
static int decompress_mjpeg_to_rgb_inplace(const uint8_t *in_buf, size_t in_size,
                                           uint8_t *out_buf, size_t out_buf_sz,
                                           int *out_w, int *out_h, int *out_components)
{
    if (!in_buf || !out_buf) return -1;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW rowptr[1];

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, in_buf, in_size);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    jpeg_start_decompress(&cinfo);

    *out_w = cinfo.output_width;
    *out_h = cinfo.output_height;
    *out_components = cinfo.output_components;

    size_t row_stride = (size_t)(*out_w) * (size_t)(*out_components);
    size_t needed = row_stride * (size_t)(*out_h);
    if (needed > out_buf_sz) {
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return -1;
    }

    while (cinfo.output_scanline < cinfo.output_height) {
        size_t y = cinfo.output_scanline;
        rowptr[0] = out_buf + y * row_stride;
        if (jpeg_read_scanlines(&cinfo, rowptr, 1) != 1) {
            jpeg_finish_decompress(&cinfo);
            jpeg_destroy_decompress(&cinfo);
            return -1;
        }
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return 0;
}

void CaptureThread::run()
{
    m_running = true;
    qDebug() << "[CaptureThread] run() starting";

    v4l2_fd = open(VIDEO_DEVICE, O_RDWR);
    if (v4l2_fd < 0) {
        qWarning() << "Failed to open video device" << VIDEO_DEVICE << ":" << strerror(errno);
        m_running = false;
        return;
    }

    // set format
    struct v4l2_format local_fmt;
    memset(&local_fmt, 0, sizeof(local_fmt));
    local_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    local_fmt.fmt.pix.width = WIDTH;
    local_fmt.fmt.pix.height = HEIGHT;
    local_fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    local_fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (ioctl(v4l2_fd, VIDIOC_S_FMT, &local_fmt) < 0) {
        qWarning() << "VIDIOC_S_FMT failed:" << strerror(errno);
        close(v4l2_fd);
        v4l2_fd = -1;
        m_running = false;
        return;
    }

    // request buffers
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(v4l2_fd, VIDIOC_REQBUFS, &req) < 0) {
        qWarning() << "VIDIOC_REQBUFS failed:" << strerror(errno);
        close(v4l2_fd); v4l2_fd = -1; m_running = false; return;
    }

    // container for buffers
    struct v4l2_buffer *bufs = nullptr;
    void **mapped_ptrs = nullptr;
    bufs = (struct v4l2_buffer*)calloc(req.count, sizeof(struct v4l2_buffer));
    mapped_ptrs = (void**)calloc(req.count, sizeof(void*));
    if (!bufs || !mapped_ptrs) {
        qWarning() << "Out of memory allocating bufs/mapped_ptrs";
        free(bufs); free(mapped_ptrs);
        close(v4l2_fd); v4l2_fd = -1; m_running = false; return;
    }

    bool buffers_ok = true;
    for (uint32_t i = 0; i < (uint32_t)req.count; ++i) {
        memset(&bufs[i], 0, sizeof(struct v4l2_buffer));
        bufs[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        bufs[i].memory = V4L2_MEMORY_MMAP;
        bufs[i].index = i;
        if (ioctl(v4l2_fd, VIDIOC_QUERYBUF, &bufs[i]) < 0) {
            qWarning() << "VIDIOC_QUERYBUF failed for index" << i << ":" << strerror(errno);
            buffers_ok = false;
            break;
        }
        mapped_ptrs[i] = mmap(NULL, bufs[i].length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd, bufs[i].m.offset);
        if (mapped_ptrs[i] == MAP_FAILED) {
            qWarning() << "mmap failed for index" << i << ":" << strerror(errno);
            mapped_ptrs[i] = nullptr;
            buffers_ok = false;
            break;
        }
        if (ioctl(v4l2_fd, VIDIOC_QBUF, &bufs[i]) < 0) {
            qWarning() << "VIDIOC_QBUF failed for index" << i << ":" << strerror(errno);
            buffers_ok = false;
            break;
        }
    }

    if (!buffers_ok) {
        qWarning() << "Buffer setup unsuccessful - cleaning up";
        for (uint32_t j = 0; j < (uint32_t)req.count; ++j) {
            if (mapped_ptrs[j]) munmap(mapped_ptrs[j], bufs[j].length);
        }
        free(bufs); free(mapped_ptrs);
        close(v4l2_fd); v4l2_fd = -1;
        m_running = false;
        return;
    }

    // store first buffer info for legacy members
    video_buffer = mapped_ptrs[0];
    buf_length = bufs[0].length;

    // start stream
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(v4l2_fd, VIDIOC_STREAMON, &type) < 0) {
        qWarning() << "VIDIOC_STREAMON failed:" << strerror(errno);
        for (uint32_t i = 0; i < (uint32_t)req.count; ++i) if (mapped_ptrs[i]) munmap(mapped_ptrs[i], bufs[i].length);
        free(bufs); free(mapped_ptrs);
        close(v4l2_fd); v4l2_fd = -1;
        m_running = false;
        return;
    }

    // Try to initialize gpio buttons but tolerate failures (don't crash)
    const unsigned int offsets[] = {0,1,4,6};
    const char *names[] = {"PF0","PF1","PF4","PF6"};
    gb = gpio_buttons_create("/dev/gpiochip5", offsets, names, 4, 200, 20000);
    if (!gb) {
        qDebug() << "gpio_buttons_create returned NULL (not fatal)";
    } else {
        for (int i = 0; i < 4; ++i) gpio_buttons_register_callback(gb, i, gpio_button_cb, this);
        gpio_buttons_start(gb);
    }

    // Precompute palette LUT
    uint32_t palette_lut[NUM_PALETTES][256];
    for (int p = 0; p < NUM_PALETTES; ++p) {
        for (int v = 0; v < 256; ++v) {
            uint8_t r,g,b;
            uint16_t gray16 = (uint16_t)(v << 8);
            gray_to_color_palette(gray16, r, g, b, p);
            palette_lut[p][v] = qRgba(r,g,b,0xFF);
        }
    }

    size_t max_decoded_sz = (size_t)WIDTH * (size_t)HEIGHT * 3;
    uint8_t *reusable_decoded = (uint8_t*)malloc(max_decoded_sz);
    if (!reusable_decoded) {
        qWarning() << "Failed to allocate decoded buffer; will skip decode";
    }

    qDebug() << "[CaptureThread] streaming started";

    // frame loop
    while (m_running) {
        struct v4l2_buffer dq;
        memset(&dq, 0, sizeof(dq));
        dq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        dq.memory = V4L2_MEMORY_MMAP;

        if (ioctl(v4l2_fd, VIDIOC_DQBUF, &dq) < 0) {
            qWarning() << "VIDIOC_DQBUF failed:" << strerror(errno);
            break;
        }

        // pointer safe check
        uint8_t *buf_ptr = nullptr;
        if ((uint32_t)dq.index < (uint32_t)req.count) buf_ptr = reinterpret_cast<uint8_t*>(mapped_ptrs[dq.index]);
        size_t jpeg_size = dq.bytesused;
        int dec_w = 0, dec_h = 0, dec_components = 0;
        bool decode_ok = false;

        if (buf_ptr && jpeg_size >= 3 && buf_ptr[0]==0xFF && buf_ptr[1]==0xD8 && buf_ptr[2]==0xFF && reusable_decoded) {
            if (decompress_mjpeg_to_rgb_inplace(buf_ptr, jpeg_size, reusable_decoded, max_decoded_sz, &dec_w, &dec_h, &dec_components) == 0) {
                decode_ok = true;
            } else {
                qWarning() << "MJPEG decode failed for this frame (skipping)";
            }
        } else {
            qWarning() << "Frame not MJPEG or buffer null size=" << (int)jpeg_size;
        }

        if (decode_ok && dec_w > 0 && dec_h > 0 && reusable_decoded) {
            QImage img(WIDTH, HEIGHT, QImage::Format_ARGB32);
            int mode_idx = m_palette_atomic.load();
            if (mode_idx < 0) mode_idx = 0;
            if (mode_idx >= NUM_PALETTES) mode_idx %= NUM_PALETTES;
            uint32_t *lut = palette_lut[mode_idx];

            qDebug() << "[CaptureThread] frame loop using palette idx =" << mode_idx << "(" << paletteName(mode_idx) << ")";

            int row_stride = dec_w * dec_components;
            for (int y = 0; y < HEIGHT; ++y) {
                uint32_t *dest = reinterpret_cast<uint32_t*>(img.scanLine(y));
                if (y < dec_h) {
                    uint8_t *src_row = reusable_decoded + y * row_stride;
                    for (int x = 0; x < WIDTH; ++x) {
                        if (x < dec_w) {
                            uint8_t r = src_row[x*dec_components + 0];
                            uint8_t g = src_row[x*dec_components + 1];
                            uint8_t b = src_row[x*dec_components + 2];
                            uint8_t lum = (uint8_t)(((19595u * r + 38470u * g + 7471u * b) >> 16) & 0xFFu);
                            dest[x] = lut[lum];
                        } else {
                            dest[x] = qRgba(0,0,0,0xFF);
                        }
                    }
                } else {
                    for (int x = 0; x < WIDTH; ++x) dest[x] = qRgba(0,0,0,0xFF);
                }
            }

            emit frameReady(img);
        }

        if (ioctl(v4l2_fd, VIDIOC_QBUF, &dq) < 0) {
            qWarning() << "VIDIOC_QBUF failed when requeueing:" << strerror(errno);
            break;
        }

        // throttle
        struct timespec ts = {0, 16666 * 1000};
        nanosleep(&ts, nullptr);
    }

    // shutdown safe
    qDebug() << "[CaptureThread] stopping stream and cleaning up";

    int type_off = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(v4l2_fd, VIDIOC_STREAMOFF, &type_off);

    // unmap and free only if those pointers were allocated
    for (uint32_t i = 0; i < (uint32_t)req.count; ++i) {
        if (mapped_ptrs && mapped_ptrs[i]) {
            if (bufs && bufs[i].length) munmap(mapped_ptrs[i], bufs[i].length);
            mapped_ptrs[i] = nullptr;
        }
    }
    free(bufs); bufs = nullptr;

    if (mapped_ptrs) { free(mapped_ptrs); mapped_ptrs = nullptr; }

    if (reusable_decoded) { free(reusable_decoded); reusable_decoded = nullptr; }

    cleanup();
    qDebug() << "[CaptureThread] run() exit";
}

void CaptureThread::cleanup()
{
    if (gb) {
        gpio_buttons_stop(gb);
        gpio_buttons_destroy(gb);
        gb = nullptr;
    }

    if (video_buffer != MAP_FAILED && buf_length > 0 && video_buffer != nullptr) {
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
