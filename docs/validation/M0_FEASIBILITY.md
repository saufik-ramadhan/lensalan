# M0 — Laporan kelayakan awal

Tanggal: 23 Oktober 2026

Status: sebagian terbukti; M0 belum dinyatakan selesai

## Lingkungan

Windows 11 Pro x64 build 26200; i5-12400F; RTX 3060 12 GB dengan driver 610.88; RAM target 16 GB. GCC UCRT64 16.1.0, Qt 6.11.2, GStreamer 1.28.6. ONNX Runtime GPU 1.23.2, YOLOX-Tiny 416, CUDA runtime/cuBLAS 12.9, cuDNN 9.10.2.21, cuFFT 11.4.1.4.

Pengujian memakai satu Tapo C520WS nyata. Versi hardware dan firmware belum diambil. Alamat dan credential berada pada konfigurasi lokal, tidak diperlukan dalam laporan yang dilacak Git.

## Bukti yang sudah diperoleh

| Uji | Hasil |
|---|---|
| Build core C++20 | Berhasil |
| Delapan skenario unit event/ring | Lulus: konfirmasi, stale sample, post-roll, keyframe, batas memori, generation, codec change, invalid settings |
| CTest media sintetis | Lulus: encode H.264, buffer pre-roll, simpan MKV, decode kembali |
| RTSP substream kamera, 12 detik | Berhasil; 640×360 H.264, 188 access unit, 4 keyframe |
| RTSP main stream dan klip, 14 detik | Berhasil; 2560×1440 H.264, 229 access unit, 5 keyframe; klip berhasil didekode D3D11 |
| Interval keyframe pada probe kamera | Maksimum teramati sekitar 2,53 detik; bukan jaminan interval seluruh kondisi |
| AI CUDA pada gambar positif | Dua orang terdeteksi; satu pengukuran setelah warmup sekitar 7,7 ms |
| AI pada kamera tanpa person terdeteksi, 40 detik | 103 frame dianalisis, 0 frame person, 0 event; rata-rata inferensi/preprocessing sekitar 37,1 ms |
| Uji orang melintas pada kamera, 60 detik | Berhasil: 158 frame dianalisis, 23 frame person, 21 konfirmasi, 1 event; klip selesai secara alami setelah post-roll dan berhasil didekode D3D11 |
| Pemeriksaan independen klip event | ffprobe: H.264 2560×1440, durasi sekitar 32,50 detik, ukuran sekitar 7,37 MB |
| Biaya AI pada uji orang melintas | Rata-rata preprocessing + inferensi + postprocessing sekitar 39,0 ms; bukan latensi end-to-end |
| Qt Quick UI smoke test | Berhasil membuat window dan screenshot offscreen, tanpa pesan error QML |

Jumlah access unit selama probe mencakup waktu koneksi/startup; jangan mengartikannya sebagai FPS kamera yang telah diukur stabil. Caps mengiklankan 25/1, sedangkan throughput aktual dan latensi perlu pengukuran terpisah. Tidak ada klaim recall dari hasil negatif satu adegan.

Report JSON, screenshot dan klip asli berada di `.local/` atau `build/`, diabaikan Git untuk menjaga credential dan video privat tetap lokal. Laporan ini menyimpan ringkasan pengukuran, bukan media pengguna.

## Belum dibuktikan

- GPU decode/inferensi dalam Windows Service setelah logout atau reboot.
- IPC kontrol/media dengan ACL, pelanggan lambat, dan shared stream antarproses.
- Plugin `qml6d3d11sink` dan video dalam grid QML; paket saat ini tidak menyediakan plugin itu.
- Uji 12 kamera, scheduler bersama, startup serentak dan 12 event bersamaan.
- Dataset recall/false event siang/malam, zona deteksi, orang jauh, serta perubahan arah PTZ.
- Sinkronisasi timestamp lintas stream jangka panjang, reconnect otomatis, segmentation dan storage recovery.
- Retensi, kuota, database event, soak 24 jam dan installer.

Hasil-hasil ini mengizinkan kelanjutan prototipe, bukan penerimaan MVP produksi.
