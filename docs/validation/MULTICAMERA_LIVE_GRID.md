# Verifikasi multi-kamera dan live grid

Tanggal: 24 Oktober 2026. Target host dan toolchain mengikuti matriks M0/M1.

## Bukti yang tersedia

| Area | Hasil |
|---|---|
| Batas/profil | 12 profil diterima, kamera ke-13/ID duplikat/path traversal ditolak; fallback credential kamera-1 lama dipertahankan |
| DPAPI desktop | Tambah 12 profil, edit tanpa mengganti password, hapus dari katalog tanpa menghapus credential, dan reload lulus pada fixture terisolasi; password tidak tersimpan plaintext |
| Scheduler | Inferensi serial dengan fairness, hasil tidak tertukar antar kanal, pending frame lama diganti, kegagalan inferensi disampaikan ke kanal |
| CUDA aktual | 12 kanal pada gambar contoh positif masing-masing mendeteksi dua orang; `modelSessions=1`; ini bukan benchmark 12 kamera |
| Engine sintetis | 12 sumber H.264 terpisah running, masing-masing menghasilkan event complete dengan minimal dua segmen; segmen dari setiap kamera berhasil didekode |
| Media | Tiap kamera memiliki kanal live sendiri; mulai keyframe, batch terbatas, cursor dan resync klien tertinggal diuji |
| Grid | 1/4/9/12 tile menghasilkan minimal lima frame per tile melalui D3D11 decode dan Qt Quick; screenshot sintetis diperiksa |
| Lifecycle | Engine tetap memiliki 12 kamera running setelah semua proses desktop uji keluar; pause/resume seluruh kamera berhasil |
| PowerShell | Uji installer CIM/resume, termasuk katalog multi-kamera dan penolakan path traversal, lulus di PowerShell 5.1 dan 7 |
| Bundle mandiri | Bundle 0.3.0-dev, 186 DLL/240 plugin: 12 worker/media/event dan decode segmen lulus dengan PATH child hanya Windows, plugin/scanner dari bundle |

CTest: 8/8 kelompok lulus dalam 51,42 detik, termasuk 12 sumber dan GUI grid serta pengelolaan profil. Bundle baru: `artifacts/engine-bundle-20261024-live-grid-v03/`; installer update `-DryRun` lulus terhadap instalasi aktual. Deployment elevated build baru belum termasuk hasil di tabel ini.

Artefak runtime berada di `.local/multicamera-validation.json`, `.local/ui-grid-*.json`, `.local/ai-scheduler-validation.json`, `.local/profile-validation.json`, serta fixture/log di `build/test-output/`; semuanya diabaikan Git. Tidak ada kamera nyata yang dibuka oleh uji sintetis. Service terpasang tidak dihentikan oleh rangkaian uji ini.

## Cara menjalankan ulang

```powershell
.\tools\dev.ps1 build
.\tools\dev.ps1 test
.\tools\test_service_install.ps1
# GPU aktual, memerlukan model/runtime yang telah disiapkan:
.\tools\dev.ps1 ai-scheduler-test
```

Untuk development manual terisolasi, `opennvr_engine.exe --console --synthetic --cameras 12 --data <folder-uji>` membuat sumber sintetis. Arahkan desktop dengan `OPENNVR_DATA_DIR` ke folder tersebut. Mode sintetis tidak mewakili deteksi person atau RTSP nyata.

## Masih harus diuji

- Kamera fisik bertahap 1/4/8/12, termasuk kamera offline, pergantian firmware/caps dan reconnect per kamera.
- FPS/latensi grid dan analisis, CPU/GPU/RAM/VRAM, throughput disk dengan event serentak dan dampak klien lambat dalam durasi panjang.
- Akurasi person pada kondisi lokasi dan soak 24 jam. Reboot pada versi service sebelumnya dilaporkan lulus oleh pengguna; perubahan build ini memerlukan verifikasi deployment tersendiri.
- Konfigurasi multi-kamera aktual membutuhkan perangkat/IP tambahan; saat pengembangan baru tersedia satu kamera nyata.
