# 🛰️ ThermalScope-UI  

### **Smart Thermal Imaging Interface for Embedded Systems (STM32MP1)**  
_A complete real-time thermal camera viewer and management application built with Qt, V4L2, and GPIO-driven controls._

---

## 🔍 Overview  

**ThermalScope-UI** is a Qt/C++ application developed for **Embedded Linux (STM32MP157F-DK2)** to stream, visualize, and manage thermal camera data in real time.  

It demonstrates a **complete imaging pipeline** — from raw V4L2 capture and palette mapping to asynchronous file I/O, responsive UI overlays, and media management.  

Designed as a **production-ready embedded UX**, this project showcases deep understanding of:  
- Multithreaded C++ design on resource-constrained systems  
- Qt signal/slot architecture and concurrent I/O  
- V4L2 video capture and libjpeg integration  
- GPIO-based human-machine interface (HMI) logic  
- Efficient, user-focused UI rendering and data persistence  

---

## ⚙️ Hardware and Software Used  

### 🧩 Hardware  
- **Processor:** STM32MP157F (Cortex-A7 + Cortex-M4)  
- **Evaluation board:** STM32MP157F-DK2  
- **Display:** HDMI / LCD (via DRM or Wayland)  
- **Camera:** USB thermal camera (Y16 or MJPEG output)  
- **GPIO Inputs:**  
  - PF0 – Zoom control  
  - PF1 – Snapshot capture  
  - PF6 – Menu / Exit / Delete action  

---

### 💻 Software Stack  
| Component | Technology |
|------------|-------------|
| **OS** | Embedded Linux (Yocto / OpenSTLinux) |
| **Framework** | Qt 5 / QtConcurrent / libjpeg |
| **APIs** | V4L2 (Video4Linux2), DRM (optional for display) |
| **Language** | C++17 |
| **Build System** | CMake |

---

## 🚀 Features  

### ✅ Live Thermal Video Stream  
- Real-time MJPEG decoding using V4L2 and libjpeg  
- Dynamic color palette mapping *(IRONBOW, RAINBOW, ARCTIC, LAVA, BLACKHOT, WHITEHOT)*  

### ✅ Hardware-Controlled User Interface  
- GPIO-driven interactions (menu, navigation, zoom, snapshot)  
- Integer digital zoom *(1× / 2× / 4×)*  
- On-screen overlays *(zoom level, palette info)*  

### ✅ Snapshot Capture & File Management  
- Captures frames as **JPEG** (fast background save using `QtConcurrent::run()`)  
- Atomic file saving *(write → rename to avoid corruption)*  
- “Saved” overlay indicator flashes briefly  
- Files stored in `/usr/local/snapshots`  

### ✅ Integrated Media Viewer  
- In-app image gallery with **thumbnail strip**  
- Navigate images using **PF0 / PF1**  
- Exit viewer with **PF6**  
- Delete current photo with **PF1 + PF6 (simultaneous press)**  

### ✅ Performance-Oriented Design  
- Dedicated **CaptureThread** for V4L2 and decoding  
- Async disk I/O — zero UI stalls  
- Optimized for **SDRAM and embedded display controllers**  

---

## 🔧 Integration Instructions  

### 1️⃣ Clone & Configure  
```bash
git clone https://github.com/vedantkhamkar/ThermalScope-UI.git
cd ThermalScope-UI
mkdir build && cd build
cmake ..
make -j$(nproc)
