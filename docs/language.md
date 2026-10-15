# rm2kscript 言語仕様

rm2kscript が使うスクリプト言語（`.rs2k` ファイル）の完全なリファレンスです。

---

## 目次

1. [ファイル構造](#1-ファイル構造)
2. [イベント・ページ宣言](#2-イベントページ宣言)
3. [ページメタデータ一覧](#3-ページメタデータ一覧)
4. [文の種類](#4-文の種類)
5. [コマンドリファレンス](#5-コマンドリファレンス)
6. [コモンイベント](#6-コモンイベント)
7. [スイッチ・変数の名前テーブル](#7-スイッチ変数の名前テーブル)
8. [エンコード時バリデーション](#8-エンコード時バリデーション)

---

## 1. ファイル構造

```
event(ID, x=X, y=Y) {
    page(ページ番号, メタデータ...) {
        文;
        文;
    }
    page(2, ...) {
        ...
    }
}

event(ID2, x=X2, y=Y2) {
    ...
}
```

- 1つのファイルに複数の `event` ブロックを並べられます
- `encode` コマンドは書かれたイベント・ページだけを上書きし、ファイル内に存在しないイベントはそのまま残ります

---

## 2. イベント・ページ宣言

### `event(id, x=X, y=Y)`

| 引数 | 型 | 説明 |
|---|---|---|
| `id` | 整数 | イベントID（1以上）|
| `x=` | 整数 | マップX座標（省略すると既存イベントの座標を維持。新規イベントでは必須）|
| `y=` | 整数 | マップY座標（同上）|
| `name=` | 文字列 | イベント名（省略すると既存の名前を維持）|

### `page(番号, ...)`

ページ番号は1から始まります。ページ番号の後にカンマ区切りでメタデータを指定します。

- 未知のメタデータキーや不正な値（例: `trigger=actoin`）はエラーになります。
- `sprite=` / `translucent=` / `overlap_forbidden=` / `cond_*` は、テキストから省略すると**解除**されます（既存ページを上書きしても古い設定は残りません）。

---

## 3. ページメタデータ一覧

### 基本

| キー | 値 | デフォルト |
|---|---|---|
| `trigger=` | `action` / `touched` / `collision` / `auto_start` / `parallel` | `action` |
| `layer=` | `below` / `same` / `above` | `below` |

### グラフィック

| キー | 値 | 説明 |
|---|---|---|
| `sprite=` | `"ファイル名"` | キャラクターグラフィック（`""` で明示的に消去。省略時もグラフィックなし）|
| `sprite_index=` | 0〜7 | グラフィック内のインデックス |
| `dir=` | `up` / `right` / `down` / `left` | 初期向き |
| `translucent=1` | — | 半透明表示 |

### 移動

| キー | 値 | 説明 |
|---|---|---|
| `move_type=` | `stationary` / `random` / `vertical` / `horizontal` / `toward` / `away` / `custom` | 移動タイプ |
| `move_speed=` | `eighth` / `quarter` / `half` / `normal` / `double` / `fourfold` | 移動速度 |
| `move_freq=` | 1〜8 | 移動頻度 |
| `anim_type=` | `non_continuous` / `continuous` / `fixed_non_continuous` / `fixed_continuous` / `fixed_graphic` / `spin` / `step_frame_fix` | アニメーションタイプ |
| `overlap_forbidden=1` | — | 重なり禁止 |

### 発生条件

| キー | 値 | 説明 |
|---|---|---|
| `cond_switch_a=N` | スイッチID | スイッチAがONのとき |
| `cond_switch_b=N` | スイッチID | スイッチBがONのとき |
| `cond_var=N, cond_var_op=ge, cond_var_val=M` | — | 変数N が M 以上のとき（op: `eq`/`ge`/`le`/`gt`/`lt`/`ne`）|
| `cond_item=N` | アイテムID | アイテムNを所持しているとき |
| `cond_actor=N` | アクターID | アクターNがパーティにいるとき |
| `cond_timer=N` | 秒数 | タイマーがN秒以下のとき |
| `cond_timer2=N` | 秒数 | タイマー2がN秒以下のとき |
| `has_end=1` | — | ページ末尾に END コマンドを付ける（デコーダが元データを忠実に復元するために出力。通常は不要）|

---

## 4. 文の種類

### 4.1 コマンド文

```
コマンド名(引数...);
```

### 4.2 スイッチ代入

```
switch[N] = ON;
switch[N] = OFF;
switch[N] = toggle;
switch[N..M] = ON;          // N〜M の範囲を一括
switch[var[N]] = ON;        // 変数Nの値をIDとして使用
switch["スイッチ名"] = ON;  // 名前テーブルがある場合
```

### 4.3 変数代入

```
var[N] = 値;
var[N] += 値;
var[N] -= 値;
var[N] *= 値;
var[N] /= 値;
var[N] %= 値;
var[N] = var[M];            // 変数Mの値を代入
var[N..M] = 値;             // N〜M の範囲を一括
var["変数名"] = 値;         // 名前テーブルがある場合
```

### 4.4 if 文

```
if (条件) {
    文;
} else {
    文;
}
```

**条件の書き方：**

```
switch[N] == ON
switch[N] == OFF
var[N] == 値
var[N] >= 値
var[N] <= 値
var[N] > 値
var[N] < 値
var[N] != 値
var[N] == var[M]            // 変数同士の比較
timer >= N                  // タイマーがN以上 (<= も可)
timer2 >= N                 // タイマー2 (RPGツクール2003)
has_item(id=N, mode=0)      // mode: 0=所持している / 1=所持していない
hero_cond(actor=N, sub=S, val=V [, str="名前"])
                            // sub: 0=パーティにいる 1=名前 2=レベル 3=HP 4=スキル 5=装備 6=状態
on_vehicle(id=ship)         // boat / ship / airship
char_facing(who=this, dir=down) // who=player/boat/ship/airship/this またはイベントID。dir: up/right/down/left
gold_cmp(amount=N, op=ge)   // op: ge (>=) / le (<=)
key_decision()              // 決定キーが押されたとき
bgm_looped()                // BGMが一巡したとき
cond_raw(p0=T, p1=.., [str=".."]) // デコーダが高水準表現できなかった条件を無損失で保持する形式
```

- 上記以外の条件はコンパイルエラーになります（以前のように `switch[1]==ON` へ黙って置き換わることはありません）。
- 各条件の末尾に `cond_pN=値` を付けて、既知パラメータより後ろの値を保持できます（デコーダが必要なときだけ出力します）。

### 4.5 while 文

```
while (条件) {
    文;
}
```

`if` 文と同じ条件式が使えます。エンコード時に以下の LCF コマンド列に展開されます（デコード結果には `while` は出現しません）。

```
Loop() {
    if (条件) {} else { BreakLoop(); }
    // body
}
```

**例：**
```
while (var[1] < 10) {
    var[1] += 1;
}

while (switch["ループ中"] == ON) {
    CallEvent(common=5);
}
```

### 4.6 for 文

```
for (var[N] = 初期値; 継続条件; var[N] op= 増分) {
    文;
}
```

エンコード時に以下の LCF コマンド列に展開されます。

```
var[N] = 初期値;
Loop() {
    if (継続条件) {} else { BreakLoop(); }
    // body
    var[N] op= 増分;
}
```

**例：**
```
// コモンイベント1 を 5 回呼ぶ
for (var[2] = 1; var[2] <= 5; var[2] += 1) {
    CallEvent(common=1);
}

// 名前テーブルを使った例
for (var["カウンター"] = 0; var["カウンター"] < 3; var["カウンター"] += 1) {
    message("ループ中");
}
```

`for` の初期値・増分には変数参照も使えます（`var[N] = var[M]`、`var[N] += var[M]`）。

### 4.7 choice 文

```
choice("選択肢A", "選択肢B", cancel=5) {
    when("選択肢A", index=0) {
        文;
    }
    when("選択肢B", index=1) {
        文;
    }
}
```

- `cancel=N` はキャンセル時の動作を表す LCF の値をそのまま指定します(省略時は 0)
- `when` の `index=` は省略可能。省略すると旧形式（末尾 END なし）で出力されます。エディタが生成した形式を忠実に復元するため、デコーダは `index=` を出力します。
- 選択肢の文字列に `/` を含めることはできません（LCF 内部で `/` 区切りの1文字列として保存されるため）。

### 4.8 コメント

ツクールのイベントコマンド「注釈」は `comment` 関数で書きます。`//` はコマンドにならず、ゲームデータにも出ません。

| 書き方 | 意味 |
|---|---|
| `comment("テキスト");` | 注釈コマンド(Comment)を1つ生成 |
| `comment_cont("テキスト");` | 直前の注釈の続きの行(Comment_2)を1つ生成 |
| `comment("1行目", "2行目");` | 複数行の注釈を1文で書く形式(Comment + Comment_2)|
| `// メモ` | 行末までの**無視されるメモ**(ゲームデータには出力されない)|

```
comment("ここから初期化");
comment_cont("2行目");       // 前の comment の続き
comment("3行", "まとめて");   // 1文で複数行

// 開発用メモ (エンコードしても何も出力されない)
// message("一時的に無効化したコード");
```

- デコーダは、注釈コマンド1つにつき `comment("…")` / `comment_cont("…")` を1文ずつ出力します。文字列中の改行(`\n`)や末尾の空白も、そのまま往復します。
- 1つの `comment("…")` 内の `\n` は、改行を含む**1つの**注釈コマンドになります(複数のコマンドには分割されません)。行を分けたいときは `comment_cont` か、複数引数形式を使ってください。
- `//` は行頭でも行末でも無視されます。コマンドを一時的に無効化する用途に使えます。

### 4.9 RawCommand（無損失保持）

```
RawCommand(code=12345, str="文字列", p0=1, p1=2);
```

高水準構文を持たないコマンドを、コード・文字列・パラメータそのままで保持する形式です。デコーダが未登録コマンドに対して出力し、エンコーダは元のコマンドに戻します。

---

## 5. コマンドリファレンス

### メッセージ

| コマンド | 説明 |
|---|---|
| `message("テキスト")` | メッセージを表示（`\n` で改行）|
| `MessageOptions(skip=0, position=2, transparent=0, continue_events=0)` | メッセージオプション |
| `ChangeFaceGraphic(file="face", index=0, right=0, flip=0)` | フェイスグラフィック設定 |
| `choice("A","B", cancel=5) { when("A"){} }` | 選択肢表示 |
| `InputNumber(digits=4, var=1)` | 数字入力 |
| `EnterHeroName(actor=1, max=6)` | 名前入力 |

### スイッチ・変数

| コマンド | 説明 |
|---|---|
| `switch[N] = ON/OFF/toggle` | スイッチ操作 |
| `var[N] op= 値` | 変数操作（単純形式）|
| `ControlVars(var=N, type=T, id2=M, op_code=op, src=src, src_val=V)` | 変数操作（間接参照など特殊形式）|
| `TimerOperation(set=N, p1=0, p2=0, p3=0, p4=0)` | タイマーセット |
| `TimerOperation(start, p1=0, p2=0, p3=0, p4=0)` | タイマー開始 |
| `TimerOperation(stop, p1=0, p2=0, p3=0, p4=0)` | タイマー停止 |

### パーティ・アイテム

| コマンド | 説明 |
|---|---|
| `ChangeGold(delta=N)` | 所持金変更（負数で減少、`delta=var[N]` で変数参照）|
| `ChangeItems(op=add, item=N, amount=M)` | アイテム増加（`item=var[N]`、`amount=var[N]` 形式も可）|
| `ChangeItems(op=remove, item=N, amount=M)` | アイテム減少 |
| `ChangePartyMembers(add, actor=N)` | パーティメンバー追加（`actor=var[N]` も可）|
| `ChangePartyMembers(remove, actor=N)` | パーティメンバー除外 |

### アクターステータス

| コマンド | 説明 |
|---|---|
| `ChangeExp(actor=N, delta=V)` | EXP変更（`delta=-V` で減少、`delta=var[N]` で変数参照）|
| `ChangeExp(party, delta=V)` | パーティ全員のEXP変更 |
| `ChangeLevel(actor=N, delta=V)` | レベル変更（`delta=-V` で減少、`delta=var[N]` で変数参照）|
| `ChangeLevel(actor=N, delta=V, show_levelup=1)` | レベルアップメッセージ表示あり |
| `ChangeHP(actor=N, delta=N)` | HP変更 |
| `ChangeSP(actor=N, delta=N)` | SP変更 |
| `FullHeal(actor=N)` | 全回復（`party` でパーティ全員）|
| `ChangeSkills(actor=N, op=learn, skill=N)` | スキル習得（`op=forget` で忘却、`skill=var[N]` も可）|
| `ChangeCondition(actor=N, op=add, state=N)` | 状態変化付与（`op=remove` で解除）|
| `ChangeEquipment(actor=N, op=0, src=0, item=N)` | 装備変更 |
| `ChangeParameters(who=0, actor=N, param=0, op=0, src=0, val=V)` | 基礎パラメータ変更 |
| `ChangeHeroName(actor=N, name="名前")` | アクター名変更 |
| `ChangeHeroTitle(actor=N, title="肩書き")` | 肩書き変更 |
| `ChangeSpriteAssociation(actor=N, sprite="ファイル名", sprite_index=0, transparent=0)` | 歩行グラ変更 |
| `ChangeActorFace(actor=N, face="ファイル名", index=0)` | 顔グラ変更 |

### 移動・テレポート

| コマンド | 説明 |
|---|---|
| `Teleport(map=N, x=X, y=Y)` | テレポート |
| `Teleport(map=N, x=X, y=Y, dir=0, fade=0)` | 向き・フェード指定付き |
| `MemorizeLocation(var_map=N, var_x=M, var_y=L)` | 現在地を変数に記録 |
| `RecallToLocation(var_map=N, var_x=M, var_y=L)` | 記録した場所へ戻る |
| `ChangeEventLocation(event=N, mode=0, x=X, y=Y)` | イベント位置変更 |
| `TradeEventLocations(event1=N, event2=M)` | イベント位置交換 |
| `SetVehicleLocation(vehicle=0, mode=0, map=N, x=X, y=Y)` | 乗り物位置設定 |
| `EnterExitVehicle()` | 乗り物乗降 |
| `StoreTerrainID(src=0, x=X, y=Y, var=N)` | 地形IDを変数に格納 |
| `StoreEventID(src=0, x=X, y=Y, var=N)` | イベントIDを変数に格納 |
| `MoveEvent(player/boat/ship/airship/this, ...)` / `MoveEvent(event=N, ...)` | イベント移動（対象は 主人公/ボート/船/飛行船/このイベント/イベントID。省略時は `this`）|
| `ProceedWithMovement()` | 移動完了まで待機 |
| `HaltAllMovement()` | 全移動停止 |

### 画面効果

| コマンド | 説明 |
|---|---|
| `EraseScreen(type=0)` | 画面消去 |
| `ShowScreen(type=0)` | 画面表示 |
| `TintScreen(r=100, g=100, b=100, s=100, frames=20, wait=1)` | 色調変更 |
| `FlashScreen(r=255, g=255, b=255, power=31, frames=20, wait=1)` | フラッシュ |
| `ShakeScreen(power=5, speed=5, frames=20, wait=1)` | シェイク |
| `PanScreen(op=move, dir=0, dist=0, speed=0, wait=0)` | 画面スクロール（op: `lock`/`unlock`/`move`/`reset`）|
| `WeatherEffects(type=0, power=2)` | 天候効果 |
| `ShowPicture(file="名前", id=N, pos_mode=0, x=X, y=Y, fixed_to_map=0, magnify=100, top_trans=0, use_transparent_color=1, red=100, green=100, blue=100, saturation=100, effect_mode=0, effect_power=0)` | ピクチャ表示（詳細は下表）|
| `MovePicture(id=N, pos_mode=0, x=X, y=Y, magnify=100, top_trans=0, red=100, green=100, blue=100, saturation=100, effect_mode=0, effect_power=0, duration=8, wait=0)` | ピクチャ移動（詳細は下表）|
| `ErasePicture(id=N)` | ピクチャ消去 |
| `ShowBattleAnimation(anim=N, event=this, wait=1)` | バトルアニメ表示（`event=` は player/boat/ship/airship/this またはイベントID）|
| `FlashSprite(event=this, r=31, g=31, b=31, chroma=31, duration=5, wait=1)` | スプライトフラッシュ |
| `PlayerVisibility(visible=1)` | プレイヤー表示切替 |

### BGM・SE

| コマンド | 説明 |
|---|---|
| `PlayBGM("filename", volume=100, tempo=100, balance=50)` | BGM再生（`fade=N` でフェードイン時間(ms)を指定。省略時は即時切り替え）|
| `FadeOutBGM(ms=800)` | BGMフェードアウト |
| `MemorizeBGM()` | BGM記憶 |
| `PlayMemorizedBGM()` | 記憶BGM再生 |
| `PlaySound("filename", volume=60, tempo=100, balance=50)` | SE再生 |
| `PlayMovie(file="filename", x=0, y=0, w=320, h=240)` | 動画再生 |
| `ChangeSystemBGM(event=N, bgm="filename", volume=100, tempo=100, balance=50)` | システムBGM変更 |
| `ChangeSystemSFX(type=N, sfx="filename", volume=100, tempo=100, balance=50)` | システムSE変更 |

### 待機・入力

| コマンド | 説明 |
|---|---|
| `Wait(frames=N)` | N フレーム待機 |
| `KeyInputProc(var=N, wait=1, dir=1, act=1, cancel=1)` | キー入力待機 |

### フロー制御

| コマンド | 説明 |
|---|---|
| `if (条件) { } else { }` | 条件分岐 |
| `while (条件) { }` | 条件が真の間繰り返す（コンパイル時展開）|
| `for (var[N]=init; 条件; var[N] op= step) { }` | カウンタ付きループ（コンパイル時展開）|
| `Loop() { ... }` | 無限ループ（`BreakLoop()` で脱出）|
| `BreakLoop()` | ループ脱出 |
| `Label(N)` | ラベル |
| `JumpToLabel(N)` | ラベルへジャンプ |
| `CallEvent(common=N)` | コモンイベント呼び出し |
| `CallEvent(event=N, page=P)` | マップイベントの指定ページを呼び出し |
| `CallCommonEvent(id=N)` | コモンイベント呼び出し（別形式）|
| `EndEventProcessing()` | イベント処理終了 |
| `EraseEvent()` | イベント消去 |

### バトル

| コマンド | 説明 |
|---|---|
| `EnemyEncounter(troop=N, type=0, can_escape=1, can_lose=0)` | 戦闘開始 |

### ショップ・宿屋

| コマンド | 説明 |
|---|---|
| `OpenShop(type=0, item1=N, item2=M, ...)` | ショップ開店 |
| `ShowInn(type=0, cost=100)` | 宿屋 |

### マップ設定

| コマンド | 説明 |
|---|---|
| `ChangeMapTileset(id=N)` | チップセット変更 |
| `ChangePBG(name="ファイル名", p0=0, ...)` | パノラマ変更 |
| `ChangeEncounterSteps(steps=30)` | エンカウント歩数変更 |
| `TileSubstitution(chip_set=1, src=N, dst=M)` | タイル置換 |
| `TeleportTargets(action=0, map=N, x=X, y=Y, switch_on=0, switch_id=1)` | テレポート先追加/削除 |
| `EscapeTarget(map=N, x=X, y=Y, switch_on=0, switch_id=1)` | 逃げる先設定 |

### システム設定

| コマンド | 説明 |
|---|---|
| `ChangeSystemGraphics(system="system", stretch=0, font=0)` | システムグラフィック変更 |
| `ChangeScreenTransitions(type=0, id=0)` | 画面切り替え効果変更 |
| `ChangeSaveAccess(enable=1)` | セーブ許可/禁止 |
| `ChangeTeleportAccess(enable=1)` | テレポート許可/禁止 |
| `ChangeEscapeAccess(enable=1)` | 逃げる許可/禁止 |
| `ChangeMainMenuAccess(enable=1)` | メインメニュー許可/禁止 |
| `OpenSaveMenu()` | セーブ画面を開く |
| `OpenLoadMenu()` | ロード画面を開く |
| `OpenMainMenu()` | メニューを開く |
| `GameOver()` | ゲームオーバー |
| `ReturntoTitleScreen()` | タイトルへ戻る |
| `ExitGame()` | ゲーム終了 |

---

### ShowPicture / MovePicture パラメータ詳細

**ShowPicture** — `ShowPicture(file="ファイル名", ...)`

| 引数 | デフォルト | 説明 |
|---|---|---|
| `id` | — | ピクチャ番号 |
| `pos_mode` | `0` | 座標指定方法（`0`=定数 / `1`=変数）|
| `x` | `0` | X座標（`pos_mode=1` のとき変数ID）|
| `y` | `0` | Y座標（`pos_mode=1` のとき変数ID）|
| `fixed_to_map` | `0` | マップに固定（`1`=固定）|
| `magnify` | `100` | 拡大率（%）|
| `top_trans` | `0` | 上側透明度（0〜100）|
| `use_transparent_color` | `0` | 透過色を使用（`1`=使用）|
| `red` | `100` | 赤（0〜200）|
| `green` | `100` | 緑（0〜200）|
| `blue` | `100` | 青（0〜200）|
| `saturation` | `100` | 彩度（0〜200）|
| `effect_mode` | `0` | エフェクト（`0`=なし / `1`=回転 / `2`=波）|
| `effect_power` | `0` | エフェクト強度 |
| `bottom_trans` | — | 下側透明度（RPGツクール2003拡張）|

**MovePicture** — `MovePicture(id=N, ...)` — ShowPicture と共通の引数に加え:

| 引数 | デフォルト | 説明 |
|---|---|---|
| `duration` | `8` | 移動フレーム数 |
| `wait` | `0` | 完了まで待つ（`1`=待つ）|

---

## 6. コモンイベント

`decode-db` / `encode-db` で扱うコモンイベントのフォーマットは以下の通りです。

```
event(コモンイベントID) {
    page(1, trigger=none) {
        文;
    }
}

event(2) {
    page(1, trigger=auto_start, cond_switch=5) {
        message("スイッチ5がONのとき自動実行");
    }
}
```

コモンイベントの `trigger` は `none` / `auto_start` / `parallel` の3種類です。  
`cond_switch=N` でトリガースイッチを指定します（省略すると解除）。`name=` でコモンイベント名を設定できます。
コモンイベントの `trigger` / `cond_switch` / `name` は `encode-db` で反映されます。

---

## 7. スイッチ・変数の名前テーブル

ゲームディレクトリ内に `RPG_RT.ldb` がある場合、`decode` / `decode-project` は自動的にそれを参照してスイッチ・変数番号を名前で出力します。

```bash
# -n を省略しても RPG_RT.ldb が同じディレクトリにあれば自動適用
rm2kscript decode Map0001.lmu

# 別のディレクトリから LDB を指定する場合
rm2kscript decode Map0001.lmu -n /path/to/RPG_RT.ldb
```

**出力例（名前テーブルあり）：**

```
var["Episode"] = 1;
var["PartyMemberID01"] = var["Episode"];
if (switch["イベント済み"] == ON) {
    CallEvent(common=5);
}
```

**名前の一意性：** 同じ名前が複数の番号に割り当てられている場合は、番号形式（`var[N]`）にフォールバックします。エンコード時は名前から番号を逆引きするため、エンコードに使うスクリプトをデコードしたゲームと同じ `RPG_RT.ldb` で処理してください。

---

## 8. エンコード時バリデーション

`encode` / `encode-db` / `encode-project` コマンドは、`RPG_RT.ldb` が参照できる場合（`-n` 指定または自動検出）、以下の項目をコンパイル時にチェックします。

| チェック対象 | エラー例 |
|---|---|
| `switch[N]` の N がデータベースのスイッチ数を超えている | `switch[99999] is out of range (1..5040)` |
| `switch[N..M]` の M がスイッチ数を超えている | 同上 |
| `var[N]` の N がデータベースの変数数を超えている | `var[99999] is out of range (1..5000)` |
| `if (switch[N] == ON)` 等の条件式内でも同様 | 同上 |

エラーが検出されるとエンコードは中断し、行番号・列番号付きでエラーが表示されます。

```
Compile error at line 3 col 9: switch[99999] is out of range (1..5040)
```

`RPG_RT.ldb` が参照できない場合（スタンドアローン使用など）は範囲チェックをスキップします。ただし名前指定（`switch["名前"]` / `var["名前"]`）は名前テーブルが必須で、見つからない名前や、名前テーブルなしでの名前指定はエラーになります。

---

## 9. コマンドラインの補足

| オプション | 説明 |
|---|---|
| `-n <ldb>` | 名前テーブル用の RPG_RT.ldb（省略時は入力の隣 / ゲームディレクトリのものを自動使用）|
| `-e <enc>` | 文字コード（既定 932）|
| `--engine 2000\|2003` | マップ保存時のエンジンバージョン（既定 2000）|
| `--prune` | `encode` / `encode-project`: スクリプトに現れないイベント・ページをマップから削除 |

- `encode-project` は全ファイルのエンコードが成功した場合のみゲームに書き込みます。失敗が1つでもあれば何も変更しません。
- 上書きされる `.lmu` / `.ldb` は、事前に `<元ファイル名>.bak` へ退避されます（既に `.bak` があれば上書きしません）。
- UTF-8 BOM 付きのスクリプトも読み込めます。
- 未知のコマンド名・条件・ページオプション・when のオプションはエラーになります。
- **キャラクター指定**（`MoveEvent` / `ShowBattleAnimation` / `FlashSprite` / `ChangeEventLocation` / `TradeEventLocations` / `char_facing`）は、`player`=10001、`boat`=10002、`ship`=10003、`airship`=10004、`this`=10005、それ以外はイベントIDです（RPG_RT / EasyRPG Editor の定数）。未知の識別子はエラーになります。
- ⚠ 以前のバージョンは `this` を 10001（実際は主人公）、`player` を -1 として扱っていました。**旧バージョンでデコードした `.rs2k` を再エンコードすると対象が変わります。** 必ず現在のバージョンで `decode` し直してください（旧 `player`=-1 は `event=-1` として出力されます）。
- ただし**コマンド引数のキー名のタイプミスは検出されません**（例: `Wait(framez=30)` はエラーにならず既定値が使われます）。引数名は「5. コマンドリファレンス」と見比べて確認してください。
