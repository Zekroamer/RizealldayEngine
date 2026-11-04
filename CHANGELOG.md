# Changelog — Rizeallday Engine v2

## [Belum dirilis]

## [2.4.0] — 2026-11-04


### Perbaikan
- Menyeret sumbu Z gizmo tidak lagi melempar objek jauh; Ctrl+Z / Delete saat mengetik di field tidak lagi mengubah scene; Ctrl+Shift+Z tidak lagi menjalankan Undo dan Redo sekaligus.
- Duplicate (dan Ctrl+D yang sebelumnya tidak berfungsi) kini menyalin seluruh hierarki dan semua objek yang terpilih; sebelumnya anak-anak objek hilang dari salinan.
- Joint di dalam prefab tetap tersambung pada setiap instance; mengganti nama file prefab tidak lagi memutus link.
- Rotasi di Timeline berputar lewat jalur terpendek; event pada detik 0 kini terpanggil.
- Geseran beberapa objek sekaligus mengikuti arah dunia walau parent-nya berputar.
- Pilihan Auto Save diingat setelah editor ditutup.
- Ragdoll yang jatuh jauh tidak lagi menghilang dari layar.
- Gizmo rotasi dan skala kini berlaku untuk semua objek yang terpilih.
- Label field komponen tampil rapi dan ikut bahasa editor; bahasa Indonesia kini selengkap bahasa Jepang.
- Generate Terrain kadang menampilkan mesh lama; undo setelah Generate kini kembali ke terrain sebelumnya.
- Riwayat undo scene tidak lagi hilang setelah membuka Prefab Mode.
- Override prefab kini per field: mengubah `lifetime` sebuah Trail atau satu sumbu skala hanya mengunci field itu, field lain tetap mengikuti prefab.

### Fitur Baru
- **Blend tree 2D** di Animator: isi Parameter Y (mis. moveX / moveY) dan posisi tiap klip untuk gerakan delapan arah.
- **Gizmo rotasi ala Blender**: cincin per sumbu; saat diseret tampil juring arah putaran, panah, dan sudut dalam derajat.
- **Gizmo**: mode Local/World, Snap (atau tahan Ctrl), F untuk fokus kamera ke objek terpilih.
- **Override prefab**: garis biru di Inspector untuk field yang di-override, klik kanan > Revert Override.
- **Timeline**: nilai key bisa diketik langsung.
- **Prefab tertaut**: instance mengingat prefab-nya. Inspector punya Apply (simpan ke prefab, perbarui semua instance), Revert, dan Unlink; membuka scene mengambil perubahan prefab terbaru. Posisi, rotasi, dan parent tiap instance tetap. Perubahan yang dibuat pada satu instance menjadi override: tetap saat prefab berubah, terdaftar di Inspector, dan bisa di-Revert satu per satu. Id objek anak tetap sama setelah sinkron, jadi referensi script ke anak tidak putus.
- **Multi-select**: Ctrl+klik di Hierarchy; Delete menghapus semua, gizmo pindah menggeser semua.
- **Editor dalam bahasa Jepang lengkap**: semua panel Inspector, modal, dan pesan status kini ikut Options > Language.
- **Auto-save**: scene yang belum disimpan disalin tiap 2 menit ke `Library/AutoSave`; saat scene dibuka setelah editor tertutup tiba-tiba, editor menawarkan memulihkannya.
- **Decal**: komponen `Decal` mengecat tekstur ke permukaan di dalam kotak objek, misalnya tanda serangan di lantai.
- **Timeline**: komponen `Timeline` dan Window > Timeline untuk adegan dan gerakan berskrip: key posisi, rotasi, skala, serta event untuk script.
- **Kabut**: di Lighting & Environment, berdasarkan jarak dan ketinggian. Script: `fogEnabled`, `fogColor`, `fogDensity`, `fogHeight`, `fogHeightFalloff` di `ScriptPostProcess`.
- **Terrain**: komponen `Terrain` membuat mesh dan Mesh Collider dari noise atau gambar heightmap.
- **Trail**: komponen `Trail`, ekor pita yang selalu menghadap kamera, untuk tebasan, dash, dan peluru.
- **Partikel GPU**: komponen `GPU Particles` untuk ribuan partikel lewat compute shader, dengan preset Fire, Sparks, Smoke, Magic, dan Snow serta pratinjau langsung di Scene view.
- **Audio 3D**: suara mengikuti posisi dan arah kamera game (kiri/kanan) serta mengecil dengan jarak. Bisa lewat komponen Audio Source (`3D Sound`, `Min/Max Distance`) atau script (`PlayAudioAt`, `PlayAudioOn`).
- **Doppler dan dinding pada audio 3D**: suara yang mendekat terdengar lebih tinggi, yang menjauh lebih rendah; suara di balik collider Static terdengar lebih pelan dan teredam.
- **Partikel GPU**: tabrakan dengan lantai (`Collide With Floor`, `Floor Height`, `Bounce`), gradasi 3 titik warna/ukuran (`Use Middle Key`), dan emitter digambar dari jauh ke dekat.
- **Trail dengan tekstur**: field `Texture` di komponen Trail.
- **Prefab Mode**: buka prefab sendirian (tombol Open di Inspector atau klik kanan file prefab), simpan dengan Ctrl+S, lalu Back to Scene.
- **Prefab variant**: Create Variant membuat prefab baru yang mengikuti prefab asalnya dan menyimpan perbedaannya sendiri.
- **Nested prefab**: prefab di dalam prefab lain tetap mengikuti perubahan prefab dalamnya.
- **Pahat terrain**: centang Sculpt di komponen Terrain, lalu seret di Scene view (Raise, Lower, Smooth, Flatten). Lingkaran kuas tampil di permukaan; Ctrl+Z membatalkan satu goresan.
- **Decal tidak lagi mengecat karakter**: objek ber-Rigidbody dan model beranimasi tidak terkena tanda di lantai.
- **Bus audio, pitch, dan batas suara**: bus Effects/Music/Voice/UI dengan `SetBusVolume`, `SetAudioPitch`, maksimal 64 suara (12 per klip); musik tidak pernah tergantikan.
- **Audio Source dari script**: `PlayAudioSource`, `StopAudioSource`. Play On Awake kini juga berlaku untuk objek yang muncul saat Play (spawn, pool, `LoadScene`).
- **Komponen dari script**: `HasComponent`, `AddComponent`, `RemoveComponent`, dan `GetComponentFloat/Int/Bool/Vector3` beserta `Set...`-nya untuk data component.
- **Kamera first/third person**: `GetMouseDelta`, `GetMouseWheel`, `SetCursorLocked`.
- **Proyeksi**: `ScreenPointToRay` dan `WorldToCanvas` untuk memilih objek 3D dan UI yang mengikuti objek.
- **`PrewarmPrefab(nama, jumlah)`**: mengisi pool lebih awal agar spawn pertama tidak tersendat.
- **Crash di script tidak lagi menutup editor**: Play berhenti, scene dipulihkan, DLL dimuat ulang, dan Console menunjukkan class serta objeknya. Game Release tetap membuat crash dump.
- **Samples > Third Person Demo**.
- **Post-processing per scene** (Lighting & Environment): tone mapping ACES, bloom, exposure, kontras, saturasi, tint, vignette, letterbox, FXAA. Scene baru aktif; scene lama tetap tampil seperti semula sampai diaktifkan.
- **Efek layar dari script**: `SetScreenEffect` menjalankan shader buatan sendiri di atas gambar HDR dan bisa membaca depth. Bawaan: `ScreenEffect.Slash` dan `ScreenEffect.RadialBlur`. Ubah tampilan saat bermain dengan `Get/SetPostProcess`.
- **Fisika Jolt** menggantikan solver lama: benda bisa berputar dan bertumpuk stabil, collider kotak mengikuti rotasi objek, dan Sphere Collider kini ikut menahan benda.
- **Komponen baru**: Capsule Collider, Mesh Collider (segitiga model; tidak pernah disimulasikan), Joint (Fixed, Point, Hinge, Distance).
- **Rigidbody**: Friction, Freeze Rotation, Continuous Collision (peluru cepat tidak menembus dinding tipis). Gerakan Rigidbody kini halus di monitor di atas 60 Hz.
- **`Raycast` dan `OverlapSphere`** kini mengikuti rotasi, kapsul, dan mesh.

- **PBR**: komponen **PBR Material** (metallic, roughness, emissive, normal map, peta occlusion/roughness/metallic). Peta yang dibawa model dipakai otomatis. **Save As Material Asset** membuat `.material.json` yang bisa dipakai bersama banyak objek.
- **Cahaya dari langit (IBL)**: material PBR memantulkan skybox, termasuk mode gradient dan warna polos. **Environment Intensity** ada di Lighting & Environment.
- **Bayangan cascade**: bayangan tajam di dekat kamera sampai **Shadow Distance**. Scene baru memakai 3 cascade; scene lama tetap satu shadow map (Shadow Cascades 0) sampai diubah.
- **Lampu tanpa batas 3**: sampai 1024 point dan spot light, dengan cluster agar setiap piksel hanya menghitung lampu terdekat (maksimum 64 per cluster).
- **Ambient Occlusion** di Post Processing: aktif di scene baru.
- **Instancing otomatis**: mesh statis dengan model dan material yang sama digambar dalam satu draw call, termasuk di shadow pass.

- **`MoveCharacter` memakai Jolt**: kini mengenal kotak yang diputar, kapsul, dan mesh collider.
- **Blend tree** di Animator: satu state mencampur beberapa klip menurut parameter float, dengan fase yang sama agar langkah kaki tetap selaras.
- **Layer animasi**: `PlayAnimationLayer` memainkan klip di atas state machine, bisa dibatasi ke satu tulang dan turunannya (menebas sambil berlari).
- **Attach ke tulang**: `AttachToBone` dan komponen Bone Attachment. `GetBonePosition` membaca posisi tulang.
- **Root motion**: opsi `rootMotion` di Animator; gerakan klip menggerakkan objek lewat collider-nya.
- **IK**: `SetBoneIK` menaruh kaki atau tangan tepat di sasaran.
- **Ragdoll**: `SetRagdoll` menyerahkan rangka ke fisika.

### Perubahan Perilaku
- `MoveCharacter` melihat objek lain pada posisi awal frame, jadi dua karakter yang bergerak di frame yang sama bisa sedikit tumpang tindih sesaat.
- Project lama perlu Build Scripts ulang (`kScriptApiVersion` 11).
- Scene lama dengan lebih dari 3 point light atau 3 spot light kini menampilkan semuanya.
- Scene lama tetap tanpa gesekan dan rotasi fisika (Friction 0, Freeze Rotation aktif) sampai diubah. Objek baru: Friction 0.5, boleh berputar.
- Collider tanpa Rigidbody yang digerakkan script melaporkan kontak barunya satu step fisika kemudian (1/60 detik).
- Dua objek yang sama-sama bertanda Static tidak lagi saling melaporkan tabrakan.
- `Raycast` dan `OverlapSphere` melihat posisi objek sejak awal frame; gerakan yang dibuat script lain di frame yang sama baru terlihat di frame berikutnya.
- `FindPath` masih memakai kotak tegak dan belum mengenal kapsul, mesh collider, atau kotak yang diputar (Fase 8).

### Perbaikan
- Ambient occlusion tidak lagi membuat garis horizontal di lantai datar.
- Membuat ulang sampel kini memperbarui skripnya di project; versi lama disimpan sebagai `.old`.
- Hot reload shader tidak lagi menghapus PSO yang mungkin masih dipakai GPU (bisa membuat driver crash).

### Performa
- Editor saat Play jauh lebih ringan: kode editor kini dikompilasi dengan optimasi, dan mesh statis tidak lagi menyalin palette tulang (benchmark 25 ms → 17 ms).

## [2.3.0] — 2026-10-31

### Fitur Baru
- **Export game sungguhan**: **File > Build & Export Game...** kini memakai runtime Release (folder `Runtime/` di paket engine), tanpa UI editor.
- **Script di-compile saat export**: konfigurasi Release yang teroptimasi. Kalau script gagal di-compile, export berhenti dengan error di Console.
- **Hanya menyalin yang perlu**: dari `Resources/` hanya file yang dipakai (ukuran contoh turun ke ±22 MB).

### Perbaikan
- Game hasil export kini membaca `ProjectSettings/input.json`, sebelumnya selalu memakai tombol default.
- Game hasil export di dalam folder project kini memuat `GameScripts.dll` miliknya sendiri.
- Folder export default kini `<Project>/Build`, bukan di dalam folder engine yang diganti saat update.

### Catatan
- Script yang menyusun path `Resources/...` saat runtime tidak terdeteksi exporter. Letakkan aset di `Assets/`.

---

## [2.2.0] — 2026-10-30

### Fitur Baru
- **Character Controller**: Fungsi `MoveCharacter(motion, stepHeight)` untuk pergerakan karakter yang halus tanpa menembus dinding (AABB anti-tunneling), dilengkapi pijakan undakan (step up) dan pergeseran dinding (wall slide).
- **Sistem Navigasi & A* Pathfinding**: Penentuan jalur otomatis `FindPath` dan `RebuildNavigation` berbasis grid dari collider objek berlabel Static, dengan penghalusan jalur garis lurus (line-of-sight smoothing).
- **State Machine AI**: Helper `StateMachine` di `ScriptTimers.h` untuk mempermudah logika Finite State Machine musuh/karakter.
- **Objek Persisten Antar-Scene**: Fungsi `KeepAcrossScenes()` untuk mempertahankan objek (seperti Player atau GameManager) saat berpindah scene dengan `LoadScene()`.
- **Hot-Reload Shader**: Perubahan pada shader `Object3d` HLSL dimuat ulang secara otomatis (≤0.5 detik) tanpa perlu me-restart editor.
- **Toon Shading**: Model pencahayaan kartun (*Reflection Model: 5: Toon*) dengan pembagian bayangan bergradasi tegas dan sorotan spekular tajam.
- **Mesh Outline**: Efek garis tepi (outline) per mesh renderer berbasis *inverted hull* (`outlineWidth` dan `outlineColor`).
- **Panduan Pengembang Lengkap**: Penambahan `GUIDE_JA.md` (panduan alur kerja untuk tim dalam bahasa Jepang) dan `SCRIPTING_GUIDE.md` (referensi lengkap API C++ scripting).

---

## [2.1.0] — 2026-10-29

### Fitur Baru
- **GameScripts Mandiri Per-Project**: Setiap project game memiliki folder `GameScripts` dan solution sendiri sehingga tidak lagi bercampur dengan repositori engine.
- **Template Project Bersih**: Opsi pembuatan project baru dengan struktur folder rapi, `.gitignore`, `.gitattributes`, dan integrasi inisialisasi Git repository.
- **Updater Engine Terintegrasi**: Fitur **Help > Install Engine Update...** serta drag & drop file zip engine langsung ke dalam editor untuk memperbarui instalasi engine secara otomatis.
- **Game Feel Toolkit**: Efek guncangan kamera `ShakeCamera(strength, duration)` dan penghentian waktu benturan `Hitstop(duration)`.
- **Sistem Timer Script**: Helper `Cooldown` dan `TimerQueue` (`After`, `Every`) berbasis waktu permainan.
- **Input Actions**: Pemetaan input aksi (`ProjectSettings/input.json`) dengan dialog konfigurasi di **Options > Input Actions** untuk keyboard, mouse, dan gamepad XInput.
- **Fixed Timestep Physics**: Pembaruan simulasi fisika dengan langkah tetap 60 Hz untuk konsistensi di berbagai framerate.
