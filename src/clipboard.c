#include "clipboard.h"
#include <stdlib.h>
#include <string.h>

#ifdef LINUX
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#elif defined(MACOS)
#include <ApplicationServices/ApplicationServices.h>
#endif

char *clipboard_get(void) {
#ifdef LINUX
    Display *display = XOpenDisplay(NULL);
    if (!display) return NULL;
    
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    Atom utf8_string = XInternAtom(display, "UTF8_STRING", False);
    
    XConvertSelection(display, clipboard, utf8_string, clipboard, window, CurrentTime);
    XFlush(display);
    
    XEvent event;
    XNextEvent(display, &event);
    
    char *result = NULL;
    if (event.type == SelectionNotify && event.xselection.property != None) {
        Atom actual_type;
        int actual_format;
        unsigned long nitems, bytes_after;
        unsigned char *data = NULL;
        
        XGetWindowProperty(display, window, clipboard, 0, 0, False, AnyPropertyType,
                          &actual_type, &actual_format, &nitems, &bytes_after, &data);
        
        if (data) {
            XFree(data);
        }
        
        XGetWindowProperty(display, window, clipboard, 0, bytes_after, False, AnyPropertyType,
                          &actual_type, &actual_format, &nitems, &bytes_after, &data);
        
        if (data && nitems > 0) {
            result = malloc(nitems + 1);
            memcpy(result, data, nitems);
            result[nitems] = '\0';
            XFree(data);
        }
    }
    
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    
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
    Display *display = XOpenDisplay(NULL);
    if (!display) return -1;
    
    Window window = XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    
    XSetSelectionOwner(display, clipboard, window, CurrentTime);
    XFlush(display);
    
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    
    return 0;
    
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

