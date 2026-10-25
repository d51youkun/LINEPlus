// Native grouped settings, presented through the host application's UIKit stack.
static char TabStateKey;
static UITabBarController *HostTabs(void) {
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes)
        if ([scene isKindOfClass:UIWindowScene.class] && scene.activationState == UISceneActivationStateForegroundActive)
            for (UIWindow *window in ((UIWindowScene *)scene).windows)
                if (window.isKeyWindow) return FindTabs(window.rootViewController);
    return nil;
}
static NSMutableDictionary *TabState(UITabBarController *host) {
    NSMutableDictionary *state = objc_getAssociatedObject(host, &TabStateKey);
    BOOL modern = host.tabs.count > 0;
    NSArray *live = modern ? (NSArray *)host.tabs : (NSArray *)host.viewControllers;
    // Re-capture if LINE replaces its tab models after login or account changes.
    if (state && live.count) {
        NSArray *known = state[@"items"];
        for (id item in live) if (![known containsObject:item]) { state = nil; break; }
    }
    if (!state) {
        NSMutableArray *rows = [NSMutableArray new];
        NSUInteger index = 0;
        for (id item in live) {
            NSString *identifier, *title; UIImage *image;
            if (modern) { UITab *tab = item; identifier = tab.identifier; title = tab.title; image = tab.image; }
            else { UIViewController *vc = item; identifier = [NSString stringWithFormat:@"legacy.%@.%lu", NSStringFromClass(vc.class), (unsigned long)index]; title = vc.tabBarItem.title; image = vc.tabBarItem.image; }
            [rows addObject:@{@"id":identifier ?: @"", @"title":title ?: @"", @"image":image ?: NSNull.null, @"item":item}];
            index++;
        }
        state = [@{@"modern":@(modern), @"items":live ?: @[], @"rows":rows, @"tint":host.tabBar.tintColor ?: NSNull.null} mutableCopy];
        objc_setAssociatedObject(host, &TabStateKey, state, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return state;
}
static NSArray *OrderedRows(NSDictionary *state) {
    NSArray *order = [Prefs stringArrayForKey:@"tabOrder"] ?: @[];
    return [state[@"rows"] sortedArrayUsingComparator:^NSComparisonResult(NSDictionary *a, NSDictionary *b) {
        NSUInteger ai = [order indexOfObject:a[@"id"]], bi = [order indexOfObject:b[@"id"]];
        if (ai == bi) return NSOrderedSame;
        return ai < bi ? NSOrderedAscending : NSOrderedDescending;
    }];
}
static void LPApplyTabs(UITabBarController *host) {
    NSMutableDictionary *state = TabState(host);
    NSArray *rows = OrderedRows(state);
    if (!rows.count) return;
    NSArray *hidden = [Prefs stringArrayForKey:@"hiddenTabs"] ?: @[];
    NSDictionary *titles = [Prefs dictionaryForKey:@"tabTitles"] ?: @{};
    NSDictionary *symbols = [Prefs dictionaryForKey:@"tabSymbols"] ?: @{};
    NSUInteger visible = 0;
    for (NSDictionary *row in rows) if (![hidden containsObject:row[@"id"]]) visible++;
    NSMutableArray *items = [NSMutableArray new];
    for (NSDictionary *row in rows) {
        NSString *key = row[@"id"], *title = titles[key] ?: row[@"title"];
        UIImage *image = row[@"image"] == NSNull.null ? nil : row[@"image"];
        if ([symbols[key] length]) image = [UIImage systemImageNamed:symbols[key]] ?: image;
        BOOL hide = visible > 0 && [hidden containsObject:key];
        if ([state[@"modern"] boolValue]) {
            UITab *tab = row[@"item"];
            if (![tab.title isEqualToString:title]) tab.title = title;
            if (![tab.image isEqual:image] && tab.image != image) tab.image = image;
            if (tab.hidden != hide) tab.hidden = hide;
            [items addObject:tab];
        } else {
            UIViewController *vc = row[@"item"];
            vc.tabBarItem.title = title; vc.tabBarItem.image = image;
            if (!hide) [items addObject:vc];
        }
    }
    if ([state[@"modern"] boolValue]) {
        if (![host.tabs isEqualToArray:items]) host.tabs = items;
        if (host.selectedTab.hidden) for (UITab *tab in items) if (!tab.hidden) { host.selectedTab = tab; break; }
    } else if (![host.viewControllers isEqualToArray:items]) {
        UIViewController *selected = host.selectedViewController;
        [host setViewControllers:items animated:NO];
        if ([items containsObject:selected]) host.selectedViewController = selected;
    }
    host.tabBar.tintColor = Enabled(@"blueTabs") ? UIColor.systemBlueColor : (state[@"tint"] == NSNull.null ? nil : state[@"tint"]);
}

@interface LPIcons : UITableViewController
@property NSArray<NSString *> *names;
@property NSDictionary *metadata;
@property BOOL changing;
@end

static void CollectStickerImages(UIView *view, BOOL sticker, NSMutableArray *images) {
    NSString *name = NSStringFromClass(view.class);
    sticker |= [name containsString:@"StickerMessage"];
    if (view.hidden || view.alpha < 0.01) return;
    if (sticker && [view isKindOfClass:UIImageView.class] && ((UIImageView *)view).image) [images addObject:view];
    for (UIView *child in view.subviews) CollectStickerImages(child, sticker, images);
}
static NSData *GIFForImage(UIImage *image) {
    if (!image) return nil;
    id raw = Get(image, @"animatedImageData");
    if (![raw isKindOfClass:NSData.class]) raw = Get(image, @"sd_imageData");
    CGImageSourceRef source = NULL;
    if ([raw isKindOfClass:NSData.class]) source = CGImageSourceCreateWithData((__bridge CFDataRef)raw, NULL);
    NSArray<UIImage *> *frames = image.images;
    size_t count = source ? CGImageSourceGetCount(source) : (frames.count ?: 1);
    if (count == 0) { if (source) CFRelease(source); return nil; }
    NSMutableData *output = [NSMutableData data];
    CGImageDestinationRef destination = CGImageDestinationCreateWithData((__bridge CFMutableDataRef)output, CFSTR("com.compuserve.gif"), count, NULL);
    if (!destination) { if (source) CFRelease(source); return nil; }
    NSDictionary *container = @{(__bridge NSString *)kCGImagePropertyGIFDictionary:@{(__bridge NSString *)kCGImagePropertyGIFLoopCount:@0}};
    CGImageDestinationSetProperties(destination, (__bridge CFDictionaryRef)container);
    BOOL valid = YES;
    for (size_t i = 0; i < count; i++) {
        CGImageRef frame = source ? CGImageSourceCreateImageAtIndex(source, i, NULL) : (frames.count ? frames[i].CGImage : image.CGImage);
        if (!frame) { valid = NO; break; }
        double delay = image.duration > 0 ? image.duration / count : 0.1;
        if (source) {
            CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, i, NULL);
            NSDictionary *props = CFBridgingRelease(properties);
            NSDictionary *gif = props[(__bridge NSString *)kCGImagePropertyGIFDictionary];
            NSNumber *stored = gif[(__bridge NSString *)kCGImagePropertyGIFUnclampedDelayTime] ?: gif[(__bridge NSString *)kCGImagePropertyGIFDelayTime];
            if (stored.doubleValue > 0) delay = stored.doubleValue;
        }
        NSDictionary *properties = @{(__bridge NSString *)kCGImagePropertyGIFDictionary:@{(__bridge NSString *)kCGImagePropertyGIFDelayTime:@(delay)}};
        CGImageDestinationAddImage(destination, frame, (__bridge CFDictionaryRef)properties);
        if (source) CGImageRelease(frame);
    }
    BOOL done = valid && CGImageDestinationFinalize(destination);
    CFRelease(destination); if (source) CFRelease(source);
    return done ? output : nil;
}
@implementation LPController (StickerCopy)
- (void)copySticker {
    UIViewController *vc = Top();
    if (!vc) return;
    NSMutableArray<UIImageView *> *images = [NSMutableArray new];
    CollectStickerImages(vc.view, NO, images);
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"スタンプをGIFコピー" message:images.count ? @"画面上の画像を上から順に表示しています。静止画は1フレームのGIFになります。" : @"画像を取得できませんでした。スタンプを表示したトークから開いてください。独自描画のスタンプには未対応です。" preferredStyle:UIAlertControllerStyleAlert];
    NSUInteger index = 0;
    for (UIImageView *imageView in images) {
        UIImage *image = imageView.image; index++;
        NSString *title = [NSString stringWithFormat:@"画像 %lu（%.0f × %.0f）", (unsigned long)index, image.size.width, image.size.height];
        [a addAction:[UIAlertAction actionWithTitle:title style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction *x) {
            // Encode off the UI thread; only access the captured image, not host views.
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
                NSData *gif = GIFForImage(image);
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (gif) UIPasteboard.generalPasteboard.items = @[@{@"com.compuserve.gif":gif}];
                    UIAlertController *result = [UIAlertController alertControllerWithTitle:gif ? @"GIFをコピーしました" : @"GIFに変換できませんでした" message:gif ? @"画像としてペーストできます。スタンプの購入状態を変更する処理ではありません。" : @"この画像形式には対応していません。" preferredStyle:UIAlertControllerStyleAlert];
                    [result addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
                    [Top() presentViewController:result animated:YES completion:nil];
                });
            });
        }]];
    }
    [a addAction:[UIAlertAction actionWithTitle:@"閉じる" style:UIAlertActionStyleCancel handler:nil]];
    [vc presentViewController:a animated:YES completion:nil];
}
@end
@implementation LPIcons
- (void)viewDidLoad {
    [super viewDidLoad]; self.title = @"アプリアイコン";
    self.metadata = [NSBundle.mainBundle infoDictionary][@"CFBundleIcons"][@"CFBundleAlternateIcons"] ?: @{};
    self.names = [@[@""] arrayByAddingObjectsFromArray:[self.metadata.allKeys sortedArrayUsingSelector:@selector(localizedStandardCompare:)]];
}
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section { return self.names.count; }
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    return @"IPA内の全代替アイコンを、LINEPlusから会員判定なしで選択できます。ダウンロード限定の未同梱アイコンは対象外です。変更はiOSの標準APIで実行し、失敗時はエラーを表示します。";
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    NSString *name = self.names[path.row];
    cell.textLabel.text = name.length ? [name stringByReplacingOccurrencesOfString:@"_" withString:@" "] : @"標準アイコン";
    cell.detailTextLabel.text = name;
    NSString *asset = self.metadata[name][@"CFBundleIconName"];
    cell.imageView.image = asset ? [UIImage imageNamed:asset] : nil;
    if (!cell.imageView.image) cell.imageView.image = [UIImage systemImageNamed:@"app.dashed"];
    cell.imageView.preferredSymbolConfiguration = [UIImageSymbolConfiguration configurationWithPointSize:32];
    if ([(UIApplication.sharedApplication.alternateIconName ?: @"") isEqualToString:name]) cell.accessoryType = UITableViewCellAccessoryCheckmark;
    return cell;
}
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
    [tableView deselectRowAtIndexPath:path animated:YES];
    if (self.changing) return;
    UIApplication *app = UIApplication.sharedApplication;
    if (!app.supportsAlternateIcons) { [self error:@"この環境では代替アイコンがサポートされていません。"]; return; }
    NSString *name = self.names[path.row];
    self.changing = YES;
    [app setAlternateIconName:name.length ? name : nil completionHandler:^(NSError *error) {
        dispatch_async(dispatch_get_main_queue(), ^{ self.changing = NO; [self.tableView reloadData]; if (error) [self error:error.localizedDescription]; });
    }];
}
- (void)error:(NSString *)message {
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"アイコンを変更できませんでした" message:message preferredStyle:UIAlertControllerStyleAlert];
    [a addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
    [self presentViewController:a animated:YES completion:nil];
}
@end

@interface LPTabs : UITableViewController
@property (weak) UITabBarController *host;
@property NSMutableArray<NSDictionary *> *rows;
@end
@implementation LPTabs
- (void)viewDidLoad {
    [super viewDidLoad]; self.title = @"タブバーの管理";
    self.host = HostTabs();
    self.rows = self.host ? [OrderedRows(TabState(self.host)) mutableCopy] : [NSMutableArray new];
    self.navigationItem.rightBarButtonItem = self.editButtonItem;
    self.navigationItem.leftItemsSupplementBackButton = YES;
    self.navigationItem.leftBarButtonItem = [[UIBarButtonItem alloc] initWithTitle:@"リセット" style:UIBarButtonItemStylePlain target:self action:@selector(reset)];
}
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    return self.rows.count ? @"スイッチで表示切替、行をタップして名前・シンボルを変更。「編集」で並べ替え。少なくとも1つのタブを残します。" : @"LINEのホーム画面を開いてから再度お試しください。";
}
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section { return self.rows.count; }
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    NSDictionary *row = self.rows[path.row]; NSString *key = row[@"id"];
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.textLabel.text = [Prefs dictionaryForKey:@"tabTitles"][key] ?: row[@"title"];
    cell.detailTextLabel.text = @"タップして名前とアイコンを編集";
    UISwitch *toggle = [UISwitch new]; toggle.accessibilityIdentifier = key;
    toggle.on = ![[Prefs stringArrayForKey:@"hiddenTabs"] containsObject:key];
    [toggle addTarget:self action:@selector(toggle:) forControlEvents:UIControlEventValueChanged]; cell.accessoryView = toggle;
    return cell;
}
- (void)toggle:(UISwitch *)sender {
    NSMutableArray *hidden = [[Prefs stringArrayForKey:@"hiddenTabs"] mutableCopy] ?: [NSMutableArray new];
    if (sender.on) [hidden removeObject:sender.accessibilityIdentifier];
    else {
        NSUInteger visible = 0;
        for (NSDictionary *row in self.rows) if (![hidden containsObject:row[@"id"]]) visible++;
        if (visible <= 1) { sender.on = YES; return; }
        if (![hidden containsObject:sender.accessibilityIdentifier]) [hidden addObject:sender.accessibilityIdentifier];
    }
    [Prefs setObject:hidden forKey:@"hiddenTabs"]; [[LPController shared] refresh];
}
- (BOOL)tableView:(UITableView *)tableView canMoveRowAtIndexPath:(NSIndexPath *)path { return YES; }
- (UITableViewCellEditingStyle)tableView:(UITableView *)tableView editingStyleForRowAtIndexPath:(NSIndexPath *)path { return UITableViewCellEditingStyleNone; }
- (BOOL)tableView:(UITableView *)tableView shouldIndentWhileEditingRowAtIndexPath:(NSIndexPath *)path { return NO; }
- (void)tableView:(UITableView *)tableView moveRowAtIndexPath:(NSIndexPath *)from toIndexPath:(NSIndexPath *)to {
    NSDictionary *row = self.rows[from.row]; [self.rows removeObjectAtIndex:from.row]; [self.rows insertObject:row atIndex:to.row];
    [Prefs setObject:[self.rows valueForKey:@"id"] forKey:@"tabOrder"]; [[LPController shared] refresh];
}
- (void)reset {
    if (!self.host) return;
    for (NSString *key in @[@"hiddenTabs", @"tabOrder", @"tabTitles", @"tabSymbols"]) [Prefs removeObjectForKey:key];
    self.rows = [OrderedRows(TabState(self.host)) mutableCopy];
    [[LPController shared] refresh]; [self.tableView reloadData];
}
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
    [tableView deselectRowAtIndexPath:path animated:YES]; NSDictionary *row = self.rows[path.row]; NSString *key = row[@"id"];
    UIAlertController *a = [UIAlertController alertControllerWithTitle:row[@"title"] message:@"空欄で標準に戻ります。シンボル例：house、message、play.rectangle、wallet.pass、person" preferredStyle:UIAlertControllerStyleAlert];
    [a addTextFieldWithConfigurationHandler:^(UITextField *f) { f.placeholder = @"表示名"; f.text = [Prefs dictionaryForKey:@"tabTitles"][key]; }];
    [a addTextFieldWithConfigurationHandler:^(UITextField *f) { f.placeholder = @"SF Symbols名"; f.autocapitalizationType = UITextAutocapitalizationTypeNone; f.text = [Prefs dictionaryForKey:@"tabSymbols"][key]; }];
    [a addAction:[UIAlertAction actionWithTitle:@"保存" style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction *x) {
        NSString *symbol = a.textFields[1].text;
        if (symbol.length && ![UIImage systemImageNamed:symbol]) {
            UIAlertController *error = [UIAlertController alertControllerWithTitle:@"シンボルが見つかりません" message:symbol preferredStyle:UIAlertControllerStyleAlert];
            [error addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
            [self presentViewController:error animated:YES completion:nil]; return;
        }
        NSArray *keys = @[@"tabTitles", @"tabSymbols"];
        for (NSUInteger i = 0; i < 2; i++) {
            NSMutableDictionary *values = [[Prefs dictionaryForKey:keys[i]] mutableCopy] ?: [NSMutableDictionary new];
            NSString *value = a.textFields[i].text; if (value.length) values[key] = value; else [values removeObjectForKey:key];
            [Prefs setObject:values forKey:keys[i]];
        }
        [[LPController shared] refresh]; [self.tableView reloadData];
    }]];
    [a addAction:[UIAlertAction actionWithTitle:@"キャンセル" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:a animated:YES completion:nil];
}
@end

@interface LPSettings : LPBaseSettings
@property NSString *page;
@end
@implementation LPSettings
- (void)viewDidLoad {
    [super viewDidLoad]; self.view.tintColor = UIColor.systemGreenColor;
    self.navigationController.navigationBar.prefersLargeTitles = YES;
    NSString *page = self.page ?: @"root";
    if ([page isEqual:@"root"]) {
        self.title = @"LINEPlus";
        self.groups = @[
            @[Option(@"read", @"既読の管理", @"既読通知・トーク別の例外"), Option(@"unsend", @"送信取消の管理", @"取消保持・通知なし取消の対応状況"), Option(@"ads", @"広告等の削除", @"広告・ホームのおすすめ"), Option(@"themes", @"着せ替えの管理", @"対応状況")],
            @[Option(@"icons", @"アプリアイコン", @"同梱された118種類＋標準から選択"), Option(@"tabs", @"タブバーの管理", @"表示・並び順・名前・アイコン"), Option(@"chat", @"トークの表示名を設定", @"自分だけの名前・識別子のコピー")],
            @[Option(@"confirm", @"操作確認の追加", @"送信・通話・ビデオ開始"), Option(@"premium", @"LYPプレミアム関連", @"メッセージ編集・通知なし取消の調査状況"), Option(@"other", @"その他・診断", @"フック診断・互換性")]
        ];
    } else if ([page isEqual:@"read"]) {
        self.title = @"既読の管理";
        self.groups = @[@[Option(@"hideRead", @"既読通知を抑制", @"試験中：送信可否判定を抑制。全経路の実機確認待ち。"), Option(@"chat", @"トーク別の例外", @"トーク画面から開いて設定します。")]];
    } else if ([page isEqual:@"ads"]) {
        self.title = @"広告等の削除";
        self.groups = @[@[Option(@"hideAds", @"広告ビューを隠す", @"特定済みの9種類が対象。余白が残る場合があります。"), Option(@"hideRecommendations", @"おすすめを隠す", @"旧ホームのおすすめ欄が対象。")]];
    } else if ([page isEqual:@"confirm"]) {
        self.title = @"操作確認の追加";
        self.groups = @[@[Option(@"confirmSend", @"送信前に確認", @"従来の入力バー経由が対象。V2・メディアは未対応。"), Option(@"minimum", @"送信確認の最低人数", @"0なら全トーク"), Option(@"confirmCall", @"通話前に確認", @"通話履歴からの開始が対象。"), Option(@"confirmVideo", @"ビデオ開始前に確認", @"音声通話画面からの開始が対象。"), Option(@"confirmDisconnect", @"切断前に確認", @"NCXBizCall画面のdisconnect経由が対象。他の通話画面は未対応。")]];
    } else {
        self.title = @"その他・診断";
        self.groups = @[@[Option(@"markdown", @"Markdownビューアーを有効化", @"LINE内蔵のMarkdown機能フラグを有効にします。適用画面は実機確認待ち。"), Option(@"copySticker", @"表示中のスタンプをGIFコピー", @"トーク上のスタンプ画像を選び、GIF画像としてコピー。"), Option(@"blueTabs", @"タブの選択色をブルーに", @"オフではLINE標準の色を維持します。"), Option(@"status", @"フックの診断", @"設置結果を表示"), Option(@"pending", @"未対応の機能", @"今後の実装項目")]];
    }
    self.tableView.estimatedRowHeight = 72;
}
- (BOOL)isToggle:(NSString *)key { return [@[@"hideRead", @"hideAds", @"hideRecommendations", @"confirmSend", @"confirmCall", @"confirmVideo", @"confirmDisconnect", @"markdown", @"blueTabs"] containsObject:key]; }
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    if (!self.page) return @[@"プライバシーと表示", @"カスタマイズ", @"詳細設定"][section];
    return @"設定";
}
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    if (section == (NSInteger)self.groups.count - 1) return @"LINEPlus 0.3 • LINE 26.16.0\n開発プレビュー：実機未検証。項目ごとの対応範囲をご確認ください。";
    return nil;
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    NSDictionary *row = self.groups[path.section][path.row]; NSString *key = row[@"key"];
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.textLabel.text = row[@"title"]; cell.detailTextLabel.text = row[@"detail"]; cell.detailTextLabel.numberOfLines = 0;
    NSDictionary *symbols = @{@"read":@"eyeglasses", @"unsend":@"arrow.uturn.backward", @"ads":@"eraser", @"themes":@"paintbrush", @"icons":@"app.badge", @"tabs":@"rectangle.split.3x1", @"chat":@"square.and.pencil", @"confirm":@"checkmark.shield", @"premium":@"sparkles", @"other":@"ellipsis.circle"};
    cell.imageView.image = [UIImage systemImageNamed:symbols[key] ?: @"gearshape"];
    cell.imageView.tintColor = UIColor.systemGreenColor;
    if ([self isToggle:key]) {
        UISwitch *toggle = [UISwitch new]; toggle.accessibilityIdentifier = key; toggle.on = Enabled(key);
        [toggle addTarget:self action:@selector(toggle:) forControlEvents:UIControlEventValueChanged]; cell.accessoryView = toggle;
    } else cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    return cell;
}
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
    NSString *key = self.groups[path.section][path.row][@"key"];
    [tableView deselectRowAtIndexPath:path animated:YES];
    if ([self isToggle:key]) return;
    if ([key isEqual:@"copySticker"]) {
        [self dismissViewControllerAnimated:YES completion:^{ [[LPController shared] copySticker]; }]; return;
    }
    UIViewController *next;
    if ([key isEqual:@"icons"]) next = [[LPIcons alloc] initWithStyle:UITableViewStyleInsetGrouped];
    else if ([key isEqual:@"tabs"]) next = [[LPTabs alloc] initWithStyle:UITableViewStyleInsetGrouped];
    else if ([@[@"read", @"ads", @"confirm", @"other"] containsObject:key]) {
        LPSettings *settings = [[LPSettings alloc] initWithStyle:UITableViewStyleInsetGrouped]; settings.page = key; next = settings;
    } else if ([key isEqual:@"unsend"] || [key isEqual:@"themes"] || [key isEqual:@"premium"]) {
        [self showText:self.groups[path.section][path.row][@"title"] text:[key isEqual:@"themes"] ? @"着せ替えの適用・管理は未実装です。" : @"この項目は調査中です。送信取消保持・メッセージ編集・通知なし取消はまだ実装していません。\n\n26.16.0には編集と通知なし取消のリクエストモデルが存在します。実際の利用条件と相手側への反映を確認する必要があります。\n\n実装例の投稿URL・作者名・対象LINE版があると追加調査できます。"];
        return;
    } else { [super tableView:tableView didSelectRowAtIndexPath:path]; return; }
    [self.navigationController pushViewController:next animated:YES];
}
@end
