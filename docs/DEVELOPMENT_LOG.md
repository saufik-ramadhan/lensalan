# Development log — LensaLAN

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

## 2026-10-24 — Kompatibilitas Windows PowerShell 5.1

- Pengguna menemukan kegagalan packaging pada `Path.GetRelativePath`, API yang tidak tersedia di .NET Framework milik Windows PowerShell 5.1. Pengujian sebelumnya menggunakan PowerShell 7 sehingga masalah ini terlewat.
- Packaging kini menghitung path relatif dari prefix direktori CUDA yang telah dinormalisasi dan diperiksa batasnya. Bundle lama/hasil percobaan yang belum selesai tetap tidak ditimpa atau dihapus.
- Installer juga diperbaiki: pembuatan direktori dengan ACL atomik menggunakan `DirectoryInfo.Create(DirectorySecurity)` di PowerShell 5.1 dan `FileSystemAclExtensions.Create` di PowerShell 7. Proteksi ACL tetap sama.
- Packaging penuh berhasil di Windows PowerShell 5.1.26100.9711 dan PowerShell 7.6.5, masing-masing dengan 186 DLL runtime dan 240 plugin GStreamer. Bundle: `artifacts/engine-bundle-ps51-fix-20261024/` dan `artifacts/engine-bundle-ps7-fix-20261024/`.
- Di kedua shell, pemeriksaan parsing seluruh skrip PowerShell, pembuatan direktori uji dengan ACL terproteksi, hash engine/model serta 22 file runtime/notice CUDA, dan installer `-DryRun` lulus. Bundle tidak berisi credential/database/video kamera. Pemeriksaan ACL memakai direktori uji dengan SID pengguna saat ini, bukan provisioning direktori service elevated.
- Tidak ada instalasi service atau perubahan folder sistem pada perbaikan ini. Verifikasi Session 0, logout/reboot dan soak tetap tertunda.

## 2026-10-24 — Perbaikan registrasi service dan resume instalasi

- Pengguna menjalankan installer elevated: provisioning machine DPAPI berhasil, tetapi `sc.exe create` menampilkan usage dan registrasi gagal. Pemeriksaan read-only mengonfirmasi folder runtime/data/config sudah tersedia dengan owner Administrators dan ACL terproteksi, sedangkan service OpenNVR belum terdaftar.
- Pemanggilan native `sc.exe` dengan command line service berkutip diganti dengan parameter string terstruktur `Win32_Service.Create/Change` melalui CIM. Ini mempertahankan kutip executable/root/data pada Windows PowerShell 5.1. Delayed startup/recovery tetap memakai argumen sederhana `sc.exe`.
- Tambahkan `-Resume` untuk instalasi yang sudah diprovision: periksa owner/ACL dan reparse point, marker/config, engine hash yang sama dengan bundle, serta SID operator. Resume tidak menyalin ulang runtime atau mengubah credential/settings, sehingga nilai retensi yang sudah ada tetap dipertahankan.
- Transcript installer kini ditutup melalui `finally`, termasuk ketika terjadi error. Error CIM menyertakan return code untuk troubleshooting.
- Uji regresi baru `tools/test_service_install.ps1` lulus di PowerShell 5.1.26100.9711 dan 7.6.5: Create/Change menjaga command line berkutip; parameter cocok dengan schema CIM lokal; error registrasi diteruskan; resume tidak mengubah fixture dan menolak operator salah, engine berbeda atau file provisioning hilang. Operasi CIM yang mengubah service dimock, bukan dijalankan.
- `-Resume -DryRun` dengan bundle `artifacts/engine-bundle-ps51-fix-20261024/` terhadap instalasi parsial aktual lulus di kedua shell. Tidak ada penulisan ulang kredensial/config atau registrasi service dari sesi non-admin ini. Langkah berikutnya: pengguna menjalankan ulang installer dengan `-Resume` dari PowerShell Administrator, lalu verifikasi status engine/CUDA di Session 0.

## 2026-10-24 — Startup service dan CUDA Session 0 lulus

- Pengguna menyelesaikan instalasi elevated dan membagikan status `service=true`, `state=running`, `streamReady=true`, `aiReady=true`, `storageReady=true`, PID 28128; 29 frame AI telah dianalisis pada attempt pertama.
- Verifikasi read-only mengonfirmasi SCM Running, akun `NT AUTHORITY\LocalService`, proses PID 28128 di Session 0, serta command line executable/root/data dengan kutip yang utuh. Pembacaan API berikutnya menunjukkan 100 frame AI dianalisis, 749 paket dan AI tetap siap pada attempt pertama.
- Konfigurasi SCM dikonfirmasi: automatic delayed start, recovery restart 5/15/60 detik, reset period 86.400 detik dan non-crash failure recovery aktif. Ini memverifikasi konfigurasi; perilaku setelah crash/reboot belum diuji.
- Tidak ada person pada kedua snapshot (`personFrames=0`, `recording=false`, storage 0 byte); hasil startup ini belum memvalidasi event positif atau video keluaran service. Pengukuran inferensi singkat juga bukan benchmark 12 kamera.
- README, panduan service/build dan acceptance M1 diperbarui. Langkah berikutnya: uji orang melintas agar service membuat event, tunggu post-roll dan verifikasi playback. Logout/reboot dijadwalkan bersama pengguna; soak 24 jam dan multi-kamera tetap belum diterima.

## 2026-10-24 — Event person pada service berhasil

- Pengguna mengonfirmasi uji sudah berfungsi. Pemeriksaan read-only melalui IPC menunjukkan service tetap running, AI/stream/storage siap pada attempt pertama, 1.801 frame dianalisis dan 14 frame person.
- Indeks service berisi satu event kamera-1 berstatus `complete`, satu segmen, durasi 36,75 detik dan ukuran 5.715.387 byte. Alasan finalisasi `post_roll_elapsed`; status saat pemeriksaan `recording=false` karena event telah selesai.
- Hasil ini menambah bukti event positif dalam LocalService/Session 0. Tidak ada perubahan konfigurasi, penghentian service atau pembukaan video privat oleh agen pada pemeriksaan ini.
- Langkah penerimaan yang masih tersisa: logout/login dan reboot bersama pengguna, soak 24 jam serta recovery service/jaringan aktual. Pengembangan berikutnya pada rencana produk adalah live grid dan scheduler multi-kamera bertahap; belum diimplementasikan pada checkpoint ini.

## 2026-10-24 — Reboot dikonfirmasi; live grid dan multi-kamera

- Pengguna menyatakan uji reboot service sebelumnya lulus dan soak belum dilakukan, lalu meminta live grid serta multi-kamera. Catatan acceptance diperbarui tanpa menganggap soak sudah lulus.
- Engine sekarang menerima 1–12 sumber ber-ID unik, dengan supervisor/reconnect, ring main stream, confirmation gate, status dan writer terpisah. SQLite/retensi tetap terpusat; kamera-1 dan indeks lama tetap kompatibel.
- Satu scheduler round-robin melayani semua kanal dengan satu model CUDA. Tiap kanal menyimpan maksimal satu pending frame dan satu hasil; pending lama diganti. Metrik `modelSessions`, `inferredFrames` dan `replacedAnalysisFrames` ditambahkan.
- Substream engine dibagi untuk AI dan cache live H.264. Media memakai pipe per kamera dengan ACL/timeout yang sudah digunakan kanal kontrol, buffer GOP 2 MiB/180 AU, batch compressed maksimal 192 KiB, epoch/generation/cursor serta resync keyframe. Desktop tidak mendapat credential dan tidak membuka RTSP tambahan.
- Qt Quick menyediakan grid 1/4/9/12, paginasi dan fokus kamera. D3D11 decode di desktop diikuti BGRA/QImage dan tekstur scene graph, sekitar 10 update/detik; belum zero-copy. Tile/tab tidak aktif dan window minimized berhenti mengonsumsi media.
- Tab Kamera menyimpan katalog/profil DPAPI CurrentUser. Installer memprovision katalog menjadi machine DPAPI dengan config read-only, mempertahankan nilai retensi lama. Update runtime dilakukan tanpa `-Resume`; tidak ada privileged write baru pada IPC.
- Uji awal menangkap double deletion tekstur scene graph; diperbaiki menggunakan ownership Qt. Uji UI juga diperbaiki agar menelusuri visual tree delegate, kemudian semua layout berhasil menghasilkan frame. Screenshot yang diperiksa hanya media sintetis.
- CTest 8/8 kelompok lulus (51,42 detik): termasuk core/storage/media/IPC/lifecycle lama, validasi limit/ID/path dan scheduler, 12 event bersegmen yang didekode, grid 1/4/9/12, desktop detach, pause/resume, serta tambah/edit/hapus/reload profil DPAPI.
- Uji CUDA aktual pada 12 kanal gambar positif lulus: masing-masing dua orang, `modelSessions=1`. Ini bukan benchmark 12 kamera nyata. Uji installer di PowerShell 5.1/7 juga lulus.
- Bundle `artifacts/engine-bundle-20261024-live-grid-v03/` versi 0.3.0-dev disiapkan, 186 DLL/240 plugin. Uji headless 12 sumber/event/media/decode lulus dengan PATH child hanya Windows dan plugin/scanner dari bundle. Dry-run update instalasi aktual lulus.
- Selama pengujian, service kamera lama tetap berjalan (PID 6076, AI/stream/storage siap, attempt pertama); rangkaian sintetis tidak membuka kamera atau menghentikan service tersebut. Deployment elevated runtime baru dan validasi live kamera nyata menjadi langkah berikutnya. Soak dan kapasitas/akurasi 12 RTSP nyata tetap terbuka.

### Deployment live grid dan handoff

- Setelah commit implementasi `5094d47`, installer update diluncurkan melalui Windows UAC dan selesai: transcript menyatakan provisioning machine DPAPI, konfigurasi SCM/recovery dan startup sukses. Service dihentikan/dijalankan kembali untuk mengganti runtime; tidak ada reboot/logout dari agen.
- Runtime terpasang 0.3.0-dev memiliki hash engine sama dengan bundle. SCM/proses mengonfirmasi LocalService Running, PID 9760 di Session 0. API sesudah warmup: `liveProtocol=1`, `cameraCount=1`, `runningCameras=1`, stream/AI/storage siap, `modelSessions=1`, attempt pertama.
- Uji live kamera nyata pada layout empat slot lulus: kamera-1 menghasilkan lima frame D3D11 dan status Live; slot lain kosong. Screenshot kamera nyata dinonaktifkan, metadata disimpan pada `.local/live-camera-grid-validation.json`.
- Desktop terbaru dibuka sebagai proses interaktif, PID 2692. Pemeriksaan koneksi TCP menunjukkan dua RTSP established milik service dan tidak ada RTSP tambahan milik desktop. Snapshot ukuran rekaman sesudah upgrade sama dengan sebelum upgrade, 16.348.448 byte.
- Pengguna dapat memakai tab Live grid dan Kamera. Profil tambahan diterapkan lewat installer elevated dengan bundle ini tanpa `-Resume`; hanya satu kamera fisik tersedia saat handoff. Reboot build sebelumnya dilaporkan lulus, sedangkan soak 24 jam, penerimaan 12 RTSP nyata dan uji person terkontrol sesudah update tetap belum dilakukan.

## 2026-10-24 — Persiapan publikasi dengan nama LensaLAN

- Pengguna meminta push ke repository GitHub publik miliknya setelah memastikan credential dan data sensitif komputer tidak ikut terunggah, lalu meminta nama yang berbeda dari OpenNVR. Nama produk/repository dipilih LensaLAN.
- README, judul aplikasi/QML, metadata CMake, dokumentasi utama, label bundle dan launcher publik diperbarui. `LensaLAN.cmd` menjadi launcher utama; alias dan identifier teknis lama dipertahankan untuk kompatibilitas service/data/IPC yang sedang berjalan.
- Audit sebelum publikasi mencakup seluruh branch/tag dan metadata commit/tag. Gitleaks 8.30.1 (binary release diverifikasi SHA-256) tidak menemukan leak pada 10 commit awal. Pemeriksaan lokal tambahan terhadap 139 blob unik historis bersih dari exact password kamera, alamat LAN aktual, path profil Windows, hostname dan SID pribadi. Password didekripsi hanya di memori untuk pencocokan dan tidak dimasukkan ke laporan/source.
- Git memakai email noreply. Runtime/model unduhan, credential/config aktual, database, video, screenshot, log dan bundle berada di lokasi yang diabaikan Git; tidak ada file tersebut dalam riwayat tracked. Ignore rules diperluas untuk katalog/per-camera credential, key/certificate privat dan dump.
- Tambahkan dokumentasi publikasi serta workflow secret scan pada setiap push/pull request, dengan download CLI yang diverifikasi checksum dan output redacted. Riwayat pengembangan dipertahankan karena audit tidak menemukan materi yang perlu dihapus.
- Build dan smoke QML branding lulus; pengujian core/multi-kamera serta installer PowerShell 5.1 lulus. Service tidak diganti/restart untuk perubahan branding ini. Audit final setelah commit dilakukan sebelum push.
