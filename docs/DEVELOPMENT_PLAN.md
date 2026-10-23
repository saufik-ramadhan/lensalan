# Rencana Pengembangan — OpenNVR

Tanggal: 23 Oktober 2026  
Status: draft perencanaan, belum dieksekusi  
Acuan kebutuhan: [PRD v0.1](PRD.md)

## 1. Strategi pengerjaan

Mulai dari pembuktian media, inferensi, dan rekaman pada satu kamera nyata. Perluas ke 12 sumber sebelum menghabiskan waktu pada antarmuka lengkap. Bagian berisiko tinggi adalah kompatibilitas firmware kamera, akurasi person pada substream, GPU dalam Windows Service, serta pemulihan file rekaman.

Pengembangan dilakukan bertahap dengan artefak dan syarat lulus yang dapat diperiksa. Prototipe bukan jaminan produk; setiap target yang tidak tercapai harus memicu pengukuran dan penyesuaian desain, bukan disembunyikan oleh UI.

## 2. Estimasi dan dependensi

Estimasi awal untuk satu pengembang penuh waktu yang berpengalaman dengan C++, Windows, dan multimedia: **27–42 hari kerja, sekitar 6–9 minggu**. Ini perkiraan usaha, bukan komitmen tanggal. Belum termasuk waktu menunggu perangkat, dataset lapangan tambahan, atau perubahan scope.

| Tahap | Estimasi | Bergantung pada | Hasil utama |
|---|---:|---|---|
| M0 — Validasi teknis | 3–5 hari | Kamera, akses LAN, host target | Laporan kelayakan dan keputusan stack/model |
| M1 — Fondasi service dan data | 4–6 hari | M0 | Service, IPC aman, SQLite, konfigurasi |
| M2 — Person detection | 4–6 hari | M0, fondasi M1 | Inferensi GPU dan aturan pemicu terukur |
| M3 — Event recording | 5–7 hari | M1–M2 | Buffer, writer, indeks, retensi, recovery |
| M4 — Desktop monitoring dan review | 4–6 hari | Protokol M1 dan media M3 | Grid, konfigurasi, event, playback, ekspor |
| M5 — Integrasi dan ketahanan | 4–7 hari | M1–M4 | Uji 12 kamera, akurasi dan soak |
| M6 — Packaging dan pilot | 3–5 hari | M5 | Installer, dokumentasi dan hasil penerimaan |

Kode contoh/prototipe UI boleh dibuat lebih awal untuk membuktikan rendering. M4 adalah penyelesaian alur pengguna lengkap, bukan pertama kalinya integrasi GUI diuji.

## 3. Prasyarat pengujian

- Satu C520WS nyata untuk M0; akses 12 kamera atau stream replay representatif untuk pengujian kapasitas. Replay tidak menggantikan uji akhir dengan kamera nyata.
- Camera Account khusus RTSP/ONVIF; credential diberikan melalui setup dan tidak dimasukkan ke source code.
- Versi hardware/firmware kamera, build Windows, driver NVIDIA, kapasitas serta tipe disk dicatat.
- Rekaman contoh siang/malam dan orang dekat/jauh dari lokasi penggunaan, dengan izin penggunaan untuk pengujian.
- Folder rekaman dan disk uji dengan kapasitas memadai. Simulasi disk penuh dilakukan pada volume/kuota uji, bukan memenuhi drive sistem pengguna.

Informasi lokasi rekaman, kapasitas disk, dan retensi akhir masih perlu ditetapkan. Pengembangan prototipe dapat berjalan dengan nilai sementara pada PRD.

## 4. M0 — Validasi teknis satu kamera

### Pekerjaan

1. Uji `/stream1` dan `/stream2`, autentikasi, codec, resolusi native, FPS, bitrate, dan interval keyframe.
2. Bandingkan transport RTSP TCP/UDP pada LAN target; gunakan TCP sebagai default awal jika hasil mendukung.
3. Buktikan decode D3D11 dan rendering `qml6d3d11sink` dengan kombinasi build Qt/GStreamer yang sama.
4. Jalankan inferensi ONNX CUDA dari C++ menggunakan satu model ringan; ukur preprocessing, transfer data, inferensi, dan postprocessing secara terpisah.
5. Uji kandidat model pada orang jauh dan malam hari. Periksa izin distribusi model sebelum mematok pilihan.
6. Jalankan decode dan inferensi dari service percobaan saat desktop terbuka, logout, dan setelah reboot.
7. Buktikan main stream dapat dibuffer dan dimux tanpa encode ulang; periksa hasil mulai keyframe, finalisasi dan crash.
8. Uji distribusi access unit terkompresi ke satu klien desktop melalui Named Pipe, termasuk codec config, reconnect, dan klien lambat.

### Artefak

- `docs/validation/M0_FEASIBILITY.md`: pengukuran, batasan, hasil kamera, dan syarat lanjut.
- `docs/validation/DEPENDENCY_MATRIX.md`: compiler, arsitektur, versi Qt/GStreamer/ONNX/CUDA/cuDNN/driver.
- `docs/adr/0001-media-and-service.md`: keputusan integrasi media dan pemisahan proses.
- `docs/adr/0002-person-model.md`: model, lisensi, input tensor, preprocessing, dan hasil awal.

File di atas dibuat pada pelaksanaan M0, belum ada pada fase dokumen ini.

### Syarat lulus

Satu kamera dapat menampilkan live video, menjalankan AI di service, dan menghasilkan klip yang dapat diputar. Jika substream gagal mengenali orang pada jarak yang dibutuhkan, selesaikan evaluasi alternatif resolusi/model sebelum membangun sistem 12 kamera. Jika GPU tidak bekerja pada service, revisi arsitektur secara eksplisit sebelum M1.

## 5. M1 — Fondasi aplikasi dan service

### Pekerjaan

- Susun proyek CMake dan pin dependensi yang lulus M0.
- Buat Windows Service dengan start/stop terkendali, autostart, recovery, dan identitas berhak minimum yang sesuai.
- Implementasikan schema SQLite, migrasi, dan single-owner akses database pada service.
- Buat API IPC versi awal untuk konfigurasi kamera, status, event dan storage; batasi ACL dan validasi ukuran/request.
- Lindungi credential menggunakan DPAPI di bawah identitas service; uji akses setelah reboot dan setelah perubahan konfigurasi.
- Implementasikan stream supervisor, backoff reconnect, status per komponen, serta log terstruktur dengan penyamaran rahasia.
- Buat kanal kontrol dan media terpisah agar klien lambat tidak memblokir operasi service.

### Syarat lulus

FR-01, FR-02, FR-09, FR-10 dan FR-11 memiliki implementasi dasar. Service memuat konfigurasi setelah restart, menolak kamera ke-13, tidak membocorkan credential, dan menolak IPC dari identitas yang tidak diizinkan. Desktop belum harus memiliki semua halaman.

## 6. M2 — Person detection pada host

### Pekerjaan

- Implementasikan adapter inferensi ONNX Runtime C++ CUDA dengan warmup dan health check.
- Buat scheduler bersama untuk maksimal 12 kamera: antrean terbatas, frame terbaru, fairness dan metrik per kamera.
- Implementasikan pemetaan koordinat hasil model ke frame sumber, confidence, filter kelas person dan zona.
- Implementasikan konfirmasi temporal dan sinyal `person_confirmed`, `person_seen`, serta `detector_unavailable`.
- Simpan metadata model dan parameter agar hasil pengujian dapat direproduksi.
- Evaluasi akurasi pada klip terpisah dari data tuning; catat kegagalan malam, orang jauh, occlusion dan perubahan arah kamera.

### Syarat lulus

FR-04 dan FR-05 bekerja. Person terkonfirmasi hanya pada zona/aturan yang sesuai, kegagalan AI terlihat, dan scheduler memenuhi target awal NFR-02 pada sumber replay. Benchmark akhir tetap dilakukan di M5.

## 7. M3 — Rekaman event, storage dan pemulihan

### Pekerjaan

- Buat ring buffer terkompresi per kamera, dengan batas memori, retensi GOP, codec config dan timestamp.
- Petakan timestamp main/substream ke clock host; deteksi reset timestamp dan perubahan parameter stream.
- Implementasikan state machine `idle → recording → post_roll → finalizing`, dengan status gangguan terpisah.
- Mulai klip dari keyframe yang sesuai pre-roll; jangan menunggu event baru untuk membuka main stream.
- Tulis MKV bersegmen sekitar 60 detik dengan satu event ID; uji file aktif, finalisasi asinkron dan backpressure.
- Simpan event/segmen dengan status transisi; tangani crash di antara penulisan file dan commit indeks.
- Implementasikan retensi, kuota, cadangan disk, serta cleanup hanya pada direktori yang dikelola aplikasi.
- Implementasikan reconciliation startup: segmen hilang, file yatim, segmen partial dan penghapusan yang belum selesai.
- Siapkan API event, manifest playback, thumbnail, dan ekspor event.

### Syarat lulus

FR-06, FR-08, FR-09, FR-12 dan backend FR-13 bekerja. Uji membuktikan pre/post-roll, perpanjangan event, 12 writer bersamaan, disk penuh, dan recovery. Semua segmen yang selesai difinalisasi tetap dapat diputar setelah service dihentikan paksa.

## 8. M4 — Desktop monitoring dan event review

### Pekerjaan

- Bangun shell QML, navigasi Live/Event/Pengaturan/Status, dan koneksi IPC.
- Implementasikan tambah/edit/uji kamera, status main/substream, dan setup storage.
- Implementasikan grid 1/4/9/12, fullscreen, waktu frame terakhir dan indikator AI/recording.
- Terima paket terkompresi dari service lalu decode di desktop; kendalikan pergantian stream dan startup keyframe tanpa menambah koneksi kamera.
- Buat editor zona dan sensitivitas dengan preview hasil deteksi.
- Implementasikan pencarian event berdasarkan tanggal/kamera, thumbnail dan status partial/gap.
- Implementasikan playback lintas segmen, seek, dan ekspor event ke lokasi pilihan pengguna.
- Tampilkan secara eksplisit kondisi service terputus, model belum siap, kamera offline dan storage bermasalah.

### Syarat lulus

FR-03, FR-05, FR-07, FR-12 dan FR-13 dapat dipakai melalui UI. Menutup atau crash desktop tidak memutus recording. GUI yang lambat tidak menahan writer atau antrean AI.

## 9. M5 — Integrasi, benchmark dan ketahanan

### Matriks verifikasi

| Area | Skenario | Bukti yang disimpan |
|---|---|---|
| Kapasitas | 1/4/8/12 kamera; GUI terbuka/tertutup; satu fullscreen | CPU, GPU decode/compute, VRAM, RAM, FPS, latensi |
| AI | Dataset siang/malam, dekat/jauh, diam, lintas zona, negatif | Event recall, false event/camera-hour, versi model |
| Event | Masuk/keluar singkat, orang menetap, deteksi berdekatan, 12 event bersamaan | Manifest dan klip pre/post-roll yang diperiksa |
| Media | Keyframe jarang, timestamp reset, codec config berubah, stream putus | Log pemulihan dan status gap/partial |
| Storage | Kuota tercapai, disk penuh, folder tidak dapat ditulis, volume terlepas | Tidak ada file aktif terhapus; status kegagalan jelas |
| Recovery | Kill service, reboot terkontrol, restart saat finalisasi/cleanup | Segmen finalized utuh dan indeks dapat dipulihkan |
| Windows | Logout/login, desktop crash, service restart, sleep/resume | Rekaman tetap aktif pada logout; gap sleep ditandai |
| Akses lokal | IPC tanpa izin, payload tidak valid, log/config | Penolakan aman dan tidak ada credential plaintext |
| Ketahanan | Soak 24 jam dengan gangguan terjadwal | Laporan tidak crash/hang dan tren pemakaian memori |

Uji simulasi crash tidak membuktikan ketahanan terhadap semua bentuk kehilangan daya. Jika dibutuhkan, uji daya dilakukan di lingkungan khusus dan dicatat terpisah.

### Syarat lulus

Semua target NFR pada PRD dinilai dengan hasil dan konteks yang tertulis. Optimasi diarahkan pada bottleneck terukur. Jika target tidak tercapai, dokumentasikan pilihan: model/resolusi lain, FPS inferensi, overhead UI, atau batas dukungan; jangan mengubah target diam-diam.

Artefak: `docs/validation/M5_ACCEPTANCE_REPORT.md`, konfigurasi benchmark, dan manifest dataset tanpa menyertakan credential atau video privat secara otomatis.

## 10. M6 — Installer dan pilot

### Pekerjaan

- Buat installer x64 untuk desktop, service, model, runtime, dan dependensi yang boleh didistribusikan.
- Pastikan konfigurasi, database, model dan rekaman tidak disimpan di direktori instalasi yang memerlukan hak tulis admin.
- Uji instalasi bersih pada host target, start otomatis, upgrade percobaan, rollback konfigurasi bila migrasi gagal, dan uninstall.
- Uninstall menghentikan service; penghapusan rekaman harus menjadi pilihan eksplisit, bukan default.
- Sertakan panduan Camera Account Tapo, setup folder/kuota, deteksi zona, review event dan troubleshooting.
- Jalankan pilot beberapa hari di lokasi nyata. Tetapkan ukuran disk dan retensi berdasarkan durasi event aktual.
- Review lisensi Qt, plugin GStreamer, runtime dan model; sertakan notice serta persyaratan distribusi yang relevan.

### Syarat lulus

Installer, panduan dan laporan penerimaan tersedia. Pengecualian terbuka memiliki dampak serta keputusan tertulis sebelum MVP dinyatakan selesai.

## 11. Struktur repositori yang diusulkan

```text
opennvr/
  CMakeLists.txt
  apps/
    desktop/            # QML, view model, playback dan rendering
    service/            # Windows Service entry point dan lifecycle
  libs/
    domain/             # Kamera, event, kebijakan dan tipe data
    ipc/                # Protokol kontrol/media, autentikasi lokal
    media/              # RTSP, parser, clock dan ring buffer
    detection/          # ONNX, preprocessing, scheduler dan zona
    recording/          # Event state machine, writer dan manifest
    storage/            # SQLite, retensi, reconciliation
    platform/           # DPAPI, ACL dan integrasi Windows
  tests/
    unit/
    integration/
    fixtures/           # Media sintetis atau yang diizinkan
  tools/                # Replay, benchmark dan inspeksi klip
  packaging/
  models/               # Manifest model/lisensi; binary sesuai kebijakan
  docs/
    PRD.md
    DEVELOPMENT_PLAN.md
    adr/
    validation/
```

Struktur ini adalah rencana. Folder dan source implementasi belum dibuat. Runtime media, model besar dan rekaman pengguna tidak dimasukkan ke Git secara otomatis.

## 12. Pengujian otomatis yang bernilai

- Unit: konfirmasi temporal, aturan zona, pemetaan koordinat, perpanjangan post-roll, dan race deteksi saat finalisasi.
- Unit: kebijakan retensi, perlindungan file aktif dan validasi path terkelola.
- Integration: ring buffer/keyframe, timestamp discontinuity, klip lintas segmen dan muxer dengan media sintetis.
- Integration: transaksi SQLite dan reconciliation setelah kegagalan terinjeksi.
- Integration: framing IPC, payload tidak valid, backpressure dan reconnect pelanggan.
- Hardware: decode GPU, ONNX CUDA, perilaku logout/service, firmware kamera dan kapasitas nyata; pengujian ini tidak digantikan mock.

CI dapat menjalankan build dan pengujian deterministik. Hasil GPU/firmware ditulis dari pengujian host target dan dilampirkan pada laporan penerimaan.

## 13. Pemetaan kebutuhan ke milestone

| Kebutuhan | Implementasi utama | Penerimaan akhir |
|---|---|---|
| FR-01, FR-02 | M1, M4 | M5 |
| FR-03 | M4 | M5 |
| FR-04 | M2 | M5 |
| FR-05 | M2, M4 | M5 |
| FR-06 | M3 | M5 |
| FR-07 | M3, M4 | M5 |
| FR-08 | M3 | M5 |
| FR-09 | M1, M3 | M5 |
| FR-10, FR-11 | M1, M6 | M5, M6 |
| FR-12 | M1–M4 | M5 |
| FR-13 | M3, M4 | M5 |
| NFR-01–NFR-10 | M0–M4 sesuai komponen | M5 |
| FR-14, FR-15 | Setelah MVP | Rencana terpisah |

## 14. Keputusan berikutnya

Sebelum pilot, tetapkan lokasi dan kapasitas disk, retensi akhir, versi kamera/firmware, serta model deteksi hasil M0. Dokumen ini tidak mengasumsikan tersedianya 12 kamera, hasil benchmark tertentu, atau dukungan AI event ONVIF.

Langkah implementasi pertama adalah M0: buktikan satu kamera, inferensi di service, dan klip dengan pre-roll sebelum memperluas sistem.
