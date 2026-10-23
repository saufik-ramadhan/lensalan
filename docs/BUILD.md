# Build dan menjalankan prototipe

## Status

Prototipe M0 menjalankan satu kamera pada proses desktop atau probe headless. Belum ada Windows Service, retensi, database, grid 12 kamera, ONVIF client, ataupun installer. Menutup proses menghentikan pengujian. Sesi UI AI dibatasi 60 detik; stream/manual probe sekitar 12 detik.

## Toolchain yang diuji

- Windows 11 Pro x64 build 26200.
- MSYS2 UCRT64, GCC 16.1.0, CMake 4.4.2 dan Ninja.
- Qt 6.11.2: Core, Gui, Qml, Quick, QuickControls2, Network.
- GStreamer 1.28.6: base, good, bad plugins.
- ONNX Runtime GPU 1.23.2, CUDA runtime 12.9.79, cuBLAS 12.9.1.4, cuFFT 11.4.1.4, cuDNN 9.10.2.21.
- RTX 3060 12 GB, driver 610.88.

MSVC pada rencana awal belum tersedia di host. Prototipe menggunakan GCC UCRT64 yang sudah terpasang; CMake tetap memiliki konfigurasi compiler-independent. Dukungan build MSVC belum diuji. Jangan mencampur library C++ Qt dari compiler yang berbeda.

## Menyiapkan dependensi

Pasang MSYS2 dan toolchain UCRT64 sesuai [panduan resmi](https://www.msys2.org/). Jalankan perintah berikut di shell UCRT64 dengan instalasi MSYS2 yang sudah diperbarui:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gstreamer mingw-w64-ucrt-x86_64-gst-plugins-base mingw-w64-ucrt-x86_64-gst-plugins-good mingw-w64-ucrt-x86_64-gst-plugins-bad mingw-w64-ucrt-x86_64-qt6-declarative
```

Dependensi AI disimpan di proyek, tidak mengubah instalasi CUDA sistem atau environment Python:

```powershell
python tools/bootstrap_ai.py
```

Memerlukan Python 3.11+ dan internet. Unduhan AI sekitar 1,8 GB sebelum ekstraksi, ditambah dependensi Qt/GStreamer. Script mengambil runtime dari Microsoft, model dari YOLOX, serta wheel DLL NVIDIA dari PyPI; SHA256 wheel dan model diperiksa. Wheel dipakai sebagai bundel DLL, bukan diinstal ke Python. Semua binary/model tetap di `.deps/` yang diabaikan Git.

## Build dan tes

Dari PowerShell di root proyek:

```powershell
.\tools\dev.ps1 build
.\tools\dev.ps1 test
.\tools\dev.ps1 capabilities
.\tools\dev.ps1 ai-test
```

Gunakan `-MsysRoot D:\msys64` bila berbeda dari `C:\msys64`. Launcher `.cmd` membaca variabel environment `OPENNVR_MSYS_ROOT` untuk lokasi alternatif.

Core dapat dibangun tanpa Qt/GStreamer/ONNX, termasuk di Linux:

```sh
cmake --preset core
cmake --build --preset core
ctest --preset core
```

Workflow GitHub Actions disiapkan untuk core, tetapi belum pernah dijalankan di remote karena repositori belum memiliki remote.

## Menghubungkan kamera

1. Buat Camera Account di aplikasi Tapo sesuai [panduan TP-Link](https://www.tp-link.com/us/support/faq/2680/). Ini berbeda dari akun cloud Tapo.
2. Pastikan PC dapat menjangkau port RTSP 554 kamera.
3. Jalankan `OpenNVR.cmd`, masukkan IP, username dan password, lalu pilih **Simpan terenkripsi**.
4. Uji substream dan main stream secara terpisah sebelum menjalankan AI.
5. Pilih **Uji deteksi orang (60 dtk)**. Orang yang memenuhi konfirmasi dua dari tiga sampel memicu klip main stream; post-roll awal adalah 20 detik.

Credential disimpan pada `.local/camera.json` dengan Windows DPAPI milik akun pengguna saat ini. Ini penyimpanan development; migrasi ke identitas Windows Service adalah pekerjaan M1. Jangan memindahkan credential antar-akun lalu menganggapnya dapat didekripsi.

Tombol **Uji klip + pre-roll** memicu recording secara manual setelah sekitar enam detik dan meminta empat detik history; tombol ini digunakan untuk memvalidasi media tanpa menunggu orang. Mode AI meminta pre-roll sepuluh detik. Pre-roll aktual dapat lebih pendek saat startup dan mengikuti keyframe.

Preview saat ini memakai jendela `d3d11videosink` terpisah. Plugin `qml6d3d11sink` tidak tersedia dalam paket MSYS2 yang diuji; integrasi video dalam grid QML belum selesai.

## Probe headless

```powershell
.\tools\dev.ps1 probe-sub
.\tools\dev.ps1 probe-main
.\tools\dev.ps1 detect
```

Probe membaca konfigurasi DPAPI yang sama. Tidak ada password pada command line. Ringkasan ditulis pada `.local/probe-result.json`; jangan menjalankan dua probe memakai path report yang sama secara bersamaan.

## Batas prototipe

- Satu kamera per sesi. Belum ada scheduler inferensi bersama 12 kamera.
- Deteksi full-frame; editor zona dan confidence per kamera belum tersedia.
- Video untuk AI didekode melalui D3D11 lalu diunduh ke CPU untuk preprocessing BGR. Inferensi berjalan di CUDA; pipeline belum zero-copy.
- Kegagalan AI/GPU ditampilkan; tidak beralih diam-diam ke CPU atau motion detection.
- Sesi yang berhenti saat event aktif akan memfinalisasi klip pendek dan menandai `stoppedDuringEvent` pada report; post-roll penuh tidak dijamin pada penghentian sesi.
- File MKV prototipe belum dibagi menjadi segmen 60 detik, belum memiliki retensi otomatis, dan file aktif belum memiliki recovery produksi.
- GPU dalam service/logout, pembacaan 12 kamera, akurasi malam/jauh dan soak test 24 jam belum diverifikasi.

Jangan menafsirkan hasil satu gambar atau satu kamera sebagai pencapaian NFR pada PRD.
