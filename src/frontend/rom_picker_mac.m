#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include "frontend/rom_picker.h"
#include <string.h>

RomPickResult rom_picker_choose(char* out, size_t out_size) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @"Choose a GBA ROM";
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        NSMutableArray<UTType*>* types = [NSMutableArray array];
        for (NSString* ext in @[ @"gba", @"bps", @"ups", @"ips" ]) {
            UTType* t = [UTType typeWithFilenameExtension:ext];
            if (t) [types addObject:t];
        }
        if (types.count) panel.allowedContentTypes = types;

        /* Bring the panel in front of Finder; activate replaces the old call on macOS 14+. */
        if (@available(macOS 14.0, *)) {
            [NSApp activate];
        } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
        }

        if ([panel runModal] != NSModalResponseOK) return ROM_PICK_CANCELLED;
        const char* path = panel.URL.fileSystemRepresentation;
        if (!path || strlen(path) >= out_size) return ROM_PICK_CANCELLED;
        memcpy(out, path, strlen(path) + 1);
        return ROM_PICK_OK;
    }
}

RomPickResult rom_picker_choose_bios(char* out, size_t out_size) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @"Choose a GBA BIOS file";
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        UTType* bin = [UTType typeWithFilenameExtension:@"bin"];
        if (bin) panel.allowedContentTypes = @[ bin ];

        if (@available(macOS 14.0, *)) {
            [NSApp activate];
        } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
        }

        if ([panel runModal] != NSModalResponseOK) return ROM_PICK_CANCELLED;
        const char* path = panel.URL.fileSystemRepresentation;
        if (!path || strlen(path) >= out_size) return ROM_PICK_CANCELLED;
        memcpy(out, path, strlen(path) + 1);
        return ROM_PICK_OK;
    }
}

RomPickResult rom_picker_choose_nds(char* out, size_t out_size) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @"Choose your HeartGold or SoulSilver ROM";
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        UTType* nds = [UTType typeWithFilenameExtension:@"nds"];
        if (nds) panel.allowedContentTypes = @[ nds ];

        if (@available(macOS 14.0, *)) {
            [NSApp activate];
        } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [NSApp activateIgnoringOtherApps:YES];
#pragma clang diagnostic pop
        }

        if ([panel runModal] != NSModalResponseOK) return ROM_PICK_CANCELLED;
        const char* path = panel.URL.fileSystemRepresentation;
        if (!path || strlen(path) >= out_size) return ROM_PICK_CANCELLED;
        memcpy(out, path, strlen(path) + 1);
        return ROM_PICK_OK;
    }
}

RomPickResult rom_picker_choose_folder(char* out, size_t out_size) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @"Choose your GBA games folder";
        panel.prompt = @"Choose";
        panel.canChooseDirectories = YES;
        panel.canChooseFiles = NO;
        panel.allowsMultipleSelection = NO;
        if ([panel runModal] != NSModalResponseOK) return ROM_PICK_CANCELLED;
        const char* path = panel.URL.fileSystemRepresentation;
        if (!path || strlen(path) >= out_size) return ROM_PICK_CANCELLED;
        memcpy(out, path, strlen(path) + 1);
        return ROM_PICK_OK;
    }
}

bool rom_picker_hide_console(void) { return false; }
