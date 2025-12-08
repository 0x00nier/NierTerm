#include "clipboard.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef LINUX
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

// Use xclip/xsel for reliable clipboard on Linux
char *clipboard_get(void) {
#ifdef LINUX
    // Try xclip first, then xsel
    FILE *fp = popen("xclip -selection clipboard -o 2>/dev/null", "r");
    if (!fp) {
        fp = popen("xsel --clipboard --output 2>/dev/null", "r");
    }
    if (!fp) return NULL;

    char *result = NULL;
    size_t size = 0;
    size_t capacity = 1024;
    result = malloc(capacity);
    if (!result) {
        pclose(fp);
        return NULL;
    }

    int ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (size + 1 >= capacity) {
            capacity *= 2;
            char *new_result = realloc(result, capacity);
            if (!new_result) {
                free(result);
                pclose(fp);
                return NULL;
            }
            result = new_result;
        }
        result[size++] = ch;
    }
    result[size] = '\0';
    pclose(fp);

    if (size == 0) {
        free(result);
        return NULL;
    }
    return result;
    
#elif defined(MACOS)
    PasteboardRef clipboard;
    PasteboardCreate(kPasteboardClipboard, &clipboard);
    
    PasteboardSynchronize(clipboard);
    
    ItemCount item_count;
    PasteboardGetItemCount(clipboard, &item_count);
    
    if (item_count == 0) {
        CFRelease(clipboard);
        return NULL;
    }
    
    PasteboardItemID item_id;
    PasteboardGetItemIdentifier(clipboard, 1, &item_id);
    
    CFDataRef data;
    OSStatus err = PasteboardCopyItemFlavorData(clipboard, item_id, CFSTR("public.utf8-plain-text"), &data);
    
    char *result = NULL;
    if (err == noErr && data) {
        CFIndex length = CFDataGetLength(data);
        result = malloc(length + 1);
        CFDataGetBytes(data, CFRangeMake(0, length), (UInt8 *)result);
        result[length] = '\0';
        CFRelease(data);
    }
    
    CFRelease(clipboard);
    return result;
#else
    return NULL;
#endif
}

int clipboard_set(const char *text) {
    if (!text) return -1;

#ifdef LINUX
    // Try xclip first, then xsel
    FILE *fp = popen("xclip -selection clipboard 2>/dev/null", "w");
    if (!fp) {
        fp = popen("xsel --clipboard --input 2>/dev/null", "w");
    }
    if (!fp) return -1;

    fputs(text, fp);
    int ret = pclose(fp);
    return (ret == 0) ? 0 : -1;
    
#elif defined(MACOS)
    PasteboardRef clipboard;
    PasteboardCreate(kPasteboardClipboard, &clipboard);
    
    PasteboardClear(clipboard);
    
    CFDataRef data = CFDataCreate(NULL, (const UInt8 *)text, strlen(text));
    PasteboardPutItemFlavor(clipboard, (PasteboardItemID)1, CFSTR("public.utf8-plain-text"), data, kPasteboardFlavorNoFlags);
    
    CFRelease(data);
    CFRelease(clipboard);
    
    return 0;
#else
    return -1;
#endif
}

