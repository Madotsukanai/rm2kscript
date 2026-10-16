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
9. [コマンドラインの補足](#9-コマンドラインの補足)

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
- エンコード時は書かれたイベント・ページだけを上書きし、ファイル内に存在しないイベントはそのまま残ります

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
| `cond_switch_a=N` | スイッチID | スイッチAが true のとき |
| `cond_switch_b=N` | スイッチID | スイッチBが true のとき |
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
switch[N] = true;
switch[N] = false;
switch[N] = toggle;
switch[N..M] = true;           // N〜M の範囲を一括
switch[var[N]] = true;         // 変数Nの値をIDとして使用
switch["スイッチ名"] = true;   // 名前テーブルがある場合
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
var[*var[N]] = 値;          // 間接参照: 変数Nの値をIDとして代入先を決定
                             // ※ デコード時は名前テーブルがあっても数値IDで出力される（エンコード時の逆引きが困難なため）
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
switch[N] == true
switch[N] == false
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

- 上記以外の条件はコンパイルエラーになります。
- 各条件の末尾に `cond_pN=値` を付けて、既知パラメータより後ろの値を保持できます（デコーダが必要なときだけ出力します）。
- 元データが既知のパラメータ数より少ない場合、デコーダは `cond_p_count=N` を出力します。エンコーダはこれを使って同じバイナリを再生成します。

### 4.5 while 文

```
while (条件) {
    文;
}
```

`if` 文と同じ条件式が使えます。デコード時にはこのパターンが認識されて `while` として出力されます。エンコード時は以下の LCF コマンド列に展開されます。

```
loop {
    if (条件) {} else { BreakLoop(); }
    // body
}
```

**例：**
```
while (var[1] < 10) {
    var[1] += 1;
}

while (switch["ループ中"] == true) {
    CallEvent(common=5);
}
```

### 4.6 for 文

```
for (var[N] = 初期値; 継続条件; var[N] op= 増分) {
    文;
}
```

デコード時にはこのパターンが認識されて `for` として出力されます。エンコード時は以下の LCF コマンド列に展開されます。

```
var[N] = 初期値;
loop {
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
- `when` の `index=` は省略可能です。デコーダは `index=` を出力します。
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
| `MessageOptions(skip=false, position=2, transparent=false, continue_events=false)` | メッセージオプション |
| `ChangeFaceGraphic(file="face", index=0, right=false, flip=false)` | フェイスグラフィック設定 |
| `choice("A","B", cancel=5) { when("A"){} }` | 選択肢表示 |
| `InputNumber(digits=4, var=1)` | 数字入力 |
| `EnterHeroName(actor=1, charset=0, use_default_name=false)` | 名前入力 |

### スイッチ・変数

| コマンド | 説明 |
|---|---|
| `switch[N] = true/false/toggle` | スイッチ操作 |
| `var[N] op= 値` | 変数操作（単純形式）|
| `var[*var[N]] op= 値` | 変数操作（間接参照：変数Nの値をIDとして代入先を決定）|
| `ControlVars(var=N, type=T, id2=M, op_code=op, src=src, src_val=V)` | 変数操作（間接参照など特殊形式）|
| `TimerOperation(set=N, visible=true, battle=false)` | タイマーにN秒をセット |
| `TimerOperation(start, set=N, visible=true, battle=false)` | タイマー開始（セット値も同時指定）|
| `TimerOperation(stop, set=N, visible=true, battle=false)` | タイマー停止 |

### パーティ・アイテム

| コマンド | 説明 |
|---|---|
| `ChangeGold(delta=N)` | 所持金変更（負数で減少、`delta=var[N]` または `delta=-var[N]` で変数参照）|
| `ChangeItems(op=add, item=N, amount=M)` | アイテム増加（`item=var[N]`、`amount=var[N]` 形式も可）|
| `ChangeItems(op=remove, item=N, amount=M)` | アイテム減少 |
| `ChangePartyMembers(add, actor=N)` | パーティメンバー追加（`actor=var[N]` も可）|
| `ChangePartyMembers(remove, actor=N)` | パーティメンバー除外 |

### アクターステータス

| コマンド | 説明 |
|---|---|
| `ChangeExp(actor=N, delta=V)` | EXP変更（負数で減少、`delta=var[N]` または `delta=-var[N]` で変数参照）|
| `ChangeExp(party, delta=V)` | パーティ全員のEXP変更 |
| `ChangeLevel(actor=N, delta=V)` | レベル変更（負数で減少、`delta=var[N]` で変数参照）|
| `ChangeLevel(actor=N, delta=V, show_levelup=true)` | レベルアップメッセージ表示あり |
| `ChangeHP(actor=N, delta=V, die_allowed=false)` | HP変更（`delta=-var[N]` で変数参照の減算）|
| `ChangeSP(actor=N, delta=V)` | SP変更（`delta=-var[N]` で変数参照の減算）|
| `FullHeal(actor=N)` | 全回復（`party` でパーティ全員）|
| `ChangeSkills(actor=N, op=learn, skill=N)` | スキル習得（`op=forget` で忘却、`skill=var[N]` も可）|
| `ChangeCondition(actor=N, op=add, state=N)` | 状態変化付与（`op=remove` で解除）|
| `ChangeEquipment(actor=N, op=0, src=0, item=N)` | 装備変更 |
| `ChangeParameters(who=0, actor=N, param=0, op=0, src=0, val=V)` | 基礎パラメータ変更 |
| `SimulatedAttack(actor=N, atk=0, def=0, spi=0, var=0 [, result_var=V])` | 疑似攻撃（`party` でパーティ全員、`result_var=` で結果格納）|
| `ChangeClass(actor=N, class=1, level1=false, skill_mode=0, param_mode=0, show_msg=false)` | クラス変更（RPGツクール2003）|
| `ChangeBattleCommands(actor=N, op=add, cmd=1)` | バトルコマンド変更（RPGツクール2003、`op=remove` で削除）|
| `ChangeHeroName(actor=N, name="名前")` | アクター名変更 |
| `ChangeHeroTitle(actor=N, title="肩書き")` | 肩書き変更 |
| `ChangeSpriteAssociation(actor=N, sprite="ファイル名", sprite_index=0, transparent=false)` | 歩行グラ変更 |
| `ChangeActorFace(face="ファイル名", actor=N, index=0)` | 顔グラ変更 |

### グラフィック変更

| コマンド | 説明 |
|---|---|
| `ChangeVehicleGraphic(vehicle=boat, sprite="ファイル名", index=0)` | 乗り物グラ変更（`vehicle=boat/ship/airship`）|

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
| `MoveEvent(player/boat/ship/airship/this, ...)` / `MoveEvent(event=N, ...)` | イベント移動（省略時は `this`）|
| `ProceedWithMovement()` | 移動完了まで待機 |
| `HaltAllMovement()` | 全移動停止 |

### 画面効果

| コマンド | 説明 |
|---|---|
| `EraseScreen(type=0)` | 画面消去（`speed=N` で速度指定可）|
| `ShowScreen(type=0)` | 画面表示（`speed=N` で速度指定可）|
| `TintScreen(r=100, g=100, b=100, s=100, frames=20, wait=true)` | 色調変更 |
| `FlashScreen(r=255, g=255, b=255, power=31, frames=20, wait=true)` | フラッシュ |
| `ShakeScreen(power=5, speed=5, frames=20, wait=true)` | シェイク |
| `PanScreen(op=move, dir=0, dist=0, speed=0, wait=false)` | 画面スクロール（op: `lock`/`unlock`/`move`/`reset`）|
| `WeatherEffects(type=0, power=2)` | 天候効果 |
| `ShowPicture(file="名前", id=N, ...)` | ピクチャ表示（詳細は下表）|
| `MovePicture(id=N, ...)` | ピクチャ移動（詳細は下表）|
| `ErasePicture(id=N)` | ピクチャ消去 |
| `ShowBattleAnimation(anim=N, event=this, wait=true [, global=false])` | バトルアニメ表示（`event=` は player/boat/ship/airship/this またはイベントID）|
| `FlashSprite(event=this, r=31, g=31, b=31, chroma=31, duration=5, wait=true)` | スプライトフラッシュ |
| `PlayerVisibility(visible=true)` | プレイヤー表示切替 |

### BGM・SE

| コマンド | 説明 |
|---|---|
| `PlayBGM("filename", fade=0, volume=100, tempo=100, balance=50)` | BGM再生（`fade=N` でフェードイン時間(ms)を指定。省略時は即時切り替え）|
| `FadeOutBGM(N)` | BGMフェードアウト（N はフェードアウト時間 ms）|
| `MemorizeBGM()` | BGM記憶 |
| `PlayMemorizedBGM()` | 記憶BGM再生 |
| `PlaySound("filename", volume=60, tempo=100, balance=50)` | SE再生 |
| `PlayMovie(file="filename", pos_mode=0, x=0, y=0, w=320, h=240)` | 動画再生 |
| `ChangeSystemBGM(event=N, bgm="filename", fadein=0, volume=100, tempo=100, balance=50)` | システムBGM変更 |
| `ChangeSystemSFX(type=N, sfx="filename", volume=100, tempo=100, balance=50)` | システムSE変更 |

### 待機・入力

| コマンド | 説明 |
|---|---|
| `Wait(N)` | N フレーム待機 |
| `KeyInputProc(var=N, wait=1, dir=1, act=1, cancel=1)` | キー入力待機 |

`KeyInputProc` の全引数：`var`, `wait`, `dir`(方向キーまとめ), `act`(決定), `cancel`(キャンセル), `shift`, `key_down`, `key_left`, `key_right`, `key_up`

### フロー制御

| コマンド | 説明 |
|---|---|
| `if (条件) { } else { }` | 条件分岐 |
| `while (条件) { }` | 条件が真の間繰り返す |
| `for (var[N]=init; 条件; var[N] op= step) { }` | カウンタ付きループ |
| `loop { ... }` | 無限ループ（`BreakLoop()` で脱出）|
| `BreakLoop()` | ループ脱出 |
| `Label(N)` | ラベル |
| `JumpToLabel(N)` | ラベルへジャンプ |
| `CallEvent(common=N)` | コモンイベント呼び出し |
| `CallEvent(event=N, page=P)` | マップイベントの指定ページを呼び出し |
| `CallCommonEvent(N)` | コモンイベント呼び出し（バトルイベント専用）|
| `EndEventProcessing()` | イベント処理終了 |
| `EraseEvent()` | イベント消去 |

### バトル

| コマンド | 説明 |
|---|---|
| `EnemyEncounter(troop=N, bg_type=0, escape_mode=1, defeat_mode=0, first_strike=false [, bg="背景ファイル名"])` | 戦闘開始 |
| `VictoryHandler()` | 勝利後処理（`EnemyEncounter` 直後に記述）|
| `EscapeHandler()` | 逃走後処理 |
| `DefeatHandler()` | 敗北後処理 |
| `EndBattle()` | 戦闘ハンドラ終了 |

`EnemyEncounter` の RPGツクール2003 追加引数: `condition=0, formation=0, terrain_id=0`

### バトルイベント専用コマンド（RPGツクール2003）

| コマンド | 説明 |
|---|---|
| `ForceFlee(mode=0, id=0, check=false)` | 逃走強制（`mode=0`=全員逃走 / `1`=敵モンスター / `2`=アクター）|
| `EnableCombo(actor=1, cmd=1, multiple=1)` | コンボ有効化 |
| `ChangeMonsterHP(enemy=0, delta=V, lethal=false)` | 敵HPを変更（`delta=-var[N]` で変数参照の減算）|
| `ChangeMonsterMP(enemy=0, delta=V)` | 敵MPを変更 |
| `ChangeMonsterCondition(enemy=0, op=add, state=1)` | 敵状態変化（`op=remove` で解除）|
| `ShowHiddenMonster(N)` | 隠れた敵を表示（N は敵インデックス）|
| `ChangeBattleBG(bg="ファイル名")` | バトル背景変更 |
| `ShowBattleAnimation_B(anim=1, target=0, wait=true)` | バトルアニメ表示（バトルイベント用）|
| `TerminateBattle()` | バトル終了 |
| `ConditionalBranch_B` / `ElseBranch_B` / `EndBranch_B` | バトルイベント用条件分岐（通常の `if` と同構造）|

### ショップ・宿屋

| コマンド | 説明 |
|---|---|
| `OpenShop(type=0, item1=N, item2=M, ...)` | ショップ開店 |
| `ShowInn(type=0, cost=100 [, has_handlers=false])` | 宿屋 |
| `Transaction()` | 購入/宿泊した場合の処理ブロック開始 |
| `NoTransaction()` | 購入しなかった/宿泊しなかった場合の処理ブロック開始 |
| `EndShop()` | ショップハンドラ終了 |
| `Stay()` | 宿泊した場合の処理ブロック開始 |
| `NoStay()` | 宿泊しなかった場合の処理ブロック開始 |
| `EndInn()` | 宿屋ハンドラ終了 |

### マップ設定

| コマンド | 説明 |
|---|---|
| `ChangeMapTileset(N)` | チップセット変更（N はチップセットID）|
| `ChangePBG(name="ファイル名", scroll_horz=0, scroll_vert=0, scroll_horz_auto=0, scroll_horz_speed=0, scroll_vert_auto=0, scroll_vert_speed=0)` | パノラマ変更 |
| `ChangeEncounterSteps(N)` | エンカウント歩数変更（N は歩数）|
| `TileSubstitution(chip_set=1, src=N, dst=M)` | タイル置換 |
| `TeleportTargets(action=0, map=N, x=X, y=Y, switch_on=false, switch_id=1)` | テレポート先追加/削除 |
| `EscapeTarget(map=N, x=X, y=Y, switch_on=false, switch_id=1)` | 逃げる先設定 |

### システム設定

| コマンド | 説明 |
|---|---|
| `ChangeSystemGraphics(system="system", stretch=false, font=0)` | システムグラフィック変更 |
| `ChangeScreenTransitions(type=0, id=0)` | 画面切り替え効果変更 |
| `ChangeSaveAccess(enable=true)` | セーブ許可/禁止 |
| `ChangeTeleportAccess(enable=true)` | テレポート許可/禁止 |
| `ChangeEscapeAccess(enable=true)` | 逃げる許可/禁止 |
| `ChangeMainMenuAccess(enable=true)` | メインメニュー許可/禁止 |
| `OpenSaveMenu()` | セーブ画面を開く |
| `OpenLoadMenu()` | ロード画面を開く |
| `OpenMainMenu()` | メニューを開く |
| `GameOver()` | ゲームオーバー |
| `ReturntoTitleScreen()` | タイトルへ戻る |
| `ExitGame()` | ゲーム終了 |
| `ToggleAtbMode()` | ATBモード切替（EasyRPG）|
| `ToggleFullscreen()` | フルスクリーン切替（EasyRPG）|
| `OpenVideoOptions()` | ビデオオプション（EasyRPG）|

### EasyRPG Player 拡張コマンド

以下のコマンドは EasyRPG Player 独自の拡張です。

| コマンド | CmdSetup サイズ | 説明 |
|---|---|---|
| `EasyRpg_TriggerEventAt(mode_x=, x=, mode_y=, y= [, flags=])` | 4 | 指定座標のイベントをトリガー |
| `EasyRpg_Pathfinder(mode_src=, src_event=, mode_dest_x=, dest_x=, mode_dest_y=, dest_y=, mode_search=, search_max=, mode_steps=, steps_max=, flags=, mode_ignore=, num_ignore= [, pN=...])` | 13 | 経路探索して移動ルートを適用 |
| `EasyRpg_CallMovementAction(pN=...)` | — | 移動アクション呼び出し (EasyRPGに実装なし) |
| `EasyRpg_WaitForSingleMovement(mode_event=, event_id=, mode_fail=, fail_limit=, mode_out=, out_var=)` | 6 | 単一移動完了まで待機 |
| `EasyRpg_AnimateVariable(pN=...)` | — | 変数アニメーション (EasyRPGに実装なし) |
| `EasyRpg_SetInterpreterFlag([flag="名前",] mode=, flag_value= [, flag_id=])` | 2 | インタープリタフラグ設定 |
| `EasyRpg_ProcessJson([json_path="パス",] mode_op=, operation=, src_mode=, src_str_id=, mode_tgt_type=, tgt_type=, mode_tgt_id=, tgt_id=, path_mode=, path_pos= [, flags=])` | 8 | JSON処理 |
| `EasyRpg_CloneMapEvent([target_name="名前",] src_map_mode=, src_map=, src_event_mode=, src_event=, target_x_mode=, target_x=, target_y_mode=, target_y=, target_event_mode=, target_event= [, str_mode=, str_id=])` | 10 | マップイベント複製 |
| `EasyRpg_DestroyMapEvent(mode=, target_event=)` | 2 | マップイベント削除 |
| `EasyRpg_StringPictureMenu([str="...",] pN=...)` | — | 文字列ピクチャメニュー (EasyRPGに実装なし) |

### Maniac Patch コマンド

以下のコマンドは [Maniac Patch](https://github.com/BuggyBurst/ManiacPatch) 独自の拡張です。

| コマンド | CmdSetup サイズ | 説明 |
|---|---|---|
| `Maniac_GetSaveInfo(slot_mode=, slot=, out_date_var=, out_time_var=, out_level_var=, out_hp_var=, p6=, face_mode=, face_pic0=, face_pic1=, face_pic2=, face_pic3=)` | 12 | セーブ情報取得 |
| `Maniac_Save(slot_mode=, slot=, has_out_var= [, out_var=])` | 3 | セーブ |
| `Maniac_Load(slot_mode=, slot=, skip_check=false)` | 3 | ロード |
| `Maniac_EndLoadProcess()` | 0 | ロードプロセス終了 |
| `Maniac_GetMousePosition(x_var=, y_var=)` | 2 | マウス座標取得 |
| `Maniac_SetMousePosition(pN=...)` | 3 | マウス座標設定 (EasyRPGに実装なし) |
| `Maniac_ShowStringPicture(bitfield=, pic_id=, x=, y=, magnify_w=, top_trans=, red=, green=, blue=, saturation=, effect_mode=, effect_power=, flags=, flags2=, flags3=, map_layer=, battle_layer=, flags4=, width=, height=, font_size=, angle_divisor=, str_id= [, magnify_h=])` | 23 | 文字列ピクチャ表示 |
| `Maniac_GetPictureInfo(bitfield=, info_type=, coord_mode=, pic_id=, out_var=, out_var2=, out_w_var=, out_h_var=)` | 8 | ピクチャ情報取得 |
| `Maniac_ControlBattle(pN=...)` | — | バトル制御 (EasyRPGに実装なし) |
| `Maniac_ControlAtbGauge(pN=...)` | — | ATBゲージ制御 (EasyRPGに実装なし) |
| `Maniac_ChangeBattleCommandEx(pN=...)` | — | バトルコマンド拡張変更 (EasyRPGに実装なし) |
| `Maniac_GetBattleInfo(pN=...)` | — | バトル情報取得 (EasyRPGに実装なし) |
| `Maniac_ControlVarArray(op=, mode=, target_a=, length=, target_b=)` | 5 | 変数配列操作 |
| `Maniac_KeyInputProcEx(operation=, start_var_id= [, mode_key=, key_id=])` | 4 | キー入力拡張 |
| `Maniac_RewriteMap(mode=, is_replace_range=false, is_upper_layer=false, tile_index=, x_start=, y_start=, width=, height=, disable_autotile=false)` | 9 | マップ書き換え |
| `Maniac_ControlGlobalSave(operation=, mode_bits=, type=, game_state_idx=, global_save_idx=, length=)` | 6 | グローバルセーブ操作 |
| `Maniac_ChangePictureId(pN=...)` | — | ピクチャID変更 (EasyRPGに実装なし) |
| `Maniac_SetGameOption(mode=, option_type=, value=, flags=)` | 4 | ゲームオプション設定 |
| `Maniac_CallCommand([str="...",] mode_code=, cmd_code=, start_or_mode_index=, length=, str_mode=, str_id= [, pN=...])` | 6 | コマンド呼び出し |
| `Maniac_ControlStrings([str="...",] mode_bits=, str_id_0=, str_id_1=, op_flags=, arg0=, arg1=, arg2= [, arg3=])` | 8 | 文字列操作 |
| `Maniac_GetGameInfo(bitfield=, info_type=, out_var=, extra_var=, extra2=, coord_x=, coord_y=, out_var2=)` | 8 | ゲーム情報取得 |
| `Maniac_EditPicture(bitfield=, pic_id=, op=, mode=, arg0=, arg1=, arg2=, arg3=)` | 8 | ピクチャ編集 |
| `Maniac_WritePicture([filename="...",] mode_bits=, target_type=, pic_id=, filename_id=, flags=)` | 5 | ピクチャ書き込み |
| `Maniac_AddMoveRoute([str="...",] pN=...)` | — | 移動ルート追加 (EasyRPGに実装なし) |
| `Maniac_EditTile(pN=...)` | — | タイル編集 (EasyRPGに実装なし) |
| `Maniac_ControlTextProcessing([str="...",] pN=...)` | — | テキスト処理制御 (EasyRPGに実装なし) |

---

### ShowPicture / MovePicture パラメータ詳細

**ShowPicture** — `ShowPicture(file="ファイル名", ...)`

| 引数 | デフォルト | 説明 |
|---|---|---|
| `id` | — | ピクチャ番号 |
| `pos_mode` | `0` | 座標指定方法（`0`=定数 / `1`=変数）|
| `x` | `0` | X座標（`pos_mode=1` のとき変数ID）|
| `y` | `0` | Y座標（`pos_mode=1` のとき変数ID）|
| `fixed_to_map` | `false` | マップに固定 |
| `magnify` | `100` | 拡大率（%）|
| `top_trans` | `0` | 上側透明度（0〜100）|
| `use_transparent_color` | `false` | 透過色を使用 |
| `red` | `100` | 赤（0〜200）|
| `green` | `100` | 緑（0〜200）|
| `blue` | `100` | 青（0〜200）|
| `saturation` | `100` | 彩度（0〜200）|
| `effect_mode` | `0` | エフェクト（`0`=なし / `1`=回転 / `2`=波）|
| `effect_power` | `0` | エフェクト強度 |
| `bottom_trans` | — | 下側透明度（RPGツクール2003拡張）|

**MovePicture** — `MovePicture(id=N, ...)` — ShowPicture と共通の引数（`file=` を除く）に加え:

| 引数 | デフォルト | 説明 |
|---|---|---|
| `duration` | `8` | 移動フレーム数 |
| `wait` | `false` | 完了まで待つ |

---

### MoveEvent の移動コマンドリスト

`MoveEvent` の `move=[...]` に指定できるコマンド名の一覧：

| 名前 | 説明 |
|---|---|
| `up` / `right` / `down` / `left` | 上下左右に1歩移動 |
| `upright` / `downright` / `downleft` / `upleft` | 斜め移動 |
| `random` | ランダム移動 |
| `toward_hero` / `away_from_hero` | 主人公へ近づく/離れる |
| `forward` | 向いている方向へ移動 |
| `face_up` / `face_right` / `face_down` / `face_left` | 向きを変える |
| `turn_right` / `turn_left` / `turn_180` / `turn_random` | 回転 |
| `face_random` / `face_hero` / `face_away` | ランダム向き/主人公の方/逆 |
| `wait` | 1フレーム待機 |
| `jump_start` / `jump_end` | ジャンプ開始/終了 |
| `lock_facing` / `unlock_facing` | 向き固定/解除 |
| `speed_up` / `speed_down` | 移動速度UP/DOWN |
| `freq_up` / `freq_down` | 移動頻度UP/DOWN |
| `switch_on(ID)` / `switch_off(ID)` | スイッチID を ON/OFF |
| `through_on` / `through_off` | すり抜けON/OFF |
| `stop_anim` / `start_anim` | アニメ停止/再開 |
| `transp_up` / `transp_down` | 透明度UP/DOWN |

`change_graphic` / `play_sfx` はパラメータに文字列を含むため、`move=[...]` リスト形式では表現できません。これらを含む `MoveEvent` はデコーダが `pN=` 数値列で出力します。

```
MoveEvent(this, move=[face_down, wait, forward, wait])
MoveEvent(event=3, freq=4, repeat=true, skip=false, move=[up, up, right, switch_on(5)])
```

---

## 6. コモンイベント

`RPG_RT.ldb` のコモンイベントを扱うフォーマットは以下の通りです。

```
event(コモンイベントID) {
    page(1, trigger=none) {
        文;
    }
}

event(2) {
    page(1, trigger=auto_start, cond_switch=5) {
        message("スイッチ5がtrueのとき自動実行");
    }
}
```

コモンイベントの `trigger` は `none` / `auto_start` / `parallel` の3種類です。  
`cond_switch=N` でトリガースイッチを指定します（省略すると解除）。`name=` でコモンイベント名を設定できます。
コモンイベントの `trigger` / `cond_switch` / `name` はエンコード時に反映されます。

---

## 7. スイッチ・変数の名前テーブル

ゲームディレクトリ内に `RPG_RT.ldb` がある場合、デコード時は自動的にそれを参照してスイッチ・変数番号を名前で出力します。

```bash
# -n を省略しても RPG_RT.ldb が同じディレクトリにあれば自動適用
r2s Map0001.lmu

# 別のディレクトリから LDB を指定する場合
r2s Map0001.lmu -n /path/to/RPG_RT.ldb
```

**出力例（名前テーブルあり）：**

名前があるスイッチ・変数は、常に `"名前[ID]"` 形式（ID 付き）で出力されます。ID を併記することで、名前が重複していても往復が保証されます。

```
var["Episode[1]"] = 1;
var["PartyMemberID01[10]"] = var["Episode[1]"];
if (switch["イベント済み[5]"] == true) {
    CallEvent(common=5);
}
```

エンコード時の名前指定は `"名前"` だけでも `"名前[ID]"` でも受け付けます。`"名前[ID]"` の場合は末尾の ID をそのまま使うため、名前が重複していても正しい番号に復元されます。名前から番号を逆引きする場合は、エンコードに使うスクリプトをデコードしたゲームと同じ `RPG_RT.ldb` で処理してください。

---

## 8. エンコード時バリデーション

エンコード時に `RPG_RT.ldb` が参照できる場合（`-n` 指定または自動検出）、以下の項目をコンパイル時にチェックします。

| チェック対象 | エラー例 |
|---|---|
| `switch[N]` の N がデータベースのスイッチ数を超えている | `switch[99999] is out of range (1..5040)` |
| `switch[N..M]` の M がスイッチ数を超えている | 同上 |
| `var[N]` の N がデータベースの変数数を超えている | `var[99999] is out of range (1..5000)` |
| `if (switch[N] == true)` 等の条件式内でも同様 | 同上 |

エラーが検出されるとエンコードは中断し、行番号・列番号付きのエラーが表示されます。

```
script.rs2k:3:9: error: switch[99999] is out of range (1..5040)
```

`RPG_RT.ldb` が参照できない場合（スタンドアローン使用など）は範囲チェックをスキップします。ただし名前指定（`switch["名前"]` / `var["名前"]`）は名前テーブルが必須で、見つからない名前や、名前テーブルなしでの名前指定はエラーになります。

---

## 9. コマンドラインの補足

入力ファイルの拡張子から操作が自動的に決まります：

```bash
r2s Map0001.lmu              # デコード → stdout
r2s Map0001.lmu -o out.rs2k  # デコード → ファイル
r2s script.rs2k Map0001.lmu  # エンコード（元ファイルを上書き、.bak 退避）
r2s RPG_RT.ldb -o out.rs2k   # コモンイベント デコード
r2s script.rs2k RPG_RT.ldb   # コモンイベント エンコード
r2s Game/ -o scripts/        # プロジェクト全体デコード
r2s scripts/ Game/           # プロジェクト全体エンコード
```

| オプション | 説明 |
|---|---|
| `-o <file\|dir>` | 出力先ファイルまたはディレクトリ（プロジェクトデコードは省略可: 無ければ `<game_dir>_scripts`）|
| `-n <ldb>` | 名前テーブル用の RPG_RT.ldb（省略時は入力の隣 / ゲームディレクトリのものを自動使用）|
| `-e <enc>` | 文字コード（既定 932 / Shift-JIS）|
| `--engine 2000\|2003` | マップ保存時のエンジンバージョン（既定 2000）|
| `--prune` | エンコード時にスクリプトに現れないイベント・ページをマップから削除 |

- プロジェクト全体エンコードは全ファイルのエンコードが成功した場合のみゲームに書き込みます。失敗が1つでもあれば何も変更しません。
- 上書きされる `.lmu` / `.ldb` は、事前に `<元ファイル名>.bak` へ退避されます（既に `.bak` があれば上書き）。
- UTF-8 BOM 付きのスクリプトも読み込めます。
- 未知のコマンド名・条件・ページオプション・when のオプションはエラーになります。
- **キャラクター指定**（`MoveEvent` / `ShowBattleAnimation` / `FlashSprite` / `ChangeEventLocation` / `TradeEventLocations` / `char_facing`）は、`player`=10001、`boat`=10002、`ship`=10003、`airship`=10004、`this`=10005、それ以外はイベントIDです（RPG_RT / EasyRPG Editor の定数）。未知の識別子はエラーになります。
- **コマンド引数のキー名のタイプミスは検出されません**（例: `Wait(framez=30)` はエラーにならず既定値が使われます）。引数名は「5. コマンドリファレンス」と見比べて確認してください。
