#import <AppKit/AppKit.h>
#include <SDL2/SDL.h>
#include "frontend/macos_menu.h"

@interface GbaSettingsTarget : NSObject
@property(nonatomic) uint32_t eventType;
- (void)openSettings:(id)sender;
@end

@implementation GbaSettingsTarget
- (void)openSettings:(id)sender {
    (void)sender;
    SDL_Event e;
    SDL_zero(e);
    e.type = self.eventType;
    SDL_PushEvent(&e);
}
- (BOOL)validateMenuItem:(NSMenuItem*)item {
    (void)item;
    return YES;
}
@end

/* Menu items hold their target weakly, so keep it alive for the app's lifetime. */
static GbaSettingsTarget* s_target;

void macos_menu_install_settings(uint32_t sdl_event_type) {
    @autoreleasepool {
        NSMenu* bar = [NSApp mainMenu];
        if (!bar || bar.numberOfItems == 0) return;
        NSMenu* app = [[bar itemAtIndex:0] submenu];
        if (!app) return;
        if (!s_target) s_target = [[GbaSettingsTarget alloc] init];
        s_target.eventType = sdl_event_type;

        NSMenuItem* item = nil;
        for (NSMenuItem* it in app.itemArray)
            if ([it.keyEquivalent isEqualToString:@","]) item = it;
        if (!item) {   /* SDL versions without the item: add one after About */
            item = [[NSMenuItem alloc] initWithTitle:@"Settings…" action:nil keyEquivalent:@","];
            [app insertItem:item atIndex:(app.numberOfItems > 1 ? 1 : app.numberOfItems)];
        }
        item.title = @"Settings…";
        item.target = s_target;
        item.action = @selector(openSettings:);
        item.enabled = YES;
    }
}
