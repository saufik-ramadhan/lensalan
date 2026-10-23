# M1 — Hasil verifikasi awal

Tanggal: 23 Oktober 2026; diperbarui 24 Oktober 2026

Status: engine proses pengguna, startup kamera/CUDA serta event person complete sebagai LocalService di Session 0 terverifikasi; logout/reboot masih perlu diuji

## Implementasi

- Engine satu kamera kontinu pada proses terpisah; supervisor membangun ulang pipeline dengan backoff saat stream gagal/stall.
- SCM entry point untuk LocalService dan installer runtime yang terpisah dari source tree.
- Named Pipe lokal dengan ACL eksplisit, versi protokol, batas payload dan timeout.
- SQLite WAL untuk event/segmen/incident, finalisasi `.part`, segmentasi pada keyframe, recovery dan retensi.
- Desktop async untuk status, pause/resume/shutdown engine pengguna, filter event dan playback segmen final.

## Pengujian yang sudah lulus

| Kelompok | Bukti |
|---|---|
| Core | Delapan skenario event/confirmation/ring tetap lulus |
| Storage | Enam skenario: durability/filter SQL, restart/partial, umur retensi, kuota tanpa menghapus aktif, path confinement, file yatim |
| Media | Sintetis H.264 → pre-roll → MKV → decode kembali |
| IPC/platform | Transaksi berulang, instance ganda ditolak, versi protokol tidak didukung ditolak, protected ACL diterima/broad ACL ditolak, DPAPI machine round-trip tanpa plaintext |
| Engine lifecycle | Singleton, pause/resume, event sintetis bersegmen yang dapat didekode, shutdown, kill pada event aktif dan restart, segmen final sebelumnya tetap ada |
| Kamera nyata | Sumber loopback gagal, konfigurasi terenkripsi dikembalikan ke kamera, recovery attempt kedua sekitar 2.278 ms dengan AI siap |
| Bundle mandiri | 186 DLL runtime dan 240 plugin dikemas; tanpa MSYS2 pada PATH, retest final kamera/CUDA pulih sekitar 2.594 ms pada attempt kedua |
| Installer | `-DryRun` lulus; tidak mengubah service, folder sistem, ACL atau credential |
| Service aktual (24 Oktober) | Pengguna menyelesaikan instalasi elevated; SCM Running, LocalService, PID 28128 di Session 0; API `service=true`, `state=running`, stream/AI/storage siap. Frame AI bertambah dari 29 pada laporan pengguna menjadi 100 saat pemeriksaan berikutnya |
| Event person pada service (24 Oktober) | Pengguna mengonfirmasi uji berfungsi; API mencatat 14 frame person dan satu event `complete`, satu segmen, 36,75 detik, 5.715.387 byte, finalisasi `post_roll_elapsed`; service tetap running pada attempt pertama |
| Reboot (konfirmasi pengguna, 24 Oktober) | Pengguna melaporkan uji reboot lulus pada service satu kamera; soak belum dilakukan. Ini bukan hasil reboot build multi-kamera baru |
| Konfigurasi SCM aktual | Delayed automatic startup; recovery restart 5/15/60 detik dan non-crash failure flag aktif. Perilaku setelah reboot/crash service belum diuji |
| UI | QML smoke test berhasil, daftar event dari IPC tampil, tidak ada error QML; engine tetap dapat menerima shutdown setelah UI keluar |
| UI playback end-to-end | Desktop meminta manifest event melalui IPC, memutar dua segmen sintetis di jendela D3D11, dan melaporkan `Playback selesai` tanpa error |
| Desktop ditutup / >60 detik | Kamera nyata: setelah desktop keluar, engine terus running 65,5 detik; frame AI bertambah dari 6 menjadi 185 |

Hasil CTest awal: 5/5 kelompok lulus dalam sekitar 30 detik. Waktu recovery adalah pengukuran satu skenario, bukan jaminan seluruh kondisi jaringan. Rekaman kamera dan credential tetap pada direktori lokal yang diabaikan Git.

## Belum diterima

- Logout/login terpisah, reboot build multi-kamera baru, soak 24 jam, serta kondisi disk/jaringan lapangan jangka panjang.
- Akurasi person siang/malam/jauh dan dataset acceptance PRD.
- Penerimaan lapangan 12 RTSP nyata; implementasi grid/scheduler dan hasil sintetis ada di [laporan multi-kamera](MULTICAMERA_LIVE_GRID.md). Editor zona, seek global, ekspor dan installer desktop masih terbuka.
- Pemulihan kontainer `.part` menjadi video utuh; file semacam itu ditandai sebagai partial dan tidak diklaim sehat.

M1 ini belum merupakan penerimaan MVP penuh. Startup CUDA dan event person complete dalam service telah lulus; hasil ini belum membuktikan seluruh lifecycle rekaman atau operasi setelah logout/reboot. Pemeriksaan event service oleh agen menggunakan metadata IPC; uji otomatis playback sebelumnya memakai media sintetis. Laporan ini harus diperbarui setelah pengujian berikutnya, tanpa menimpa bukti M0.

Pada handoff 23 Oktober, engine kamera nyata berjalan sebagai proses pengguna dengan `service=false`. Commit implementasi: `ac9c6cc`; tag: `m1-user-engine`. Pada checkpoint 24 Oktober, service terpasang dan berjalan dengan `service=true` setelah perbaikan installer `9eb7522`.
