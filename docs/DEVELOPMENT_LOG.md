# Development log — OpenNVR

Catatan keputusan, perubahan, dan bukti pengujian. Gunakan commit Git sebagai riwayat perubahan sumber. Jangan menyimpan password kamera, URL berkredensial, atau video privat di log ini.

## 2026-10-23 — Baseline perencanaan

- Pengguna menyetujui pengembangan aplikasi Windows untuk maksimal 12 Tapo C520WS.
- Host target: i5-12400F, RTX 3060 12 GB, RAM 16 GB.
- Deteksi orang dilakukan di host; rekaman berbasis event dengan pre/post-roll.
- PRD dan rencana pengembangan dibuat; nilai retensi, model, dan target performa masih perlu divalidasi.
- Repositori Git lokal diinisialisasi pada branch `main`; remote belum dikonfigurasi.
- Langkah berikutnya: M0, prototipe satu kamera dan verifikasi toolchain/media/AI pada lingkungan aktual.
