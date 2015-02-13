#pragma once

#define APPNAME "Isotoxin"

#define WINDOWS_ONLY // #error

#include "libresample/src/samplerate.h"
#include "ipc/ipc.h"
#include "rectangles/rectangles.h"

#pragma push_macro("delete")
#undef delete
#include "s3/s3.h"
#pragma pop_macro("delete")


#define STRTYPE(TCHARACTER) ts::pstr_t<TCHARACTER>
#define MAKESTRTYPE(TCHARACTER, s, l) STRTYPE(TCHARACTER)( ts::sptr<TCHARACTER>((s),(l)) )
#include "../plugins/plgcommon/plghost_interface.h"
#undef MAKESTRTYPE
#undef STRTYPE
#include "../plugins/plgcommon/common_types.h"

#ifdef _FINAL
#define USELIB(ln) comment(lib, #ln ".lib")
#elif defined _DEBUG_OPTIMIZED
#define USELIB(ln) comment(lib, #ln "do.lib")
#else
#define USELIB(ln) comment(lib, #ln "d.lib")
#endif

/*
INLINE ipcr & operator>>(ipcr &r, ts::astrings_c &s)
{
    int cnt = r.get<int>();
    for(int i=0;i<cnt;++i)
        s.add( r.getastr() );
    return r;
}
*/

#include "tools.h"
#include "mediasystem.h"

#include "contacts.h"
#include "config.h"
#include "activeprotocol.h"
#include "profile.h"

// gui
#include "contactlist.h"
#include "conversation.h"
#include "isodialog.h"
#include "mainrect.h"
#include "firstrun.h"
#include "addcontact.h"
#include "metacontact.h"
#include "settings.h"
#include "msgbox.h"
#include "entertext.h"

#include "res/resource.h"
#include "application.h"
