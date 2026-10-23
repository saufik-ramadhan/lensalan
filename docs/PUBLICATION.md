# Publikasi LensaLAN

Nama publik proyek: **LensaLAN**. Identifier source/binary `opennvr`, service `OpenNVR`, path data lama dan protokol pipe dipertahankan untuk kompatibilitas deployment development. Riwayat commit sebelum pergantian nama tetap dipertahankan.

Repository publik: [saufik-ramadhan/lensalan](https://github.com/saufik-ramadhan/lensalan). Branch utama `main`; milestone `m0-camera-prototype` dan `m1-user-engine` ikut dipublikasikan. Publikasi awal pada 24 Oktober 2026 telah lulus workflow Core dan Secret scan.

## Batas isi repository

Repository hanya memuat source, pengujian sintetis, skrip dan dokumentasi yang direview. Credential DPAPI, katalog kamera aktual, konfigurasi mesin, database, rekaman, screenshot kamera, log/transcript, runtime/model unduhan dan bundle deployment disimpan di direktori lokal yang diabaikan Git. Tidak ada binary/model/rekaman yang diunggah sebagai release pada publikasi awal.

Sebelum publikasi awal, Gitleaks 8.30.1 memeriksa seluruh commit yang dapat dijangkau branch/tag dengan output rahasia disamarkan. Pemeriksaan tambahan mencakup seluruh blob historis, metadata commit/tag, exact-match password kamera hanya di memori, alamat LAN, path profil Windows, hostname dan SID pengguna. Hasil awal bersih; alamat email commit menggunakan GitHub noreply. Artefak audit terperinci tetap lokal dan tidak ikut dipublikasikan.

Workflow Secret scan memeriksa riwayat lengkap pada push/pull request. Workflow ini membantu menemukan secret yang dikenali scanner; review file tetap diperlukan sebelum setiap publikasi. File yang pernah masuk commit tetap berada dalam riwayat walaupun kemudian ditambahkan ke `.gitignore`.
