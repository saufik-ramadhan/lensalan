# ADR 0001 — Toolchain dan batas proses pada prototipe M0

Tanggal: 23 Oktober 2026

Status: diterapkan untuk M0; belum menetapkan packaging produksi

## Konteks

Host sudah memiliki MSYS2 UCRT64/GCC, tetapi MSVC, Qt dan GStreamer belum tersedia. Kita perlu membuktikan stream kamera dan inferensi pada RTX 3060 sebelum membangun service serta IPC produksi.

## Keputusan

- Gunakan toolchain UCRT64 konsisten untuk Qt, GStreamer dan kode C++ prototipe.
- Pakai API C ONNX Runtime melalui wrapper header dan dynamic loading DLL Microsoft. Tambahkan pemetaan `_stdcall` ke `__stdcall` hanya pada translation unit adapter MinGW.
- Simpan SDK, model dan DLL CUDA/cuDNN di `.deps/`. Tambahan DLL search path hanya berlaku pada proses aplikasi; tidak mengubah PATH atau CUDA tingkat sistem.
- Jalankan M0 sebagai desktop/probe headless di sesi pengguna. Jangan menginstalnya sebagai Windows Service sebelum uji Session 0.
- Gunakan preview D3D11 di jendela terpisah karena paket GStreamer saat ini tidak menyertakan `qml6d3d11sink`.

## Konsekuensi dan pekerjaan lanjutan

Core tetap dapat diuji tanpa multimedia. M0 dapat menunjukkan kemampuan media dan AI host, tetapi belum membuktikan service/logout, IPC, rendering grid QML, installer, atau 12 kamera. M1 harus menguji identitas service, DPAPI dan GPU dalam Session 0. Evaluasi kembali MSVC serta build plugin Qt/D3D11 sebelum mematok distribusi produksi.
