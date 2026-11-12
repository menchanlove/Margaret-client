# Margaret Client

Minecraft: Java Edition 用クライアント。

## 構成

| ディレクトリ | 内容 |
| --- | --- |
| `injector/` | `Margaret.dll` を起動中の Minecraft に読み込ませるインジェクター (Windows x64) |

## Injector

起動中の Minecraft (`javaw.exe` / `java.exe` のうち、タイトルが `Minecraft` で始まるウィンドウを持つプロセス) を自動検出し、
`LoadLibraryW` + `CreateRemoteThread` で DLL を読み込ませます。
バージョン番号に依存しない検出方法なので、最新版の公式ランチャー起動の Minecraft でもそのまま動きます。

### 使い方

1. `MargaretInjector.exe` と同じフォルダに `Margaret.dll` (x64) を置く
2. Minecraft を起動してタイトル画面まで進める
3. `MargaretInjector.exe` を実行

```
MargaretInjector.exe                 自動検出して Margaret.dll を注入
MargaretInjector.exe --wait          Minecraft の起動を待ってから注入
MargaretInjector.exe --pid 1234      PID を指定して注入
MargaretInjector.exe --dll C:\x.dll  別の DLL を注入
MargaretInjector.exe --list          検出された Minecraft を一覧表示
MargaretInjector.exe --no-pause      終了時に Enter 待ちしない
```

複数の Minecraft が起動している場合は番号で選択します。

### 注入前のチェック

- DLL が存在し、x64 の PE DLL であること
- 対象の Java が 64bit であること
- 同じ DLL がすでに読み込まれていないこと (二重注入防止)
- 注入後、実際にモジュールが読み込まれたかを確認

### ビルド

Visual Studio 2022 (C++ デスクトップ開発 + CMake) で:

```bat
injector\build.bat
```

出力: `injector\build\Release\MargaretInjector.exe`

GitHub Actions (`Build Injector`) でもビルドされ、成果物として exe をダウンロードできます。
