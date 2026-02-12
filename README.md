# SRT Importer ExMultiLine Plugin for AviUtl 2

このプラグインは、AviUtl ExEdit2でSRT字幕ファイルをインポートするためのプラグインです。本体の使い方については公式HPを参照してください。

公式HP：[AviUtlのお部屋](https://spring-fragrance.mints.ne.jp/aviutl/)


## 機能

- SRTファイルを参照して、テキストオブジェクトとして指定レイヤーにインポートできます。文字のエンコーディングはUTF-8、改行コードはCRLF/CR/LFに対応しています。
- 字幕の複数行表示（1つの字幕ブロック内での改行）に対応しています。
- AviUtl ExEdit2 beta32にて起動確認しています。

## 使用方法

1. Releaseページから、`SrtImporter_ex.aux2`（またはビルド時に指定した名前の `.aux2`）をダウンロードしてください
2. プラグインをAviUtlにインストールします。インストール方法は、環境によって異なりますが、多くの場合上記ファイルを次の位置にコピーして起動するだけです。

    `SrtImporter_ex.aux2` を次の位置にコピー<br>
    ```C:\ProgramData\aviutl2\Plugin```

3. 本体を起動し、「表示」から「SRT Importer ExMultiLine」をチェックを入れます。次のような画面になります。

    <img src="./images/gui_image_001.png" alt="GUIの説明画像">

    中央にあるのがプラグイン本体です。初回起動時は小さく畳んであることがあるので、ドラックしてウィンドウを広げてください。

4. SRTファイルをインポートする前に、必要に応じてレイヤー、位置、フォント、色などを設定します。インポートしたあとの変更はテキストオブジェクトごとの変更となります。
6. 「Import SRT...」ボタンをクリックしてインポートを実行します。

## 対応形式

- 文字エンコーディング: UTF-8
- 改行コード: CRLF / CR / LF
- 時間形式: 00:00:00,000

## 注意事項

- 時刻からフレームへの変換は切り捨てられます。
- 改行コードの混在（CRLF/CR/LF）でも読み込み可能です。
- GUIからの設定はインポート時に適用されます。

## ビルド

AviUtl ExEdit2 beta24aのSDKを使用しています。
ビルドはw64devkitとCMakeを使用しています。

VSCode上で.vscode/tasks.jsonを設定し、ビルドが可能です。
環境によってパラメータが異なるので参考にしてください。

tasks.json
```
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "cmake configure (w64devkit)",
      "type": "process",
      "command": "C:\\Program Files\\CMake\\bin\\cmake.exe",
      "args": [
        "-S", ".",
        "-B", "build",
        "-G", "MinGW Makefiles",
        "-DCMAKE_C_COMPILER=gcc",
        "-DCMAKE_CXX_COMPILER=g++",
        "-DCMAKE_BUILD_TYPE=Release"
      ],
      "options": {
        "cwd": "${workspaceFolder}",
        "env": {
          "PATH": "C:\\w64devkit\\bin;${env:PATH}"
        }
      }
    },
    {
      "label": "cmake build (w64devkit)",
      "type": "process",
      "command": "C:\\Program Files\\CMake\\bin\\cmake.exe",
      "args": [
        "--build", "build", "--config", "Release"
      ],
      "options": {
        "cwd": "${workspaceFolder}",
        "env": {
          "PATH": "C:\\w64devkit\\bin;${env:PATH}"
        }
      },
      "dependsOn": ["cmake configure (w64devkit)"],
      "group": { "kind": "build", "isDefault": true }
    }
  ]
}
```

### Dockerでビルド（ホストにCMake不要）

このリポジトリには `docker/Dockerfile` を用意しています。  
Docker Desktop があれば、Windows側に CMake や MinGW を入れなくても `.aux2` を生成できます。

1. ビルド用イメージを作成

```powershell
docker build -t srtimporter-build -f docker/Dockerfile .
```

2. コンテナ内でビルド実行（リポジトリを `/work` にマウント）

```powershell
docker run --rm -v "${PWD}:/work" -w /work srtimporter-build
```

生成物:

- `build-docker/SrtImporter_ex.aux2`

出力ファイル名を変更したい場合（拡張子は自動で `.aux2`）:

```powershell
cmake -S . -B build-docker -DSRTIMPORTER_OUTPUT_NAME=YourPluginName
cmake --build build-docker --config Release
```

補足:

- `SDK/plugin2.h` と `SDK/logger2.h` が存在することが前提です。
