# Rizeallday Engine 使い方ガイド

Rizeallday Engine は DirectX 12 / C++20 で作られた、Unity に近い操作感のゲームエンジンです。
ゲームのロジックは C++ スクリプト（DLL）で書き、エディターでシーンを組み立てて Play で確認します。

> このガイドは配布 zip に同梱されています。チーム開発の要点だけは `README_TEAM.md` にもまとめてあります。

---

## 目次
1. [必要なもの](#1-必要なもの)
2. [インストールと起動](#2-インストールと起動)
3. [プロジェクトを作る / 開く](#3-プロジェクトを作る--開く)
4. [チームでの開発（Git）](#4-チームでの開発git)
5. [エディターの基本操作](#5-エディターの基本操作)
6. [C++ スクリプトの書き方](#6-c-スクリプトの書き方)
7. [スクリプト API リファレンス](#7-スクリプト-api-リファレンス)
8. [見た目（トゥーン・アウトライン）](#8-見た目トゥーンアウトライン)
9. [マップチップでレベルを作る](#9-マップチップでレベルを作る)
10. [最近の主な新機能](#10-最近の主な新機能)
11. [エンジンの更新](#11-エンジンの更新)
12. [困ったとき](#12-困ったとき)

---

## 1. 必要なもの
- Windows 10 / 11（Direct3D 12 対応 GPU）
- **Visual Studio 2022 または 2026**（ワークロード「C++ によるデスクトップ開発」）
  スクリプトは各 PC でビルドされるため、チーム全員に必要です。
- **Git**（チームでプロジェクトを共有する場合）

## 2. インストールと起動
1. `RizealldayEngine-x.y.z.zip` を好きな場所に展開します（例: `D:\RizealldayEngine`）。
2. `Editor\RizealldaySandbox.exe` を起動します。ショートカットから起動しても構いません。
3. 最初に **Project Hub** が開きます。

```
RizealldayEngine/
├─ Editor/        エディター本体（exe、シェーダー、リソース）
├─ Runtime/       書き出したゲームが使う Release 版の実行ファイル
├─ SDK/           スクリプトが使うヘッダー（ScriptAPI.h など）
├─ Templates/     新規プロジェクト用のテンプレート
├─ engine.json    エンジンのバージョン
└─ README_TEAM.md / GUIDE_JA.md / CHANGELOG.md（変更履歴）/ SCRIPTING_GUIDE.md（API の詳細、インドネシア語）
```

> ゲームプロジェクトは **エンジンのフォルダーの外** に置いてください（更新時にエンジンのフォルダーは入れ替わります）。

## 3. プロジェクトを作る / 開く
### 新規プロジェクト
Project Hub > **New Project** で名前と場所を入力し **Create Project**。
「**Initialize Git repository**」をオンにすると `git init` と最初のコミットまで自動で行います。

作られるもの:
```
MyGame/
├─ Assets/        Scenes, Models, Textures, Audio, Prefabs, Animators
├─ GameScripts/   C++ スクリプト（Scripts/*.cpp, *.h）とプロジェクトファイル
├─ ProjectSettings/project.json, input.json
├─ MyGame.sln     Visual Studio でスクリプトを開くソリューション
├─ .gitignore / .gitattributes / README.md
└─ Library/       ビルドした DLL など（自動生成・Git に入れない）
```

### 既存プロジェクトを開く
Project Hub の最近使ったプロジェクトをダブルクリック、または **Browse...** でフォルダーを選択。
メニューの **File > Open Project... / Open Recent** でも開けます。

### アセットの取り込み
- **Assets > Import Assets... / Import Folder...**、または Explorer からエディターにドラッグ＆ドロップ。
- `.obj` の `.mtl` やテクスチャ、`.gltf` のバッファ、`.fbx` の `.fbm` も一緒にコピーされます。
- 各アセットの隣に `.meta`（GUID）ができます。**`.meta` も必ずコミット**してください（名前を変えても参照が切れません）。
- Project パネルの右クリックで名前変更・削除、フォルダーへのドラッグで移動（シーンの参照も自動で更新）。

## 4. チームでの開発（Git）
### 一人目（プロジェクトを作る人）
1. New Project（Initialize Git repository をオン）。
2. GitHub で空のリポジトリを作る。
3. プロジェクトのフォルダーで:
   ```
   git remote add origin <URL>
   git push -u origin HEAD
   ```

### 二人目以降（参加する人）
1. `git clone <URL>`
2. エディターの **File > Open Project...** でクローンしたフォルダーを開く。
3. この PC 用の `GameScripts/RizeEngine.props` が作られ、スクリプトが自動でビルドされます。

### 毎日のルール
- `git pull` の前に **エディターを閉じる（または Play を止める）**。
- **シーンは一人ずつ編集**。みんなで使う部分はプレハブにする（マージの衝突を防ぐ）。
- `.meta` をコミットする。`Library/`、`GameScripts/RizeEngine.props` はコミットしない（`.gitignore` 済み）。
- プロジェクトが新しいエンジンで保存されていると警告が出ます。その場合は最新の zip をインストールしてください。

## 5. エディターの基本操作
| 場所 | 内容 |
|---|---|
| Hierarchy（左） | シーン内のオブジェクト。ドラッグで親子付け、右クリックで「Save as Prefab」など |
| Viewport（中央） | **Scene**（編集）/ **Game**（カメラの映像）/ Dual Split |
| Inspector（右） | 選択中のオブジェクトのコンポーネント。**Add Component** で追加 |
| Project / Console（下） | アセットの一覧 / ログ（ビルドエラーもここに出ます） |

- ギズモ: **W** 移動 / **E** 回転 / **R** 拡大縮小（ツールバーの Q W E R T C）。
- **Play** / **Pause** / **Step** / **Stop**。Play 中の変更は Stop で元に戻ります。**F11** で全画面プレビュー。
- 保存: **Ctrl+S**（シーン）。元に戻す: **Ctrl+Z** / **Ctrl+Y**。
- 言語: **Options > Language**（English / 日本語 / Indonesia）。
- キー設定: **Options > Input Actions...**（`ProjectSettings/input.json` に保存、チームで共有されます）。
- アニメーション: **Window > Animator**（ノードで状態遷移を組む）。性能: **Window > Profiler**（Ctrl+P）。
- サンプル: **GameObject > Samples > Survivors Demo** で見下ろし型シューターの例が作られます（スクリプト付き）。
- ゲームの書き出し: **File > Build & Export Game...**（既定の出力先は `<プロジェクト>/Build`）
  - エディターなしの Release ランタイム（`Runtime/`）を使い、スクリプトも Release でコンパイルします。コンパイルに失敗すると書き出しは止まり、Console にエラーが出ます。
  - `Resources/` からは、シーン・プレハブ・スクリプトに書かれたパスのファイルだけがコピーされます。実行時に組み立てたパスは検出できないので、自分のアセットは `Assets/` に置いてください。
  - 提出前に、出力フォルダーを別の場所にコピーして exe を起動し、動作を確認してください。

### よく使うコンポーネント
- **Mesh Renderer**: 形・モデル・色・テクスチャ・反射モデル（Unlit / Lambert / Half-Lambert / Phong / Blinn-Phong / Toon）・アウトライン
- **Box / Sphere Collider**: 当たり判定。**Is Trigger** = すり抜けてイベントだけ。**Fit to Mesh** でモデルに合わせる
- **Rigidbody**: 重力・速度で動く物体（60 Hz の固定ステップ）
- **Layer / Collides With**: どのレイヤー同士が当たるか
- **Static**（チェック）: 動かない地形。経路探索（FindPath）の障害物になります
- **Camera / Light / Audio Source / Particle System / Animator**

## 6. C++ スクリプトの書き方
### 作り方
Inspector > **Add Component > Script > New Script...** でクラス名を入れると、
`GameScripts/Scripts/<名前>.h / .cpp` が作られ、プロジェクトに登録され、ビルドされ、Visual Studio で開きます。
保存するとエディターが自動でビルドし、**ホットリロード**されます（`EXPOSE_FIELD` の値は保持）。手動ビルドは **Ctrl+Shift+B**。

### 基本形
```cpp
// Mover.h
#pragma once
#include "base/ScriptAPI.h"

class Mover : public ScriptBase {
public:
    EXPOSE_FIELD(float, speed, 5.0f);   // Inspector に表示・編集できる

    void Initialize() override;          // Play 開始時に一度（プールから再利用されたときも）
    void Update(float deltaTime) override;
    void OnCollisionEnter(GameObject& other) override {} // 触れ始めた瞬間
    void OnCollision(GameObject& other) override {}      // 触れている間、毎フレーム
    void OnCollisionExit(GameObject& other) override {}  // 離れたとき
    void OnDestroy() override {}

private:
    float timer_ = 0.0f;   // EXPOSE_FIELD 以外はホットリロードで保持されない → Initialize で初期化
};

// Mover.cpp
#include "Mover.h"
void Mover::Initialize() { timer_ = 0.0f; }
void Mover::Update(float dt) {
    GameObject* self = GetOwner();
    if (!self) return;
    const Vector2 move = GetAxis2D();               // WASD / 矢印 / 左スティック
    self->translation.x += move.x * speed * dt;
    self->translation.z += move.y * speed * dt;
}
REGISTER_SCRIPT(Mover)
```

- `EXPOSE_FIELD` の型: `float`, `int`, `bool`, `Vector3`, `char[N]`。
- スクリプト内の `GameObject` は軽量なビュー: `id`, `name`, `tag`, `layer`, `translation`, `rotation`（オイラー角・ラジアン）,
  `rotationQuat`, `scale`, `velocity`, `isActive`, `isGrounded`, `isColliding`。
  `rotation` を変えたら `rotationQuat = Quaternion::FromEuler(rotation)` も更新してください。
- 多くの関数は最後の引数 `id` を省略できます（省略 = このスクリプトのオブジェクト）。
- エンジンとの境界で `std::string` / `std::vector` を受け渡ししないでください（DLL の中で使うのは自由）。
- Visual Studio では `<プロジェクト名>.sln` を開くと補完（IntelliSense）が効きます。構成は **Development | x64**。

## 7. スクリプト API リファレンス
### 入力
| 関数 | 説明 |
|---|---|
| `IsActionDown("Jump")` / `IsActionTriggered("Attack")` / `IsActionReleased(...)` | アクション（キーボード・マウス・パッドをまとめて扱う）。既定: MoveX, MoveY, Jump, Attack, Submit, Cancel, Pause |
| `GetAxis("MoveX")`, `GetAxis2D()` | -1..1、2D は長さ最大 1 |
| `IsKeyDown(KEY_W)`, `IsKeyTriggered(KEY_SPACE)` | キー直接（`KEY_*` 定数を使う） |
| `IsMouseDown(0)`, `GetMouseWorldPosition(out, planeY)` | マウス（0 左 / 1 右 / 2 中）、カーソル下の床の座標 |
| `IsPadButtonDown(PAD_A)`, `GetLeftStick()`, `SetPadVibration(l, r)` | ゲームパッド（XInput） |

### 時間
| 関数 | 説明 |
|---|---|
| `deltaTime` | タイムスケール適用済み |
| `SetTimeScale(0)` | ポーズ（物理・アニメーションも停止）。`GetUnscaledDeltaTime()` は止まらない |
| `GetTime()` | シーン開始からの秒数 |
| `Cooldown fire{ 0.2f };` → `if (fire.TryUse()) Shoot();` | `Update` で `fire.Tick(dt)` を呼ぶ |
| `TimerQueue timers;` → `timers.After(0.5f, [this]{ ... });`, `Every(2.0f, ...)` | `Update` で `timers.Tick(dt)` |

### オブジェクト・プレハブ
| 関数 | 説明 |
|---|---|
| `FindGameObjectByName("Player")`, `FindWithTag("Player")`, `FindAllWithTag(tag, ids, max)` | 検索 |
| `InstantiatePrefab("Enemy", pos)` | `Assets/Prefabs/Enemy.prefab.json` を生成（フレーム終わりに作成） |
| `Despawn(id)` / `DespawnSelf()` / `DespawnAfter(秒)` | プレハブはプールに戻る（軽い） |
| `DestroySelf()`, `DestroyGameObject(id)` | 完全に削除 |
| `SetParent(parentId)`, `GetChildren(...)`, `GetWorldPosition(id)` | 親子関係 |

### 当たり判定・検索
| 関数 | 説明 |
|---|---|
| `OnCollisionEnter / OnCollision / OnCollisionExit` | イベント（レイヤー設定に従う） |
| `Raycast(origin, dir, maxDist, hit, layerMask)` | 最も近いコライダー |
| `OverlapSphere(center, r, ids, max, layerMask)` | 球に触れているオブジェクト |
| `FindNearestWithTag("Enemy", maxDist)` | 一番近いオブジェクト |

### キャラクター移動（壁をすり抜けない）
Box Collider を付けたオブジェクトで（Rigidbody は付けない）:
```cpp
fallSpeed = GetOwner()->isGrounded ? 0.0f : fallSpeed - 20.0f * dt;   // 重力は自分で
const ScriptCharacterMove r = MoveCharacter({ in.x * speed * dt, fallSpeed * dt, in.y * speed * dt });
if (r.hitWall) { /* 壁に当たった */ }
```
壁に沿って滑る・低い段差（既定 0.3）を登る・接地判定・高速でも壁を抜けない。トリガーと対象外レイヤーは無視。

### 経路探索と AI
障害物（壁・箱）に **Static** を付けると、床より 0.3 以上高いものが障害物になります。
```cpp
enum State { Idle, Chase };
StateMachine ai{ Idle };
Vector3 path[32];
int count = FindPath(self->translation, target, path, 32);   // 0 = 道がない
// path[1] に向かって MoveCharacter で進む。経路は 0.5 秒ごとくらいに取り直す
ai.Tick(dt);   // Update の最後に（JustEntered() が 1 回の Update の間 true）
```
`RebuildNavigation(セルサイズ, 壁からの距離)` は Static オブジェクトを動かした後に。

### シーン・セーブ
| 関数 | 説明 |
|---|---|
| `LoadScene("Stage2")` | `Assets/Scenes/Stage2.scene.json` へ（フレーム終わり）。エディターでは Stop で元のシーンに戻る |
| `KeepAcrossScenes()` | このオブジェクト（ルート、子も一緒）を LoadScene で消さない（DontDestroyOnLoad） |
| `SaveInt / LoadInt`, `SaveFloat / LoadFloat`, `SaveText / LoadText`, `HasSaveKey`, `DeleteAllSaveData` | `SaveData/save.json` に保存 |

チェックポイントやリスポーンはゲーム側のロジックです（位置を `SaveFloat` で保存して戻す、など）。

### UI（1280×720 のキャンバス、書き出したゲームでも表示）
| 関数 | 説明 |
|---|---|
| `UIText("Score 10", x, y, size, color, align)` | 日本語も表示できます |
| `UIRect`, `UIImage(path, x, y, w, h)`, `UIProgressBar(...)` | 図形・画像・ゲージ |
| `UIButton("Retry", x, y, w, h, selected)` | クリック、または selected で Enter / パッド A |

### 音・パーティクル・見た目の演出
| 関数 | 説明 |
|---|---|
| `PlayAudio(path, volume, loop)`, `PlayMusic(path, volume)`, `StopMusic()` | 効果音・BGM |
| `EmitParticles(count)`, `SetParticlesEmitting(bool)` | パーティクル |
| `SetColor`, `SetVisible`, `SetTexture`, `GetMaterial / SetMaterial` | 被弾フラッシュ・点滅など |
| `ShakeCamera(0.3f, 0.25f)` | カメラシェイク |
| `Hitstop(0.06f)` | ヒットストップ（ゲーム時間を一瞬止める） |
| `SetAnimTrigger("attack")`, `SetAnimFloat("speed", v)`, `IsAnimState("Attack")` | Animator の操作。`OnAnimationEvent(name)` でイベント受信 |

> HP・ダメージ・スコアなどのゲームルールは、エンジンではなくチームのスクリプトで作ります。

## 8. 見た目（トゥーン・アウトライン）
- Mesh Renderer > Reflection Model を **5: Toon** にすると、段階的な陰影とくっきりしたハイライトになります。
- **Outline Width**（例 0.03、ワールド単位。0.01 スケールのモデルでも太さは同じ）と **Outline Color** で輪郭線。
- スクリプトから: `ScriptMaterialData m; GetMaterial(m); m.lightingType = 5; m.outlineWidth = 0.03f; SetMaterial(m);`
- 輪郭線は丸いモデル（キャラクター・球）できれいに出ます。角の鋭い箱では途切れることがあります。
- シェーダー（`Editor/shaders/Object3d*.hlsl`）を保存すると、エディター実行中にすぐ反映されます（エラー時は前のシェーダーのまま、Console に表示）。

## 9. マップチップでレベルを作る
マス目（グリッド）にタイルを塗って、ボタン一つでレベルのオブジェクトを並べられます。

### 作り方
1. Hierarchy の **+ Create > 2D Object > Mapchip Grid**（または既存のオブジェクトに **Add Component > Mapchip Level Editor**）。
2. Inspector で大きさと向きを決めます。
   | 項目 | 説明 |
   |---|---|
   | **列 / 行 / レイヤー** | グリッドの大きさ。列と行は最大 256、レイヤーは最大 64。入力したら Enter か別の場所をクリックで反映 |
   | **タイルサイズ** | 生成されたオブジェクトの間隔（とプレハブなしのキューブの大きさ） |
   | **配置** | **横から見る (X / Y)**: 横スクロール向け、行は上に並ぶ。**上から見る (X / Z)**: 見下ろし型の 3D ゲーム向け、レベルは床に置かれる |
3. パレットでタイルを選び、グリッドを **左ドラッグで塗り、右ドラッグで消す**。1 回のドラッグが 1 回の取り消し（Ctrl+Z）。**ズーム** で表示を拡大、大きいグリッドはスクロールできます。
4. **Build Level Objects** で、タイル 1 マスごとに子オブジェクトが作られます。もう一度押すと作り直し（自分で追加した子オブジェクトは残ります）。
   - **自動生成** をオンにすると、塗り終えるたびに作り直します。
   - 黄色の「前回の生成からグリッドが変更されています。」が出たら、もう一度 Build してください。

### レイヤー（3 つ目の軸）
- **横から見る** ではレイヤー 1、2 … が奥（+Z）に、**上から見る** では上の階（+Y）に並びます。
- **編集するレイヤー** のスライダーで塗るレイヤーを選びます。1 つ前のレイヤーが薄く表示されるので、位置を合わせやすくなります。
- **次のレイヤーにコピー**: 今のレイヤーを次のレイヤーにコピー（最後のレイヤーなら追加）して、そのレイヤーに移ります。何階建ての壁などに便利です。
- **レイヤーを消去**: 今のレイヤーを空にします。
- レイヤーが 1 つだけのとき、スライダーはグレーです。「列 / 行 / レイヤー」の 3 つ目の数字を増やしてください。

### タイルセット（タイルの種類を自分で決める）
**Window > Tile Set Editor**（または Inspector の **Edit Tile Set...**）で開きます。
- 最初は組み込みタイル（1 Wall、2 Spike、3 Spawn、4 Goal）で、変更できません。**新しいタイルセットとして保存** を押すと `Assets/Levels/<名前>.tileset.json` ができ、編集できるようになります。複数のシーンで同じタイルセットを使えます。
- 各タイルの項目:
  | 項目 | 説明 |
  |---|---|
  | 番号 | CSV やシーンに保存される数字。名前を変えても番号は変わらず、削除した番号は二度と使われません |
  | 色 / 名前 | グリッドとパレットでの表示 |
  | プレハブ | このタイルに置くプレハブ（Project パネルからドラッグ）。プレハブは **元の大きさのまま** 置かれます |
  | プレハブなしの場合 | **ブロック**（当たる静的なキューブ）/ **トリガー**（通り抜けて `OnCollisionEnter` が届くキューブ）/ **マーカー**（空のオブジェクト、出現位置など） |
  | タグ | 生成したオブジェクトのタグ。空欄なら名前と同じ |
- **+ タイルを追加** で最大 63 種類まで。使用中のタイルを削除すると確認が出て、そのマスは空になります。

### 見た目をモデルにする
1. モデルを取り込み、オブジェクトに置いて大きさをタイル 1 マスに合わせる。
2. 当たり判定が必要なら Box Collider（壁なら **Static**、トゲなら **Is Trigger**）を付ける。
3. Hierarchy で右クリック > **Save as Prefab**。
4. Tile Set Editor でそのタイルの **プレハブ** 列にプレハブをドラッグ > **Build Level Objects**。

プレハブを後で直したら、もう一度 Build してください（生成されたオブジェクトはプレハブとリンクしません）。

### スクリプトから使う
生成されたオブジェクトにはタイルのタグが付いているので、タグで探せます。
```cpp
GameObject* goal = FindWithTag("Goal");          // 1 つ
uint32_t ids[256];
int coins = FindAllWithTag("Coin", ids, 256);    // 全部
GameObject* spike = FindNearestWithTag("Spike"); // 一番近いもの
```

### CSV
- **Save / Load Mapchip CSV**。パス欄が空なら `Assets/Levels/<シーン名>.csv`（プロジェクトの中なので書き出したゲームにも入ります）。
- レイヤーは空行で区切って順に並びます。前のバージョンの CSV は 1 レイヤーとして読めます。
- CSV を読み込むとシーンは未保存の扱いになり、Ctrl+Z で戻せます。

> 1 マスが 1 オブジェクトになります。埋まったマスが 2 万を超えると警告が出ます（シーンが重くなるため）。

## 10. 最近の主な新機能
詳しい変更点は同梱の `CHANGELOG.md` を見てください。
| 機能 | 場所 |
|---|---|
| エフェクト（パーティクル・トレイル・タイミング） | **+ Create > Visual Effects > Effect** のテンプレート、**Window > Effect Editor** |
| マテリアルグラフ / パーティクルグラフ（ノードで見た目や動きを作る） | **Window > Effect Graph**、Project パネルで `[MAT]` / `[PFX]` をダブルクリック |
| プレハブモード・バリアント・入れ子プレハブ | Hierarchy でプレハブを右クリック > **Open Prefab** |
| 自動保存 | 未保存の変更は定期的に `Library/AutoSave` にコピー。次に開くとき復元するか聞かれます |
| 複数選択 | Hierarchy で Ctrl / Shift + クリック。ギズモでまとめて動かせます |
| 閉じるときの「保存しない」 | このセッションで作ったファイル（シーン・プレハブ・グラフなど）の一覧が出て、チェックしたものをごみ箱へ移せます |
| マップチップ | 上の「9. マップチップでレベルを作る」 |

## 11. エンジンの更新
新しい `RizealldayEngine-x.y.z.zip` を受け取ったら:
1. **Help > Install Engine Update...** で zip を選ぶ（zip をエディターのウィンドウにドロップしても可）。
2. 確認画面で **Install and Restart**。未保存のシーンは先に保存されます。
3. エディターが閉じて更新がインストールされ、自動で再起動します。

- ゲームプロジェクトは変更されません。スクリプト API が変わった場合は、プロジェクトを開いたときにスクリプトが自動で再ビルドされます。
- エディターのバージョンは **Help > About Rizeallday Engine** で確認できます。

## 12. 困ったとき
| 症状 | 対処 |
|---|---|
| スクリプトのビルドが失敗する | Console のエラーを確認。Visual Studio（C++）が入っているか確認 |
| 「GameScripts.dll was built against a different engine version」 | Ctrl+Shift+B で再ビルド |
| 「Engine SDK not found」 | エディターでプロジェクトを一度開く（RizeEngine.props が作られる） |
| Visual Studio で補完が効かない | `<プロジェクト名>.sln` から開く。Project > Rescan Solution、だめなら `.vs` フォルダーを削除 |
| 「This project was saved with engine ...」 | 最新のエンジン zip をインストール |
| コライダーが見えない・小さすぎる | Box Collider の **Fit to Mesh**（モデルのスケールが 0.01 のときなど） |
| 敵が経路を見つけない | 壁・床に Collider と **Static** が付いているか確認 |
| マップチップのレイヤーが選べない | 「列 / 行 / レイヤー」の 3 つ目の数字を 2 以上にする（入力後 Enter）。または「次のレイヤーにコピー」 |
| Build したのにモデルが出ない | Tile Set Editor でプレハブ名を確認（Console に「Prefab ... was not found」）。プレハブを直したら Build し直す |
| 更新に失敗した | エンジンのフォルダーのファイルを開いているプログラムを閉じて再実行（失敗しても前のバージョンは残ります） |

不具合を見つけたら、Console のログと再現手順をエンジン担当に送ってください。修正版の zip で更新できます。
