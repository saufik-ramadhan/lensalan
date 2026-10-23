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
