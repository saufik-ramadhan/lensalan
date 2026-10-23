# LensaLAN

Aplikasi desktop Windows untuk memonitor maksimal 12 IP camera di LAN dan merekam event ketika host computer mendeteksi orang.

Visi produk: platform NVR native Windows yang berkembang menuju analitik, otomasi dan integrasi modular. Kapasitas serta fitur yang sudah terbukti tetap dibedakan dari [roadmap jangka panjang](docs/PRODUCT_VISION.md).

Status: live grid 1/4/9/12 dan engine hingga 12 kamera sudah diimplementasikan, dengan satu scheduler CUDA bersama, reconnect/writer per kamera, SQLite, retensi, kontrol IPC dan review event. Dua belas sumber sintetis serta render grid telah diuji; validasi kapasitas 12 kamera RTSP nyata dan soak masih terbuka. Ini belum merupakan NVR produksi.

- [Product Requirements Document](docs/PRD.md)
- [Rencana pengembangan](docs/DEVELOPMENT_PLAN.md)
- [Visi produk dan roadmap](docs/PRODUCT_VISION.md)
- [Build dan menjalankan prototipe](docs/BUILD.md)
- [Catatan pengembangan](docs/DEVELOPMENT_LOG.md)
- [Hasil validasi M0](docs/validation/M0_FEASIBILITY.md)
- [Hasil validasi M1](docs/validation/M1_ACCEPTANCE.md)
- [Pemasangan Windows Service](docs/SERVICE.md)
- [Verifikasi multi-kamera dan live grid](docs/validation/MULTICAMERA_LIVE_GRID.md)

Target awal: TP-Link Tapo C520WS, Intel Core i5-12400F, NVIDIA RTX 3060 12 GB, RAM DDR4 16 GB. Stack yang direncanakan: C++20, Qt 6/QML, GStreamer, ONNX Runtime CUDA, SQLite, dan Windows Service.

Pada komputer dengan service yang sudah terpasang, jalankan `LensaLAN.cmd`; desktop mendeteksi konfigurasi service dan terhubung ke engine di ProgramData. Untuk development tanpa service, pilih **Jalankan engine pengguna**. Engine berjalan terus dan tidak ikut berhenti saat jendela ditutup. Gunakan **Jeda deteksi** atau **Hentikan engine pengguna** untuk mengontrol proses pengguna. Tab Diagnostik mempertahankan probe M0 yang terpisah.

Tab **Live grid** menampilkan substream dari engine tanpa membuka RTSP tambahan. Tab **Kamera** menyimpan hingga 12 profil terenkripsi; katalog diterapkan ke service lewat installer elevated. Runtime service lama perlu diperbarui untuk mengaktifkan kanal media baru. Klik dua kali pada tile untuk fokus satu kamera; gunakan pemilih grid/paginasi untuk kembali atau berpindah kamera.

Data engine development berada di `.local/engine/`, termasuk database, credential terenkripsi bila diprovision, dan rekaman. Default retensi adalah 14 hari/100 GB, dengan reserve ruang disk. Klip M0 pada `.local/recordings/` tetap terpisah dan tidak dibersihkan oleh retensi engine.

Service sebelumnya sudah lulus startup kamera/CUDA, event person di Session 0, dan reboot berdasarkan konfirmasi pengguna. Build multi-kamera 0.3.0-dev kini terpasang; CUDA dan live grid satu kamera nyata sudah diuji. Soak 24 jam belum dilakukan; benchmark 12 kamera nyata, zona deteksi, ekspor klip dan installer desktop lengkap masih tahap berikutnya.

Nama produk dan repository adalah **LensaLAN**. Identifier internal `opennvr`, nama service `OpenNVR`, path data serta variabel `OPENNVR_*` dipertahankan untuk kompatibilitas instalasi development yang sudah ada. Launcher lama tetap tersedia sebagai alias. Detail publikasi dan perlindungan data lokal ada di [PUBLICATION.md](docs/PUBLICATION.md).
