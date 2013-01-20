#include <pjsua-lib/pjsua.h>
#include <pjsua-lib/pjsua_internal.h>


#define THIS_FILE   "pjsua_presence.h"

enum
{
    PJSIP_PRESENCE_METHOD = PJSIP_OTHER_METHOD + 1
};

const pjsip_method pjsip_message_method =
{
    (pjsip_method_e) PJSIP_PRESENCE_METHOD,
    { "PRESENCE", 8 }
};
