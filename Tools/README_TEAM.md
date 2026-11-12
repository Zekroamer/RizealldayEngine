# Rizeallday Engine — for the team

**Needs:** Windows 10/11, Visual Studio 2022 or 2026 with "Desktop development with C++", Git.

## Start
1. Extract the zip anywhere (e.g. `D:\RizealldayEngine`). Keep game projects **outside** this folder.
2. Run `Editor\RizealldaySandbox.exe`.

## Make a game project (one person)
1. Project Hub > New Project (keep "Initialize Git repository" on).
2. Create an empty repository on GitHub, then in the project folder:
   `git remote add origin <url>` and `git push -u origin HEAD`

## Join a game project (everyone else)
1. `git clone <url>`
2. Editor > File > Open Project... > the cloned folder. The scripts build by themselves the first time.

## Every day
- Close the editor (or stop Play) before `git pull`.
- One person per scene at a time; shared things go into prefabs.
- Commit the `.meta` files next to your assets.
- Scripts: `GameScripts\Scripts`. Open `<Project>.sln` in Visual Studio, or use the editor's "Create C++ Script".

## Building levels
GameObject > 2D Object > **Mapchip Grid**: paint tiles on a grid (with layers for depth or floors) and press
**Build Level Objects**. Window > **Tile Set Editor** decides what each tile becomes (your own prefabs, names and tags).
Step by step: `GUIDE_JA.md`, section 9. What changed in each version: `CHANGELOG.md`.

## Engine update
Got a new `RizealldayEngine-x.y.z.zip`? **Help > Install Engine Update...** (or drop the zip on the editor window).
The editor closes, installs the update and starts again. Projects are not changed; their scripts rebuild when opened.
If the editor says a project was saved with a newer engine, install the newest zip first.

---

## Bahasa Indonesia (ringkas)
- Ekstrak zip, jalankan `Editor\RizealldaySandbox.exe`. Project game disimpan di luar folder engine.
- Anggota baru: `git clone` → File > Open Project → script ter-build otomatis.
- Tutup editor sebelum `git pull`. Satu scene satu orang. Commit file `.meta`.
- Update engine: **Help > Install Engine Update...** lalu pilih zip baru (atau drag zip ke editor).
- Membuat level: **Mapchip Grid** (cat petak, bisa berlapis) lalu **Build Level Objects**. Jenis petak dan prefab-nya diatur di **Window > Tile Set Editor**.

## 日本語 (要約) — 詳しい使い方は `GUIDE_JA.md`
- zip を展開して `Editor\RizealldaySandbox.exe` を起動。ゲームプロジェクトはエンジンのフォルダーの外に置く。
- 参加するとき: `git clone` → File > Open Project → スクリプトは自動でビルドされる。
- `git pull` の前にエディターを閉じる。シーンは一人ずつ編集。`.meta` もコミットする。
- エンジン更新: **Help > Install Engine Update...** で新しい zip を選ぶ (zip をエディターにドロップしても可)。
- レベル作り: **Mapchip Grid** でタイルを塗り（レイヤーで奥行き・階も）、**Build Level Objects**。タイルの種類とプレハブは **Window > Tile Set Editor**。詳しくは `GUIDE_JA.md` の 9 章。
