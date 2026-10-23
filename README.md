# OpenNVR

Aplikasi desktop Windows untuk memonitor maksimal 12 IP camera di LAN dan merekam event ketika host computer mendeteksi orang.

Status: implementasi M1 satu kamera. Engine latar belakang, reconnect otomatis, indeks SQLite, segmentasi event, retensi, kontrol IPC, dan daftar/playback event sudah tersedia. Kamera, CUDA dan recovery telah diuji pada proses pengguna. Ini belum merupakan NVR produksi atau implementasi lengkap 12 kamera.

- [Product Requirements Document](docs/PRD.md)
- [Rencana pengembangan](docs/DEVELOPMENT_PLAN.md)
- [Build dan menjalankan prototipe](docs/BUILD.md)
- [Catatan pengembangan](docs/DEVELOPMENT_LOG.md)
- [Hasil validasi M0](docs/validation/M0_FEASIBILITY.md)
- [Hasil validasi M1](docs/validation/M1_ACCEPTANCE.md)
- [Pemasangan Windows Service](docs/SERVICE.md)

Target awal: TP-Link Tapo C520WS, Intel Core i5-12400F, NVIDIA RTX 3060 12 GB, RAM DDR4 16 GB. Stack yang direncanakan: C++20, Qt 6/QML, GStreamer, ONNX Runtime CUDA, SQLite, dan Windows Service.

Pada komputer pengembangan yang sudah disiapkan, jalankan `OpenNVR.cmd` lalu pilih **Jalankan engine pengguna**. Engine berjalan terus dan tidak ikut berhenti saat jendela ditutup. Gunakan **Jeda deteksi** atau **Hentikan engine pengguna** untuk mengontrolnya. Tab Diagnostik mempertahankan probe M0 yang terpisah.

Data engine development berada di `.local/engine/`, termasuk database, credential terenkripsi bila diprovision, dan rekaman. Default retensi adalah 14 hari/100 GB, dengan reserve ruang disk. Klip M0 pada `.local/recordings/` tetap terpisah dan tidak dibersihkan oleh retensi engine.

Entry point SCM, bundle runtime dan installer LocalService tersedia; instalasi serta pengujian GPU setelah logout/reboot memerlukan Administrator dan belum boleh dianggap lulus hanya dari uji proses pengguna. Grid/scheduler 12 kamera, zona deteksi, ekspor klip, serta installer desktop lengkap masih tahap berikutnya.
