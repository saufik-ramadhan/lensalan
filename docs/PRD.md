# Product Requirements Document — OpenNVR

Tanggal: 23 Oktober 2026  
Versi: 0.1 — draft untuk review  
Platform: Windows x64  
Bahasa antarmuka awal: Bahasa Indonesia

## 1. Ringkasan produk

OpenNVR adalah aplikasi lokal untuk memonitor maksimal 12 IP camera dan menyimpan klip video ketika komputer host mendeteksi orang. Deteksi berjalan di GPU host, tanpa mengandalkan event AI kamera atau cloud. Pengguna dapat melihat grid kamera, membuka kamera secara fullscreen, mencari event, dan memutar rekaman.

Penerimaan stream, deteksi, serta perekaman dijalankan oleh Windows Service. Aplikasi desktop menjadi antarmuka kontrol dan monitoring. Menutup desktop atau logout tidak menghentikan service; komputer tetap harus menyala dan tidak sleep agar perekaman berlangsung.

## 2. Keputusan yang sudah disepakati

| Aspek | Keputusan |
|---|---|
| Sistem operasi | Windows |
| Kapasitas | Maksimal 12 kamera |
| Kamera awal | TP-Link Tapo C520WS 2K QHD Pan/Tilt |
| Host | Intel Core i5-12400F, RTX 3060 12 GB, RAM DDR4 16 GB |
| Lokasi jaringan | LAN lokal |
| Sumber deteksi orang | Komputer host |
| Jenis rekaman | Klip event berdasarkan deteksi orang |
| Fungsi utama | Live monitoring, deteksi orang, rekaman, pencarian dan playback event |

## 3. Asumsi dan keputusan sementara

Nilai berikut adalah usulan awal yang dapat disesuaikan setelah prototipe. Nilai ini belum merupakan hasil benchmark atau keputusan pengguna.

| Item | Default/usulan |
|---|---|
| Baseline pengujian OS | Windows 11 x64; build dan driver dicatat pada hasil uji |
| Akses pengguna | Satu operator lokal; tidak ada akses jarak jauh pada MVP |
| Retensi | 14 hari, disertai batas pemakaian disk dan ruang cadangan |
| Lokasi dan kuota rekaman | Dipilih saat setup; kapasitas disk aktual belum diketahui |
| Pre-roll | 10 detik sebelum deteksi terkonfirmasi, jika buffer sudah tersedia |
| Post-roll | 20 detik setelah terakhir kali orang terkonfirmasi |
| Frekuensi inferensi | Target awal 3 frame/detik/kamera, dapat dinaikkan setelah benchmark |
| Resolusi input model | Ditetapkan setelah uji akurasi orang dekat dan jauh |
| Audio | Tidak direkam pada MVP |
| Format penyimpanan internal | MKV bersegmen; harus lolos uji crash dan playback |
| Model deteksi | Model ringan berformat ONNX dengan kelas person; varian dan lisensi belum dipilih |
| Kamera uji awal | Satu C520WS; versi hardware dan firmware dicatat saat pengujian |

## 4. Tujuan dan batas cakupan

### Tujuan MVP

1. Menampilkan maksimal 12 kamera secara bersamaan dengan antarmuka responsif.
2. Mendeteksi orang di host, dengan zona deteksi per kamera.
3. Menyimpan klip main stream dengan konteks sebelum dan setelah kejadian.
4. Memungkinkan pengguna meninjau event berdasarkan kamera dan waktu.
5. Melanjutkan operasi setelah gangguan kamera, service, atau restart komputer.
6. Mengelola ruang disk otomatis dan menampilkan gangguan perekaman secara jelas.

### Di luar MVP

- Pengenalan wajah, identitas orang, dan pencarian berdasarkan wajah.
- Deteksi kendaraan/hewan dan analitik perilaku.
- Akses internet, cloud sync, aplikasi mobile, dan notifikasi eksternal.
- Rekaman kontinu sebagai mode produk utama.
- Dukungan lintas platform, multi-host, multioperator, atau lebih dari 12 kamera.
- PTZ manual, auto-tracking buatan aplikasi, audio, serta discovery ONVIF otomatis; masuk kandidat tahap berikutnya.

Deteksi keberadaan orang adalah pemicu awal: orang yang masih terlihat, walaupun sedang diam, tetap dapat memperpanjang event. Pelacakan pergerakan tidak menjadi syarat agar rekaman dimulai. Gerakan daun, kendaraan, atau hewan saja tidak boleh menjadi pemicu yang disengaja.

## 5. Pengguna dan alur utama

### Setup pertama

1. Pengguna memilih folder rekaman, kuota, dan retensi.
2. Pengguna menambahkan kamera melalui IP atau URL RTSP beserta Camera Account Tapo.
3. Aplikasi menguji main stream dan substream, lalu menampilkan codec, resolusi, FPS, dan status.
4. Pengguna memberi nama kamera, menggambar zona deteksi, dan menguji deteksi orang.
5. Setelah model siap dan akses disk lolos, service mengaktifkan monitoring dan recording.

### Monitoring harian

1. Pengguna melihat grid 4×3 atau layout 1/4/9/12 kamera.
2. Klik ganda membuka satu kamera; main stream digunakan untuk fullscreen jika tersedia.
3. Status kamera, AI, dan recording ditampilkan secara terpisah.
4. Kamera yang putus menampilkan waktu frame terakhir, bukan gambar beku yang terlihat live.
5. Pengguna dapat menutup desktop; service tetap bekerja.

### Meninjau kejadian

1. Pengguna membuka daftar event, memilih tanggal dan kamera.
2. Pengguna melihat waktu, durasi, thumbnail, dan status kelengkapan rekaman.
3. Playback memutar seluruh segmen yang termasuk dalam satu event.
4. Pengguna dapat mengekspor event lengkap menjadi satu file MKV tanpa encode ulang jika kompatibel.

## 6. Kebutuhan fungsional

P0 wajib tersedia untuk MVP. P1 adalah pengembangan setelah MVP lolos penerimaan.

| ID | Prioritas | Kebutuhan | Kriteria penerimaan |
|---|---|---|---|
| FR-01 | P0 | Kelola maksimal 12 kamera | Tambah, edit, nonaktifkan, hapus konfigurasi, serta uji koneksi; kamera ke-13 ditolak dengan alasan jelas |
| FR-02 | P0 | Main stream dan substream | Profil stream dapat diuji; kegagalan salah satu stream tidak disamarkan sebagai sukses penuh |
| FR-03 | P0 | Grid dan fullscreen | Layout 1/4/9/12, nama kamera, waktu frame terakhir, dan status koneksi tersedia |
| FR-04 | P0 | Deteksi person pada host | Tidak memerlukan event AI ONVIF atau cloud; status model dan GPU dapat diperiksa |
| FR-05 | P0 | Zona dan sensitivitas | Zona poligon, ambang confidence, dan parameter konfirmasi dapat disetel per kamera dan tersimpan |
| FR-06 | P0 | Rekaman event | Pre-roll, post-roll, penggabungan deteksi berdekatan, serta pemisahan file panjang bekerja tanpa encode ulang video |
| FR-07 | P0 | Daftar dan playback event | Filter kamera/tanggal, thumbnail, durasi, playback berurutan, dan penanda gap/partial tersedia |
| FR-08 | P0 | Retensi dan kuota | Hapus event tertua yang selesai ditulis sesuai kebijakan; file aktif tidak terhapus |
| FR-09 | P0 | Pemulihan otomatis | Retry koneksi dengan backoff; service memeriksa indeks dan file setelah restart |
| FR-10 | P0 | Windows Service | Autostart dan recovery service dikonfigurasi; logout/desktop crash tidak menghentikan deteksi |
| FR-11 | P0 | Keamanan akses lokal | Password terlindungi, tidak masuk log; kanal lokal dibatasi ACL untuk identitas yang berwenang |
| FR-12 | P0 | Status kesehatan | Kamera offline, AI gagal, disk penuh, dan gagal tulis terlihat sebagai keadaan berbeda |
| FR-13 | P0 | Ekspor event | Salinan event lengkap dapat dibuka di luar aplikasi; event bersegmen digabung atau diekspor dengan manifest jika penggabungan gagal |
| FR-14 | P1 | ONVIF discovery dan PTZ | Penemuan dan kontrol diuji pada firmware target sebelum dinyatakan didukung |
| FR-15 | P1 | Audio dan ekspor MP4 | Ditambahkan setelah codec sumber, kebutuhan pengguna, dan interoperabilitas diverifikasi |

Menonaktifkan recording atau menghapus kamera memerlukan tindakan eksplisit pengguna. Menghapus konfigurasi kamera tidak otomatis menghapus rekaman; indeks tetap menyimpan identitas dan nama kamera saat event terjadi.

## 7. Aturan deteksi dan rekaman

### 7.1 Pemilihan frame dan konfirmasi

- Substream digunakan untuk inferensi reguler. Resolusi aktual harus dibaca dari kamera; aplikasi tidak mengasumsikan substream dapat diubah menjadi resolusi tertentu.
- Model hanya menghasilkan pemicu untuk kelas person yang memenuhi confidence dan aturan zona.
- Default konfirmasi awal: minimal dua deteksi valid pada tiga sampel terakhir dari kamera yang sama, dalam jendela maksimum dua detik. Parameter harus dapat dituning.
- Aturan zona menggunakan titik tengah bawah bounding box sebagai referensi awal. Koordinat disimpan secara ternormalisasi.
- Tidak ada motion gate wajib pada MVP: perubahan gambar atau event gerakan kamera tidak menjadi syarat menjalankan AI.
- Penjadwal adil memilih frame terbaru dari setiap kamera. Frame inferensi lama dibuang agar antrean tidak menambah keterlambatan.
- Pergantian arah kamera/PTZ mengubah area yang dilihat; zona tetap relatif terhadap gambar. UI harus menjelaskan perilaku ini.

### 7.2 Buffer dan siklus event

1. Main stream terkompresi diterima terus-menerus ke ring buffer RAM berbatas ukuran dan durasi.
2. Buffer menyimpan timestamp, parameter codec, dan keyframe yang cukup untuk menghasilkan klip yang dapat diputar. Target kedalaman minimal adalah pre-roll ditambah satu GOP terukur, selama batas memori memungkinkan.
3. Saat konfirmasi person terjadi, writer memulai klip dari keyframe pada atau sebelum target pre-roll jika tersedia.
4. Timestamp substream dan main stream dipetakan ke clock host monotonic yang sama; keduanya tidak boleh dianggap memiliki timestamp identik.
5. Deteksi valid berikutnya memperpanjang batas akhir event. Event tetap aktif selama orang terkonfirmasi masih terlihat.
6. Writer menyelesaikan event setelah post-roll berlalu tanpa konfirmasi baru.
7. Event panjang dibagi menjadi segmen sekitar 60 detik pada batas keyframe dan tetap memakai satu event ID.
8. Deteksi pada saat event sedang ditutup tidak boleh hilang; proses finalisasi harus diserialkan dengan pembuatan event baru.

Pre-roll dapat lebih pendek saat startup, reconnect, perubahan codec, atau buffer belum cukup. Batas klip dapat bergeser mengikuti keyframe. Aplikasi mencatat kelengkapan aktual, bukan menjanjikan durasi tepat pada semua kondisi.

### 7.3 Gangguan

- Main stream putus: event diberi gap/partial; deteksi dapat tetap berjalan jika substream tersedia, dengan status video tidak terekam.
- Substream atau AI gagal: status deteksi tidak tersedia; aplikasi tidak menyatakan recording berbasis person tetap sehat. Tidak beralih diam-diam ke motion detection atau rekaman kontinu.
- Disk gagal/penuh: hentikan penulisan secara terkendali dan tampilkan gangguan; monitoring serta AI tetap diupayakan berjalan.
- GPU gagal: lakukan retry terbatas. CPU fallback hanya opsi eksplisit setelah kapasitasnya diuji, bukan fallback otomatis untuk 12 kamera.
- Crash/listrik padam: segmen terakhir mungkin tidak lengkap; segmen yang sudah selesai harus tetap dapat ditemukan dan diputar.
- Komputer sleep/offline: waktu tersebut dicatat sebagai gap setelah pemulihan. Aplikasi tidak mengubah kebijakan power Windows secara diam-diam.

## 8. Arsitektur dan stack

| Lapisan | Pilihan |
|---|---|
| Bahasa/build | C++20, CMake, MSVC x64 |
| Desktop | Qt 6, QML/Qt Quick, rendering Direct3D 11 |
| Media | GStreamer untuk RTSP, parsing, decode, muxing, dan playback |
| AI | ONNX Runtime C++ dengan CUDA Execution Provider |
| Model | Detektor person ringan; model, preprocessing, threshold, dan lisensi dipatok setelah evaluasi |
| Data | SQLite dimiliki service; file video disimpan terpisah |
| Background | Windows Service, watchdog melalui Service Control Manager |
| Kontrol lokal | Named Pipe dengan ACL dan versi protokol |
| Media ke desktop | Kanal Named Pipe terpisah untuk access unit terkompresi, dengan framing dan backpressure |
| Kredensial | Windows DPAPI di bawah identitas service; direktori dan endpoint dibatasi ACL |

### Pembagian tanggung jawab

Service memiliki koneksi main/substream, inferensi, ring buffer, recording, indeks, retensi, dan status kesehatan. Desktop meminta konfigurasi/status melalui IPC, menerima media, menampilkan grid, dan memutar file melalui akses yang dikontrol service. Desktop tidak menulis database secara langsung dan tidak diberi password kamera untuk membuka koneksi tambahan.

Untuk MVP, service membagikan paket video terkompresi ke desktop. Desktop melakukan decode untuk tampilan; service melakukan decode substream untuk AI. Ketika desktop terbuka, ada tambahan decode substream. Ini merupakan tradeoff yang disengaja untuk menghindari ketergantungan pada berbagi tekstur GPU lintas Windows Session 0 dan sesi pengguna. Beban ini wajib diukur pada host target.

Kanal media harus membawa identitas kamera, jenis stream, timestamp, codec/config, flag keyframe, dan panjang payload yang tervalidasi. Pelanggan baru menerima konfigurasi codec dan memulai dari keyframe. Klien lambat tidak boleh menahan recording; setelah drop, decoder dilanjutkan pada keyframe berikutnya.

Integrasi D3D11, CUDA, preprocessing, dan ONNX mungkin memerlukan transfer memori. Zero-copy bukan janji MVP; jumlah transfer dan biayanya diukur sebelum optimasi. Version matrix Qt, GStreamer, ONNX Runtime, CUDA/cuDNN, dan driver harus dipatok dari kombinasi yang lulus prototipe.

### Entitas data minimal

- `Camera`: ID, nama, alamat, referensi credential, profil main/substream, enabled.
- `DetectionConfig`: zona, confidence, aturan konfirmasi, frekuensi, pre/post-roll.
- `Event`: ID, camera ID, nama historis, waktu mulai/akhir UTC, status, model/version.
- `RecordingSegment`: event ID, path relatif, waktu, ukuran, status, informasi codec.
- `DetectionSummary`: event ID, confidence maksimum dan ringkasan deteksi; tidak menyimpan setiap frame secara default.
- `HealthIncident`: komponen, waktu mulai/akhir, kode gangguan, ringkasan tanpa rahasia.
- `StoragePolicy`: direktori, kuota, retensi, ruang cadangan.

Gunakan clock monotonic untuk durasi internal dan UTC untuk indeks; UI menampilkan zona waktu Windows. Perubahan jam atau restart harus menghasilkan mapping/discontinuity yang eksplisit.

## 9. Target kualitas dan performa

Angka berikut adalah target penerimaan sementara, bukan klaim performa perangkat. Benchmark menggunakan host yang disebutkan, 12 C520WS atau sumber ekuivalen yang karakteristiknya dicatat, main stream 1440p H.264 sekitar 15 FPS jika tersedia, dan substream native. Bitrate, driver, firmware, dan model dicatat.

| ID | Target | Cara verifikasi |
|---|---|---|
| NFR-01 | 12 kamera live dan AI aktif bersamaan | Uji dengan GUI terbuka dan tertutup; tidak ada kamera yang kelaparan jadwal |
| NFR-02 | Sedikitnya 90% target 3 inferensi/detik/kamera tercapai pada setiap jendela 60 detik | Metrik per kamera pada beban penuh, termasuk semua kamera memiliki event |
| NFR-03 | P95 latensi live ≤2 detik pada LAN sehat | Uji visual dengan clock/gerakan referensi; kondisi jaringan dicatat |
| NFR-04 | P95 waktu frame siap untuk analisis → konfirmasi person ≤2 detik | Timestamp host; dilaporkan terpisah dari latensi kamera/jaringan |
| NFR-05 | Konfirmasi → writer mulai ≤1 detik pada P95 | Trace event dan file; pre-roll tetap disertakan |
| NFR-06 | Working set seluruh proses OpenNVR ≤8 GB pada kondisi stabil | Sampling 24 jam; ring buffer dan antrean berbatas |
| NFR-07 | Tidak ada crash/hang selama soak 24 jam | Termasuk 12 event bersamaan, putus jaringan, dan desktop dibuka/ditutup |
| NFR-08 | Setelah stream kembali dapat dibaca, reconnect ≤30 detik | Putus/sambung terkontrol; waktu startup kamera tidak dihitung |
| NFR-09 | Semua segmen yang sudah difinalisasi tetap dapat diputar setelah restart paksa | Uji kill service dan reboot; segmen aktif boleh partial dan ditandai |
| NFR-10 | Event recall awal ≥95%; false event rata-rata ≤1/kamera-jam pada dataset evaluasi | Definisi dan protokol evaluasi di bawah; hasil malam/jauh dilaporkan terpisah |

Dataset akurasi minimal: 100 episode orang muncul, termasuk siang/malam, dekat/jauh, diam/bergerak, sebagian tertutup, dan lintas zona; ditambah sedikitnya 24 camera-hours adegan negatif representatif. Recall dihitung dari episode orang yang memicu event dengan overlap waktu yang benar. False event adalah event tanpa orang valid di zona. Klip tuning dan evaluasi dipisahkan. Target tidak dianggap lulus hanya dari demo beberapa klip.

Simpan metrik CPU, GPU decode/compute, VRAM, RAM, FPS input/tampil/inferensi, drop, latensi, bitrate, reconnect, dan throughput disk. CPU/GPU tidak diberi angka jaminan sebelum prototipe.

## 10. Penyimpanan dan retensi

Perkiraan desimal: `GB ≈ bitrate Mbps × durasi detik ÷ 8 ÷ 1000`. Ukur bitrate aktual; event recording mengurangi video permanen, tetapi stream jaringan dan analisis berjalan terus.

Contoh pada 4 Mbps, rata-rata satu jam klip tersimpan per kamera per hari, termasuk pre/post-roll: 12 kamera menggunakan sekitar 21,6 GB/hari atau 648 GB/30 hari, sebelum overhead.

Retensi bekerja pada event selesai: hapus yang melewati umur retensi, lalu hapus event tertua sampai pemakaian berada di bawah kuota dan ruang cadangan terpenuhi. Default cadangan sementara adalah nilai terbesar antara 10 GB dan 5% kapasitas volume; validasi agar folder, kuota, dan cadangan memungkinkan operasi. Jika tidak ada event aman dihapus dan kapasitas tetap tidak cukup, tandai storage degraded dan hentikan recording secara terkendali.

Penghapusan file dan indeks menggunakan status transisi agar dapat dipulihkan setelah crash. Jangan menghapus file di luar direktori rekaman terkelola. Backup manual konfigurasi tidak menyertakan credential plaintext.

## 11. Dependensi dan risiko yang harus dibuktikan

| Risiko/dependensi | Mitigasi dan syarat keputusan |
|---|---|
| Detail hardware/firmware Tapo belum diketahui | Inventaris unit aktual dan uji dua stream sebelum menetapkan kompatibilitas |
| Kapasitas koneksi kamera terbatas | Satu pemilik koneksi di service; GUI menerima stream dari service; uji bersama Tapo App dan opsi penyimpanan kamera |
| Orang kecil pada substream tidak terdeteksi | Uji jarak aktual; jika recall tidak cukup, evaluasi resolusi lebih tinggi dan biaya GPU sebelum lanjut |
| GPU dalam service berbeda dari desktop | Buktikan decode/inferensi pada service setelah logout dan reboot |
| Model lambat atau terlalu banyak false event | Bandingkan model ringan, preprocessing dan threshold pada dataset terpisah |
| Keyframe jarang atau timestamp drift | Ring buffer sadar GOP, pemetaan clock, uji playback dan pre-roll |
| Disk/antivirus memperlambat penulisan | Uji disk aktual, logging kegagalan, dan antrean writer terbatas |
| Lisensi dan packaging dependensi/model | Review izin distribusi dan sertakan notice yang diperlukan sebelum installer rilis |

## 12. Definisi MVP selesai

- Seluruh FR P0 dan target NFR telah diuji; pengecualian ditulis sebagai perubahan scope yang ditinjau pengguna.
- Instalasi bersih, startup otomatis, logout, reboot, upgrade percobaan, dan uninstall telah diverifikasi.
- Event dapat ditemukan, diputar, dan diekspor; gap serta gangguan AI/storage terlihat.
- Dataset, konfigurasi model, version matrix, dan laporan benchmark tersimpan sebagai bukti penerimaan.
- Tidak ada credential di log atau konfigurasi plaintext, dan IPC tidak dapat diakses identitas lokal yang tidak berwenang.
- Panduan setup Tapo, operasi, retensi, dan troubleshooting tersedia.

## 13. Referensi dan batas verifikasi

Referensi ditinjau dalam percakapan perencanaan tanggal 23 Oktober 2026. Belum ada pengujian kamera atau benchmark host secara langsung.

- [Spesifikasi Tapo C520WS](https://www.tapo.com/en/product/smart-camera/tapo-c520ws/v1/): RTSP, ONVIF, H.264, dan konektivitas; variasi regional/hardware perlu diperiksa.
- [Panduan RTSP/ONVIF Tapo](https://www.tp-link.com/us/support/faq/2680/): Camera Account, main/substream, dan keterbatasan sumber daya stream.
- [GStreamer Direct3D 11](https://gstreamer.freedesktop.org/documentation/d3d11/index.html): decoder dan elemen GPU Windows.
- [GStreamer qml6d3d11sink](https://gstreamer.freedesktop.org/documentation/qt6d3d11/index.html): rendering Qt Quick melalui D3D11.
- [GStreamer splitmuxsink](https://gstreamer.freedesktop.org/documentation/multifile/splitmuxsink.html): segmentasi pada keyframe dan finalisasi asinkron.
- [ONNX Runtime CUDA](https://onnxruntime.ai/docs/execution-providers/CUDA-ExecutionProvider.html): inferensi GPU serta kompatibilitas runtime.
- [Windows Interactive Services](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services): pemisahan service dan sesi desktop.
