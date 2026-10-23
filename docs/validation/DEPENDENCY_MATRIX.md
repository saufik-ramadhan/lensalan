# Dependency matrix — M0

Tanggal verifikasi: 23 Oktober 2026. Matriks ini merekam build development yang diuji, bukan lockfile installer produksi.

| Komponen | Versi/bundle | Hasil |
|---|---|---|
| Windows | 11 Pro x64, 26200 | Build dan runtime berhasil |
| CPU / GPU | i5-12400F / RTX 3060 12 GB | Satu kamera dan inferensi berhasil |
| Driver NVIDIA | 610.88 | D3D11 dan CUDA berhasil |
| GCC UCRT64 | 16.1.0-5 | C++20 berhasil |
| CMake / Ninja | 4.4.2 / 1.13.2 | Configure/build/CTest berhasil |
| Qt base/declarative | 6.11.2 | QML smoke test berhasil |
| GStreamer core/base/good/bad | 1.28.6 | RTSP, H.264, MKV, D3D11 berhasil |
| qml6d3d11sink | Tidak disertakan dalam paket ini | Grid GPU dalam QML belum diuji |
| ONNX Runtime GPU SDK | 1.23.2, Windows x64 | C API dynamic loading dan CUDA berhasil |
| NVIDIA CUDA runtime | 12.9.79 | DLL lokal per proses |
| NVIDIA cuBLAS | 12.9.1.4 | DLL lokal per proses |
| NVIDIA cuFFT | 11.4.1.4 | Diperlukan CUDA provider DLL |
| NVIDIA cuDNN | 9.10.2.21 | DLL lokal per proses |
| YOLOX-Tiny ONNX | Release 0.1.1rc0, input 416 | Uji positif gambar dan kamera berhasil |
| Tapo C520WS firmware/hardware | Belum diinventarisasi | Jangan menggeneralisasi ke semua revisi |
| MSVC | Belum tersedia/diuji | Masih target evaluasi packaging |

Checksum model dan alasan preprocessing tercatat di [ADR model](../adr/0002-m0-person-model.md). Cara mendapatkan dependensi ada di [BUILD](../BUILD.md).
