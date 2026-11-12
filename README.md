# Margaret Client

Minecraft: Bedrock Edition (Windows / Microsoft Store 版) 用クライアント。

## 構成

| ディレクトリ | 内容 |
| --- | --- |
| `injector/` | `Margaret.dll` を起動中の Bedrock Edition に読み込ませるインジェクター (Windows x64) |

## Injector

起動中の Bedrock Edition (`Minecraft.Windows.exe`) を自動検出し、
`LoadLibraryW` + `CreateRemoteThread` で DLL を読み込ませます。
プロセス名で検出するため、バージョンに依存せず最新版でもそのまま動きます。

### Bedrock 特有の注意

Bedrock Edition は **UWP の AppContainer サンドボックス**内で動作します。
サンドボックス内のプロセスは、ACL で読み取り/実行が許可されたファイルしか `LoadLibrary` できません。
本インジェクターは注入前に、DLL ファイルへ **ALL APPLICATION PACKAGES (S-1-15-2-1)** の
読み取り/実行権限を自動で付与します。これが無いと `LoadLibraryW` が ERROR_ACCESS_DENIED (5) で失敗します。

### GUI 版 (MargaretInjector.exe)

- **言語**: 左下のボタンで English / 日本語 / 中文 を切り替え (既定は English)。選択は `margaretinjector.ini` に保存されます。
- **色のカスタマイズ**: `Text` / `BG` ボタンで文字色・背景色を変更でき、設定は保存されます。

ダークテーマの GUI。起動すると検出された Bedrock インスタンスの一覧が出るので、
選んで `inject` ボタンを押すだけです。

- **Refresh**: 一覧を再取得
- **DLL**: 注入する DLL (既定は exe と同じフォルダの `Margaret.dll`、`...` で選択)
- **inject**: 選択中の Minecraft に注入 (一覧をダブルクリックでも可)

### CLI 版 (MargaretInjectorCLI.exe)

自動化・スクリプト向けのコンソール版。

### 使い方

1. `MargaretInjector.exe` と同じフォルダに `Margaret.dll` (x64) を置く
2. Bedrock Edition を起動してタイトル画面まで進める
3. `MargaretInjector.exe` を実行

```
MargaretInjector.exe                 自動検出して Margaret.dll を注入
MargaretInjector.exe --wait          Bedrock の起動を待ってから注入
MargaretInjector.exe --pid 1234      PID を指定して注入
MargaretInjector.exe --dll C:\x.dll  別の DLL を注入
MargaretInjector.exe --list          検出された Minecraft を一覧表示
MargaretInjector.exe --no-pause      終了時に Enter 待ちしない
```

### 注入前のチェック

- DLL が存在し、x64 の PE DLL であること
- AppContainer アクセス権 (ALL APPLICATION PACKAGES) を付与
- 対象プロセスが 64bit であること
- 同じ DLL がすでに読み込まれていないこと (二重注入防止)
- 注入後、実際にモジュールが読み込まれたかを確認

### ビルド

Visual Studio 2022 (C++ デスクトップ開発 + CMake) で:

```bat
injector\build.bat
```

出力: `injector\build\Release\MargaretInjector.exe`

GitHub Actions (`Build Injector`) でもビルドされ、成果物として exe をダウンロードできます。
