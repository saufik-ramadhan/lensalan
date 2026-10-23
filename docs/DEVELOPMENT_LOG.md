# Development log — OpenNVR

Catatan keputusan, perubahan, dan bukti pengujian. Gunakan commit Git sebagai riwayat perubahan sumber. Jangan menyimpan password kamera, URL berkredensial, atau video privat di log ini.

## 2026-10-23 — Baseline perencanaan

- Pengguna menyetujui pengembangan aplikasi Windows untuk maksimal 12 Tapo C520WS.
- Host target: i5-12400F, RTX 3060 12 GB, RAM 16 GB.
- Deteksi orang dilakukan di host; rekaman berbasis event dengan pre/post-roll.
- PRD dan rencana pengembangan dibuat; nilai retensi, model, dan target performa masih perlu divalidasi.
- Repositori Git lokal diinisialisasi pada branch `main`; remote belum dikonfigurasi.
- Langkah berikutnya: M0, prototipe satu kamera dan verifikasi toolchain/media/AI pada lingkungan aktual.

## 2026-10-23 — Implementasi prototipe M0

- Commit baseline dibuat sebelum source implementasi.
- Toolchain UCRT64 yang sudah tersedia digunakan; dependensi Qt/GStreamer ditambahkan. Perbedaan dari rencana MSVC dicatat pada ADR 0001.
- Core confirmation gate, post-roll, dan ring buffer GOP dengan batas memori dibuat dan diuji pada delapan skenario.
- Probe H.264 sintetis, akses RTSP Camera Account, perekaman pre-roll tanpa encode ulang, serta validasi decode file dibuat.
- Kamera nyata berhasil menyediakan substream 640×360 dan main stream 2560×1440; klip main stream berhasil didekode melalui D3D11.
- Qt Quick workbench dibuat untuk konfigurasi DPAPI, stream probe, preview GPU terpisah, dan sesi deteksi person selama 60 detik.
- ONNX Runtime CUDA dan kandidat YOLOX-Tiny diintegrasikan. Binary/model tetap lokal di `.deps/`; provider gagal secara eksplisit jika CUDA tidak tersedia.
- Uji positif model pada gambar contoh mendeteksi dua orang. Sesi kamera 40 detik memproses 103 frame dan tidak mendeteksi person; hasil ini bukan ukuran recall.
- Pengguna membantu uji orang melintas selama satu menit: 158 frame dianalisis, 23 frame person, 21 konfirmasi, 1 rekaman otomatis. Event selesai setelah post-roll, bukan karena sesi habis; file berhasil didekode D3D11.
- Workflow CI core disiapkan, belum dijalankan pada remote. Repo masih lokal.
- Detail bukti dan pekerjaan tersisa ada di `validation/M0_FEASIBILITY.md`. Service/IPC/grid/database/retensi belum diimplementasikan.

## 2026-10-23 — M1 dimulai

- Pengguna meminta kelanjutan implementasi dan pencatatan progress.
- Fokus: engine terpisah yang berjalan terus, supervisor reconnect, indeks SQLite, kontrol lokal, review event, dan kebijakan storage.
- Siapkan entry point Windows Service dan installer yang dapat direview. Token proses pengembangan tidak elevated; instalasi service dan pengujian GPU di Session 0 belum dapat diklaim berhasil.
- Kredensial development tetap menggunakan DPAPI CurrentUser. Provisioning service akan menggunakan DPAPI machine scope dengan ACL direktori ketat; tidak ada password pada command line atau Git.
- Pengujian direncanakan: storage/recovery/retensi terisolasi, rekaman sintetis, IPC, engine kamera nyata, serta desktop ditutup saat engine berjalan.

### Checkpoint engine dan storage

- Engine headless dengan supervisor reconnect sudah berjalan terpisah dari desktop; pause/resume/shutdown tersedia melalui protokol lokal versi 1.
- Named Pipe menggunakan ACL operator/SYSTEM/Administrators/LocalService, menolak klien remote, membatasi payload, dan memiliki timeout I/O. Pengiriman respons memakai ACK agar disconnect tidak membuang data yang belum dibaca.
- Indeks SQLite WAL menyimpan event, segmen, ukuran, status dan incident. Pemulihan menandai file `.part`/segmen hilang; retensi tidak menghapus event aktif atau file di luar folder terkelola.
- Writer membuat `.part`, finalisasi lalu rename, serta memecah event panjang pada keyframe. Penghentian atau error menandai event interrupted/partial.
- Lima grup CTest lulus: core, enam skenario storage, media sintetis, IPC/ACL/DPAPI, serta lifecycle engine. Lifecycle mencakup singleton, pause/resume, dua segmen yang dapat didekode, dan crash/restart pada event aktif.
- Uji kamera nyata dilakukan dengan konfigurasi terenkripsi terisolasi: loopback yang tidak dapat dijangkau diganti kembali ke kamera, lalu pulih otomatis pada attempt kedua dalam sekitar 2.278 ms. Tidak ada konfigurasi jaringan/kamera yang diubah.
- Desktop kini memiliki halaman engine dan daftar event bertanggal/kamera. Playback berurutan menggunakan jendela video D3D11. Verifikasi visual/integrasi desktop dan bundle service masih berjalan.
- Native entry point SCM dan provisioning credential machine DPAPI sudah dikompilasi; validasi ACL dilakukan sebelum provisioning atau startup service. Instalasi service belum dijalankan karena token non-admin.
- Verifikasi kamera setelah desktop keluar lulus: engine tetap running selama 65,5 detik tambahan dan frame AI bertambah dari 6 ke 185. Batas diagnostik 60 detik tidak berlaku pada engine.
- Review deployment menambahkan direktori config read-only untuk LocalService/operator, ownership SYSTEM/Administrators, pembuatan direktori dengan ACL atomik, dan penolakan target first-install yang sudah ada/reparse point. Tujuannya mencegah elevated provisioning melalui path yang dapat diganti pihak lain.
- Pemeriksaan UI end-to-end lulus: desktop membaca manifest melalui IPC lalu memutar dua segmen sintetis sampai selesai melalui D3D11. QML smoke test dan pemeriksaan visual tidak menemukan error layout/runtime.
- Installer dry-run lulus tanpa mengubah service/folder sistem/ACL. Paket mandiri disiapkan di `artifacts/` (diabaikan Git); runtime final harus diinstal dengan hak Administrator untuk memverifikasi Session 0.
- Checkpoint akhir: 5/5 grup CTest kembali lulus (sekitar 30,2 detik); uji ulang bundle final tanpa MSYS2 pada PATH pulih ke kamera/CUDA dalam sekitar 2.594 ms. Instalasi SCM, logout/reboot dan soak 24 jam tetap belum diverifikasi.

### Handoff M1

- Implementasi disimpan pada commit `ac9c6cc`, tag `m1-user-engine`.
- Desktop Monitoring & Event dibuka, dan engine kamera nyata dijalankan sebagai proses pengguna terpisah. Status terakhir: `running`, `streamReady=true`, `aiReady=true`, `storageReady=true`, `service=false`.
- Engine ini terus berjalan setelah desktop ditutup. Operator dapat menggunakan Jeda deteksi/Hentikan engine pengguna untuk mengontrolnya. Belum ada autostart setelah login atau jaminan bertahan setelah logout.
- Bundle final: `artifacts/engine-bundle-20261023-233840/`; panduan Administrator ada pada `SERVICE.md`. Tidak ada Windows Service/folder Program Files/ProgramData yang dibuat oleh sesi non-admin ini.
