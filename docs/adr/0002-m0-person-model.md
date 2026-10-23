# ADR 0002 — Model person awal untuk pembuktian M0

Tanggal: 23 Oktober 2026

Status: kandidat prototipe; belum lulus evaluasi akurasi lapangan

## Keputusan

Gunakan YOLOX-Tiny ONNX 416×416 dari release resmi `0.1.1rc0` dengan ONNX Runtime CUDA 1.23.2. Batasi pemicu ke kelas COCO `person` (kelas 0), confidence awal 0,45 dan NMS IoU 0,45. Dua deteksi valid dari tiga sampel dalam dua detik mengonfirmasi orang.

Preprocessing: BGR 0–255, CHW float32, letterbox bernilai 114 di kanan/bawah, tanpa normalisasi tambahan. Output raw `[1,3549,85]` didekode pada stride 8/16/32 dan koordinat dikembalikan ke frame sumber. Shape lain ditolak, tidak ditebak otomatis.

Model SHA256: `427cc366d34e27ff7a03e2899b5e3671425c262ea2291f88bb942bc1cc70b0f7`.

## Bukti dan batas

Uji positif awal pada gambar contoh OpenCV `basketball1.png` menghasilkan dua box person. Angka inferensi satu gambar bukan benchmark kapasitas atau ukuran recall. Substream kamera nyata terbaca 640×360; akurasi orang jauh dan malam hari masih perlu dataset lokasi.

## Sumber dan distribusi

- [Model dan panduan ONNX resmi YOLOX](https://github.com/Megvii-BaseDetection/YOLOX/tree/main/demo/ONNXRuntime).
- [Lisensi repositori YOLOX, Apache 2.0](https://github.com/Megvii-BaseDetection/YOLOX/blob/main/LICENSE); salinan disimpan bersama model lokal. Tinjau seluruh syarat model/runtime pada packaging produksi.
- [Gambar uji OpenCV pada tag 4.12.0](https://github.com/opencv/opencv/blob/4.12.0/samples/data/basketball1.png). Gambar diunduh ke `.deps/`, tidak ditambahkan ke Git.

Tidak ada training, face recognition, atau upload video kamera. Model dapat diganti setelah evaluasi tanpa mengubah domain event.
