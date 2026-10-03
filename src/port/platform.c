#include "port/platform.h"

#ifdef __APPLE__
#include <objc/message.h>
#include <objc/runtime.h>

/* [[NSProcessInfo processInfo] beginActivityWithOptions:reason:], via the Objective-C runtime
 * so the rest of the port stays plain C. */
void platform_keep_awake(void) {
    static id token;
    if (token != NULL) {
        return;
    }
    typedef id (*MsgClass)(Class, SEL);
    typedef id (*MsgString)(Class, SEL, const char *);
    typedef id (*MsgActivity)(id, SEL, unsigned long long, id);
    typedef id (*MsgRetain)(id, SEL);
    id info =
        ((MsgClass)objc_msgSend)(objc_getClass("NSProcessInfo"), sel_registerName("processInfo"));
    id reason = ((MsgString)objc_msgSend)(
        objc_getClass("NSString"), sel_registerName("stringWithUTF8String:"), "Game emulation");
    /* NSActivityUserInitiated | NSActivityLatencyCritical */
    const unsigned long long options = 0x00FFFFFFULL | 0xFF00000000ULL;
    token = ((MsgActivity)objc_msgSend)(info, sel_registerName("beginActivityWithOptions:reason:"),
                                        options, reason);
    if (token != NULL) {
        ((MsgRetain)objc_msgSend)(token, sel_registerName("retain"));
    }
}
#else
void platform_keep_awake(void) {}
#endif
