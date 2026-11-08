# Changelog — Rizeallday Engine v2

## [2.9.1] — 2026-11-09

### Perbaikan
- Window graph (Material/Particle Graph) ikut pindah saat file atau foldernya di-rename/dipindah di panel Project, dan berhenti menulis file yang sudah dihapus.
- Angka yang masih diketik di window graph sekarang tersimpan saat graph lain dibuka, saat Close ditekan, atau saat window ditutup.
- Kalau file graph tidak bisa ditulis (read-only atau dipakai program lain), muncul label merah **Not saved**. Editan tidak hilang: Close dan membuka graph lain ditolak, dan editan tersimpan begitu file bisa ditulis lagi.
- Pipeline GPU lama dari material/particle graph dilepas setelah graph diubah (sebelumnya menumpuk sampai editor ditutup).
- Effect Editor selalu mengedit layer yang dipilih; petunjuk preview menjadi "Press Play above to see the effect here."

## [2.9.0] — 2026-11-08

### Fitur Baru: Material Graph dan Particle Graph
- **Material Graph (runtime)**: GPU Particles mendapat field **Material** yang menunjuk file `.vfxmat.json`. Graph node menentukan warna dan opacity setiap piksel partikel. Node yang tersedia: Color/Float/Vector3/Time, math (Add, Multiply, Lerp, Step, Smoothstep, Remap, dll.), Particle Color/Age/Random, Texture Sample (sampai 4 tekstur), Panner, Rotator, Flipbook, Polar Coordinates, Noise, Radial Gradient, Dissolve. Material yang rusak menampilkan pesan dan partikel tetap tergambar seperti biasa. Window editor graph-nya menyusul di fase berikutnya.
- **Window Material Graph** (*Window > Material Graph*, tombol **Edit Material / New Material...** di Inspector GPU Particles, atau double-click file `[MAT]` di panel Project): palet node yang bisa dicari, canvas, dan panel detail dengan penjelasan setiap node. Tarik dari pin ke pin untuk menyambung (pin berwarna per tipe, pin yang cocok disorot, alasan penolakan ditampilkan). Lepas kabel di tempat kosong untuk memilih node yang langsung tersambung. Undo/Redo, Duplicate (Ctrl+D), Fit (F). Perubahan tersimpan otomatis, jadi partikel di Scene view langsung berubah. Template: Soft Glow, Scrolling Fire, Dissolve, Flipbook Smoke, Energy Ring. Semua nama node dan petunjuk tersedia dalam bahasa Jepang dan Indonesia.
- Material Graph juga bisa dipakai **Trail** (field Material + tombol Edit/New Material di Inspector Trail).
- Panel **preview** di window Material Graph: material langsung terlihat pada beberapa partikel.
- Tekstur yang dipilih di node material tetap ditemukan setelah file tekstur dipindah atau di-rename.
- **Particle Graph (runtime)**: GPU Particles mendapat field **Graph** (`.vfxparticle.json`) yang menentukan sendiri bagaimana partikel lahir dan bergerak. Spawn: Sphere/Box/Ring Shape, Cone Direction, Random Range, Emitter. Update: Gravity, Drag, Wind, Vortex, Curl Noise, Point Attractor, Floor Collision, Kill. **Curve** dan **Gradient** (sampai 8 key) untuk ukuran dan warna sepanjang umur partikel. Material Graph tetap bisa dipakai bersamaan.
- **Parameter efek**: graph bisa punya sampai 8 parameter (angka, vektor, warna). Nilainya bisa diubah per objek di Inspector (dengan tombol Reset) dan dari script dengan `SetEffectParameter("Spin", 6.0f)`. `kScriptApiVersion` naik ke 16, jadi script project perlu di-build ulang.
- Template particle graph di Inspector (**New Particle Graph...**): Fountain, Magic Swirl, Burst, Rising Embers. Window graph yang sama juga mengedit particle graph (judulnya berubah menjadi **Particle Graph**): palet node partikel, daftar **Parameters** (tambah, rename yang ikut mengubah node, tipe, nilai bawaan), editor key **Curve** (titik bisa diseret) dan **Gradient**, preview partikel langsung. Buka lewat **Edit Graph** di Inspector, double-click file `[PFX]` di panel Project, atau dari window graph.

- **Panel preview di Effect Editor**: efek yang sedang di-preview tampil sendiri (tanpa objek lain) di dalam window. Seret untuk memutar kamera, roda mouse untuk zoom. Efek baru dari template langsung diputar.

### Perubahan (tanpa duplikasi antara Effect Editor dan Inspector)
- Kurva **Size over Lifetime** (key bisa diseret) dan gradien **Color over Lifetime** pindah ke Inspector GPU Particles, menggantikan baris angka Size/Color Start/Middle/End.
- Effect Editor hanya berisi timing, timeline, preview dan Preview Motion. Layer yang memakai particle graph menampilkan penjelasan dan tombol **Edit Graph**.
- Selama field **Graph** terisi, Inspector GPU Particles menyembunyikan field yang tidak dipakai graph (Life, Speed, Shape, Gravity, Floor, Intensity, ...). Nilainya tetap tersimpan.
- Checkbox **Post FX** cukup ada di toolbar Scene (dihapus dari Effect Editor).
- Menu *Window > Material Graph* menjadi *Window > Effect Graph* (window yang sama untuk material dan particle graph).
- Effect Editor mengikuti layer yang dipilih di Hierarchy.

### Pengujian rilis
- Smoke test export baru (`Tools/SmokeTestExport.ps1 -Sample effects`): project dengan satu material graph dan satu particle graph di-export lalu dijalankan. Game gagal (`--smoke-frames`) kalau ada graph yang hilang atau tidak ter-compile. Berjalan di CI dan di `MakeRelease.ps1` (memakai editor dan runtime dari paket rilis itu sendiri).

### Perbaikan
- Preview Effect Editor langsung berhenti selama window Material Graph tertutup.
- Tombol Delete, Ctrl+Z dan Ctrl+D di window Material Graph / Animator tidak lagi ikut menghapus atau mengubah objek yang dipilih di scene.

## [2.8.1] — 2026-11-07

### Perbaikan
- Scene dengan siklus parent (file rusak) tidak lagi membuat editor hang saat ada efek atau partikel.
- `SetParticlesEmitting(false)` dan field `emitting` sekarang bisa menghentikan layer efek yang sedang berjalan.
- Timeline Effect Editor memakai aturan layer yang sama dengan runtime, jadi panjang lane sesuai.
- GPU Particles: objek yang kembali dari pool memakai buffer GPU yang sama (tidak membuat buffer baru setiap spawn), dan partikel lamanya tidak muncul lagi.

## [2.8.0] — 2026-11-07

### Fitur Baru
- **Look Preset** di panel Post Processing: Clean Default, Vivid Action, Cinematic Warm, Neon Glow, Dark Moody. Bayangan, ambient occlusion, dan kabut tidak ikut berubah.
- **Post FX di Scene view** (toolbar viewport atau "Preview in Scene View"): bloom, grading, dan efek layar game terlihat saat merancang. Default mati karena menambah satu pass post-process.
- **Screen Effects studio**: coba shader `ScreenEffect.*.PS.hlsl` beserta parameternya tanpa menulis script.
- **Effect Editor** (*Window > Effect Editor*, atau *GameObject > Visual Effects > Effect*): efek berlapis berbasis prefab. Root efek memakai komponen baru **Effect Timing** (start delay, duration, loop, despawn saat selesai, impact pulse per layer), dan setiap anak dengan GPU Particles atau Trail menjadi satu layer.
  - Transport Play/Pause/Restart/Stop dengan kecepatan 0,1–2x, lane waktu per layer, kurva *Size over Lifetime* yang bisa diseret, dan gradien *Color over Lifetime*.
  - **Preview Motion** (Circle, Figure Eight, Line) hanya menggerakkan tampilan di Edit, sehingga Trail bisa dilihat tanpa Play. Posisi yang disimpan tidak berubah.
  - Template: Hit Spark, Explosion, Magic Aura, Slash Trail. **Save as Prefab**, lalu `InstantiatePrefab` dari script akan memutar efek dari awal setiap kali di-spawn (termasuk dari pool).
  - Di Edit, efek hanya bergerak saat di-preview; efek lain diam.
- Script: `TriggerImpactPulse(bloom, blur, seconds, shake)` dan `SetLookPreset("Dark Moody")`. `kScriptApiVersion` naik ke 15, jadi script project perlu di-build ulang.
- **Impact Pulse**: kilatan bloom, radial blur, dan guncangan kamera yang memudar sendiri, bisa diuji di Edit mode lewat tombol **Test Impact Pulse**. Engine memakai slot efek layar 4; script tetap memakai slot 0..3.

## [2.7.0] — 2026-11-07

### Perubahan
- **Particle System (CPU) digabung ke GPU Particles.** Hanya ada satu komponen partikel. Scene dan prefab lama otomatis dimuat sebagai GPU Particles dengan nilai terdekat; opsi "Attach To" lama menjadi parent di Hierarchy.
- GPU Particles mendapat **Upright Billboard** (partikel berdiri di sumbu Y, untuk api dan rumput) dan **Spawn Burst** (prefab menyemburkan partikel saat di-spawn atau diambil dari pool).
- `EmitParticles()` dan `SetParticlesEmitting()` sekarang bekerja pada GPU Particles. Script tidak perlu diubah.

## [2.6.0] — 2026-11-07

### Fitur Baru
- **Bentuk emitter GPU Particles**: Titik, Bola, Kotak, Kerucut, dan Cincin, ikut rotasi dan skala objek, dengan gizmo oranye di Scene view untuk objek terpilih.
- **Arah Radial**: partikel menjauh dari pusat bentuk, untuk ledakan dan gelombang kejut.
- **Partikel memanjang (stretched billboard)**: `stretchFactor` membuat percikan dan hujan berbentuk goresan searah gerak. Preset Sparks kini memakainya.
- Preset baru: **Shockwave Ring**, **Explosion**, **Portal**. Scene lama tetap terlihat sama (titik, arah, tanpa regangan).

## [2.5.3] — 2026-11-07

### Perbaikan
- Mengganti nama scene di Hierarchy lalu Ctrl+S tidak lagi mengembalikan nama lama: file scene (beserta `.meta`) ikut diganti namanya. Kotak nama langsung bisa diketik (huruf tidak lagi memicu shortcut tool), dan double-click tidak lagi menutup daftar objek.

## [2.5.2] — 2026-11-06

### Perbaikan
- Setelah update engine, script tidak lagi ditolak dengan "built against a different engine version". Editor sekarang otomatis menjalankan **Rebuild** penuh satu kali bila DLL ditolak karena versi API berbeda. Export game selalu memakai Rebuild. Penyebabnya: header SDK hasil update membawa tanggal lama, jadi build incremental memakai ulang file `.obj` dari versi sebelumnya.

## [2.5.1] — 2026-11-06

### Perbaikan
- Build script tidak lagi tertahan di "Status: Compiling..." selamanya. Penyebabnya: program bantu Visual Studio (`mspdbsrv.exe`, `vctip.exe`) masih hidup setelah build selesai dan memegang output build. Akibatnya script baru tidak pernah di-load dan player tidak bergerak. Editor sekarang berhenti membaca output begitu MSBuild selesai (juga untuk export game dan git).

### Perbaikan
- Paste Component pada Camera kini ikut menyalin Orthographic dan Size; gizmo kamera orthographic digambar sebagai kotak seukuran area yang terlihat.

## [2.5.0] — 2026-11-05

### Fitur Baru
- **Kamera orthographic**: komponen Camera punya centang *Orthographic* + *Size* (setengah tinggi area yang terlihat). Cocok untuk tampilan isometric. Script: `ScriptCameraData::orthoSize`. API script naik ke versi 14, jadi rebuild script project setelah update.

## [2.4.3] — 2026-11-04

### Perbaikan
- `SetRotation`, `SetPosition`, dan `SetScale` tidak lagi menimpa perubahan `self->translation` / `rotation` / `scale` yang dibuat script di frame yang sama (player bisa berputar tapi tidak berpindah). Rebuild script project setelah update.

## [2.4.2] — 2026-11-04

### Perbaikan
- Update engine tidak lagi gagal di tengah jalan saat `engine.json` sedang dibaca program lain (antivirus, indexer): tiap file ditunggu sebentar, lalu ditimpa langsung bila tidak bisa diganti nama. Pesan error kini menyebut bila sebagian update sudah terpasang.

## [2.4.1] — 2026-11-04

### Fitur Baru
- **Warna material dari file model**: model low-poly tanpa tekstur (glTF, FBX, OBJ) kini tampil dengan warna tiap bagiannya. Material Color di Inspector tetap dikalikan ke semuanya, jadi mudah dipakai untuk mewarnai varian (mis. zombie kehijauan).

### Catatan
- Karakter FBX hasil ekspor Blender dengan skala unit cm/m yang tidak konsisten bisa tampil pecah. Pakai versi glTF-nya (FBX dari Mixamo aman).

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
