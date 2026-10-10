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
        UTType* gba = [UTType typeWithFilenameExtension:@"gba"];
        if (gba) panel.allowedContentTypes = @[ gba ];

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

bool rom_picker_hide_console(void) { return false; }
