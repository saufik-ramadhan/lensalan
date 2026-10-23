# Visi produk dan roadmap LensaLAN

Tanggal: 24 Oktober 2026. Status: arah produk; fitur roadmap belum merupakan kemampuan yang dirilis.

## Arah produk

LensaLAN dikembangkan menjadi platform NVR native Windows untuk monitoring, rekaman, analitik dan otomasi kamera lokal. Sasaran awal adalah operator rumah dan usaha kecil yang membutuhkan setup sederhana, video tetap lokal, serta rekaman yang dapat dipercaya. Batas awal tetap 12 kamera pada host acuan; dukungan lebih besar ditetapkan setelah benchmark, bukan dari batas konfigurasi saja.

Produk berkembang dari engine C++ dan desktop Qt yang sudah bekerja. Service tetap mengelola kamera, media, inferensi, indeks dan storage secara mandiri dari desktop. Integrasi dan model baru ditambahkan melalui batas yang jelas agar tidak mengganggu recording atau mengharuskan pengguna mengoperasikan banyak komponen saat setup dasar.

## Pembeda yang akan dibuktikan

| Pembeda | Bukti yang diperlukan |
|---|---|
| Pengalaman Windows native | Installer desktop/service, startup otomatis, setup kamera/storage, upgrade dan pemulihan yang dapat dilakukan operator |
| Responsivitas dan efisiensi | CPU, RAM, VRAM, GPU decode/compute, FPS, latensi P50/P95 dan throughput disk pada workload terdefinisi |
| Rekaman dapat dipercaya | Pre/post-roll, finalisasi segmen, event serentak, disk penuh, kamera putus, restart/crash dan recovery indeks |
| Operasi lokal | Kamera dan credential dimiliki service; akses integrasi opsional dan terautentikasi; jalur dasar tidak memerlukan cloud |
| Perluasan terkendali | Model/integrasi dapat gagal atau kelebihan beban tanpa memblokir kontrol dan penulisan video |

C++ memberi kontrol langsung atas lifecycle, alokasi, antrean, interop GPU dan API Windows. Implementasi live saat ini masih memakai download BGRA dan upload tekstur Qt; zero-copy adalah kandidat optimasi setelah profiling. Bahasa implementasi tidak digunakan sebagai klaim kemenangan performa tanpa benchmark.

Sebagai konteks perbandingan, arsitektur OpenNVR yang ditinjau menempatkan MediaMTX pada jalur media dan backend FastAPI pada pengelolaan/control plane. Dokumentasi mereka juga menjelaskan FFmpeg/OpenCV menjalankan bagian komputasi native. Karena itu perbandingan perlu menilai pipeline dan hasil pengguna secara menyeluruh. Referensi: [arsitektur](https://github.com/open-nvr/open-nvr/blob/main/docs/ARCHITECTURE.md), [catatan biaya deteksi](https://github.com/open-nvr/open-nvr/blob/main/docs/DETECT_CPU.md).

## Fase pengembangan

| Fase | Cakupan | Gerbang penerimaan |
|---|---|---|
| R0 — Fondasi terukur | Panel kesehatan/metrik per kamera, benchmark 1/4/8/12 sumber, uji event setelah update, reconnect independen, storage/recovery dan soak | Target PRD dinilai; soak 24 jam lulus; replay dan kamera fisik dibedakan; tidak ada klaim kapasitas hanya dari demo sintetis |
| R1 — Produk harian | Zona/confidence, thumbnail, timeline/seek, ekspor event, ONVIF discovery/PTZ sesuai kemampuan perangkat, setup storage serta installer/update/backup | Alur setup → live → event → playback → ekspor selesai melalui UI; upgrade menjaga config/rekaman; kredensial tetap terlindungi |
| R2 — Integrasi dan otomasi | Aturan zona/jadwal, notifikasi, MQTT/webhook/API terautentikasi dan integrasi Home Assistant; akses klien tambahan bila dibutuhkan | Event memiliki schema/ID/versi stabil; retry/idempotensi dan izin diuji; konsumen lambat atau putus tidak menahan recording |
| R3 — Analitik modular | Kelas objek tambahan, tracking, line crossing/dwell, pemilihan model dan SDK/adapter analitik opsional | Akurasi serta biaya per kemampuan diukur; antrean/anggaran GPU terbatas; crash adapter terisolasi; kompatibilitas API dan provenance model terdokumentasi |

Roadmap ini tidak mengubah mode rekaman person-event yang sudah dipilih. Mode kontinu, kapasitas di atas 12, multi-host, klien mobile/web lengkap dan model berat diputuskan melalui kebutuhan dan PRD tambahan. Waktu penyelesaian fase lanjutan belum diestimasi.

## Batas arsitektur untuk perluasan

- Pertahankan core media/recording/control pada C++ dengan ownership dan batas memori yang dapat diaudit. UI tetap mengonsumsi service; tutup/crash desktop tidak menghentikan engine.
- Pisahkan persepsi model dari aturan event dan aksi. Hasil model berisi waktu/identitas kamera/kelas/confidence; aturan menentukan pemicu, dan konsumen integrasi menerima event yang sudah tervalidasi.
- Mulai dengan kontrak internal bertipe dan berversi. SDK publik atau adapter proses terpisah dibuat setelah kebutuhan pertama membuktikan kontraknya; hindari ketergantungan ABI C++ lintas compiler sebagai kontrak plugin publik.
- Adapter opsional memakai antrean terbatas, timeout, pembatalan dan pemisahan proses jika diperlukan. Service tetap memiliki kontrol resource dan akses credential; ekstensi menerima hak minimum yang dibutuhkan.
- API jaringan dan remote viewing tetap opsional. Sebelum dirilis, definisikan autentikasi, otorisasi, audit, update schema dan perilaku saat klien terputus. Tidak ada endpoint jaringan baru yang dibuka oleh perubahan roadmap ini.

## Metode pembandingan

Gunakan rekaman/sumber kamera, codec/resolusi/FPS/GOP, model/threshold, target recall, retensi dan mode rekaman yang sama atau laporkan perbedaannya. Ukur seluruh proses, termasuk runtime/container bila digunakan, dengan GUI terbuka dan tertutup. Laporkan warmup terpisah, latensi distribusi, drop/stale frame, jumlah stream dan kegagalan rekaman. Optimasi yang mengurangi akurasi atau konteks rekaman dicatat sebagai tradeoff.

## Pekerjaan berikutnya

Prioritas implementasi berikut adalah panel kesehatan dan pencatatan metrik untuk R0, kemudian benchmark/fault test dan soak. Pengujian terjadwal, reboot atau perubahan kamera tetap dilakukan dengan waktu dan kondisi uji yang disepakati. Fitur R1–R3 tidak ditandai selesai sebelum memiliki implementasi dan bukti penerimaan sendiri.
