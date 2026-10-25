# LINEPlus — LINE 26.16.0向け開発プレビュー

K2ge3Airの公開機能説明を参考にした独立実装です。K2ge3Airのプログラムやライセンス処理は含みません。

**これは全機能対応の完成版ではありません。実機での起動・ログイン・機能動作は未確認です。**

## 出力

`dist/LINEPlus_26.16.0_v0.3_preview_unsigned.ipa` はSideStore／AltStoreで再署名するための開発版です。元のIPAは変更しません。iOS 18以降、arm64用。

## v0.3の追加処理

- `NLConfigurationManager.isMarkdownViewerEnabled`を有効化するスイッチ。内蔵ビューアーの適用範囲は実機確認待ち。
- `NCXBizCallViewController.disconnect:forEvent:`経由の切断確認。他の通話画面には未対応。
- 表示中のスタンプのUIImageViewを探し、選択した画像をGIFへ変換してクリップボードにコピー。取得可能なアニメーションフレームは遅延時間を保持して変換、静止画は1フレーム。独自描画、音声、エフェクト、購入状態の変更には未対応。これは無料取得機能ではありません。
- 提供された全機能一覧を引き続き対象としていますが、未実装の要件は残っています。

## v0.2の設定画面

写真のようなアイコン付きカテゴリ一覧を採用し、YouTubePlus風に設定を分類しました。既読、送信取消、広告、着せ替え、アプリアイコン、タブバー、表示名、操作確認、LYP関連、診断の各画面から操作します。未実装のカテゴリには対応状況を表示します。

- アプリアイコン：IPAの`CFBundleAlternateIcons`に登録された**118種類＋標準**を一覧から直接選択。LINEPlus内では会員判定を設けず、iOSの`setAlternateIconName:`で切り替えます。未同梱のダウンロード限定アイコンは対象外。実機での適用は未確認、失敗時はiOSのエラーを表示します。
- タブバー：表示切替、ドラッグで並べ替え、表示名編集、SF Symbolsによるアイコン変更、選択色のブルー切替、リセット。新しい`UITab`形式と従来の`viewControllers`形式の両方を処理します。LINE固有の画面遷移との整合は実機検証待ちです。

## 実装済みの処理（動作は未検証）

- アプリ上の「LP」ボタンから日本語設定画面を表示。長押しで現在のトークを操作。
- `ManagedChat`／`ManagedSquareChat`の`canMarkChatAsReadToServer`を使った既読送信可否の抑制とトーク別例外。別の送信経路まで遮断できるかは未確認。
- 解析で特定した9種類の広告ビュー、旧ホームのおすすめビューを非表示。広告通信の遮断や空白の除去は未実装。
- `ManagedUser.displayUserName`、`ManagedChat`／`ManagedSquareChat.titleWithMemberCount:`でローカル表示名を適用。全画面への反映は未確認。
- 従来の`ChatInputBar.send:`を通る送信時の確認、最低人数設定。新しいV2入力バー・メディア・スタンプ等の送信経路は未対応。
- 通話履歴セルの`startCall`、音声通話画面の`startVideo:`で確認。他の開始経路は未対応。
- iOS 18以降の`UITabBarController.tabs`を使ったタブ非表示／再表示。全タブを隠すことは不可。
- 現在のトークの識別子をコピー。取得できない場合のCore Data URIはMIDではありません。
- メソッドの引数・戻り値を確認してフックし、見つからない処理を診断画面に表示。

すべての機能スイッチは初期状態でオフ。フック設置成功は機能の動作確認を意味しません。

## 未実装

- iPhoneでiPad／サブ端末ログイン
- グループ既読ユーザーの記録と表示
- メッセージ送信時の既読送信
- 送信取消メッセージ保持、自分の取消保持、バックグラウンド時の保持
- 着せ替えダウンロード・iPad適用・ランダム適用
- 音声入力ボタン非表示
- 全経路での送信／通話開始／カメラオン／切断確認
- 独自描画スタンプGIFコピー、MID検索
- 有料スタンプの無料取得・無制限使用
- LYPプレミアムのメッセージ編集・通知なし送信取消

未実装の項目を動作するスイッチとして表示しません。

## ビルドとIPA作成

### GitHub Actions：URLを入力してビルド

ワークフロー: [`.github/workflows/build-ipa.yml`](.github/workflows/build-ipa.yml)

1. このプロジェクトの`Sources`、`tools`、`tests`、`.github/workflows`を含むファイルをGitHubリポジトリのデフォルトブランチへアップロードします。
2. リポジトリの **Actions → Build LINEPlus IPA → Run workflow** を開きます。
3. `ipa_url`に**LINE 26.16.0の復号済みIPAが直接ダウンロードできるURL**を入力します。ブラウザーでのログインが必要なページや共有ページは使用できません。HTTP(S)のリダイレクトとクエリ付きURLに対応しています。
4. 必要なら`ipa_sha256`を入力し、**Run workflow**を押します。空欄でも実行できます。
5. 完了した実行の **Artifacts → LINEPlus-26.16.0-unsigned-実行番号** をダウンロードし、ZIPを展開します。
6. 中の`LINEPlus_26.16.0_unsigned.ipa`をMacのダウンロードフォルダに保存し、SideStore／AltStoreで再署名して導入します。

GitHubのmacOSランナー上でダウンロード、テスト、dylibビルド、IPA注入、整合性検証まで自動実行します。ArtifactsにはIPAとSHA-256付きJSONが入り、保存期間は7日です。GitHub側からMacのダウンロードフォルダへ直接書き込むことはできないため、Artifactsをダウンロードしてください。

現在対応する入力は`jp.naver.line`・26.16.0・復号済みarm64 IPAです。暗号化されたIPA、別バージョン、LINEPlus注入済みIPA、HTMLが返るURLは失敗になります。これは現在のLINEPlus実装をビルドする仕組みで、写真にある未実装機能を追加するものではありません。

### ローカルビルド

XcodeのiOS SDK、Python 3が必要です。外部Pythonパッケージは不要です。

```sh
python3 tools/inspect_ipa.py "/path/to/LINE.ipa" --output build/line-inventory.json
python3 tools/build.py
python3 -m unittest discover -s tests -v
python3 tools/package_ipa.py "/path/to/LINE.ipa" --output dist/LINEPlus_26.16.0_v0.3_preview_unsigned.ipa
```

`DEVELOPER_DIR`でXcodeの場所を上書きできます。26.16.0以外はパッケージ作成を拒否し、実行時もフックしません。署名はSideStore／AltStoreで付け直してください。元のbundle ID、アプリ名、拡張は維持しています。サイドロード時のApp Groups・Keychain・通知権限の適合は未確認です。

## 解析の根拠

- 元IPAのバージョン: `26.16.0`、ビルド: `2026.1006.1718`
- 実行バイナリSHA-256: `e1ad61171b021566f4a318c93ba1260747bfb220c81a79d5a80121311787d6ad`
- `LC_ENCRYPTION_INFO_64.cryptid = 0`（復号済み）
- Objective-Cメタデータから17,914クラスを取得。
- Swift内部の`ChatViewControllerMarkAsReadHandler`／`MessageSendHandler`等はObjective-Cメソッドを公開していない。Swift ABI・コールサイトの追加解析と実機追跡が必要。
- 機能資料: https://m4fn3.github.io/repo/web/K2ge3Air_ja.html

## 完成に必要な検証

まずサイドロードしたテスト端末で起動とログインを確認し、実際の画面・クラッシュログ・フック診断を取得する必要があります。受信・既読・取消を試せる別アカウントとの実機試験が必要です。既読抑制は相手側の表示で、送信取消保持はアプリの前景／背景／再起動後を個別に検証してください。

注入処理については、暗号化IPA・二重注入・ヘッダー余白不足の拒否、および実コード領域を変えないことを自動テストします。これはLINE実行時の試験とは別です。
