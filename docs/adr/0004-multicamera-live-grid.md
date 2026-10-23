# ADR 0004 — Multi-kamera, inferensi bersama dan live grid

Tanggal: 24 Oktober 2026. Status: diimplementasikan; benchmark lapangan dan soak masih terbuka.

## Keputusan

- Engine memuat 1–12 profil dengan ID stabil. Tiap profil memiliki supervisor RTSP, ring main stream 64 MiB, writer, confirmation gate dan status sendiri. SQLite dan kebijakan retensi tetap terpusat. Katalog kosong, kamera ke-13, ID ganda dan path credential di luar pola yang diizinkan ditolak.
- Satu `InferenceScheduler` memiliki satu sesi ONNX CUDA, melayani kanal siap secara round-robin. Tiap kamera menyimpan maksimal satu frame BGR pending dan satu hasil terbaru. Frame pending lama diganti; model tidak diduplikasi per kamera. Decoder mengambil sampel sekitar 3 FPS per kamera; pencapaian FPS akhir perlu benchmark.
- Substream RTSP dibuka sekali oleh engine per kamera, lalu di-tee untuk AI dan cache H.264 live. Main stream dipakai untuk ring/rekaman. Desktop tidak menerima credential dan tidak membuka RTSP tambahan.
- Kanal kontrol tetap terpisah dari media. Setiap kamera memiliki Named Pipe `.Media.<id>` dengan ACL yang sama, penolakan klien remote dan timeout. Klien lambat tidak menahan callback RTSP: cache menyimpan GOP dengan batas 2 MiB/180 AU; balasan maksimal 192 KiB payload compressed sebelum base64. Epoch proses, generation dan cursor memaksa resync dari keyframe saat reconnect/tertinggal. Base64/JSON dipilih agar framing dan ACL yang sudah diuji dapat dipakai ulang; protokol binary streaming dapat menggantikannya setelah pengukuran.
- Desktop mendecode H.264 memakai D3D11, lalu mengunduh BGRA ke QImage dan mengunggah tekstur Qt Quick. Komposisi grid memakai scene graph; jalur ini belum zero-copy. Tampilan dibatasi sekitar 10 update/detik per tile. Tile yang tersembunyi, tab lain dan window minimized menghentikan konsumsi media, sedangkan service tetap berjalan.
- Grid menyediakan 1/4/9/12 slot, paginasi dan fokus lewat klik dua kali. Daftar kamera tidak dibuat ulang pada setiap polling status; status AI/rekaman berasal dari ID kamera engine. Frame yang terhenti ditandai dan dibersihkan agar tidak terlihat sebagai video aktif.
- Katalog lokal desktop memakai DPAPI CurrentUser. Installer elevated memvalidasi dan memprovision ke DPAPI machine di config read-only service. Perubahan katalog berlaku setelah update/restart service; tidak ada endpoint privileged write baru pada IPC. Nilai retensi yang sudah ada dipertahankan saat provisioning ulang.

## Batas penerimaan

Uji sintetis membuktikan worker, writer, isolasi ID, media IPC, GPU decode dan render 12 tile. Uji positif gambar pada 12 kanal membuktikan satu sesi CUDA dapat melayani semuanya. Keduanya belum membuktikan performa/akurasi 12 kamera RTSP nyata. Kompatibilitas kamera nyata pada build baru diverifikasi terpisah setelah deployment. Belum ada soak 24 jam, editor zona, seek global, ekspor atau installer desktop produksi.
