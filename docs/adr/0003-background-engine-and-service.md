# ADR 0003 — Engine terpisah, indeks event, dan batas service M1

Tanggal: 23 Oktober 2026

Status: implementasi M1; Session 0 belum diverifikasi

## Keputusan

- Gunakan executable `opennvr_engine` yang sama untuk console/background user process dan Windows Service SCM. Mode console tidak bergantung pada lifecycle desktop.
- Engine memegang lock direktori data sebelum membuka/reconcile database. Satu direktori data hanya boleh dimiliki satu engine, termasuk lintas sesi.
- Engine menjadi pemilik SQLite dan recording. Desktop memakai Named Pipe versi 1; tidak membuka database untuk menulis.
- Named Pipe menolak klien remote, memakai DACL eksplisit, batas request 64 KiB/response 1 MiB, serta timeout I/O. ACK klien menggantikan `FlushFileBuffers` yang dapat tertahan klien lambat.
- Simpan segmen sebagai `.mkv.part` saat aktif. Setelah finalisasi sukses, rename menjadi `.mkv`, baru catat segmen complete. Event panjang dipecah sekitar 60 detik pada keyframe.
- Reconnect memakai backoff 1/2/4/8/15 detik. Watchdog frame mengakhiri sesi yang tidak menghasilkan frame, sehingga supervisor dapat membangun ulang pipeline.
- Retensi awal 14 hari, kuota 100 GB, dan reserve minimal 10 GB atau 5% volume. Semua file terkelola dihitung, termasuk `.part`; hanya event terminal yang boleh dihapus. Nilai ini masih default development, bukan pilihan final pengguna.

## Identitas dan deployment

Console engine menggunakan DPAPI CurrentUser. Service direncanakan sebagai LocalService dengan credential DPAPI machine scope, dibatasi direktori data ber-DACL protected yang hanya mengizinkan SYSTEM, Administrators, LocalService dan operator yang ditetapkan. Validasi native menolak trustee tambahan sebelum menulis/membaca credential service.

Runtime service disalin ke Program Files dengan hak read/execute bagi LocalService; data berada di ProgramData. Bundle tidak berisi credential, database atau rekaman. Installer elevated melakukan provisioning dari credential pengguna tanpa password pada command line. Tidak memasang service yang menunjuk executable di direktori proyek yang dapat ditulis pengguna.

Subfolder `config/` memakai ACL terproteksi read-only untuk LocalService/operator; hanya SYSTEM/Administrators boleh mengganti credential/settings. Direktori dibuat dengan descriptor ACL secara atomik. First install menolak target yang sudah ada, dan update menolak owner/ACL atau reparse point yang tidak sesuai, untuk menghindari elevated copy/provisioning melalui direktori yang dikendalikan pihak lain.

## Batas dan konsekuensi

- Proses background pengguna bertahan setelah jendela desktop ditutup, tetapi tidak menjamin operasi setelah logout. Hanya service SCM yang telah diuji di Session 0 dapat memenuhi bagian itu.
- Token pengembangan tidak elevated. Installer, entry point, dan bundle dapat diuji sebagian tanpa admin; status service/autostart/GPU Session 0 tidak boleh dinyatakan berhasil sampai diuji dengan elevasi.
- M1 masih satu kamera, full-frame detection dan pipeline decode terpisah. Scheduler 12 kamera, shared media IPC, zona, ekspor dan installer desktop lengkap tetap tahap berikutnya.
- Plugin `qml6d3d11sink` belum tersedia. Preview/playback sementara memakai jendela video native terpisah; pemutaran antarsegmen belum menjanjikan transisi tanpa jeda atau seek global.
