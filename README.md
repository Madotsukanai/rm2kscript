# rm2kscript

RPGツクール2000/2003 のイベントスクリプトをC言語風のテキスト形式（`.rs2k`）にデコンパイルし、再コンパイルできるCLIツールです。

## 機能

- **decode** — `.lmu` マップファイルをテキスト（`.rs2k`）に変換
- **encode** — テキストを `.lmu` マップファイルに書き戻す
- **decode-db** — `RPG_RT.ldb` のコモンイベントをテキストに変換
- **encode-db** — テキストをコモンイベントとして `.ldb` に書き戻す

## ダウンロード

[Releases](../../releases) から最新の `rm2kscript.exe`（Windows）または `rm2kscript`（Linux）をダウンロードしてください。

## 使い方

```bash
# マップファイルをテキストに変換
rm2kscript decode Map0001.lmu -o Map0001.rs2k

# テキストをマップファイルに書き戻す
rm2kscript encode Map0001.rs2k Map0001.lmu

# 別ファイルに出力
rm2kscript encode Map0001.rs2k Map0001.lmu -o Map0001_new.lmu

# コモンイベントをテキストに変換
rm2kscript decode-db RPG_RT.ldb -o CommonEvents.rs2k

# コモンイベントを書き戻す
rm2kscript encode-db CommonEvents.rs2k RPG_RT.ldb
```

> [!NOTE]
> ゆめ2っきなど日本語版ツクール2000/2003 プロジェクト（Shift-JIS、コードページ932）をデフォルトで想定しています。
> 別のエンコーディングを使う場合は `-e` オプションで指定してください（例: `-e 1252` で西欧）。

## スクリプト言語の概要

詳細な仕様は [docs/language.md](docs/language.md) を参照してください。

### 基本構造

```
event(イベントID, x=X座標, y=Y座標) {
    page(ページ番号, trigger=action, layer=same, sprite="Actor1", sprite_index=0) {
        message("こんにちは！");

        if (switch[5] == ON) {
            ChangeItems(item=10, op=0, src=0, amount=1, p4=0);
            switch[5] = OFF;
        } else {
            message("もう何もありません。");
        }

        Wait(frames=30);
    }
}
```

### 主なコマンド

| 構文 | 説明 |
|---|---|
| `switch[N] = ON/OFF/toggle` | スイッチ操作 |
| `var[N] = value` / `var[N] += value` | 変数操作 |
| `message("テキスト")` | メッセージ表示 |
| `if (switch[N] == ON) { }` | 条件分岐 |
| `choice("A","B") { when("A"){} }` | 選択肢 |
| `Teleport(map=N, x=X, y=Y)` | テレポート |
| `CallCommonEvent(id=N)` | コモンイベント呼び出し |
| `Wait(frames=N)` | 待機 |
| `PlayBGM("filename", volume=100, tempo=100, balance=50)` | BGM再生 |

## ビルド方法

### 依存ライブラリ

- [liblcf](https://github.com/EasyRPG/liblcf) (EasyRPG Project)
- CMake 3.16 以上
- C++17 対応コンパイラ

### Linux / macOS

```bash
# liblcf をインストール（Arch Linux の例）
sudo pacman -S liblcf

# ビルド
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Windows（MinGW-w64 クロスコンパイル）

依存ライブラリを自動的にダウンロード・ビルドするスクリプトを用意しています。

```bash
# Arch Linux + mingw-w64 環境が必要
# mingw-w64-gcc, mingw-w64-cmake, ninja が必要
bash scripts/build-win64.sh

# 成功すると dist/ に rm2kscript.exe が生成されます
```

## ライセンス

MIT License — [LICENSE](LICENSE) を参照してください。

liblcf は MIT License で配布されています（EasyRPG Project）。
