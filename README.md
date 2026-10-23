# OpenNVR

Aplikasi desktop Windows untuk memonitor maksimal 12 IP camera di LAN dan merekam event ketika host computer mendeteksi orang.

Status: prototipe M0 satu kamera sudah dapat dibangun dan dijalankan. Main/substream Tapo, rekaman klip tanpa encode ulang, decode D3D11, serta inferensi person CUDA telah diuji. Ini belum merupakan NVR produksi atau implementasi lengkap 12 kamera.

- [Product Requirements Document](docs/PRD.md)
- [Rencana pengembangan](docs/DEVELOPMENT_PLAN.md)
- [Build dan menjalankan prototipe](docs/BUILD.md)
- [Catatan pengembangan](docs/DEVELOPMENT_LOG.md)
- [Hasil validasi M0](docs/validation/M0_FEASIBILITY.md)

Target awal: TP-Link Tapo C520WS, Intel Core i5-12400F, NVIDIA RTX 3060 12 GB, RAM DDR4 16 GB. Stack yang direncanakan: C++20, Qt 6/QML, GStreamer, ONNX Runtime CUDA, SQLite, dan Windows Service.

Pada komputer pengembangan yang sudah disiapkan, jalankan `OpenNVR.cmd` untuk membuka workbench. Gunakan tombol **Uji deteksi orang (60 dtk)** untuk menguji rekaman otomatis; tombol **Uji klip + pre-roll** memakai pemicu manual yang terpisah. Credential disimpan dengan DPAPI dalam `.local/`, diabaikan Git.

Windows Service, database/index event, retensi otomatis, grid multi-kamera dan installer masih masuk tahap berikutnya. Rekaman prototipe disimpan di `.local/recordings/` dan belum dibersihkan otomatis.
