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
| `x=` | 整数 | マップX座標 |
| `y=` | 整数 | マップY座標 |

### `page(番号, ...)`

ページ番号は1から始まります。ページ番号の後にスペース区切りでメタデータを指定します。

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
| `sprite=` | `"ファイル名"` | キャラクターグラフィック（`""`で非表示）|
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
var[N] == var[M]       // 変数同士の比較
timer >= N             // タイマーがN以上
timer <= N
has_item(id=N)         // アイテムN所持
has_equip(actor=N, item=M)  // アクターNがアイテムM装備
actor_cond(actor=N, cond=C, val=V)
on_vehicle(type=ship)  // ship / skiff / airship / any
char_facing(who=N, dir=down)
event_state(event=N, val=V)
gold_cmp(gold=N, op=ge)     // op: ge (>=) / le (<=)
```

### 4.5 choice 文

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

- `cancel=N` はキャンセル時に N 番目の選択肢として処理する（0 で最初、5 でキャンセル無効など）
- `when` の `index=` は省略可能

### 4.6 comment

```
comment("コメントテキスト");
comment("複数行\n2行目");
```

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
| `ControlVars(var=N, type=0, id2=N, op_code=0, src=0, src_val=V)` | 変数操作（詳細形式） |
| `TimerOperation(set=N, p1=0, p2=0, p3=0, p4=0)` | タイマーセット |
| `TimerOperation(start, p1=0, p2=0, p3=0, p4=0)` | タイマー開始 |
| `TimerOperation(stop, p1=0, p2=0, p3=0, p4=0)` | タイマー停止 |

### パーティ・アイテム

| コマンド | 説明 |
|---|---|
| `ChangeGold(delta=N)` | 所持金変更（負数で減少）|
| `ChangeItems(item=N, op=0, src=0, amount=M, p4=0)` | アイテム増減（op: 0=増加, 1=減少）|
| `ChangePartyMembers(add, src=0, actor=N)` | パーティメンバー追加 |
| `ChangePartyMembers(remove, src=0, actor=N)` | パーティメンバー除外 |

### アクターステータス

| コマンド | 説明 |
|---|---|
| `ChangeExp(actor=N, op=0, src=0, val=V, show=0)` | EXP変更 |
| `ChangeLevel(actor=N, op=0, src=0, val=V, show=0)` | レベル変更 |
| `ChangeHP(actor=N, delta=N)` | HP変更 |
| `ChangeSP(actor=N, delta=N)` | SP変更 |
| `FullHeal(actor=N)` | 全回復（`party` でパーティ全員）|
| `ChangeSkills(actor=N, op=learn, src=0, skill=N)` | スキル習得/忘却 |
| `ChangeCondition(actor=N, op=add, state=N)` | 状態変化付与/解除 |
| `ChangeEquipment(actor=N, op=0, src=0, item=N)` | 装備変更 |
| `ChangeParameters(who=0, actor=N, param=0, op=0, src=0, val=V)` | 基礎パラメータ変更 |
| `ChangeHeroName(actor=N, name="名前")` | アクター名変更 |
| `ChangeHeroTitle(actor=N, title="肩書き")` | 肩書き変更 |
| `ChangeSpriteAssociation(actor=N, sprite="ファイル名", sprite_index=0, p2=0)` | 歩行グラ変更 |
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
| `MoveEvent(event=N/player/this, p1=N, ...)` | イベント移動 |
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
| `ShowPicture(file="名前", p0=N, ...)` | ピクチャ表示 |
| `MovePicture(p0=N, ...)` | ピクチャ移動 |
| `ErasePicture(id=N)` | ピクチャ消去 |
| `ShowBattleAnimation(anim=N, event=10001, wait=1)` | バトルアニメ表示 |
| `FlashSprite(event=this, r=31, g=31, b=31, chroma=31, duration=5, wait=1)` | スプライトフラッシュ |
| `PlayerVisibility(visible=1)` | プレイヤー表示切替 |

### BGM・SE

| コマンド | 説明 |
|---|---|
| `PlayBGM("filename", event=0, volume=100, tempo=100, balance=50)` | BGM再生 |
| `FadeOutBGM(ms=800)` | BGMフェードアウト |
| `MemorizeBGM()` | BGM記憶 |
| `PlayMemorizedBGM()` | 記憶BGM再生 |
| `PlaySound("filename", p0=60, p1=100, p2=50)` | SE再生 |
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
| `Loop() { ... }` | ループ（`BreakLoop()` で脱出）|
| `BreakLoop()` | ループ脱出 |
| `Label(N)` | ラベル |
| `JumpToLabel(N)` | ラベルへジャンプ |
| `CallEvent(type=0, id=N, page=0)` | イベント呼び出し（type: 0=マップイベント, 1=コモンイベント）|
| `CallCommonEvent(id=N)` | コモンイベント呼び出し |
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
| `TeleportTargets(action=0, map=N, x=X, y=Y, p4=0, p5=1)` | テレポート先追加/削除 |
| `EscapeTarget(map=N, x=X, y=Y, p3=0, p4=1)` | 逃げる先設定 |

### システム設定

| コマンド | 説明 |
|---|---|
| `ChangeSystemGraphics(system="system", opacity=0)` | システムグラフィック変更 |
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
`cond_switch=N` でトリガースイッチを指定します。
