#import <UIKit/UIKit.h>
#import <objc/runtime.h>
#import <objc/message.h>
#import <ImageIO/ImageIO.h>

// Independent implementation. No K2ge3Air code or license mechanism is used.
static NSUserDefaults *Prefs;
static NSMutableArray<NSDictionary *> *Hooks;
static char ButtonKey, HiddenKey;
static BOOL Enabled(NSString *key) { return [Prefs boolForKey:key]; }
static id Get(id object, NSString *selector) {
    SEL sel = NSSelectorFromString(selector);
    if (![object respondsToSelector:sel]) return nil;
    NSMethodSignature *signature = [object methodSignatureForSelector:sel];
    if (signature.numberOfArguments != 2 || signature.methodReturnType[0] != '@') return nil;
    return ((id (*)(id, SEL))objc_msgSend)(object, sel);
}
static NSString *Identifier(id object) {
    id value = Get(object, @"midString") ?: Get(object, @"sourceMID");
    if ([value isKindOfClass:NSString.class]) return value;
    // Core Data identifiers are local stable keys, never sent to a server.
    id oid = Get(object, @"objectID");
    return [Get(oid, @"URIRepresentation") absoluteString];
}
static NSString *Alias(id object) { return [Prefs dictionaryForKey:@"aliases"][Identifier(object) ?: @""]; }
static UIViewController *Top(void) {
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class] || scene.activationState != UISceneActivationStateForegroundActive) continue;
        for (UIWindow *window in ((UIWindowScene *)scene).windows) {
            if (!window.isKeyWindow) continue;
            UIViewController *vc = window.rootViewController;
            while (YES) {
                if (vc.presentedViewController) vc = vc.presentedViewController;
                else if ([vc isKindOfClass:UINavigationController.class]) vc = ((UINavigationController *)vc).visibleViewController;
                else if ([vc isKindOfClass:UITabBarController.class]) vc = ((UITabBarController *)vc).selectedViewController;
                else break;
            }
            return vc;
        }
    }
    return nil;
}
static BOOL Install(NSString *name, NSString *selector, IMP imp, const char *resultType, unsigned args, IMP *original) {
    Class cls = NSClassFromString(name);
    SEL sel = NSSelectorFromString(selector);
    Method method = class_getInstanceMethod(cls, sel);
    BOOL good = method && method_getNumberOfArguments(method) == args;
    char result[256] = {0};
    if (method) method_getReturnType(method, result, sizeof(result));
    good &= result[0] == resultType[0];
    // All argument-bearing hooks in this build take Objective-C objects only.
    for (unsigned i = 2; good && i < args; i++) {
        char type[256] = {0};
        method_getArgumentType(method, i, type, sizeof(type));
        good &= type[0] == '@';
    }
    if (good) {
        *original = method_getImplementation(method);
        if (!class_addMethod(cls, sel, imp, method_getTypeEncoding(method))) class_replaceMethod(cls, sel, imp, method_getTypeEncoding(method));
    }
    [Hooks addObject:@{@"class": name, @"selector": selector, @"installed": @(good)}];
    return good;
}

static IMP ReadChat, ReadSquare, NameUser, ChatSend, StartVideo, StartCall, Markdown, Disconnect;
static IMP ChatTitle, SquareTitle;
static BOOL ReadAllowed(id self, SEL cmd, IMP original) {
    NSString *key = Identifier(self);
    BOOL excepted = key && [[Prefs stringArrayForKey:@"readExceptions"] containsObject:key];
    if (Enabled(@"hideRead") && !excepted) return NO;
    return ((BOOL (*)(id, SEL))original)(self, cmd);
}
static BOOL ReadChatHook(id self, SEL cmd) { return ReadAllowed(self, cmd, ReadChat); }
static BOOL ReadSquareHook(id self, SEL cmd) { return ReadAllowed(self, cmd, ReadSquare); }
static id UserNameHook(id self, SEL cmd) { return Alias(self) ?: ((id (*)(id, SEL))NameUser)(self, cmd); }
static id ChatTitleHook(id self, SEL cmd, BOOL count) { return Alias(self) ?: ((id (*)(id, SEL, BOOL))ChatTitle)(self, cmd, count); }
static id SquareTitleHook(id self, SEL cmd, BOOL count) { return Alias(self) ?: ((id (*)(id, SEL, BOOL))SquareTitle)(self, cmd, count); }

static void Confirm(NSString *title, void (^action)(void)) {
    UIViewController *vc = Top();
    if (!vc || [vc isKindOfClass:UIAlertController.class]) return;
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"LINEPlus" message:title preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"キャンセル" style:UIAlertActionStyleCancel handler:nil]];
    [alert addAction:[UIAlertAction actionWithTitle:@"続ける" style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction *a) { action(); }]];
    [vc presentViewController:alert animated:YES completion:nil];
}
static void SendHook(id self, SEL cmd, id sender) {
    void (^send)(void) = ^{ ((void (*)(id, SEL, id))ChatSend)(self, cmd, sender); };
    if (!Enabled(@"confirmSend")) { send(); return; }
    NSInteger minimum = [Prefs integerForKey:@"confirmMinimum"];
    if (minimum > 0) {
        id chat = Get(Top(), @"chat");
        SEL members = NSSelectorFromString(@"memberCountExcludingMyself");
        if ([chat respondsToSelector:members] && ((NSInteger (*)(id, SEL))objc_msgSend)(chat, members) + 1 < minimum) { send(); return; }
    }
    Confirm(@"メッセージを送信しますか？", send);
}
static void VideoHook(id self, SEL cmd, id sender) {
    void (^action)(void) = ^{ ((void (*)(id, SEL, id))StartVideo)(self, cmd, sender); };
    if (Enabled(@"confirmVideo")) Confirm(@"ビデオ通話を開始しますか？", action); else action();
}
static void CallHook(id self, SEL cmd) {
    void (^action)(void) = ^{ ((void (*)(id, SEL))StartCall)(self, cmd); };
    if (Enabled(@"confirmCall")) Confirm(@"通話を開始しますか？", action); else action();
}
static BOOL MarkdownHook(id self, SEL cmd) {
    return Enabled(@"markdown") || ((BOOL (*)(id, SEL))Markdown)(self, cmd);
}
static void DisconnectHook(id self, SEL cmd, id sender, id event) {
    void (^action)(void) = ^{ ((void (*)(id, SEL, id, id))Disconnect)(self, cmd, sender, event); };
    if (Enabled(@"confirmDisconnect")) Confirm(@"通話を切断しますか？", action); else action();
}

static void Visibility(UIView *view, BOOL hide) {
    NSNumber *previous = objc_getAssociatedObject(view, &HiddenKey);
    if (hide) {
        if (!previous) objc_setAssociatedObject(view, &HiddenKey, @(view.hidden), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        view.hidden = YES;
    } else if (previous) {
        view.hidden = previous.boolValue;
        objc_setAssociatedObject(view, &HiddenKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
}
static NSSet *AdClasses;
static UITabBarController *FindTabs(UIViewController *vc) {
    if ([vc isKindOfClass:UITabBarController.class]) return (id)vc;
    for (UIViewController *child in vc.childViewControllers) {
        UITabBarController *found = FindTabs(child);
        if (found) return found;
    }
    return nil;
}
static void UpdateViews(UIView *view) {
    NSString *name = NSStringFromClass(view.class);
    BOOL candidate = [AdClasses containsObject:name];
    if (candidate) Visibility(view, Enabled(@"hideAds"));
    if ([name isEqualToString:@"_TtC11LineHomeTab25ContentRecommendationView"])
        Visibility(view, Enabled(@"hideRecommendations"));
    for (UIView *child in view.subviews) UpdateViews(child);
}

@interface LPBaseSettings : UITableViewController
@property NSArray<NSArray<NSDictionary *> *> *groups;
- (void)showText:(NSString *)title text:(NSString *)text;
@end
@interface LPController : NSObject
+ (instancetype)shared;
- (void)openSettings;
- (void)chatMenu;
- (void)refresh;
@end
@interface LPController (StickerCopy)
- (void)copySticker;
@end

static NSDictionary *Option(NSString *key, NSString *title, NSString *detail) {
    return @{@"key":key, @"title":title, @"detail":detail};
}
@implementation LPBaseSettings
- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"LINEPlus";
    self.navigationItem.rightBarButtonItem = [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemDone target:self action:@selector(close)];
    self.groups = @[
        @[Option(@"hideRead", @"既読通知を抑制（試験中）", @"送信可否判定をフック。既読が付かない保証は実機検証後。"),
          Option(@"hideAds", @"広告ビューを隠す", @"解析で特定した広告ビューが対象。余白が残る場合があります。"),
          Option(@"hideRecommendations", @"ホームのおすすめを隠す", @"旧ホームのContentRecommendationViewが対象。")],
        @[Option(@"confirmSend", @"送信前に確認", @"従来のChatInputBar経由のみ。V2やメディア送信は未対応。"),
          Option(@"confirmCall", @"通話前に確認", @"通話履歴セルのstartCall経由が対象。"),
          Option(@"confirmVideo", @"ビデオ通話前に確認", @"音声通話画面のstartVideo経由が対象。")],
        @[Option(@"tabs", @"タブの管理", @"表示中のタブを選んで表示／非表示を切り替え。"),
          Option(@"minimum", @"送信確認の最低人数", @"0なら全トーク。人数を取得できない場合も確認。"),
          Option(@"chat", @"現在のトークの操作", @"ローカル表示名・既読例外・識別子のコピー。")],
        @[Option(@"status", @"フックの診断", @"端末で実際にフックが見つかったか確認できます。"),
          Option(@"pending", @"未対応の機能", @"送信取消保持・既読ユーザー・iPadログインなど。")]
    ];
    self.tableView.rowHeight = UITableViewAutomaticDimension;
    self.tableView.estimatedRowHeight = 85;
}
- (void)close { [self dismissViewControllerAnimated:YES completion:nil]; }
- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView { return self.groups.count; }
- (NSInteger)tableView:(UITableView *)tableView numberOfRowsInSection:(NSInteger)section { return self.groups[section].count; }
- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section {
    return @[@"表示とプライバシー", @"操作確認", @"カスタマイズ", @"互換性・開発状況"][section];
}
- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section {
    return section == 3 ? @"LINE 26.16.0向け独立実装・開発プレビュー。全機能対応版ではありません。LPボタン長押しで現在のトークを操作できます。" : nil;
}
- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)path {
    NSDictionary *row = self.groups[path.section][path.row];
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleSubtitle reuseIdentifier:nil];
    cell.textLabel.text = row[@"title"];
    cell.detailTextLabel.text = row[@"detail"];
    cell.detailTextLabel.numberOfLines = 0;
    if (path.section < 2) {
        UISwitch *toggle = [UISwitch new];
        toggle.accessibilityIdentifier = row[@"key"];
        toggle.on = Enabled(row[@"key"]);
        [toggle addTarget:self action:@selector(toggle:) forControlEvents:UIControlEventValueChanged];
        cell.accessoryView = toggle;
    } else cell.accessoryType = UITableViewCellAccessoryDisclosureIndicator;
    return cell;
}
- (void)toggle:(UISwitch *)sender {
    [Prefs setBool:sender.on forKey:sender.accessibilityIdentifier];
    [[LPController shared] refresh];
}
- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)path {
    [tableView deselectRowAtIndexPath:path animated:YES];
    NSString *key = self.groups[path.section][path.row][@"key"];
    if ([key isEqualToString:@"chat"]) {
        [self dismissViewControllerAnimated:YES completion:^{ [[LPController shared] chatMenu]; }];
        return;
    }
    if ([key isEqualToString:@"minimum"]) {
        UIAlertController *a = [UIAlertController alertControllerWithTitle:@"送信確認の最低人数" message:@"0 = 全トーク" preferredStyle:UIAlertControllerStyleAlert];
        [a addTextFieldWithConfigurationHandler:^(UITextField *f) { f.keyboardType = UIKeyboardTypeNumberPad; f.text = [@([Prefs integerForKey:@"confirmMinimum"]) stringValue]; }];
        [a addAction:[UIAlertAction actionWithTitle:@"保存" style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction *x) { [Prefs setInteger:MAX(0, a.textFields.firstObject.text.integerValue) forKey:@"confirmMinimum"]; }]];
        [a addAction:[UIAlertAction actionWithTitle:@"キャンセル" style:UIAlertActionStyleCancel handler:nil]];
        [self presentViewController:a animated:YES completion:nil];
        return;
    }
    if ([key isEqualToString:@"tabs"]) {
        UIViewController *presenter = self.presentingViewController;
        while (presenter.presentingViewController) presenter = presenter.presentingViewController;
        UITabBarController *tabs = [presenter isKindOfClass:UITabBarController.class] ? (id)presenter : presenter.tabBarController;
        if (!tabs) tabs = FindTabs(presenter);
        if (!tabs) { [self showText:@"タブの管理" text:@"タブ画面を開いてから再実行してください。"]; return; }
        UIAlertController *a = [UIAlertController alertControllerWithTitle:@"タブの管理" message:@"再表示するには再度この画面を開いてください。" preferredStyle:UIAlertControllerStyleAlert];
        NSMutableArray *hidden = [[Prefs stringArrayForKey:@"hiddenTabs"] mutableCopy] ?: [NSMutableArray new];
        for (UITab *tab in tabs.tabs) {
            NSString *identifier = tab.identifier;
            [a addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"%@ %@", [hidden containsObject:identifier] ? @"○" : @"✓", tab.title] style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction *x) {
                if ([hidden containsObject:identifier]) [hidden removeObject:identifier];
                else {
                    NSUInteger visible = 0;
                    for (UITab *item in tabs.tabs) if (![hidden containsObject:item.identifier]) visible++;
                    if (visible <= 1) return;
                    [hidden addObject:identifier];
                }
                [Prefs setObject:hidden forKey:@"hiddenTabs"];
                [[LPController shared] refresh];
            }]];
        }
        [a addAction:[UIAlertAction actionWithTitle:@"閉じる" style:UIAlertActionStyleCancel handler:nil]];
        [self presentViewController:a animated:YES completion:nil];
        return;
    }
    if ([key isEqualToString:@"status"]) {
        NSMutableString *text = [NSMutableString stringWithFormat:@"LINE %@\n", [NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"]];
        for (NSDictionary *hook in Hooks) [text appendFormat:@"%@ %@ %@\n", [hook[@"installed"] boolValue] ? @"✓" : @"×", hook[@"class"], hook[@"selector"]];
        [self showText:@"フック診断（動作保証ではありません）" text:text];
    }
    if ([key isEqualToString:@"pending"]) [self showText:@"未対応・実装待ち" text:@"・iPhoneでサブ端末ログイン\n・既読ユーザー記録／表示\n・送信時のみ既読を送信\n・送信取消メッセージ保持（バックグラウンド含む）\n・着せ替え管理／ランダム適用\n・音声入力ボタン非表示\n・全経路の送信／通話／切断／カメラ確認\n・独自描画スタンプのGIFコピー\n・MIDによるユーザー検索\n・有料スタンプの無料取得／無制限使用\n・LYPの編集／通知なし取消\n\nこれらを有効に見せるスイッチは設けていません。"];
}
- (void)showText:(NSString *)title text:(NSString *)text {
    UIViewController *vc = [UIViewController new]; vc.title = title;
    UITextView *view = [UITextView new]; view.editable = NO; view.text = text;
    view.font = [UIFont systemFontOfSize:14]; view.backgroundColor = UIColor.systemBackgroundColor;
    vc.view = view; [self.navigationController pushViewController:vc animated:YES];
}
@end

#include "Settings.inc"

@implementation LPController
+ (instancetype)shared { static LPController *instance; static dispatch_once_t once; dispatch_once(&once, ^{ instance = [self new]; }); return instance; }
- (void)openSettings {
    UIViewController *vc = Top();
    if (!vc || [vc isKindOfClass:LPSettings.class] || [vc isKindOfClass:UIAlertController.class]) return;
    UINavigationController *nav = [[UINavigationController alloc] initWithRootViewController:[[LPSettings alloc] initWithStyle:UITableViewStyleInsetGrouped]];
    [vc presentViewController:nav animated:YES completion:nil];
}
- (void)chatMenu {
    UIViewController *vc = Top(); id chat = Get(vc, @"chat");
    NSString *key = Identifier(chat);
    if (!key) { Confirm(@"トーク画面を開いてからLPボタンを長押ししてください。", ^{}); return; }
    UIAlertController *a = [UIAlertController alertControllerWithTitle:@"このトーク" message:key preferredStyle:UIAlertControllerStyleAlert];
    [a addTextFieldWithConfigurationHandler:^(UITextField *f) { f.placeholder = @"自分だけの表示名（空欄で解除）"; f.text = Alias(chat); }];
    [a addAction:[UIAlertAction actionWithTitle:@"表示名を保存" style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction *x) {
        NSMutableDictionary *aliases = [[Prefs dictionaryForKey:@"aliases"] mutableCopy] ?: [NSMutableDictionary new];
        NSString *name = a.textFields.firstObject.text;
        if (name.length) aliases[key] = name; else [aliases removeObjectForKey:key];
        [Prefs setObject:aliases forKey:@"aliases"];
    }]];
    BOOL excepted = [[Prefs stringArrayForKey:@"readExceptions"] containsObject:key];
    [a addAction:[UIAlertAction actionWithTitle:excepted ? @"既読例外を解除" : @"このトークは既読を許可" style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction *x) {
        NSMutableArray *items = [[Prefs stringArrayForKey:@"readExceptions"] mutableCopy] ?: [NSMutableArray new];
        if (excepted) [items removeObject:key]; else [items addObject:key];
        [Prefs setObject:items forKey:@"readExceptions"];
    }]];
    [a addAction:[UIAlertAction actionWithTitle:@"識別子をコピー" style:UIAlertActionStyleDefault handler:^(__unused UIAlertAction *x) { UIPasteboard.generalPasteboard.string = key; }]];
    [a addAction:[UIAlertAction actionWithTitle:@"閉じる" style:UIAlertActionStyleCancel handler:nil]];
    [vc presentViewController:a animated:YES completion:nil];
}
- (void)longPress:(UILongPressGestureRecognizer *)sender { if (sender.state == UIGestureRecognizerStateBegan) [self chatMenu]; }
- (void)updateTabs:(UIViewController *)vc {
    if ([vc isKindOfClass:UITabBarController.class]) {
        LPApplyTabs((id)vc);
    }
    for (UIViewController *child in vc.childViewControllers) [self updateTabs:child];
}
- (void)refresh {
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:UIWindowScene.class] || scene.activationState != UISceneActivationStateForegroundActive) continue;
        for (UIWindow *window in ((UIWindowScene *)scene).windows) {
            if (!window.isKeyWindow) continue;
            UIButton *button = objc_getAssociatedObject(window, &ButtonKey);
            if (!button) {
                button = [UIButton buttonWithType:UIButtonTypeSystem];
                [button setTitle:@"LP" forState:UIControlStateNormal];
                button.accessibilityLabel = @"LINEPlus設定。長押しでトーク操作";
                button.backgroundColor = [UIColor.systemGreenColor colorWithAlphaComponent:0.9];
                [button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
                button.layer.cornerRadius = 19;
                [button addTarget:self action:@selector(openSettings) forControlEvents:UIControlEventTouchUpInside];
                [button addGestureRecognizer:[[UILongPressGestureRecognizer alloc] initWithTarget:self action:@selector(longPress:)]];
                [window addSubview:button];
                objc_setAssociatedObject(window, &ButtonKey, button, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
            }
            button.frame = CGRectMake(window.bounds.size.width - window.safeAreaInsets.right - 48, window.safeAreaInsets.top + 6, 38, 38);
            [window bringSubviewToFront:button];
            UpdateViews(window);
            [self updateTabs:window.rootViewController];
        }
    }
}
@end

__attribute__((constructor)) static void Start(void) {
    @autoreleasepool {
        if (![[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleShortVersionString"] isEqual:@"26.16.0"]) return;
        Prefs = [[NSUserDefaults alloc] initWithSuiteName:@"local.lineplus.preferences"];
        Hooks = [NSMutableArray new];
        Install(@"ManagedChat", @"canMarkChatAsReadToServer", (IMP)ReadChatHook, "B", 2, &ReadChat);
        Install(@"ManagedSquareChat", @"canMarkChatAsReadToServer", (IMP)ReadSquareHook, "B", 2, &ReadSquare);
        Install(@"ManagedUser", @"displayUserName", (IMP)UserNameHook, "@", 2, &NameUser);
        // BOOL arguments require explicit validation, separate from object-only Install.
        for (NSString *name in @[@"ManagedChat", @"ManagedSquareChat"]) {
            Class cls = NSClassFromString(name); SEL sel = NSSelectorFromString(@"titleWithMemberCount:");
            Method method = class_getInstanceMethod(cls, sel); char arg[16] = {0}, ret[16] = {0};
            if (method) { method_getArgumentType(method, 2, arg, sizeof(arg)); method_getReturnType(method, ret, sizeof(ret)); }
            BOOL valid = method && method_getNumberOfArguments(method) == 3 && arg[0] == 'B' && ret[0] == '@';
            if (valid) {
                BOOL normal = [name isEqual:@"ManagedChat"];
                *(normal ? &ChatTitle : &SquareTitle) = method_getImplementation(method);
                class_replaceMethod(cls, sel, normal ? (IMP)ChatTitleHook : (IMP)SquareTitleHook, method_getTypeEncoding(method));
            }
            [Hooks addObject:@{@"class":name, @"selector":@"titleWithMemberCount:", @"installed":@(valid)}];
        }
        Install(@"_TtC15LineMessagingUI12ChatInputBar", @"send:", (IMP)SendHook, "v", 3, &ChatSend);
        Install(@"_TtC11NCXFreeCall22NCFAudioViewController", @"startVideo:", (IMP)VideoHook, "v", 3, &StartVideo);
        Install(@"_TtC11LineCallTab27CallTabRecentsTableViewCell", @"startCall", (IMP)CallHook, "v", 2, &StartCall);
        Install(@"NLConfigurationManager", @"isMarkdownViewerEnabled", (IMP)MarkdownHook, "B", 2, &Markdown);
        Install(@"_TtC11NCXFreeCall24NCXBizCallViewController", @"disconnect:forEvent:", (IMP)DisconnectHook, "v", 4, &Disconnect);
        AdClasses = [NSSet setWithArray:@[
            @"_TtC11LineHomeTab24HomeTabRCAdContainerView", @"_TtC11LineHomeTab17RCAdTableViewCell",
            @"_TtC15LineMessagingUI20SquareEmbeddedAdView", @"_TtC15LineMessagingUI18SquareHeaderAdView",
            @"_TtC11LineSmartCh13SmartChAdView", @"_TtC10LineWallet12WalletAdCell",
            @"_TtC12LineTimeline10AdPostCell", @"_TtC19LineGeneralContents13GCSLGFPAdCell", @"_TtC19LineGeneralContents15GCSLGFPAdV2Cell"
        ]];
        dispatch_async(dispatch_get_main_queue(), ^{
            [NSTimer scheduledTimerWithTimeInterval:1.5 target:[LPController shared] selector:@selector(refresh) userInfo:nil repeats:YES];
        });
    }
}
