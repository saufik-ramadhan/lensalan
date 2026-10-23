# Windows Service — OpenNVR M1

## Status dan batas verifikasi

Pada 24 Oktober 2026 pengguna menyelesaikan instalasi elevated. Pemeriksaan SCM/proses mengonfirmasi OpenNVR berjalan sebagai LocalService di Session 0; API menunjukkan `service=true`, `state=running`, `streamReady=true`, `aiReady=true`, dan `storageReady=true`. Startup kamera/CUDA dalam service sudah terverifikasi. Event person positif dalam service, logout/reboot dan soak 24 jam belum diuji.

Sesi agen pengembangan tidak elevated; pemasangan dilakukan pengguna dari PowerShell Administrator. Windows memerlukan hak Administrator untuk registrasi service, penulisan Program Files dan pengaturan ACL ProgramData. Installer dapat direview dan diuji dengan `-DryRun` sebelum pemasangan.

## Menyiapkan bundle

Dari root proyek, setelah build dan `ctest` lulus:

Skrip packaging dan installer mendukung Windows PowerShell 5.1 bawaan Windows maupun PowerShell 7. Packaging tidak memerlukan hak Administrator; pemasangan service memerlukannya.

```powershell
.\tools\package-engine.ps1
```

Output berada di `artifacts/engine-bundle-<timestamp>/`. Bundle menyertakan engine/probe, DLL runtime yang ditemukan dari import table, plugin/scanner GStreamer, ONNX Runtime, DLL CUDA/cuDNN/cuFFT, model, notice dan manifest. Tidak menyertakan password, database, atau video kamera.

Bundle ini untuk development internal. Ini belum merupakan installer desktop lengkap atau bukti bahwa seluruh aspek lisensi/deployment produksi telah ditinjau.

## Review rencana pemasangan

```powershell
.\tools\install-service.ps1 -PackageRoot .\artifacts\engine-bundle-<timestamp> -DryRun
```

Dry run memvalidasi input dan menunjukkan target; tidak mengubah service, file, ACL atau credential.

## Pemasangan elevated

Pastikan Camera Account telah tersimpan melalui desktop. Hentikan engine pengguna agar tidak membuka stream kamera tambahan. Buka PowerShell **Run as Administrator** dan jalankan dari root proyek:

```powershell
.\tools\install-service.ps1 -PackageRoot .\artifacts\engine-bundle-<timestamp>
```

Jika provisioning berhasil tetapi registrasi service gagal, gunakan bundle yang sama dan `-Resume`. Opsi ini memvalidasi owner/ACL, reparse point, file provisioning, hash engine dan SID operator, lalu melanjutkan registrasi/start tanpa menyalin ulang runtime atau menulis ulang credential/settings. Tidak perlu menghapus direktori instalasi atau data.

```powershell
.\tools\install-service.ps1 -PackageRoot .\artifacts\engine-bundle-<timestamp> -Resume -DryRun
# Di PowerShell Administrator:
.\tools\install-service.ps1 -PackageRoot .\artifacts\engine-bundle-<timestamp> -Resume
```

Registrasi dan pembaruan command line service menggunakan `Win32_Service` melalui CIM agar kutip pada path `Program Files` tetap utuh di PowerShell 5.1 maupun 7. `sc.exe` dipakai untuk delayed startup dan recovery, dengan argumen yang tidak mengandung kutip bersarang. Transcript ditutup juga ketika instalasi gagal.

Default:

| Item | Nilai |
|---|---|
| Nama service | OpenNVR |
| Identitas | NT AUTHORITY\LocalService |
| Runtime | C:\Program Files\OpenNVR |
| Data | C:\ProgramData\OpenNVR |
| Startup | Automatic, delayed |
| Recovery SCM | Restart setelah 5/15/60 detik bila proses gagal |
| Operator | SID pengguna yang menjalankan installer |

Jika elevasi menggunakan akun administrator lain, tetapkan `-OperatorSid` ke SID operator yang benar dan lakukan provisioning credential dengan akun yang dapat membuka blob DPAPI sumber. Installer berhenti jika dekripsi gagal; tidak menulis password ke command line.

Installer membuat direktori baru dengan ACL terproteksi secara atomik. Runtime read/execute, data writable oleh LocalService, sedangkan subfolder `config/` hanya writable oleh SYSTEM/Administrators. Native provisioning memeriksa ACL, termasuk penolakan hak tulis service/operator pada config, sebelum menyimpan credential DPAPI machine scope. First install menolak direktori target yang sudah ada; update menolak service lain, owner/ACL tidak sesuai, atau reparse point. Uninstall/penghapusan rekaman belum diotomatisasi; jangan menghapus direktori data untuk menghentikan service.

## Verifikasi setelah service terpasang

```powershell
Get-Service OpenNVR
& 'C:\Program Files\OpenNVR\bin\opennvr_engine.exe' --request status --data 'C:\ProgramData\OpenNVR'
```

`Running` dari SCM hanya berarti proses service hidup. Verifikasi juga status engine `running`, `streamReady=true`, `aiReady=true`, dan `service=true`. Jika driver/GPU tidak dapat dipakai di Session 0, pertahankan error tersebut sebagai temuan; jangan mengganti ke akun LocalSystem atau CPU fallback secara diam-diam.

Selanjutnya lakukan uji event, tutup desktop, logout/login, reboot, gangguan kamera dan recovery. Uji logout/reboot harus dijadwalkan bersama pengguna karena dapat mengganggu aplikasi lain; belum dilakukan dalam pengembangan ini.

## Kontrol dan troubleshooting

- Desktop menyediakan pause/resume deteksi melalui Named Pipe. Start/stop service dilakukan melalui Services Windows atau PowerShell elevated.
- Error startup disimpan pada `last-startup-error.json` di direktori data bila dapat ditulis. Incident media disimpan di database; status API menunjukkan pesan dan retry berikutnya.
- Transcript pemasangan setelah direktori aman dibuat disimpan pada `install.log` di direktori data, tanpa password pada command line.
- `config/camera.json` service adalah hasil provisioning. Mengubah credential diagnostik `.local/camera.json` tidak otomatis mengganti credential service.
- Service menggunakan registry/plugin path GStreamer milik bundle serta data directory yang dapat ditulis LocalService.
- Jangan menyalin `engine.lock`, database aktif atau file `.part` untuk membuat instalasi kedua pada direktori data yang sama.
