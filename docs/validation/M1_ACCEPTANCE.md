# M1 — Hasil verifikasi awal

Tanggal: 23 Oktober 2026

Status: engine proses pengguna terverifikasi; pemasangan service dan Session 0 masih menunggu elevasi

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
| UI | QML smoke test berhasil, daftar event dari IPC tampil, tidak ada error QML; engine tetap dapat menerima shutdown setelah UI keluar |
| UI playback end-to-end | Desktop meminta manifest event melalui IPC, memutar dua segmen sintetis di jendela D3D11, dan melaporkan `Playback selesai` tanpa error |
| Desktop ditutup / >60 detik | Kamera nyata: setelah desktop keluar, engine terus running 65,5 detik; frame AI bertambah dari 6 menjadi 185 |

Hasil CTest awal: 5/5 kelompok lulus dalam sekitar 30 detik. Waktu recovery adalah pengukuran satu skenario, bukan jaminan seluruh kondisi jaringan. Rekaman kamera dan credential tetap pada direktori lokal yang diabaikan Git.

## Belum diterima

- Registrasi/startup service secara elevated, validasi `service=true`/GPU Session 0.
- Logout/login dan reboot host, soak 24 jam, serta kondisi disk/jaringan lapangan jangka panjang.
- Akurasi person siang/malam/jauh dan dataset acceptance PRD.
- Multi-kamera/scheduler bersama, live grid QML, editor zona, seek global, ekspor dan installer desktop.
- Pemulihan kontainer `.part` menjadi video utuh; file semacam itu ditandai sebagai partial dan tidak diklaim sehat.

M1 ini belum merupakan penerimaan MVP penuh. Laporan ini harus diperbarui setelah elevasi dan pengujian berikutnya, tanpa menimpa bukti M0.

Pada handoff, engine kamera nyata sedang berjalan sebagai proses pengguna dan desktop terbuka. Native status API mengonfirmasi stream/AI/storage siap dengan `service=false`. Commit implementasi: `ac9c6cc`; tag: `m1-user-engine`.
