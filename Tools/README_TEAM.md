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
- Pull and push in Visual Studio (**Git > Pull** / **Git > Push**). The editor and Visual Studio can stay open:
  the open scene reloads by itself, and unsaved edits are merged with the pulled file (a pull during Play applies after Stop).
- Several people may edit the same scene: scenes and prefabs merge object by object and field by field.
  When both sides changed the same field, the editor shows a yellow notice: decide each one in
  **Window > Merge Conflicts** (pick **Mine** / **Theirs** per field, it previews in the scene, then **Apply choices**), then save and commit.
- **Never** press "Keep Current" / "Take Incoming" in Visual Studio on a `*.scene.json` or `*.prefab.json`: it throws away
  the other person's changes. If a scene shows up there, Abort the merge, open the project in the editor once, and pull again.
- GitHub says a pull request "has conflicts"? Do not fix scenes on the web. In Visual Studio, merge `origin/master` into the branch
  (Git > Manage Branches > right-click `origin/master` > Merge), decide any Merge Conflicts, then push.
- Commit the `.meta` files next to your assets.
- Scripts: `GameScripts\Scripts`. Open `<Project>.sln` in Visual Studio, or use the editor's "Create C++ Script".

## Building levels
GameObject > 2D Object > **Mapchip Grid**: paint tiles on a grid (with layers for depth or floors) and press
**Build Level Objects**. Window > **Tile Set Editor** decides what each tile becomes (your own prefabs, names and tags).
Step by step: `GUIDE_JA.md`, section 9. What changed in each version: `CHANGELOG.md`.

## Game UI
GameObject > **UI** > Canvas / Panel / Image / Text / Progress Bar / Button. Elements are scene objects (undo, prefabs, merge);
layout in the Inspector's **UI Rect**. Scripts change them by name (`SetUIText`, `SetUIVisible`, `SetUIProgress`, ...) and get clicks in `OnUIButton`.

## Engine update
Got a new `RizealldayEngine-x.y.z.zip`? **Help > Install Engine Update...** (or drop the zip on the editor window).
The editor closes, installs the update and starts again. Projects are not changed; their scripts rebuild when opened.
If the editor says a project was saved with a newer engine, install the newest zip first.

---

## Bahasa Indonesia (ringkas)
- Ekstrak zip, jalankan `Editor\RizealldaySandbox.exe`. Project game disimpan di luar folder engine.
- Anggota baru: `git clone` → File > Open Project → script ter-build otomatis.
- Pull/push lewat Visual Studio, editor boleh tetap terbuka (scene yang terbuka dimuat ulang atau digabung otomatis). Satu scene boleh diedit beberapa orang. Commit file `.meta`.
- Kalau muncul tulisan kuning konflik merge: buka **Window > Merge Conflicts**, pilih **Mine** / **Theirs** per field (langsung terlihat di scene), klik **Apply choices**, lalu simpan dan commit.
- **Jangan** pakai "Keep Current" / "Take Incoming" di Visual Studio untuk file scene/prefab. Pull request yang konflik di GitHub: merge `origin/master` ke branch di Visual Studio, lalu push.
- UI game: **GameObject > UI** (Canvas, Panel, Image, Text, Progress Bar, Button); script mengubahnya lewat nama (`SetUIText`, `OnUIButton`).
- Update engine: **Help > Install Engine Update...** lalu pilih zip baru (atau drag zip ke editor).
- Membuat level: **Mapchip Grid** (cat petak, bisa berlapis) lalu **Build Level Objects**. Jenis petak dan prefab-nya diatur di **Window > Tile Set Editor**.

## 日本語 (要約) — 詳しい使い方は `GUIDE_JA.md`
- zip を展開して `Editor\RizealldaySandbox.exe` を起動。ゲームプロジェクトはエンジンのフォルダーの外に置く。
- 参加するとき: `git clone` → File > Open Project → スクリプトは自動でビルドされる。
- pull / push は Visual Studio で。エディターは開いたままでよい（開いているシーンは自動で読み込み直し・マージされる）。同じシーンを複数人で編集してよい。`.meta` もコミットする。
- メニューバーに黄色のマージ競合の表示が出たら **Window > Merge Conflicts** で項目ごとに **Mine** / **Theirs** を選び（シーンでプレビューされる）、**Apply choices** を押して保存・コミット。
- シーン・プレハブに Visual Studio の「Keep Current」/「Take Incoming」は**使わない**。GitHub でマージできないプルリクエストは、Visual Studio で `origin/master` をブランチにマージしてから push。詳しくは `GUIDE_JA.md` の 4 章。
- ゲーム UI: **GameObject > UI**（Canvas・Panel・Image・Text・Progress Bar・Button）。スクリプトからは名前で操作（`SetUIText`、`OnUIButton`）。
- エンジン更新: **Help > Install Engine Update...** で新しい zip を選ぶ (zip をエディターにドロップしても可)。
- レベル作り: **Mapchip Grid** でタイルを塗り（レイヤーで奥行き・階も）、**Build Level Objects**。タイルの種類とプレハブは **Window > Tile Set Editor**。詳しくは `GUIDE_JA.md` の 9 章。
