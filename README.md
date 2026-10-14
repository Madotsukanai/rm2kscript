# rm2kscript

RPGツクール2000/2003 のイベントスクリプトをC言語風のテキスト形式（`.rs2k`）にデコンパイルし、再コンパイルできるCLIツールです。

## 機能

- **decode** — `.lmu` マップファイルをテキスト（`.rs2k`）に変換
- **encode** — テキストを `.lmu` マップファイルに書き戻す
- **decode-db** — `RPG_RT.ldb` のコモンイベントをテキストに変換
- **encode-db** — テキストをコモンイベントとして `.ldb` に書き戻す
- **decode-project** — ゲームディレクトリの全マップ + LDB を一括デコード
- **encode-project** — スクリプトディレクトリの全 `.rs2k` を一括エンコードしてゲームに書き戻す

## ダウンロード

[Releases](../../releases) から最新のバイナリをダウンロードしてください。

- Linux: `rm2kscript-linux-x86_64`
- Windows (MinGW ビルド): `rm2kscript-windows-x86_64.exe`
- Windows (MSVC ビルド): `rm2kscript-windows-msvc-x86_64.exe`

## 使い方

```bash
# プロジェクト全体を一括デコード（全マップ + コモンイベント）
# ゲームディレクトリ内の RPG_RT.ldb からスイッチ・変数名を自動読み込み
rm2kscript decode-project /path/to/Game -o /path/to/scripts

# 一括エンコードしてゲームに書き戻す
rm2kscript encode-project /path/to/scripts /path/to/Game

# 単体ファイル操作
rm2kscript decode Map0001.lmu -o Map0001.rs2k
rm2kscript encode Map0001.rs2k Map0001.lmu

# コモンイベント単体
rm2kscript decode-db RPG_RT.ldb -o CommonEvents.rs2k
rm2kscript encode-db CommonEvents.rs2k RPG_RT.ldb

# スイッチ・変数名テーブルを明示指定する場合（-n オプション）
rm2kscript decode-project /path/to/Game -o /path/to/scripts -n /path/to/Game/RPG_RT.ldb
```

> [!NOTE]
> ゲームディレクトリ内に `RPG_RT.ldb` があれば、`-n` を省略してもスイッチ・変数名が自動的に使われます。
> エンコーディングは日本語版（Shift-JIS / コードページ932）がデフォルトです。別のエンコーディングには `-e` オプションを使用してください（例: `-e 1252` で西欧）。

> [!IMPORTANT]
> `encode-project` は、全ファイルのエンコードが成功した場合のみゲームに書き込みます。上書きされるファイルは事前に `.bak` として退避されます。
> マップの保存エンジンは `--engine 2003` で RPGツクール2003 形式に切り替えられます（既定は2000）。
> 未知のコマンド・条件・オプションは黙って無視されず、エラーになります。

## スクリプト言語の概要

詳細な仕様は [docs/language.md](docs/language.md) を参照してください。

### 基本構造

```
event(イベントID, x=X座標, y=Y座標) {
    page(ページ番号, trigger=action, layer=same, sprite="Actor1", sprite_index=0) {
        message("こんにちは！");

        if (switch["フラグ名"] == ON) {
            var["所持金"] += 100;
            ChangeItems(op=add, item=10, amount=1);
            switch["フラグ名"] = OFF;
        } else {
            message("もう何もありません。");
        }

        Wait(frames=30);
        CallEvent(common=5);
    }
}
```

### 主なコマンド

| 構文 | 説明 |
|---|---|
| `switch[N] = ON/OFF/toggle` | スイッチ操作（名前テーブルがあれば `switch["名前"]` も可）|
| `var[N] = 値` / `var[N] += 値` | 変数操作（同様に `var["名前"]` も可）|
| `message("テキスト")` | メッセージ表示 |
| `if (switch[N] == ON) { }` | 条件分岐 |
| `choice("A","B") { when("A"){} }` | 選択肢 |
| `Teleport(map=N, x=X, y=Y)` | テレポート |
| `CallEvent(common=N)` | コモンイベント呼び出し |
| `CallEvent(event=N, page=P)` | マップイベント呼び出し |
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
make

# テスト
make test

# インストール (デフォルト: /usr/local)
make install

# インストール先を変更する場合
make install PREFIX=$HOME/.local
```

### Windows（MSVC / Visual Studio）

[vcpkg](https://vcpkg.io) で依存ライブラリを揃えてからビルドします。

```powershell
# vcpkg で依存ライブラリをインストール
vcpkg install expat inih icu --triplet x64-windows-static

# liblcf をビルド
git clone --depth=1 https://github.com/EasyRPG/liblcf.git C:\liblcf
cmake -S C:\liblcf -B C:\liblcf\build `
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DLIBLCF_WITH_ICU=ON `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT\scripts\buildsystems\vcpkg.cmake" `
  "-DVCPKG_TARGET_TRIPLET=x64-windows-static" `
  -G "Visual Studio 17 2022" -A x64
cmake --build C:\liblcf\build --config Release
cmake --install C:\liblcf\build --prefix C:\liblcf-install

# rm2kscript をビルド
cmake -B build `
  -DCMAKE_BUILD_TYPE=Release `
  "-DCMAKE_PREFIX_PATH=C:\liblcf-install" `
  "-DCMAKE_TOOLCHAIN_FILE=$env:VCPKG_INSTALLATION_ROOT\scripts\buildsystems\vcpkg.cmake" `
  "-DVCPKG_TARGET_TRIPLET=x64-windows-static" `
  -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Windows（MinGW-w64 クロスコンパイル、Linux上で実行）

依存ライブラリを自動的にダウンロード・ビルドするスクリプトを用意しています。

```bash
# Arch Linux + mingw-w64 環境が必要
# mingw-w64-gcc, mingw-w64-cmake, ninja が必要
make win64

# 成功すると dist/ に rm2kscript.exe が生成されます
```

### Makefile ターゲット一覧

| ターゲット | 説明 |
|---|---|
| `make` | Release ビルド |
| `make debug` | Debug ビルド |
| `make test` | テスト実行 |
| `make install` | インストール（`PREFIX` で変更可） |
| `make win64` | Windows 向けクロスビルド |
| `make clean` | ビルドディレクトリ削除 |
| `make distclean` | ビルド＋依存キャッシュも削除 |

## ライセンス

rm2kscript 本体は **MIT License** で配布されています — [LICENSE](LICENSE) を参照してください。

### サードパーティライブラリ

rm2kscript は以下のオープンソースライブラリを使用しています。

#### liblcf

- **用途**: RPGツクール2000/2003 の LMU / LDB ファイル読み書き
- **ライセンス**: MIT License
- **ソース**: https://github.com/EasyRPG/liblcf

```
Copyright (c) 2014-2025 liblcf authors

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

#### Expat

- **用途**: liblcf の依存ライブラリ（XML パース）
- **ライセンス**: MIT License
- **ソース**: https://github.com/libexpat/libexpat

```
Copyright (c) 1998-2000 Thai Open Source Software Center Ltd and Clark Cooper
Copyright (c) 2001-2025 Expat maintainers

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

#### inih

- **用途**: liblcf の依存ライブラリ（INI ファイルパース）
- **ライセンス**: BSD 3-Clause License
- **ソース**: https://github.com/benhoyt/inih

```
Copyright (c) 2009, Ben Hoyt
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Ben Hoyt nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY BEN HOYT ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL BEN HOYT BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

#### ICU (International Components for Unicode)

- **用途**: liblcf の依存ライブラリ（文字エンコーディング変換）。Windows 版ビルドでのみ使用。
- **ライセンス**: Unicode License V3 (SPDX: `Unicode-3.0`)
- **ソース**: https://github.com/unicode-org/icu

```
UNICODE LICENSE V3

COPYRIGHT AND PERMISSION NOTICE

Copyright © 1991-2026 Unicode, Inc.

Permission is hereby granted, free of charge, to any person obtaining a
copy of data files and any associated documentation (the "Data Files") or
software and any associated documentation (the "Software") to deal in the
Data Files or Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, and/or sell
copies of the Data Files or Software, and to permit persons to whom the
Data Files or Software are furnished to do so, provided that either (a)
this copyright and permission notice appear with all copies of the Data
Files or Software, or (b) this copyright and permission notice appear in
associated Documentation.

THE DATA FILES AND SOFTWARE ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF
THIRD PARTY RIGHTS.

IN NO EVENT SHALL THE COPYRIGHT HOLDER OR HOLDERS INCLUDED IN THIS NOTICE
BE LIABLE FOR ANY CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES,
OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THE DATA
FILES OR SOFTWARE.

Except as contained in this notice, the name of a copyright holder shall
not be used in advertising or otherwise to promote the sale, use or other
dealings in these Data Files or Software without prior written
authorization of the copyright holder.

SPDX-License-Identifier: Unicode-3.0
```

ICU には追加のサードパーティライセンスが含まれます。詳細は
https://github.com/unicode-org/icu/blob/main/LICENSE を参照してください。
