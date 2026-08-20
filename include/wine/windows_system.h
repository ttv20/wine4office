/* Private Windows.System interfaces shared by Wine modules. */

#ifndef __WINE_WINDOWS_SYSTEM_H
#define __WINE_WINDOWS_SYSTEM_H

#include "objbase.h"
#include "winstring.h"

typedef struct IWineSystemUserIdentity IWineSystemUserIdentity;

static const IID IID_IWineSystemUserIdentity =
    {0xbabbf76d, 0xe331, 0x48a4, {0x9a, 0x85, 0x14, 0xf1, 0x8d, 0x5a, 0x7f, 0x28}};

typedef struct IWineSystemUserIdentityVtbl
{
    BEGIN_INTERFACE

    HRESULT (STDMETHODCALLTYPE *QueryInterface)(IWineSystemUserIdentity *iface, REFIID iid, void **out);
    ULONG (STDMETHODCALLTYPE *AddRef)(IWineSystemUserIdentity *iface);
    ULONG (STDMETHODCALLTYPE *Release)(IWineSystemUserIdentity *iface);
    HRESULT (STDMETHODCALLTYPE *GetSid)(IWineSystemUserIdentity *iface, HSTRING *sid);

    END_INTERFACE
} IWineSystemUserIdentityVtbl;

struct IWineSystemUserIdentity
{
    const IWineSystemUserIdentityVtbl *lpVtbl;
};

#ifdef COBJMACROS
#define IWineSystemUserIdentity_QueryInterface(iface, iid, out) \
    ((iface)->lpVtbl->QueryInterface((iface), (iid), (out)))
#define IWineSystemUserIdentity_AddRef(iface) ((iface)->lpVtbl->AddRef((iface)))
#define IWineSystemUserIdentity_Release(iface) ((iface)->lpVtbl->Release((iface)))
#define IWineSystemUserIdentity_GetSid(iface, sid) ((iface)->lpVtbl->GetSid((iface), (sid)))
#endif

#endif /* __WINE_WINDOWS_SYSTEM_H */
