/*
 * RichEdit - functions and interfaces around CreateTextServices
 *
 * Copyright 2005, 2006, Maarten Lankhorst
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS

#include "editor.h"
#include "ole2.h"
#include "oleauto.h"
#include "richole.h"
#include "tom.h"
#include "imm.h"
#include "textserv.h"
#include "wine/debug.h"
#include "editstr.h"

WINE_DEFAULT_DEBUG_CHANNEL(richedit);

static inline struct text_services *impl_from_IUnknown( IUnknown *iface )
{
    return CONTAINING_RECORD( iface, struct text_services, IUnknown_inner );
}

static HRESULT WINAPI ITextServicesImpl_QueryInterface( IUnknown *iface, REFIID iid, void **obj )
{
    struct text_services *services = impl_from_IUnknown( iface );

    TRACE( "(%p)->(%s, %p)\n", iface, debugstr_guid( iid ), obj );

    if (IsEqualIID( iid, &IID_IUnknown )) *obj = &services->IUnknown_inner;
    else if (IsEqualIID( iid, &IID_ITextServices ) || IsEqualIID( iid, &IID_ITextServices2 ))
        *obj = &services->ITextServices_iface;
    else if (IsEqualIID( iid, &IID_IRichEditOle )) *obj= &services->IRichEditOle_iface;
    else if (IsEqualIID( iid, &IID_IDispatch ) ||
             IsEqualIID( iid, &IID_ITextDocument ) ||
             IsEqualIID( iid, &IID_ITextDocument2Old )) *obj = &services->ITextDocument2Old_iface;
    else if (IsEqualIID( iid, &IID_ITextDocument2 )) *obj = &services->ITextDocument2_iface;
    else
    {
        *obj = NULL;
        FIXME( "Unknown interface: %s\n", debugstr_guid( iid ) );
        return E_NOINTERFACE;
    }

    IUnknown_AddRef( (IUnknown *)*obj );
    return S_OK;
}

static ULONG WINAPI ITextServicesImpl_AddRef(IUnknown *iface)
{
    struct text_services *services = impl_from_IUnknown( iface );
    LONG ref = InterlockedIncrement( &services->ref );

    TRACE( "(%p) ref = %ld\n", services, ref );

    return ref;
}

static ULONG WINAPI ITextServicesImpl_Release(IUnknown *iface)
{
    struct text_services *services = impl_from_IUnknown( iface );
    LONG ref = InterlockedDecrement( &services->ref );

    TRACE( "(%p) ref = %ld\n", services, ref );

    if (!ref)
    {
        richole_release_children( services );
        ME_DestroyEditor( services->editor );
        free( services );
    }
    return ref;
}

static const IUnknownVtbl textservices_inner_vtbl =
{
   ITextServicesImpl_QueryInterface,
   ITextServicesImpl_AddRef,
   ITextServicesImpl_Release
};

static inline struct text_services *impl_from_ITextServices( ITextServices *iface )
{
    return CONTAINING_RECORD( iface, struct text_services, ITextServices_iface );
}

static HRESULT WINAPI fnTextSrv_QueryInterface( ITextServices *iface, REFIID iid, void **obj )
{
    struct text_services *services = impl_from_ITextServices( iface );
    return IUnknown_QueryInterface( services->outer_unk, iid, obj );
}

static ULONG WINAPI fnTextSrv_AddRef(ITextServices *iface)
{
    struct text_services *services = impl_from_ITextServices( iface );
    return IUnknown_AddRef( services->outer_unk );
}

static ULONG WINAPI fnTextSrv_Release(ITextServices *iface)
{
    struct text_services *services = impl_from_ITextServices( iface );
    return IUnknown_Release( services->outer_unk );
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxSendMessage,20)
HRESULT __thiscall fnTextSrv_TxSendMessage( ITextServices *iface, UINT msg, WPARAM wparam,
                                                            LPARAM lparam, LRESULT *result )
{
    struct text_services *services = impl_from_ITextServices( iface );
    HRESULT hr;
    LRESULT res;

    res = editor_handle_message( services->editor, msg, wparam, lparam, &hr );
    if (result) *result = res;
    return hr;
}

static HRESULT update_client_rect( struct text_services *services, const RECT *client )
{
    RECT rect;
    HRESULT hr;

    if (!client)
    {
        if (!services->editor->in_place_active) return E_INVALIDARG;
        hr = ITextHost_TxGetClientRect( services->editor->texthost, &rect );
        if (FAILED( hr )) return hr;
    }
    else rect = *client;

    rect.left += services->editor->selofs;

    if (EqualRect( &rect, &services->editor->rcFormat )) return S_FALSE;
    services->editor->rcFormat = rect;
    return S_OK;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxDraw,52)
HRESULT __thiscall fnTextSrv_TxDraw( ITextServices *iface, DWORD aspect, LONG index, void *aspect_info,
                                                     DVTARGETDEVICE *td, HDC draw, HDC target,
                                                     const RECTL *bounds, const RECTL *mf_bounds, RECT *update,
                                                     BOOL (CALLBACK *continue_fn)(DWORD), DWORD continue_param,
                                                     LONG view_id )
{
    struct text_services *services = impl_from_ITextServices( iface );
    HRESULT hr;
    HDC dc = draw;
    BOOL rewrap = FALSE;

    TRACE( "%p: aspect %ld, %ld, %p, %p, draw %p, target %p, bounds %s, mf_bounds %s, update %s, %p, %ld, view %ld\n",
           services, aspect, index, aspect_info, td, draw, target, wine_dbgstr_rect( (RECT *)bounds ),
           wine_dbgstr_rect( (RECT *)mf_bounds ), wine_dbgstr_rect( update ), continue_fn, continue_param, view_id );

    if (aspect != DVASPECT_CONTENT || aspect_info || td || target || mf_bounds || continue_fn )
        FIXME( "Many arguments are ignored\n" );

    if (view_id == TXTVIEW_ACTIVE && services->editor->freeze_count) return E_UNEXPECTED;

    hr = update_client_rect( services, (RECT *)bounds );
    if (FAILED( hr )) return hr;
    if (hr == S_OK) rewrap = TRUE;

    if (!dc && services->editor->in_place_active)
        dc = ITextHost_TxGetDC( services->editor->texthost );
    if (!dc) return E_FAIL;

    if (rewrap)
    {
        editor_mark_rewrap_all( services->editor );
        wrap_marked_paras_dc( services->editor, dc, FALSE );
    }

    if (!services->editor->bEmulateVersion10 || services->editor->nEventMask & ENM_UPDATE)
        ITextHost_TxNotify( services->editor->texthost, EN_UPDATE, NULL );

    editor_draw( services->editor, dc, update );

    if (!draw) ITextHost_TxReleaseDC( services->editor->texthost, dc );
    return S_OK;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxGetHScroll,24)
HRESULT __thiscall fnTextSrv_TxGetHScroll( ITextServices *iface, LONG *min_pos, LONG *max_pos, LONG *pos,
                                                           LONG *page, BOOL *enabled )
{
    struct text_services *services = impl_from_ITextServices( iface );

    if (min_pos) *min_pos = services->editor->horz_si.nMin;
    if (max_pos) *max_pos = services->editor->horz_si.nMax;
    if (pos) *pos = services->editor->horz_si.nPos;
    if (page) *page = services->editor->horz_si.nPage;
    if (enabled) *enabled = services->editor->horz_sb_enabled;
    return S_OK;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxGetVScroll,24)
HRESULT __thiscall fnTextSrv_TxGetVScroll( ITextServices *iface, LONG *min_pos, LONG *max_pos, LONG *pos,
                                                           LONG *page, BOOL *enabled )
{
    struct text_services *services = impl_from_ITextServices( iface );

    if (min_pos) *min_pos = services->editor->vert_si.nMin;
    if (max_pos) *max_pos = services->editor->vert_si.nMax;
    if (pos) *pos = services->editor->vert_si.nPos;
    if (page) *page = services->editor->vert_si.nPage;
    if (enabled) *enabled = services->editor->vert_sb_enabled;
    return S_OK;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_OnTxSetCursor,40)
HRESULT __thiscall fnTextSrv_OnTxSetCursor( ITextServices *iface, DWORD aspect, LONG index,
                                                            void *aspect_info, DVTARGETDEVICE *td, HDC draw,
                                                            HDC target, const RECT *client, INT x, INT y )
{
    struct text_services *services = impl_from_ITextServices( iface );

    TRACE( "%p: %ld, %ld, %p, %p, draw %p target %p client %s pos (%d, %d)\n", services, aspect, index, aspect_info, td, draw,
           target, wine_dbgstr_rect( client ), x, y );

    if (aspect != DVASPECT_CONTENT || index || aspect_info || td || draw || target || client)
        FIXME( "Ignoring most params\n" );

    link_notify( services->editor, WM_SETCURSOR, 0, MAKELPARAM( x, y ) );
    editor_set_cursor( services->editor, x, y );
    return S_OK;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxQueryHitPoint,44)
HRESULT __thiscall fnTextSrv_TxQueryHitPoint(ITextServices *iface, DWORD dwDrawAspect, LONG lindex,
                                                             void *pvAspect, DVTARGETDEVICE *ptd, HDC hdcDraw,
                                                             HDC hicTargetDev, LPCRECT lprcClient, INT x, INT y,
                                                             DWORD *pHitResult)
{
    struct text_services *services = impl_from_ITextServices( iface );

    FIXME( "%p: STUB\n", services );
    return E_NOTIMPL;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_OnTxInPlaceActivate,8)
HRESULT __thiscall fnTextSrv_OnTxInPlaceActivate( ITextServices *iface, const RECT *client )
{
    struct text_services *services = impl_from_ITextServices( iface );
    HRESULT hr;
    BOOL old_active = services->editor->in_place_active;

    TRACE( "%p: %s\n", services, wine_dbgstr_rect( client ) );

    services->editor->in_place_active = TRUE;
    hr = update_client_rect( services, client );
    if (FAILED( hr ))
    {
        services->editor->in_place_active = old_active;
        return hr;
    }
    ME_RewrapRepaint( services->editor );
    return S_OK;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_OnTxInPlaceDeactivate,4)
HRESULT __thiscall fnTextSrv_OnTxInPlaceDeactivate(ITextServices *iface)
{
    struct text_services *services = impl_from_ITextServices( iface );

    TRACE( "%p\n", services );
    services->editor->in_place_active = FALSE;
    return S_OK;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_OnTxUIActivate,4)
HRESULT __thiscall fnTextSrv_OnTxUIActivate(ITextServices *iface)
{
    struct text_services *services = impl_from_ITextServices( iface );

    FIXME( "%p: STUB\n", services );
    return E_NOTIMPL;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_OnTxUIDeactivate,4)
HRESULT __thiscall fnTextSrv_OnTxUIDeactivate(ITextServices *iface)
{
    struct text_services *services = impl_from_ITextServices( iface );

    FIXME( "%p: STUB\n", services );
    return E_NOTIMPL;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxGetText,8)
HRESULT __thiscall fnTextSrv_TxGetText( ITextServices *iface, BSTR *text )
{
    struct text_services *services = impl_from_ITextServices( iface );
    int length;

    length = ME_GetTextLength( services->editor );
    if (length)
    {
        ME_Cursor start;
        BSTR bstr;
        bstr = SysAllocStringByteLen( NULL, length * sizeof(WCHAR) );
        if (bstr == NULL) return E_OUTOFMEMORY;

        cursor_from_char_ofs( services->editor, 0, &start );
        ME_GetTextW( services->editor, bstr, length, &start, INT_MAX, FALSE, FALSE );
        *text = bstr;
    }
    else *text = NULL;

    return S_OK;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxSetText,8)
HRESULT __thiscall fnTextSrv_TxSetText( ITextServices *iface, const WCHAR *text )
{
    struct text_services *services = impl_from_ITextServices( iface );
    ME_Cursor cursor;

    ME_SetCursorToStart( services->editor, &cursor );
    ME_InternalDeleteText( services->editor, &cursor, ME_GetTextLength( services->editor ), FALSE );
    if (text) ME_InsertTextFromCursor( services->editor, 0, text, -1, services->editor->pBuffer->pDefaultStyle );
    set_selection_cursors( services->editor, 0, 0);
    services->editor->nModifyStep = 0;
    OleFlushClipboard();
    ME_EmptyUndoStack( services->editor );
    ME_UpdateRepaint( services->editor, FALSE );

    return S_OK;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxGetCurTargetX,8)
HRESULT __thiscall fnTextSrv_TxGetCurTargetX(ITextServices *iface, LONG *x)
{
    struct text_services *services = impl_from_ITextServices( iface );

    FIXME( "%p: STUB\n", services );
    return E_NOTIMPL;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxGetBaseLinePos,8)
HRESULT __thiscall fnTextSrv_TxGetBaseLinePos(ITextServices *iface, LONG *x)
{
    struct text_services *services = impl_from_ITextServices( iface );

    FIXME( "%p: STUB\n", services );
    return E_NOTIMPL;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxGetNaturalSize,36)
HRESULT __thiscall fnTextSrv_TxGetNaturalSize( ITextServices *iface, DWORD aspect, HDC draw,
                                                               HDC target, DVTARGETDEVICE *td, DWORD mode,
                                                               const SIZEL *extent, LONG *width, LONG *height )
{
    struct text_services *services = impl_from_ITextServices( iface );
    RECT rect;
    HDC dc = draw;
    BOOL rewrap = FALSE;
    HRESULT hr;

    TRACE( "%p: aspect %ld, draw %p, target %p, td %p, mode %08lx, extent %s, *width %ld, *height %ld\n", services,
           aspect, draw, target, td, mode, wine_dbgstr_point( (POINT *)extent ), *width, *height );

    if (aspect != DVASPECT_CONTENT || target || td || mode != TXTNS_FITTOCONTENT )
        FIXME( "Many arguments are ignored\n" );

    SetRect( &rect, 0, 0, *width, *height );

    hr = update_client_rect( services, &rect );
    if (FAILED( hr )) return hr;
    if (hr == S_OK) rewrap = TRUE;

    if (!dc && services->editor->in_place_active)
        dc = ITextHost_TxGetDC( services->editor->texthost );
    if (!dc) return E_FAIL;

    if (rewrap)
    {
        editor_mark_rewrap_all( services->editor );
        wrap_marked_paras_dc( services->editor, dc, FALSE );
    }

    *width = services->editor->nTotalWidth;
    *height = services->editor->nTotalLength;

    if (!draw) ITextHost_TxReleaseDC( services->editor->texthost, dc );
    return S_OK;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxGetDropTarget,8)
HRESULT __thiscall fnTextSrv_TxGetDropTarget(ITextServices *iface, IDropTarget **ppDropTarget)
{
    struct text_services *services = impl_from_ITextServices( iface );

    FIXME( "%p: STUB\n", services );
    return E_NOTIMPL;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_OnTxPropertyBitsChange,12)
HRESULT __thiscall fnTextSrv_OnTxPropertyBitsChange( ITextServices *iface, DWORD mask, DWORD bits )
{
    struct text_services *services = impl_from_ITextServices( iface );
    DWORD scrollbars;
    HRESULT hr;
    BOOL repaint = FALSE;

    TRACE( "%p, mask %08lx, bits %08lx\n", services, mask, bits );

    services->editor->props = (services->editor->props & ~mask) | (bits & mask);
    if (mask & (TXTBIT_WORDWRAP | TXTBIT_MULTILINE))
        services->editor->bWordWrap = (services->editor->props & TXTBIT_WORDWRAP) && (services->editor->props & TXTBIT_MULTILINE);

    if (mask & TXTBIT_SCROLLBARCHANGE)
    {
        hr = ITextHost_TxGetScrollBars( services->editor->texthost, &scrollbars );
        if (SUCCEEDED( hr ))
        {
            if ((services->editor->scrollbars ^ scrollbars) & WS_HSCROLL)
                ITextHost_TxShowScrollBar( services->editor->texthost, SB_HORZ, (scrollbars & WS_HSCROLL) &&
                                           services->editor->nTotalWidth > services->editor->sizeWindow.cx );
            if ((services->editor->scrollbars ^ scrollbars) & WS_VSCROLL)
                ITextHost_TxShowScrollBar( services->editor->texthost, SB_VERT, (scrollbars & WS_VSCROLL) &&
                                           services->editor->nTotalLength > services->editor->sizeWindow.cy );
            services->editor->scrollbars = scrollbars;
        }
    }

    if ((mask & TXTBIT_HIDESELECTION) && !services->editor->bHaveFocus) ME_InvalidateSelection( services->editor );

    if (mask & TXTBIT_SELBARCHANGE)
    {
        LONG width;

        hr = ITextHost_TxGetSelectionBarWidth( services->editor->texthost, &width );
        if (hr == S_OK)
        {
            ITextHost_TxInvalidateRect( services->editor->texthost, &services->editor->rcFormat, TRUE );
            services->editor->rcFormat.left -= services->editor->selofs;
            services->editor->selofs = width ? SELECTIONBAR_WIDTH : 0; /* FIXME: convert from HIMETRIC */
            services->editor->rcFormat.left += services->editor->selofs;
            repaint = TRUE;
        }
    }

    if (mask & TXTBIT_CLIENTRECTCHANGE)
    {
        hr = update_client_rect( services, NULL );
        if (SUCCEEDED( hr )) repaint = TRUE;
    }

    if (mask & TXTBIT_USEPASSWORD)
    {
        if (bits & TXTBIT_USEPASSWORD) ITextHost_TxGetPasswordChar( services->editor->texthost, &services->editor->password_char );
        else services->editor->password_char = 0;
        repaint = TRUE;
    }

    if (repaint) ME_RewrapRepaint( services->editor );

    return S_OK;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxGetCachedSize,12)
HRESULT __thiscall fnTextSrv_TxGetCachedSize(ITextServices *iface, DWORD *pdwWidth, DWORD *pdwHeight)
{
    struct text_services *services = impl_from_ITextServices( iface );

    FIXME( "%p: STUB\n", services );
    return E_NOTIMPL;
}

#ifdef __ASM_USE_THISCALL_WRAPPER

#define STDCALL(func) (void *) __stdcall_ ## func
#ifdef _MSC_VER
#define DEFINE_STDCALL_WRAPPER(num,func) \
    __declspec(naked) HRESULT __stdcall_##func(void) \
    { \
        __asm pop eax \
        __asm pop ecx \
        __asm push eax \
        __asm mov eax, [ecx] \
        __asm jmp dword ptr [eax + 4*num] \
    }
#else /* _MSC_VER */
#define DEFINE_STDCALL_WRAPPER(num,func) \
   extern HRESULT __stdcall_ ## func(void); \
   __ASM_GLOBAL_FUNC(__stdcall_ ## func, \
                   "popl %eax\n\t" \
                   "popl %ecx\n\t" \
                   "pushl %eax\n\t" \
                   "movl (%ecx), %eax\n\t" \
                   "jmp *(4*(" #num "))(%eax)" )
#endif /* _MSC_VER */

DEFINE_STDCALL_WRAPPER(3, ITextServices_TxSendMessage)
DEFINE_STDCALL_WRAPPER(4, ITextServices_TxDraw)
DEFINE_STDCALL_WRAPPER(5, ITextServices_TxGetHScroll)
DEFINE_STDCALL_WRAPPER(6, ITextServices_TxGetVScroll)
DEFINE_STDCALL_WRAPPER(7, ITextServices_OnTxSetCursor)
DEFINE_STDCALL_WRAPPER(8, ITextServices_TxQueryHitPoint)
DEFINE_STDCALL_WRAPPER(9, ITextServices_OnTxInPlaceActivate)
DEFINE_STDCALL_WRAPPER(10, ITextServices_OnTxInPlaceDeactivate)
DEFINE_STDCALL_WRAPPER(11, ITextServices_OnTxUIActivate)
DEFINE_STDCALL_WRAPPER(12, ITextServices_OnTxUIDeactivate)
DEFINE_STDCALL_WRAPPER(13, ITextServices_TxGetText)
DEFINE_STDCALL_WRAPPER(14, ITextServices_TxSetText)
DEFINE_STDCALL_WRAPPER(15, ITextServices_TxGetCurTargetX)
DEFINE_STDCALL_WRAPPER(16, ITextServices_TxGetBaseLinePos)
DEFINE_STDCALL_WRAPPER(17, ITextServices_TxGetNaturalSize)
DEFINE_STDCALL_WRAPPER(18, ITextServices_TxGetDropTarget)
DEFINE_STDCALL_WRAPPER(19, ITextServices_OnTxPropertyBitsChange)
DEFINE_STDCALL_WRAPPER(20, ITextServices_TxGetCachedSize)

const ITextServicesVtbl text_services_stdcall_vtbl =
{
    NULL,
    NULL,
    NULL,
    STDCALL(ITextServices_TxSendMessage),
    STDCALL(ITextServices_TxDraw),
    STDCALL(ITextServices_TxGetHScroll),
    STDCALL(ITextServices_TxGetVScroll),
    STDCALL(ITextServices_OnTxSetCursor),
    STDCALL(ITextServices_TxQueryHitPoint),
    STDCALL(ITextServices_OnTxInPlaceActivate),
    STDCALL(ITextServices_OnTxInPlaceDeactivate),
    STDCALL(ITextServices_OnTxUIActivate),
    STDCALL(ITextServices_OnTxUIDeactivate),
    STDCALL(ITextServices_TxGetText),
    STDCALL(ITextServices_TxSetText),
    STDCALL(ITextServices_TxGetCurTargetX),
    STDCALL(ITextServices_TxGetBaseLinePos),
    STDCALL(ITextServices_TxGetNaturalSize),
    STDCALL(ITextServices_TxGetDropTarget),
    STDCALL(ITextServices_OnTxPropertyBitsChange),
    STDCALL(ITextServices_TxGetCachedSize),
};

#endif /* __ASM_USE_THISCALL_WRAPPER */

struct text_services2_vtbl
{
    ITextServicesVtbl base;
    HRESULT (__thiscall *TxGetNaturalSize2)( ITextServices *iface, DWORD aspect, HDC draw,
                                            HDC target, DVTARGETDEVICE *device, DWORD mode,
                                            const SIZEL *extent, LONG *width, LONG *height,
                                            LONG *ascent );
    HRESULT (__thiscall *TxDrawD2D)( ITextServices *iface, IUnknown *render_target,
                                    const RECTL *bounds, RECT *update, LONG view_id );
};

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxDrawD2D,20)
static HRESULT __thiscall fnTextSrv_TxDrawD2D( ITextServices *iface, IUnknown *render_target,
                                               const RECTL *bounds, RECT *update, LONG view_id )
{
    FIXME( "TxDrawD2D is not implemented for formatted rich text.\n" );
    return E_NOTIMPL;
}

DEFINE_THISCALL_WRAPPER(fnTextSrv_TxGetNaturalSize2,40)
static HRESULT __thiscall fnTextSrv_TxGetNaturalSize2( ITextServices *iface, DWORD aspect, HDC draw,
                                                       HDC target, DVTARGETDEVICE *device, DWORD mode,
                                                       const SIZEL *extent, LONG *width, LONG *height,
                                                       LONG *ascent )
{
    struct text_services *services = impl_from_ITextServices( iface );
    ME_Row *row;
    HRESULT hr;

    hr = fnTextSrv_TxGetNaturalSize( iface, aspect, draw, target, device, mode, extent, width, height );
    if (SUCCEEDED(hr) && ascent)
    {
        row = para_first_row( editor_first_para( services->editor ) );
        if (!row) return E_FAIL;
        *ascent = row->nBaseline;
    }
    return hr;
}

static const struct text_services2_vtbl textservices_vtbl =
{
    {
        fnTextSrv_QueryInterface,
        fnTextSrv_AddRef,
        fnTextSrv_Release,
        THISCALL(fnTextSrv_TxSendMessage),
        THISCALL(fnTextSrv_TxDraw),
        THISCALL(fnTextSrv_TxGetHScroll),
        THISCALL(fnTextSrv_TxGetVScroll),
        THISCALL(fnTextSrv_OnTxSetCursor),
        THISCALL(fnTextSrv_TxQueryHitPoint),
        THISCALL(fnTextSrv_OnTxInPlaceActivate),
        THISCALL(fnTextSrv_OnTxInPlaceDeactivate),
        THISCALL(fnTextSrv_OnTxUIActivate),
        THISCALL(fnTextSrv_OnTxUIDeactivate),
        THISCALL(fnTextSrv_TxGetText),
        THISCALL(fnTextSrv_TxSetText),
        THISCALL(fnTextSrv_TxGetCurTargetX),
        THISCALL(fnTextSrv_TxGetBaseLinePos),
        THISCALL(fnTextSrv_TxGetNaturalSize),
        THISCALL(fnTextSrv_TxGetDropTarget),
        THISCALL(fnTextSrv_OnTxPropertyBitsChange),
        THISCALL(fnTextSrv_TxGetCachedSize),
    },
    THISCALL(fnTextSrv_TxGetNaturalSize2),
    THISCALL(fnTextSrv_TxDrawD2D),
};

static inline struct text_services *impl_from_ITextDocument2( ITextDocument2 *iface )
{
    return CONTAINING_RECORD( iface, struct text_services, ITextDocument2_iface );
}

static HRESULT WINAPI fnTextDoc2_QueryInterface( ITextDocument2 *iface, REFIID iid, void **obj )
{
    struct text_services *services = impl_from_ITextDocument2( iface );
    return IUnknown_QueryInterface( services->outer_unk, iid, obj );
}

static ULONG WINAPI fnTextDoc2_AddRef( ITextDocument2 *iface )
{
    struct text_services *services = impl_from_ITextDocument2( iface );
    return IUnknown_AddRef( services->outer_unk );
}

static ULONG WINAPI fnTextDoc2_Release( ITextDocument2 *iface )
{
    struct text_services *services = impl_from_ITextDocument2( iface );
    return IUnknown_Release( services->outer_unk );
}

#define DOC2_FORWARD0(name) \
static HRESULT WINAPI fnTextDoc2_##name( ITextDocument2 *iface ) \
{ \
    struct text_services *services = impl_from_ITextDocument2( iface ); \
    return ITextDocument2Old_##name( &services->ITextDocument2Old_iface ); \
}

#define DOC2_FORWARD(name, decl, ...) \
static HRESULT WINAPI fnTextDoc2_##name decl \
{ \
    struct text_services *services = impl_from_ITextDocument2( iface ); \
    return ITextDocument2Old_##name( &services->ITextDocument2Old_iface, __VA_ARGS__ ); \
}

DOC2_FORWARD(GetTypeInfoCount, (ITextDocument2 *iface, UINT *count), count)
DOC2_FORWARD(GetTypeInfo, (ITextDocument2 *iface, UINT index, LCID lcid, ITypeInfo **info), index, lcid, info)
DOC2_FORWARD(GetIDsOfNames, (ITextDocument2 *iface, REFIID iid, LPOLESTR *names, UINT count,
        LCID lcid, DISPID *ids), iid, names, count, lcid, ids)
DOC2_FORWARD(Invoke, (ITextDocument2 *iface, DISPID id, REFIID iid, LCID lcid, WORD flags,
        DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception, UINT *argerr),
        id, iid, lcid, flags, params, result, exception, argerr)
DOC2_FORWARD(GetName, (ITextDocument2 *iface, BSTR *name), name)
DOC2_FORWARD(GetSelection, (ITextDocument2 *iface, ITextSelection **selection), selection)
DOC2_FORWARD(GetStoryCount, (ITextDocument2 *iface, LONG *count), count)
DOC2_FORWARD(GetStoryRanges, (ITextDocument2 *iface, ITextStoryRanges **stories), stories)
DOC2_FORWARD(GetSaved, (ITextDocument2 *iface, LONG *value), value)
DOC2_FORWARD(SetSaved, (ITextDocument2 *iface, LONG value), value)
DOC2_FORWARD(GetDefaultTabStop, (ITextDocument2 *iface, float *value), value)
DOC2_FORWARD(SetDefaultTabStop, (ITextDocument2 *iface, float value), value)
DOC2_FORWARD0(New)
DOC2_FORWARD(Open, (ITextDocument2 *iface, VARIANT *value, LONG flags, LONG codepage), value, flags, codepage)
DOC2_FORWARD(Save, (ITextDocument2 *iface, VARIANT *value, LONG flags, LONG codepage), value, flags, codepage)
DOC2_FORWARD(Freeze, (ITextDocument2 *iface, LONG *count), count)
DOC2_FORWARD(Unfreeze, (ITextDocument2 *iface, LONG *count), count)
DOC2_FORWARD0(BeginEditCollection)
DOC2_FORWARD0(EndEditCollection)
DOC2_FORWARD(Undo, (ITextDocument2 *iface, LONG count, LONG *prop), count, prop)
DOC2_FORWARD(Redo, (ITextDocument2 *iface, LONG count, LONG *prop), count, prop)
DOC2_FORWARD(Range, (ITextDocument2 *iface, LONG start, LONG end, ITextRange **range), start, end, range)
DOC2_FORWARD(RangeFromPoint, (ITextDocument2 *iface, LONG x, LONG y, ITextRange **range), x, y, range)
DOC2_FORWARD(GetCaretType, (ITextDocument2 *iface, LONG *value), value)
DOC2_FORWARD(SetCaretType, (ITextDocument2 *iface, LONG value), value)
DOC2_FORWARD(GetNotificationMode, (ITextDocument2 *iface, LONG *mode), mode)
DOC2_FORWARD(SetNotificationMode, (ITextDocument2 *iface, LONG mode), mode)
DOC2_FORWARD(GetWindow, (ITextDocument2 *iface, LONG *hwnd), hwnd)
DOC2_FORWARD(AttachMsgFilter, (ITextDocument2 *iface, IUnknown *filter), filter)
DOC2_FORWARD(CheckTextLimit, (ITextDocument2 *iface, LONG count, LONG *exceed), count, exceed)
DOC2_FORWARD(GetClientRect, (ITextDocument2 *iface, LONG type, LONG *left, LONG *top, LONG *right,
        LONG *bottom), type, left, top, right, bottom)
DOC2_FORWARD(GetEffectColor, (ITextDocument2 *iface, LONG index, COLORREF *color), index, color)
DOC2_FORWARD(GetImmContext, (ITextDocument2 *iface, LONG *context), context)
DOC2_FORWARD(GetPreferredFont, (ITextDocument2 *iface, LONG cp, LONG codepage, LONG option,
        LONG current_codepage, LONG current_size, BSTR *name, LONG *pitch_family, LONG *new_size),
        cp, codepage, option, current_codepage, current_size, name, pitch_family, new_size)
DOC2_FORWARD(Notify, (ITextDocument2 *iface, LONG notify), notify)
DOC2_FORWARD(ReleaseImmContext, (ITextDocument2 *iface, LONG context), context)
DOC2_FORWARD(SetEffectColor, (ITextDocument2 *iface, LONG index, LONG color), index, color)
DOC2_FORWARD0(SysBeep)
DOC2_FORWARD(Update, (ITextDocument2 *iface, LONG value), value)
DOC2_FORWARD0(UpdateWindow)

#undef DOC2_FORWARD
#undef DOC2_FORWARD0

static HRESULT WINAPI fnTextDoc2_SetIMEInProgress( ITextDocument2 *iface, LONG value )
{
    struct text_services *services = impl_from_ITextDocument2( iface );
    return ITextDocument2Old_IMEInProgress( &services->ITextDocument2Old_iface, value );
}

static HRESULT WINAPI fnTextDoc2_GetDisplays( ITextDocument2 *iface, ITextDisplays **displays )
{
    if (displays) *displays = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_GetDocumentFont( ITextDocument2 *iface, ITextFont2 **font )
{
    if (font) *font = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_SetDocumentFont( ITextDocument2 *iface, ITextFont2 *font )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_GetDocumentPara( ITextDocument2 *iface, ITextPara2 **para )
{
    if (para) *para = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_SetDocumentPara( ITextDocument2 *iface, ITextPara2 *para )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_GetEastAsianFlags( ITextDocument2 *iface, LONG *flags )
{
    if (!flags) return E_INVALIDARG;
    *flags = 0;
    return S_OK;
}

static HRESULT WINAPI fnTextDoc2_GetGenerator( ITextDocument2 *iface, BSTR *generator )
{
    if (!generator) return E_INVALIDARG;
    *generator = SysAllocString( L"Wine RichEdit" );
    return *generator ? S_OK : E_OUTOFMEMORY;
}

static HRESULT WINAPI fnTextDoc2_GetSelection2( ITextDocument2 *iface, ITextSelection2 **selection )
{
    if (selection) *selection = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_GetStoryRanges2( ITextDocument2 *iface, ITextStoryRanges2 **stories )
{
    if (stories) *stories = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_GetTypographyOptions( ITextDocument2 *iface, LONG *options )
{
    struct text_services *services = impl_from_ITextDocument2( iface );

    if (!options) return E_INVALIDARG;
    *options = services->typography_options;
    return S_OK;
}

static HRESULT WINAPI fnTextDoc2_GetVersion( ITextDocument2 *iface, LONG *version )
{
    if (!version) return E_INVALIDARG;
    *version = 8;
    return S_OK;
}

static HRESULT WINAPI fnTextDoc2_GetCallManager( ITextDocument2 *iface, IUnknown **manager )
{
    if (manager) *manager = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_GetProperty( ITextDocument2 *iface, LONG type, LONG *value )
{
    struct text_services *services = impl_from_ITextDocument2( iface );
    HRESULT hr;
    LRESULT result;

    if (!value) return E_INVALIDARG;
    *value = 0;

    switch (type)
    {
    case tomCanCopy:
    {
        LONG start, end;

        ME_GetSelectionOfs( services->editor, &start, &end );
        *value = start != end ? tomTrue : tomFalse;
        return S_OK;
    }
    case tomCanUndo:
        result = editor_handle_message( services->editor, EM_CANUNDO, 0, 0, &hr );
        *value = result ? tomTrue : tomFalse;
        return hr;
    case tomCanRedo:
        result = editor_handle_message( services->editor, EM_CANREDO, 0, 0, &hr );
        *value = result ? tomTrue : tomFalse;
        return hr;
    case tomUndoLimit:
        *value = services->editor->nUndoLimit;
        return S_OK;
    case tomDocAutoLink:
        result = editor_handle_message( services->editor, EM_GETAUTOURLDETECT, 0, 0, &hr );
        *value = result ? tomTrue : tomFalse;
        return hr;
    default:
        return E_NOTIMPL;
    }
}

static HRESULT WINAPI fnTextDoc2_GetStrings( ITextDocument2 *iface, ITextStrings **strings )
{
    if (strings) *strings = NULL;
    return E_NOTIMPL;
}

struct text_range2_proxy
{
    ITextRange2 ITextRange2_iface;
    LONG ref;
    ITextRange *range;
};

static HRESULT create_text_range2_proxy( ITextRange *base_range, ITextRange2 **range );

static inline struct text_range2_proxy *impl_from_ITextRange2( ITextRange2 *iface )
{
    return CONTAINING_RECORD( iface, struct text_range2_proxy, ITextRange2_iface );
}

static HRESULT WINAPI fnTextRange2_QueryInterface( ITextRange2 *iface, REFIID iid, void **obj )
{
    if (!obj) return E_POINTER;
    *obj = NULL;
    if (!IsEqualIID( iid, &IID_IUnknown ) && !IsEqualIID( iid, &IID_IDispatch ) &&
        !IsEqualIID( iid, &IID_ITextRange ) && !IsEqualIID( iid, &IID_ITextSelection ) &&
        !IsEqualIID( iid, &IID_ITextRange2 ))
        return E_NOINTERFACE;
    *obj = iface;
    ITextRange2_AddRef( iface );
    return S_OK;
}

static ULONG WINAPI fnTextRange2_AddRef( ITextRange2 *iface )
{
    struct text_range2_proxy *proxy = impl_from_ITextRange2( iface );
    return InterlockedIncrement( &proxy->ref );
}

static ULONG WINAPI fnTextRange2_Release( ITextRange2 *iface )
{
    struct text_range2_proxy *proxy = impl_from_ITextRange2( iface );
    ULONG ref = InterlockedDecrement( &proxy->ref );

    if (!ref)
    {
        ITextRange_Release( proxy->range );
        free( proxy );
    }
    return ref;
}

#define RANGE2_FORWARD0(name) \
static HRESULT WINAPI fnTextRange2_##name( ITextRange2 *iface ) \
{ \
    struct text_range2_proxy *proxy = impl_from_ITextRange2( iface ); \
    return ITextRange_##name( proxy->range ); \
}

#define RANGE2_FORWARD(name, decl, ...) \
static HRESULT WINAPI fnTextRange2_##name decl \
{ \
    struct text_range2_proxy *proxy = impl_from_ITextRange2( iface ); \
    return ITextRange_##name( proxy->range, __VA_ARGS__ ); \
}

RANGE2_FORWARD(GetTypeInfoCount, (ITextRange2 *iface, UINT *count), count)
RANGE2_FORWARD(GetTypeInfo, (ITextRange2 *iface, UINT index, LCID lcid, ITypeInfo **info),
        index, lcid, info)
RANGE2_FORWARD(GetIDsOfNames, (ITextRange2 *iface, REFIID iid, LPOLESTR *names, UINT count,
        LCID lcid, DISPID *ids), iid, names, count, lcid, ids)
RANGE2_FORWARD(Invoke, (ITextRange2 *iface, DISPID id, REFIID iid, LCID lcid, WORD flags,
        DISPPARAMS *params, VARIANT *result, EXCEPINFO *exception, UINT *argerr),
        id, iid, lcid, flags, params, result, exception, argerr)
RANGE2_FORWARD(GetText, (ITextRange2 *iface, BSTR *text), text)
RANGE2_FORWARD(SetText, (ITextRange2 *iface, BSTR text), text)
RANGE2_FORWARD(GetChar, (ITextRange2 *iface, LONG *ch), ch)
RANGE2_FORWARD(SetChar, (ITextRange2 *iface, LONG ch), ch)
RANGE2_FORWARD(GetDuplicate, (ITextRange2 *iface, ITextRange **range), range)
RANGE2_FORWARD(GetFormattedText, (ITextRange2 *iface, ITextRange **range), range)
RANGE2_FORWARD(SetFormattedText, (ITextRange2 *iface, ITextRange *range), range)
RANGE2_FORWARD(GetStart, (ITextRange2 *iface, LONG *start), start)
RANGE2_FORWARD(SetStart, (ITextRange2 *iface, LONG start), start)
RANGE2_FORWARD(GetEnd, (ITextRange2 *iface, LONG *end), end)
RANGE2_FORWARD(SetEnd, (ITextRange2 *iface, LONG end), end)
RANGE2_FORWARD(GetFont, (ITextRange2 *iface, ITextFont **font), font)
RANGE2_FORWARD(SetFont, (ITextRange2 *iface, ITextFont *font), font)
RANGE2_FORWARD(GetPara, (ITextRange2 *iface, ITextPara **para), para)
RANGE2_FORWARD(SetPara, (ITextRange2 *iface, ITextPara *para), para)
RANGE2_FORWARD(GetStoryLength, (ITextRange2 *iface, LONG *length), length)
RANGE2_FORWARD(GetStoryType, (ITextRange2 *iface, LONG *type), type)
RANGE2_FORWARD(Collapse, (ITextRange2 *iface, LONG start), start)
RANGE2_FORWARD(Expand, (ITextRange2 *iface, LONG unit, LONG *delta), unit, delta)
RANGE2_FORWARD(GetIndex, (ITextRange2 *iface, LONG unit, LONG *index), unit, index)
RANGE2_FORWARD(SetIndex, (ITextRange2 *iface, LONG unit, LONG index, LONG extend), unit, index, extend)
RANGE2_FORWARD(SetRange, (ITextRange2 *iface, LONG anchor, LONG active), anchor, active)
RANGE2_FORWARD(InRange, (ITextRange2 *iface, ITextRange *range, LONG *value), range, value)
RANGE2_FORWARD(InStory, (ITextRange2 *iface, ITextRange *range, LONG *value), range, value)
RANGE2_FORWARD(IsEqual, (ITextRange2 *iface, ITextRange *range, LONG *value), range, value)
RANGE2_FORWARD0(Select)
RANGE2_FORWARD(StartOf, (ITextRange2 *iface, LONG unit, LONG extend, LONG *delta), unit, extend, delta)
RANGE2_FORWARD(EndOf, (ITextRange2 *iface, LONG unit, LONG extend, LONG *delta), unit, extend, delta)
RANGE2_FORWARD(Move, (ITextRange2 *iface, LONG unit, LONG count, LONG *delta), unit, count, delta)
RANGE2_FORWARD(MoveStart, (ITextRange2 *iface, LONG unit, LONG count, LONG *delta), unit, count, delta)
RANGE2_FORWARD(MoveEnd, (ITextRange2 *iface, LONG unit, LONG count, LONG *delta), unit, count, delta)
RANGE2_FORWARD(MoveWhile, (ITextRange2 *iface, VARIANT *set, LONG count, LONG *delta), set, count, delta)
RANGE2_FORWARD(MoveStartWhile, (ITextRange2 *iface, VARIANT *set, LONG count, LONG *delta),
        set, count, delta)
RANGE2_FORWARD(MoveEndWhile, (ITextRange2 *iface, VARIANT *set, LONG count, LONG *delta),
        set, count, delta)
RANGE2_FORWARD(MoveUntil, (ITextRange2 *iface, VARIANT *set, LONG count, LONG *delta), set, count, delta)
RANGE2_FORWARD(MoveStartUntil, (ITextRange2 *iface, VARIANT *set, LONG count, LONG *delta),
        set, count, delta)
RANGE2_FORWARD(MoveEndUntil, (ITextRange2 *iface, VARIANT *set, LONG count, LONG *delta),
        set, count, delta)
RANGE2_FORWARD(FindText, (ITextRange2 *iface, BSTR text, LONG count, LONG flags, LONG *length),
        text, count, flags, length)
RANGE2_FORWARD(FindTextStart, (ITextRange2 *iface, BSTR text, LONG count, LONG flags, LONG *length),
        text, count, flags, length)
RANGE2_FORWARD(FindTextEnd, (ITextRange2 *iface, BSTR text, LONG count, LONG flags, LONG *length),
        text, count, flags, length)
RANGE2_FORWARD(Delete, (ITextRange2 *iface, LONG unit, LONG count, LONG *delta), unit, count, delta)
RANGE2_FORWARD(Cut, (ITextRange2 *iface, VARIANT *value), value)
RANGE2_FORWARD(Copy, (ITextRange2 *iface, VARIANT *value), value)
RANGE2_FORWARD(Paste, (ITextRange2 *iface, VARIANT *value, LONG format), value, format)
RANGE2_FORWARD(CanPaste, (ITextRange2 *iface, VARIANT *value, LONG format, LONG *result),
        value, format, result)
RANGE2_FORWARD(CanEdit, (ITextRange2 *iface, LONG *result), result)
RANGE2_FORWARD(ChangeCase, (ITextRange2 *iface, LONG type), type)
RANGE2_FORWARD(GetPoint, (ITextRange2 *iface, LONG type, LONG *x, LONG *y), type, x, y)
RANGE2_FORWARD(SetPoint, (ITextRange2 *iface, LONG x, LONG y, LONG type, LONG extend),
        x, y, type, extend)
RANGE2_FORWARD(ScrollIntoView, (ITextRange2 *iface, LONG value), value)
RANGE2_FORWARD(GetEmbeddedObject, (ITextRange2 *iface, IUnknown **object), object)

#define RANGE2_SELECTION_FORWARD(name, decl, ...) \
static HRESULT WINAPI fnTextRange2_##name decl \
{ \
    struct text_range2_proxy *proxy = impl_from_ITextRange2( iface ); \
    ITextSelection *selection; \
    HRESULT hr; \
    hr = ITextRange_QueryInterface( proxy->range, &IID_ITextSelection, (void **)&selection ); \
    if (FAILED(hr)) return E_NOTIMPL; \
    hr = ITextSelection_##name( selection, __VA_ARGS__ ); \
    ITextSelection_Release( selection ); \
    return hr; \
}

RANGE2_SELECTION_FORWARD(GetFlags, (ITextRange2 *iface, LONG *flags), flags)
RANGE2_SELECTION_FORWARD(SetFlags, (ITextRange2 *iface, LONG flags), flags)
RANGE2_SELECTION_FORWARD(GetType, (ITextRange2 *iface, LONG *type), type)
RANGE2_SELECTION_FORWARD(MoveLeft, (ITextRange2 *iface, LONG unit, LONG count, LONG extend, LONG *delta),
        unit, count, extend, delta)
RANGE2_SELECTION_FORWARD(MoveRight, (ITextRange2 *iface, LONG unit, LONG count, LONG extend, LONG *delta),
        unit, count, extend, delta)
RANGE2_SELECTION_FORWARD(MoveUp, (ITextRange2 *iface, LONG unit, LONG count, LONG extend, LONG *delta),
        unit, count, extend, delta)
RANGE2_SELECTION_FORWARD(MoveDown, (ITextRange2 *iface, LONG unit, LONG count, LONG extend, LONG *delta),
        unit, count, extend, delta)
RANGE2_SELECTION_FORWARD(HomeKey, (ITextRange2 *iface, LONG unit, LONG extend, LONG *delta),
        unit, extend, delta)
RANGE2_SELECTION_FORWARD(EndKey, (ITextRange2 *iface, LONG unit, LONG extend, LONG *delta),
        unit, extend, delta)
RANGE2_SELECTION_FORWARD(TypeText, (ITextRange2 *iface, BSTR text), text)

static HRESULT WINAPI fnTextRange2_GetCch( ITextRange2 *iface, LONG *count )
{
    struct text_range2_proxy *proxy = impl_from_ITextRange2( iface );
    LONG start, end;
    HRESULT hr;

    if (!count) return E_INVALIDARG;
    *count = 0;
    hr = ITextRange_GetStart( proxy->range, &start );
    if (SUCCEEDED(hr)) hr = ITextRange_GetEnd( proxy->range, &end );
    if (SUCCEEDED(hr)) *count = end - start;
    return hr;
}

static HRESULT WINAPI fnTextRange2_GetFont2( ITextRange2 *iface, ITextFont2 **font )
{
    if (font) *font = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextRange2_GetDuplicate2( ITextRange2 *iface, ITextRange2 **range )
{
    struct text_range2_proxy *proxy = impl_from_ITextRange2( iface );
    ITextRange *duplicate;
    HRESULT hr;

    if (!range) return E_INVALIDARG;
    *range = NULL;
    hr = ITextRange_GetDuplicate( proxy->range, &duplicate );
    if (FAILED(hr)) return hr;
    return create_text_range2_proxy( duplicate, range );
}

static HRESULT WINAPI fnTextRange2_GetFormattedText2( ITextRange2 *iface, ITextRange2 **range )
{
    struct text_range2_proxy *proxy = impl_from_ITextRange2( iface );
    ITextRange *formatted;
    HRESULT hr;

    if (!range) return E_INVALIDARG;
    *range = NULL;
    hr = ITextRange_GetFormattedText( proxy->range, &formatted );
    if (FAILED(hr)) return hr;
    return create_text_range2_proxy( formatted, range );
}

static HRESULT WINAPI fnTextRange2_SetFormattedText2( ITextRange2 *iface, ITextRange2 *range )
{
    struct text_range2_proxy *proxy = impl_from_ITextRange2( iface );
    ITextRange *base_range;
    HRESULT hr;

    if (!range) return E_INVALIDARG;
    hr = ITextRange2_QueryInterface( range, &IID_ITextRange, (void **)&base_range );
    if (FAILED(hr)) return hr;
    hr = ITextRange_SetFormattedText( proxy->range, base_range );
    ITextRange_Release( base_range );
    return hr;
}

static HRESULT WINAPI fnTextRange2_GetText2( ITextRange2 *iface, LONG flags, BSTR *text )
{
    struct text_range2_proxy *proxy = impl_from_ITextRange2( iface );

    if (flags) return E_NOTIMPL;
    return ITextRange_GetText( proxy->range, text );
}

static HRESULT WINAPI fnTextRange2_SetText2( ITextRange2 *iface, LONG flags, BSTR text )
{
    struct text_range2_proxy *proxy = impl_from_ITextRange2( iface );

    if (flags) return E_NOTIMPL;
    return ITextRange_SetText( proxy->range, text );
}

#define RANGE2_STUB(name, decl) \
static HRESULT WINAPI fnTextRange2_##name decl \
{ \
    return E_NOTIMPL; \
}

RANGE2_STUB(GetCells, (ITextRange2 *iface, IUnknown **cells))
RANGE2_STUB(GetColumn, (ITextRange2 *iface, IUnknown **column))
RANGE2_STUB(GetCount, (ITextRange2 *iface, LONG *count))
RANGE2_STUB(SetFont2, (ITextRange2 *iface, ITextFont2 *font))
RANGE2_STUB(GetGravity, (ITextRange2 *iface, LONG *value))
RANGE2_STUB(SetGravity, (ITextRange2 *iface, LONG value))
RANGE2_STUB(GetPara2, (ITextRange2 *iface, ITextPara2 **para))
RANGE2_STUB(SetPara2, (ITextRange2 *iface, ITextPara2 *para))
RANGE2_STUB(GetRow, (ITextRange2 *iface, ITextRow **row))
RANGE2_STUB(GetStartPara, (ITextRange2 *iface, LONG *value))
RANGE2_STUB(GetTable, (ITextRange2 *iface, IUnknown **table))
RANGE2_STUB(GetURL, (ITextRange2 *iface, BSTR *url))
RANGE2_STUB(SetURL, (ITextRange2 *iface, BSTR url))
RANGE2_STUB(AddSubrange, (ITextRange2 *iface, LONG cp1, LONG cp2, LONG activate))
RANGE2_STUB(BuildUpMath, (ITextRange2 *iface, LONG flags))
RANGE2_STUB(DeleteSubrange, (ITextRange2 *iface, LONG first, LONG limit))
RANGE2_STUB(Find, (ITextRange2 *iface, ITextRange2 *range, LONG count, LONG flags, LONG *delta))
RANGE2_STUB(GetChar2, (ITextRange2 *iface, LONG *ch, LONG offset))
RANGE2_STUB(GetDropCap, (ITextRange2 *iface, LONG *line, LONG *position))
RANGE2_STUB(GetInlineObject, (ITextRange2 *iface, LONG *type, LONG *align, LONG *ch, LONG *ch1,
        LONG *ch2, LONG *count, LONG *style, LONG *columns, LONG *level))
RANGE2_STUB(GetProperty, (ITextRange2 *iface, LONG type, LONG *value))
RANGE2_STUB(GetRect, (ITextRange2 *iface, LONG type, LONG *left, LONG *top, LONG *right,
        LONG *bottom, LONG *hit))
RANGE2_STUB(GetSubrange, (ITextRange2 *iface, LONG subrange, LONG *first, LONG *limit))
RANGE2_STUB(HexToUnicode, (ITextRange2 *iface))
RANGE2_STUB(InsertTable, (ITextRange2 *iface, LONG columns, LONG rows, LONG autofit))
RANGE2_STUB(Linearize, (ITextRange2 *iface, LONG flags))
RANGE2_STUB(SetActiveSubrange, (ITextRange2 *iface, LONG anchor, LONG active))
RANGE2_STUB(SetDropCap, (ITextRange2 *iface, LONG line, LONG position))
RANGE2_STUB(SetProperty, (ITextRange2 *iface, LONG type, LONG value))
RANGE2_STUB(UnicodeToHex, (ITextRange2 *iface))
RANGE2_STUB(SetInlineObject, (ITextRange2 *iface, LONG type, LONG align, LONG ch, LONG ch1,
        LONG ch2, LONG count, LONG style, LONG columns))
RANGE2_STUB(GetMathFunctionType, (ITextRange2 *iface, BSTR text, LONG *value))
RANGE2_STUB(InsertImage, (ITextRange2 *iface, LONG width, LONG height, LONG ascent, LONG type,
        BSTR alttext, IStream *stream))

static const ITextRange2Vtbl text_range2_vtbl =
{
    .QueryInterface = fnTextRange2_QueryInterface,
    .AddRef = fnTextRange2_AddRef,
    .Release = fnTextRange2_Release,
    .GetTypeInfoCount = fnTextRange2_GetTypeInfoCount,
    .GetTypeInfo = fnTextRange2_GetTypeInfo,
    .GetIDsOfNames = fnTextRange2_GetIDsOfNames,
    .Invoke = fnTextRange2_Invoke,
    .GetText = fnTextRange2_GetText,
    .SetText = fnTextRange2_SetText,
    .GetChar = fnTextRange2_GetChar,
    .SetChar = fnTextRange2_SetChar,
    .GetDuplicate = fnTextRange2_GetDuplicate,
    .GetFormattedText = fnTextRange2_GetFormattedText,
    .SetFormattedText = fnTextRange2_SetFormattedText,
    .GetStart = fnTextRange2_GetStart,
    .SetStart = fnTextRange2_SetStart,
    .GetEnd = fnTextRange2_GetEnd,
    .SetEnd = fnTextRange2_SetEnd,
    .GetFont = fnTextRange2_GetFont,
    .SetFont = fnTextRange2_SetFont,
    .GetPara = fnTextRange2_GetPara,
    .SetPara = fnTextRange2_SetPara,
    .GetStoryLength = fnTextRange2_GetStoryLength,
    .GetStoryType = fnTextRange2_GetStoryType,
    .Collapse = fnTextRange2_Collapse,
    .Expand = fnTextRange2_Expand,
    .GetIndex = fnTextRange2_GetIndex,
    .SetIndex = fnTextRange2_SetIndex,
    .SetRange = fnTextRange2_SetRange,
    .InRange = fnTextRange2_InRange,
    .InStory = fnTextRange2_InStory,
    .IsEqual = fnTextRange2_IsEqual,
    .Select = fnTextRange2_Select,
    .StartOf = fnTextRange2_StartOf,
    .EndOf = fnTextRange2_EndOf,
    .Move = fnTextRange2_Move,
    .MoveStart = fnTextRange2_MoveStart,
    .MoveEnd = fnTextRange2_MoveEnd,
    .MoveWhile = fnTextRange2_MoveWhile,
    .MoveStartWhile = fnTextRange2_MoveStartWhile,
    .MoveEndWhile = fnTextRange2_MoveEndWhile,
    .MoveUntil = fnTextRange2_MoveUntil,
    .MoveStartUntil = fnTextRange2_MoveStartUntil,
    .MoveEndUntil = fnTextRange2_MoveEndUntil,
    .FindText = fnTextRange2_FindText,
    .FindTextStart = fnTextRange2_FindTextStart,
    .FindTextEnd = fnTextRange2_FindTextEnd,
    .Delete = fnTextRange2_Delete,
    .Cut = fnTextRange2_Cut,
    .Copy = fnTextRange2_Copy,
    .Paste = fnTextRange2_Paste,
    .CanPaste = fnTextRange2_CanPaste,
    .CanEdit = fnTextRange2_CanEdit,
    .ChangeCase = fnTextRange2_ChangeCase,
    .GetPoint = fnTextRange2_GetPoint,
    .SetPoint = fnTextRange2_SetPoint,
    .ScrollIntoView = fnTextRange2_ScrollIntoView,
    .GetEmbeddedObject = fnTextRange2_GetEmbeddedObject,
    .GetFlags = fnTextRange2_GetFlags,
    .SetFlags = fnTextRange2_SetFlags,
    .GetType = fnTextRange2_GetType,
    .MoveLeft = fnTextRange2_MoveLeft,
    .MoveRight = fnTextRange2_MoveRight,
    .MoveUp = fnTextRange2_MoveUp,
    .MoveDown = fnTextRange2_MoveDown,
    .HomeKey = fnTextRange2_HomeKey,
    .EndKey = fnTextRange2_EndKey,
    .TypeText = fnTextRange2_TypeText,
    .GetCch = fnTextRange2_GetCch,
    .GetCells = fnTextRange2_GetCells,
    .GetColumn = fnTextRange2_GetColumn,
    .GetCount = fnTextRange2_GetCount,
    .GetDuplicate2 = fnTextRange2_GetDuplicate2,
    .GetFont2 = fnTextRange2_GetFont2,
    .SetFont2 = fnTextRange2_SetFont2,
    .GetFormattedText2 = fnTextRange2_GetFormattedText2,
    .SetFormattedText2 = fnTextRange2_SetFormattedText2,
    .GetGravity = fnTextRange2_GetGravity,
    .SetGravity = fnTextRange2_SetGravity,
    .GetPara2 = fnTextRange2_GetPara2,
    .SetPara2 = fnTextRange2_SetPara2,
    .GetRow = fnTextRange2_GetRow,
    .GetStartPara = fnTextRange2_GetStartPara,
    .GetTable = fnTextRange2_GetTable,
    .GetURL = fnTextRange2_GetURL,
    .SetURL = fnTextRange2_SetURL,
    .AddSubrange = fnTextRange2_AddSubrange,
    .BuildUpMath = fnTextRange2_BuildUpMath,
    .DeleteSubrange = fnTextRange2_DeleteSubrange,
    .Find = fnTextRange2_Find,
    .GetChar2 = fnTextRange2_GetChar2,
    .GetDropCap = fnTextRange2_GetDropCap,
    .GetInlineObject = fnTextRange2_GetInlineObject,
    .GetProperty = fnTextRange2_GetProperty,
    .GetRect = fnTextRange2_GetRect,
    .GetSubrange = fnTextRange2_GetSubrange,
    .GetText2 = fnTextRange2_GetText2,
    .HexToUnicode = fnTextRange2_HexToUnicode,
    .InsertTable = fnTextRange2_InsertTable,
    .Linearize = fnTextRange2_Linearize,
    .SetActiveSubrange = fnTextRange2_SetActiveSubrange,
    .SetDropCap = fnTextRange2_SetDropCap,
    .SetProperty = fnTextRange2_SetProperty,
    .SetText2 = fnTextRange2_SetText2,
    .UnicodeToHex = fnTextRange2_UnicodeToHex,
    .SetInlineObject = fnTextRange2_SetInlineObject,
    .GetMathFunctionType = fnTextRange2_GetMathFunctionType,
    .InsertImage = fnTextRange2_InsertImage,
};

static HRESULT create_text_range2_proxy( ITextRange *base_range, ITextRange2 **range )
{
    struct text_range2_proxy *proxy;

    if (!range)
    {
        ITextRange_Release( base_range );
        return E_INVALIDARG;
    }
    *range = NULL;
    proxy = calloc( 1, sizeof(*proxy) );
    if (!proxy)
    {
        ITextRange_Release( base_range );
        return E_OUTOFMEMORY;
    }
    proxy->ITextRange2_iface.lpVtbl = &text_range2_vtbl;
    proxy->ref = 1;
    proxy->range = base_range;
    *range = &proxy->ITextRange2_iface;
    return S_OK;
}

static HRESULT WINAPI fnTextDoc2_Range2( ITextDocument2 *iface, LONG active, LONG anchor,
        ITextRange2 **range )
{
    struct text_services *services = impl_from_ITextDocument2( iface );
    ITextRange *base_range;
    HRESULT hr;
    hr = ITextDocument2Old_Range( &services->ITextDocument2Old_iface, active, anchor, &base_range );
    if (FAILED(hr)) return hr;
    return create_text_range2_proxy( base_range, range );
}

static HRESULT WINAPI fnTextDoc2_RangeFromPoint2( ITextDocument2 *iface, LONG x, LONG y, LONG type,
        ITextRange2 **range )
{
    if (range) *range = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_ReleaseCallManager( ITextDocument2 *iface, IUnknown *manager )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_SetProperty( ITextDocument2 *iface, LONG type, LONG value )
{
    struct text_services *services = impl_from_ITextDocument2( iface );
    HRESULT hr;

    switch (type)
    {
    case tomUndoLimit:
        editor_handle_message( services->editor, EM_SETUNDOLIMIT, (WPARAM)(ULONG)value, 0, &hr );
        return hr;
    case tomDocAutoLink:
        if (value != tomFalse && value != tomTrue) return E_INVALIDARG;
        editor_handle_message( services->editor, EM_AUTOURLDETECT, value == tomTrue, 0, &hr );
        return hr;
    default:
        return E_NOTIMPL;
    }
}

static HRESULT WINAPI fnTextDoc2_SetTypographyOptions( ITextDocument2 *iface, LONG options, LONG mask )
{
    struct text_services *services = impl_from_ITextDocument2( iface );

    services->typography_options = (services->typography_options & ~mask) | (options & mask);
    return S_OK;
}

static HRESULT WINAPI fnTextDoc2_GetMathProperties( ITextDocument2 *iface, LONG *options )
{
    struct text_services *services = impl_from_ITextDocument2( iface );

    if (!options) return E_INVALIDARG;
    *options = services->math_properties;
    return S_OK;
}

static HRESULT WINAPI fnTextDoc2_SetMathProperties( ITextDocument2 *iface, LONG options, LONG mask )
{
    struct text_services *services = impl_from_ITextDocument2( iface );

    services->math_properties = (services->math_properties & ~mask) | (options & mask);
    return S_OK;
}

static HRESULT WINAPI fnTextDoc2_GetActiveStory( ITextDocument2 *iface, ITextStory **story )
{
    if (story) *story = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_SetActiveStory( ITextDocument2 *iface, ITextStory *story )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_GetMainStory( ITextDocument2 *iface, ITextStory **story )
{
    if (story) *story = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_GetNewStory( ITextDocument2 *iface, ITextStory **story )
{
    if (story) *story = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI fnTextDoc2_GetStory( ITextDocument2 *iface, LONG index, ITextStory **story )
{
    if (story) *story = NULL;
    return E_NOTIMPL;
}

static const ITextDocument2Vtbl text_doc2_vtbl =
{
    fnTextDoc2_QueryInterface,
    fnTextDoc2_AddRef,
    fnTextDoc2_Release,
    fnTextDoc2_GetTypeInfoCount,
    fnTextDoc2_GetTypeInfo,
    fnTextDoc2_GetIDsOfNames,
    fnTextDoc2_Invoke,
    fnTextDoc2_GetName,
    fnTextDoc2_GetSelection,
    fnTextDoc2_GetStoryCount,
    fnTextDoc2_GetStoryRanges,
    fnTextDoc2_GetSaved,
    fnTextDoc2_SetSaved,
    fnTextDoc2_GetDefaultTabStop,
    fnTextDoc2_SetDefaultTabStop,
    fnTextDoc2_New,
    fnTextDoc2_Open,
    fnTextDoc2_Save,
    fnTextDoc2_Freeze,
    fnTextDoc2_Unfreeze,
    fnTextDoc2_BeginEditCollection,
    fnTextDoc2_EndEditCollection,
    fnTextDoc2_Undo,
    fnTextDoc2_Redo,
    fnTextDoc2_Range,
    fnTextDoc2_RangeFromPoint,
    fnTextDoc2_GetCaretType,
    fnTextDoc2_SetCaretType,
    fnTextDoc2_GetDisplays,
    fnTextDoc2_GetDocumentFont,
    fnTextDoc2_SetDocumentFont,
    fnTextDoc2_GetDocumentPara,
    fnTextDoc2_SetDocumentPara,
    fnTextDoc2_GetEastAsianFlags,
    fnTextDoc2_GetGenerator,
    fnTextDoc2_SetIMEInProgress,
    fnTextDoc2_GetNotificationMode,
    fnTextDoc2_SetNotificationMode,
    fnTextDoc2_GetSelection2,
    fnTextDoc2_GetStoryRanges2,
    fnTextDoc2_GetTypographyOptions,
    fnTextDoc2_GetVersion,
    fnTextDoc2_GetWindow,
    fnTextDoc2_AttachMsgFilter,
    fnTextDoc2_CheckTextLimit,
    fnTextDoc2_GetCallManager,
    fnTextDoc2_GetClientRect,
    fnTextDoc2_GetEffectColor,
    fnTextDoc2_GetImmContext,
    fnTextDoc2_GetPreferredFont,
    fnTextDoc2_GetProperty,
    fnTextDoc2_GetStrings,
    fnTextDoc2_Notify,
    fnTextDoc2_Range2,
    fnTextDoc2_RangeFromPoint2,
    fnTextDoc2_ReleaseCallManager,
    fnTextDoc2_ReleaseImmContext,
    fnTextDoc2_SetEffectColor,
    fnTextDoc2_SetProperty,
    fnTextDoc2_SetTypographyOptions,
    fnTextDoc2_SysBeep,
    fnTextDoc2_Update,
    fnTextDoc2_UpdateWindow,
    fnTextDoc2_GetMathProperties,
    fnTextDoc2_SetMathProperties,
    fnTextDoc2_GetActiveStory,
    fnTextDoc2_SetActiveStory,
    fnTextDoc2_GetMainStory,
    fnTextDoc2_GetNewStory,
    fnTextDoc2_GetStory,
};

HRESULT create_text_services( IUnknown *outer, ITextHost *text_host, IUnknown **unk, BOOL emulate_10 )
{
    struct text_services *services;

    TRACE( "%p %p --> %p\n", outer, text_host, unk );
    if (text_host == NULL) return E_POINTER;

    services = malloc( sizeof(*services) );
    if (services == NULL) return E_OUTOFMEMORY;
    services->ref = 1;
    services->IUnknown_inner.lpVtbl = &textservices_inner_vtbl;
    services->ITextServices_iface.lpVtbl = &textservices_vtbl.base;
    services->IRichEditOle_iface.lpVtbl = &re_ole_vtbl;
    services->ITextDocument2Old_iface.lpVtbl = &text_doc2old_vtbl;
    services->ITextDocument2_iface.lpVtbl = &text_doc2_vtbl;
    services->editor = ME_MakeEditor( text_host, emulate_10 );
    services->editor->richole = &services->IRichEditOle_iface;

    if (outer) services->outer_unk = outer;
    else services->outer_unk = &services->IUnknown_inner;

    services->text_selection = NULL;
    services->typography_options = 0;
    services->math_properties = 0;
    list_init( &services->rangelist );
    list_init( &services->clientsites );

    *unk = &services->IUnknown_inner;
    return S_OK;
}

/******************************************************************
 *        CreateTextServices (RICHED20.4)
 */
HRESULT WINAPI CreateTextServices( IUnknown *outer, ITextHost *text_host, IUnknown **unk )
{
    return create_text_services( outer, text_host, unk, FALSE );
}
