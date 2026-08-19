/*
 * Copyright 2008 Jacek Caban for CodeWeavers
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

#include <stdarg.h>

#define COBJMACROS

#include "windef.h"
#include "winbase.h"
#include "winuser.h"
#include "winreg.h"
#include "ole2.h"
#include "shlguid.h"
#include "wininet.h"
#include "winternl.h"

#include "mshtml_private.h"
#include "htmlscript.h"
#include "htmlevent.h"
#include "binding.h"

#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(mshtml);

const compat_mode_info_t compat_mode_info[] = {
    { 5, 7 },   /* DOCMODE_QUIRKS */
    { 5, 5 },   /* DOCMODE_IE5 */
    { 7, 7 },   /* DOCMODE_IE7 */
    { 8, 8 },   /* DOCMODE_IE8 */
    { 9, 9 },   /* DOCMODE_IE8 */
    { 10, 10 }, /* DOCMODE_IE10 */
    { 11, 11 }  /* DOCMODE_IE11 */
};

static const IID NS_ICONTENTUTILS_CID =
    {0x762C4AE7,0xB923,0x422F,{0xB9,0x7E,0xB9,0xBF,0xC1,0xEF,0x7B,0xF0}};

static nsIContentUtils *content_utils;

static BOOL is_iexplore(void)
{
    static volatile char cache = -1;
    BOOL ret = cache;
    if(ret == -1) {
        const WCHAR *p, *name = NtCurrentTeb()->Peb->ProcessParameters->ImagePathName.Buffer;
        if((p = wcsrchr(name, '/'))) name = p + 1;
        if((p = wcsrchr(name, '\\'))) name = p + 1;
        ret = !wcsicmp(name, L"iexplore.exe");
        cache = ret;
    }
    return ret;
}

static PRUnichar *handle_insert_comment(HTMLDocumentNode *doc, const PRUnichar *comment)
{
    unsigned majorv = 0, minorv = 0, compat_version;
    const PRUnichar *ptr, *end;
    PRUnichar *buf;
    DWORD len;

    enum {
        CMP_EQ,
        CMP_LT,
        CMP_LTE,
        CMP_GT,
        CMP_GTE
    } cmpt = CMP_EQ;

    static const PRUnichar endifW[] = {'<','!','[','e','n','d','i','f',']'};

    if(comment[0] != '[' || comment[1] != 'i' || comment[2] != 'f')
        return NULL;

    ptr = comment+3;
    while(iswspace(*ptr))
        ptr++;

    if(ptr[0] == 'l' && ptr[1] == 't') {
        ptr += 2;
        if(*ptr == 'e') {
            cmpt = CMP_LTE;
            ptr++;
        }else {
            cmpt = CMP_LT;
        }
    }else if(ptr[0] == 'g' && ptr[1] == 't') {
        ptr += 2;
        if(*ptr == 'e') {
            cmpt = CMP_GTE;
            ptr++;
        }else {
            cmpt = CMP_GT;
        }
    }

    if(!iswspace(*ptr++))
        return NULL;
    while(iswspace(*ptr))
        ptr++;

    if(ptr[0] != 'I' || ptr[1] != 'E')
        return NULL;

    ptr +=2;
    if(!iswspace(*ptr++))
        return NULL;
    while(iswspace(*ptr))
        ptr++;

    if(!is_digit(*ptr))
        return NULL;
    while(is_digit(*ptr))
        majorv = majorv*10 + (*ptr++ - '0');

    if(*ptr == '.') {
        ptr++;
        if(!is_digit(*ptr))
            return NULL;
        while(is_digit(*ptr))
            minorv = minorv*10 + (*ptr++ - '0');
    }

    while(iswspace(*ptr))
        ptr++;
    if(ptr[0] != ']' || ptr[1] != '>')
        return NULL;
    ptr += 2;

    len = lstrlenW(ptr);
    if(len < ARRAY_SIZE(endifW))
        return NULL;

    end = ptr + len - ARRAY_SIZE(endifW);
    if(memcmp(end, endifW, sizeof(endifW)))
        return NULL;

    compat_version = compat_mode_info[doc->document_mode].ie_version;

    switch(cmpt) {
    case CMP_EQ:
        if(compat_version == majorv && !minorv)
            break;
        return NULL;
    case CMP_LT:
        if(compat_version < majorv || (compat_version == majorv && minorv))
            break;
        return NULL;
    case CMP_LTE:
        if(compat_version <= majorv)
            break;
        return NULL;
    case CMP_GT:
        if(compat_version > majorv)
            break;
        return NULL;
    case CMP_GTE:
        if(compat_version >= majorv || (compat_version == majorv && !minorv))
            break;
        return NULL;
    }

    buf = malloc((end - ptr + 1) * sizeof(WCHAR));
    if(!buf)
        return NULL;

    memcpy(buf, ptr, (end-ptr)*sizeof(WCHAR));
    buf[end-ptr] = 0;

    return buf;
}

static nsresult run_insert_comment(HTMLDocumentNode *doc, nsISupports *comment_iface, nsISupports *arg2)
{
    const PRUnichar *comment;
    nsIDOMComment *nscomment;
    PRUnichar *replace_html;
    nsAString comment_str;
    nsresult nsres;

    nsres = nsISupports_QueryInterface(comment_iface, &IID_nsIDOMComment, (void**)&nscomment);
    if(NS_FAILED(nsres)) {
        ERR("Could not get nsIDOMComment iface:%08lx\n", nsres);
        return nsres;
    }

    nsAString_Init(&comment_str, NULL);
    nsres = nsIDOMComment_GetData(nscomment, &comment_str);
    if(NS_FAILED(nsres))
        return nsres;

    nsAString_GetData(&comment_str, &comment);
    replace_html = handle_insert_comment(doc, comment);
    nsAString_Finish(&comment_str);

    if(replace_html) {
        HRESULT hres;

        hres = replace_node_by_html(doc->dom_document, (nsIDOMNode*)nscomment, replace_html);
        free(replace_html);
        if(FAILED(hres))
            nsres = NS_ERROR_FAILURE;
    }


    nsIDOMComment_Release(nscomment);
    return nsres;
}

static nsresult run_bind_to_tree(HTMLDocumentNode *doc, nsISupports *nsiface, nsISupports *arg2)
{
    nsIDOMNode *nsnode;
    HTMLDOMNode *node;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", doc, nsiface);

    nsres = nsISupports_QueryInterface(nsiface, &IID_nsIDOMNode, (void**)&nsnode);
    if(NS_FAILED(nsres))
        return nsres;

    hres = get_node(nsnode, TRUE, &node);
    nsIDOMNode_Release(nsnode);
    if(FAILED(hres)) {
        ERR("Could not get node\n");
        return nsres;
    }

    if(node->vtbl->bind_to_tree)
        node->vtbl->bind_to_tree(node);

    node_release(node);
    return nsres;
}

/* Calls undocumented 69 cmd of CGID_Explorer */
static void call_explorer_69(HTMLDocumentObj *doc)
{
    IOleCommandTarget *olecmd;
    VARIANT var;
    HRESULT hres;

    if(!doc->client)
        return;

    hres = IOleClientSite_QueryInterface(doc->client, &IID_IOleCommandTarget, (void**)&olecmd);
    if(FAILED(hres))
        return;

    VariantInit(&var);
    hres = IOleCommandTarget_Exec(olecmd, &CGID_Explorer, 69, 0, NULL, &var);
    IOleCommandTarget_Release(olecmd);
    if(SUCCEEDED(hres) && V_VT(&var) != VT_NULL)
        FIXME("handle result\n");
}

static void parse_complete(HTMLDocumentObj *doc)
{
    TRACE("(%p)\n", doc);

    if(doc->nscontainer->usermode == EDITMODE)
        init_editor(doc->doc_node);

    call_explorer_69(doc);
    if(doc->view_sink)
        IAdviseSink_OnViewChange(doc->view_sink, DVASPECT_CONTENT, -1);
    call_property_onchanged(&doc->cp_container, 1005);
    call_explorer_69(doc);

    if(doc->webbrowser && !(doc->window->load_flags & BINDING_REFRESH))
        IDocObjectService_FireNavigateComplete2(doc->doc_object_service, &doc->window->base.IHTMLWindow2_iface, 0);

    /* FIXME: IE7 calls EnableModelless(TRUE), EnableModelless(FALSE) and sets interactive state here */
}

static nsresult run_end_load(HTMLDocumentNode *This, nsISupports *arg1, nsISupports *arg2)
{
    HTMLDocumentObj *doc_obj = This->doc_obj;
    HTMLInnerWindow *window = This->window;

    TRACE("(%p)\n", This);

    if(!doc_obj)
        return NS_OK;
    IHTMLWindow2_AddRef(&window->base.IHTMLWindow2_iface);

    if(This == doc_obj->doc_node) {
        /*
         * This should be done in the worker thread that parses HTML,
         * but we don't have such thread (Gecko parses HTML for us).
         */
        IUnknown_AddRef(doc_obj->outer_unk);
        parse_complete(doc_obj);
        IUnknown_Release(doc_obj->outer_unk);
    }

    bind_event_scripts(This);

    if(This->window == window && window->base.outer_window) {
        window->dom_interactive_time = get_time_stamp();
        set_ready_state(window->base.outer_window, READYSTATE_INTERACTIVE);
    }
    IHTMLWindow2_Release(&window->base.IHTMLWindow2_iface);
    return NS_OK;
}

static nsresult run_insert_script(HTMLDocumentNode *doc, nsISupports *script_iface, nsISupports *parser_iface)
{
    nsIDOMHTMLScriptElement *nsscript;
    HTMLScriptElement *script_elem;
    nsIParser *nsparser = NULL;
    script_queue_entry_t *iter;
    HTMLInnerWindow *window;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%p)\n", doc, script_iface);

    window = doc->window;
    if(!window)
        return NS_OK;

    nsres = nsISupports_QueryInterface(script_iface, &IID_nsIDOMHTMLScriptElement, (void**)&nsscript);
    if(NS_FAILED(nsres)) {
        ERR("Could not get nsIDOMHTMLScriptElement: %08lx\n", nsres);
        return nsres;
    }

    if(parser_iface) {
        nsres = nsISupports_QueryInterface(parser_iface, &IID_nsIParser, (void**)&nsparser);
        if(NS_FAILED(nsres)) {
            ERR("Could not get nsIParser iface: %08lx\n", nsres);
            nsparser = NULL;
        }
    }

    hres = script_elem_from_nsscript(nsscript, &script_elem);
    nsIDOMHTMLScriptElement_Release(nsscript);
    if(FAILED(hres)) {
        if(nsparser)
            nsIParser_Release(nsparser);
        return NS_ERROR_FAILURE;
    }

    if(nsparser) {
        nsIParser_BeginEvaluatingParserInsertedScript(nsparser);
        window->parser_callback_cnt++;
    }

    IHTMLWindow2_AddRef(&window->base.IHTMLWindow2_iface);

    doc_insert_script(window, script_elem, TRUE);

    while(!list_empty(&window->script_queue)) {
        iter = LIST_ENTRY(list_head(&window->script_queue), script_queue_entry_t, entry);
        list_remove(&iter->entry);
        if(!iter->script->parsed)
            doc_insert_script(window, iter->script, TRUE);
        IHTMLScriptElement_Release(&iter->script->IHTMLScriptElement_iface);
        free(iter);
    }

    if(nsparser) {
        window->parser_callback_cnt--;
        nsIParser_EndEvaluatingParserInsertedScript(nsparser);
        nsIParser_Release(nsparser);
    }

    IHTMLWindow2_Release(&window->base.IHTMLWindow2_iface);
    IHTMLScriptElement_Release(&script_elem->IHTMLScriptElement_iface);

    return NS_OK;
}

DWORD get_compat_mode_version(compat_mode_t compat_mode)
{
    switch(compat_mode) {
    case COMPAT_MODE_QUIRKS:
    case COMPAT_MODE_IE5:
    case COMPAT_MODE_IE7:
        return 7;
    case COMPAT_MODE_IE8:
        return 8;
    case COMPAT_MODE_IE9:
        return 9;
    case COMPAT_MODE_IE10:
        return 10;
    case COMPAT_MODE_IE11:
        return 11;
    DEFAULT_UNREACHABLE;
    }
    return 0;
}

/*
 * We may change document mode only in early stage of document lifetime.
 * Later attempts will not have an effect.
 */
compat_mode_t lock_document_mode(HTMLDocumentNode *doc)
{
    if(!doc->document_mode_locked) {
        doc->document_mode_locked = TRUE;

        if(doc->emulate_mode && doc->document_mode < COMPAT_MODE_IE10) {
            nsIDOMDocumentType *nsdoctype;

            if(NS_SUCCEEDED(nsIDOMDocument_GetDoctype(doc->dom_document, &nsdoctype)) && nsdoctype)
                nsIDOMDocumentType_Release(nsdoctype);
            else
                doc->document_mode = COMPAT_MODE_QUIRKS;
        }

        if(doc->html_document)
            nsIDOMHTMLDocument_SetIECompatMode(doc->html_document, get_compat_mode_version(doc->document_mode));
    }

    TRACE("%p: %d\n", doc, doc->document_mode);

    return doc->document_mode;
}

static void set_document_mode(HTMLDocumentNode *doc, compat_mode_t document_mode, BOOL emulate_mode, BOOL lock)
{
    compat_mode_t max_compat_mode;

    if(doc->document_mode_locked) {
        WARN("attempting to set document mode %d on locked document %p\n", document_mode, doc);
        return;
    }

    TRACE("%p: %d\n", doc, document_mode);

    max_compat_mode = doc->window && !is_detached_window(doc->window)
        ? get_max_compat_mode(doc->window->base.outer_window->uri)
        : COMPAT_MODE_IE11;
    if(max_compat_mode < document_mode) {
        WARN("Tried to set compat mode %u higher than maximal configured %u\n",
             document_mode, max_compat_mode);
        document_mode = max_compat_mode;
    }

    doc->document_mode = document_mode;
    doc->emulate_mode = emulate_mode;
    if(lock)
        lock_document_mode(doc);
}

static BOOL is_ua_compatible_delimiter(WCHAR c)
{
    return !c || c == ';' || c == ',' || iswspace(c);
}

const WCHAR *parse_compat_version(const WCHAR *version_string, compat_mode_t *r)
{
    DWORD version = 0;
    const WCHAR *p;

    for(p = version_string; '0' <= *p && *p <= '9'; p++)
        version = version * 10 + *p-'0';
    if(!is_ua_compatible_delimiter(*p) || p == version_string)
        return NULL;

    switch(version){
    case 5:
    case 6:
        *r = COMPAT_MODE_IE5;
        break;
    case 7:
        *r = COMPAT_MODE_IE7;
        break;
    case 8:
        *r = COMPAT_MODE_IE8;
        break;
    case 9:
        *r = COMPAT_MODE_IE9;
        break;
    case 10:
        *r = COMPAT_MODE_IE10;
        break;
    default:
        *r = version < 5 ? COMPAT_MODE_QUIRKS : COMPAT_MODE_IE11;
    }
    return p;
}

static compat_mode_t parse_ua_compatible(const WCHAR *p, BOOL *emulate_mode)
{
    static const WCHAR emulateIEW[] = {'E','m','u','l','a','t','e','I','E'};
    static const WCHAR ie_eqW[] = {'I','E','='};
    static const WCHAR edgeW[] = {'e','d','g','e'};
    compat_mode_t parsed_mode, mode = COMPAT_MODE_INVALID;
    *emulate_mode = FALSE;

    TRACE("%s\n", debugstr_w(p));

    if(wcsnicmp(ie_eqW, p, ARRAY_SIZE(ie_eqW)))
        return mode;
    p += 3;

    do {
        BOOL is_emulate = FALSE;

        while(iswspace(*p)) p++;
        if(!wcsnicmp(p, edgeW, ARRAY_SIZE(edgeW))) {
            p += ARRAY_SIZE(edgeW);
            if(is_ua_compatible_delimiter(*p))
                mode = COMPAT_MODE_IE11;
            break;
        }
        if(!wcsnicmp(p, emulateIEW, ARRAY_SIZE(emulateIEW))) {
            p += ARRAY_SIZE(emulateIEW);
            is_emulate = TRUE;
        }
        if(!(p = parse_compat_version(p, &parsed_mode)))
            break;
        if(mode < parsed_mode) {
            mode = parsed_mode;
            *emulate_mode = is_emulate;
        }
        while(iswspace(*p)) p++;
    } while(*p++ == ',');

    return mode;
}

void process_document_response_headers(HTMLDocumentNode *doc, IBinding *binding)
{
    IWinInetHttpInfo *http_info;
    char buf[1024];
    DWORD size;
    HRESULT hres;

    hres = IBinding_QueryInterface(binding, &IID_IWinInetHttpInfo, (void**)&http_info);
    if(FAILED(hres)) {
        TRACE("No IWinInetHttpInfo\n");
        return;
    }

    size = sizeof(buf);
    strcpy(buf, "X-UA-Compatible");
    hres = IWinInetHttpInfo_QueryInfo(http_info, HTTP_QUERY_CUSTOM, buf, &size, NULL, NULL);
    if(hres == S_OK && size) {
        compat_mode_t document_mode;
        BOOL emulate_mode;
        WCHAR *header;

        TRACE("size %lu\n", size);

        header = strdupAtoW(buf);
        if(header) {
            document_mode = parse_ua_compatible(header, &emulate_mode);

            if(document_mode != COMPAT_MODE_INVALID) {
                TRACE("setting document mode %d\n", document_mode);
                set_document_mode(doc, document_mode, emulate_mode, FALSE);
            }
        }
        free(header);
    }

    IWinInetHttpInfo_Release(http_info);
}

static void process_meta_element(HTMLDocumentNode *doc, nsIDOMHTMLMetaElement *meta_element)
{
    nsAString http_equiv_str, content_str;
    nsresult nsres;

    nsAString_Init(&http_equiv_str, NULL);
    nsAString_Init(&content_str, NULL);
    nsres = nsIDOMHTMLMetaElement_GetHttpEquiv(meta_element, &http_equiv_str);
    if(NS_SUCCEEDED(nsres))
        nsres = nsIDOMHTMLMetaElement_GetContent(meta_element, &content_str);

    if(NS_SUCCEEDED(nsres)) {
        const PRUnichar *http_equiv, *content;

        nsAString_GetData(&http_equiv_str, &http_equiv);
        nsAString_GetData(&content_str, &content);

        TRACE("%s: %s\n", debugstr_w(http_equiv), debugstr_w(content));

        if(!wcsicmp(http_equiv, L"x-ua-compatible")) {
            BOOL emulate_mode;
            compat_mode_t document_mode = parse_ua_compatible(content, &emulate_mode);

            if(document_mode != COMPAT_MODE_INVALID)
                set_document_mode(doc, document_mode, emulate_mode, TRUE);
            else
                FIXME("Unsupported document mode %s\n", debugstr_w(content));
        }
    }

    nsAString_Finish(&http_equiv_str);
    nsAString_Finish(&content_str);
}

typedef struct nsRunnable nsRunnable;

typedef nsresult (*runnable_proc_t)(HTMLDocumentNode*,nsISupports*,nsISupports*);

struct nsRunnable {
    nsIRunnable  nsIRunnable_iface;

    LONG ref;

    runnable_proc_t proc;

    HTMLDocumentNode *doc;
    nsISupports *arg1;
    nsISupports *arg2;
};

static inline nsRunnable *impl_from_nsIRunnable(nsIRunnable *iface)
{
    return CONTAINING_RECORD(iface, nsRunnable, nsIRunnable_iface);
}

static nsresult NSAPI nsRunnable_QueryInterface(nsIRunnable *iface,
        nsIIDRef riid, void **result)
{
    nsRunnable *This = impl_from_nsIRunnable(iface);

    if(IsEqualGUID(riid, &IID_nsISupports)) {
        TRACE("(%p)->(IID_nsISupports %p)\n", This, result);
        *result = &This->nsIRunnable_iface;
    }else if(IsEqualGUID(riid, &IID_nsIRunnable)) {
        TRACE("(%p)->(IID_nsIRunnable %p)\n", This, result);
        *result = &This->nsIRunnable_iface;
    }else {
        *result = NULL;
        WARN("(%p)->(%s %p)\n", This, debugstr_guid(riid), result);
        return NS_NOINTERFACE;
    }

    nsISupports_AddRef((nsISupports*)*result);
    return NS_OK;
}

static nsrefcnt NSAPI nsRunnable_AddRef(nsIRunnable *iface)
{
    nsRunnable *This = impl_from_nsIRunnable(iface);
    LONG ref = InterlockedIncrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    return ref;
}

static nsrefcnt NSAPI nsRunnable_Release(nsIRunnable *iface)
{
    nsRunnable *This = impl_from_nsIRunnable(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    TRACE("(%p) ref=%ld\n", This, ref);

    if(!ref) {
        IHTMLDOMNode_Release(&This->doc->node.IHTMLDOMNode_iface);
        if(This->arg1)
            nsISupports_Release(This->arg1);
        if(This->arg2)
            nsISupports_Release(This->arg2);
        free(This);
    }

    return ref;
}

static nsresult NSAPI nsRunnable_Run(nsIRunnable *iface)
{
    nsRunnable *This = impl_from_nsIRunnable(iface);
    nsresult nsres;

    block_task_processing();
    nsres = This->proc(This->doc, This->arg1, This->arg2);
    unblock_task_processing();
    return nsres;
}

static const nsIRunnableVtbl nsRunnableVtbl = {
    nsRunnable_QueryInterface,
    nsRunnable_AddRef,
    nsRunnable_Release,
    nsRunnable_Run
};

static BOOL add_script_runner(HTMLDocumentNode *This, runnable_proc_t proc, nsISupports *arg1, nsISupports *arg2)
{
    nsRunnable *runnable;
    nsresult nsres;

    if(!content_utils)
        return FALSE;

    runnable = calloc(1, sizeof(*runnable));
    if(!runnable)
        return FALSE;

    runnable->nsIRunnable_iface.lpVtbl = &nsRunnableVtbl;
    runnable->ref = 1;

    IHTMLDOMNode_AddRef(&This->node.IHTMLDOMNode_iface);
    runnable->doc = This;
    runnable->proc = proc;

    if(arg1)
        nsISupports_AddRef(arg1);
    runnable->arg1 = arg1;

    if(arg2)
        nsISupports_AddRef(arg2);
    runnable->arg2 = arg2;

    nsres = nsIContentUtils_AddScriptRunner(content_utils, &runnable->nsIRunnable_iface);

    nsIRunnable_Release(&runnable->nsIRunnable_iface);
    return NS_SUCCEEDED(nsres);
}

static inline HTMLDocumentNode *impl_from_nsIDocumentObserver(nsIDocumentObserver *iface)
{
    return CONTAINING_RECORD(iface, HTMLDocumentNode, nsIDocumentObserver_iface);
}

static nsresult NSAPI nsDocumentObserver_QueryInterface(nsIDocumentObserver *iface,
        nsIIDRef riid, void **result)
{
    HTMLDocumentNode *This = impl_from_nsIDocumentObserver(iface);

    if(IsEqualGUID(&IID_nsISupports, riid)) {
        TRACE("(%p)->(IID_nsISupports, %p)\n", This, result);
        *result = &This->nsIDocumentObserver_iface;
    }else if(IsEqualGUID(&IID_nsIMutationObserver, riid)) {
        TRACE("(%p)->(IID_nsIMutationObserver %p)\n", This, result);
        *result = &This->nsIDocumentObserver_iface;
    }else if(IsEqualGUID(&IID_nsIDocumentObserver, riid)) {
        TRACE("(%p)->(IID_nsIDocumentObserver %p)\n", This, result);
        *result = &This->nsIDocumentObserver_iface;
    }else {
        *result = NULL;
        TRACE("(%p)->(%s %p)\n", This, debugstr_guid(riid), result);
        return NS_NOINTERFACE;
    }

    IHTMLDOMNode_AddRef(&This->node.IHTMLDOMNode_iface);
    return NS_OK;
}

static nsrefcnt NSAPI nsDocumentObserver_AddRef(nsIDocumentObserver *iface)
{
    HTMLDocumentNode *This = impl_from_nsIDocumentObserver(iface);
    return IHTMLDOMNode_AddRef(&This->node.IHTMLDOMNode_iface);
}

static nsrefcnt NSAPI nsDocumentObserver_Release(nsIDocumentObserver *iface)
{
    HTMLDocumentNode *This = impl_from_nsIDocumentObserver(iface);
    return IHTMLDOMNode_Release(&This->node.IHTMLDOMNode_iface);
}

static void NSAPI nsDocumentObserver_CharacterDataWillChange(nsIDocumentObserver *iface,
        nsIDocument *aDocument, nsIContent *aContent, void /*CharacterDataChangeInfo*/ *aInfo)
{
}

static void NSAPI nsDocumentObserver_CharacterDataChanged(nsIDocumentObserver *iface,
        nsIDocument *aDocument, nsIContent *aContent, void /*CharacterDataChangeInfo*/ *aInfo)
{
}

static void NSAPI nsDocumentObserver_AttributeWillChange(nsIDocumentObserver *iface, nsIDocument *aDocument,
        void *aElement, LONG aNameSpaceID, nsIAtom *aAttribute, LONG aModType, const nsAttrValue *aNewValue)
{
}

static void NSAPI nsDocumentObserver_AttributeChanged(nsIDocumentObserver *iface, nsIDocument *aDocument,
        /*mozilla::dom::Element*/ void *aElement, LONG aNameSpaceID, nsIAtom *aAttribute, LONG aModType, const nsAttrValue *aOldValue)
{
    HTMLDocumentNode *This = impl_from_nsIDocumentObserver(iface);
    nsIDOMElement *elem;
    nsAString name_str;
    const WCHAR *name;
    nsresult nsres;

    nsAString_Init(&name_str, NULL);
    nsres = nsIAtom_ScriptableToString(aAttribute, &name_str);
    assert(nsres == NS_OK);
    nsAString_GetData(&name_str, &name);

    TRACE("(%p)->(%p, %s)\n", This, aElement, debugstr_w(name));

    nsres = nsISupports_QueryInterface(aElement, &IID_nsIDOMElement, (void **)&elem);
    assert(nsres == NS_OK);

    event_attr_changed(This, elem, name);
    nsAString_Finish(&name_str);
    nsIDOMElement_Release(elem);
}

static void NSAPI nsDocumentObserver_NativeAnonymousChildListChange(nsIDocumentObserver *iface, nsIDocument *aDocument,
        nsIContent *aContent, cpp_bool aIsRemove)
{
}

static void NSAPI nsDocumentObserver_AttributeSetToCurrentValue(nsIDocumentObserver *iface, nsIDocument *aDocument,
        void *aElement, LONG aNameSpaceID, nsIAtom *aAttribute)
{
}

static void NSAPI nsDocumentObserver_ContentAppended(nsIDocumentObserver *iface, nsIDocument *aDocument,
        nsIContent *aContainer, nsIContent *aFirstNewContent, LONG aNewIndexInContainer)
{
}

static void NSAPI nsDocumentObserver_ContentInserted(nsIDocumentObserver *iface, nsIDocument *aDocument,
        nsIContent *aContainer, nsIContent *aChild, LONG aIndexInContainer)
{
}

static void NSAPI nsDocumentObserver_ContentRemoved(nsIDocumentObserver *iface, nsIDocument *aDocument,
        nsIContent *aContainer, nsIContent *aChild, LONG aIndexInContainer,
        nsIContent *aProviousSibling)
{
}

static void NSAPI nsDocumentObserver_NodeWillBeDestroyed(nsIDocumentObserver *iface, const nsINode *aNode)
{
}

static void NSAPI nsDocumentObserver_ParentChainChanged(nsIDocumentObserver *iface, nsIContent *aContent)
{
}

static void NSAPI nsDocumentObserver_BeginUpdate(nsIDocumentObserver *iface, nsIDocument *aDocument,
        nsUpdateType aUpdateType)
{
}

static void NSAPI nsDocumentObserver_EndUpdate(nsIDocumentObserver *iface, nsIDocument *aDocument,
        nsUpdateType aUpdateType)
{
}

static void NSAPI nsDocumentObserver_BeginLoad(nsIDocumentObserver *iface, nsIDocument *aDocument)
{
}

static void NSAPI nsDocumentObserver_EndLoad(nsIDocumentObserver *iface, nsIDocument *aDocument)
{
    HTMLDocumentNode *This = impl_from_nsIDocumentObserver(iface);

    TRACE("(%p)\n", This);

    if(This->skip_mutation_notif)
        return;

    This->content_ready = TRUE;
    add_script_runner(This, run_end_load, NULL, NULL);
}

static void NSAPI nsDocumentObserver_ContentStatesChanged(nsIDocumentObserver *iface, nsIDocument *aDocument,
        nsIContent *aContent, EventStates aStateMask)
{
}

static void NSAPI nsDocumentObserver_DocumentStatesChanged(nsIDocumentObserver *iface, nsIDocument *aDocument,
        EventStates aStateMask)
{
}

static void NSAPI nsDocumentObserver_StyleSheetAdded(nsIDocumentObserver *iface, mozilla_StyleSheetHandle aStyleSheet,
        cpp_bool aDocumentSheet)
{
}

static void NSAPI nsDocumentObserver_StyleSheetRemoved(nsIDocumentObserver *iface, mozilla_StyleSheetHandle aStyleSheet,
        cpp_bool aDocumentSheet)
{
}

static void NSAPI nsDocumentObserver_StyleSheetApplicableStateChanged(nsIDocumentObserver *iface,
        mozilla_StyleSheetHandle aStyleSheet)
{
}

static void NSAPI nsDocumentObserver_StyleRuleChanged(nsIDocumentObserver *iface, mozilla_StyleSheetHandle aStyleSheet)
{
}

static void NSAPI nsDocumentObserver_StyleRuleAdded(nsIDocumentObserver *iface, mozilla_StyleSheetHandle aStyleSheet)
{
}

static void NSAPI nsDocumentObserver_StyleRuleRemoved(nsIDocumentObserver *iface, mozilla_StyleSheetHandle aStyleSheet)
{
}

static void NSAPI nsDocumentObserver_BindToDocument(nsIDocumentObserver *iface, nsIDocument *aDocument,
        nsIContent *aContent)
{
    HTMLDocumentNode *This = impl_from_nsIDocumentObserver(iface);
    nsIDOMHTMLIFrameElement *nsiframe;
    nsIDOMHTMLFrameElement *nsframe;
    nsIDOMHTMLScriptElement *nsscript;
    nsIDOMHTMLMetaElement *nsmeta;
    nsIDOMElement *nselem;
    nsIDOMComment *nscomment;
    nsresult nsres;

    TRACE("(%p)->(%p %p)\n", This, aDocument, aContent);

    if(This->document_mode < COMPAT_MODE_IE10) {
        nsres = nsIContent_QueryInterface(aContent, &IID_nsIDOMComment, (void**)&nscomment);
        if(NS_SUCCEEDED(nsres)) {
            TRACE("comment node\n");

            add_script_runner(This, run_insert_comment, (nsISupports*)nscomment, NULL);
            nsIDOMComment_Release(nscomment);
            return;
        }
    }

    if(This->document_mode == COMPAT_MODE_QUIRKS) {
        nsIDOMDocumentType *nsdoctype;

        nsres = nsIContent_QueryInterface(aContent, &IID_nsIDOMDocumentType, (void**)&nsdoctype);
        if(NS_SUCCEEDED(nsres)) {
            compat_mode_t mode = COMPAT_MODE_IE7;

            TRACE("doctype node\n");

            /* Native mshtml hardcodes special behavior for iexplore.exe here. The feature control registry
               keys under HKLM or HKCU\Software\Microsoft\Internet Explorer\Main\FeatureControl are not used
               in this case (neither in Wow6432Node), although FEATURE_BROWSER_EMULATION does override this,
               but it is not set by default on native, and the behavior is still different. This was tested
               by removing all iexplore.exe values from any FeatureControl subkeys, and renaming the test
               executable to iexplore.exe, which changed its default compat mode in such cases. */
            if(This->window && This->window->base.outer_window && is_iexplore()) {
                HTMLOuterWindow *window = This->window->base.outer_window;
                DWORD zone;
                HRESULT hres;

                /* Internet URL zone is treated differently and defaults to the latest supported mode. */
                hres = IInternetSecurityManager_MapUrlToZone(get_security_manager(), window->url, &zone, 0);
                if(SUCCEEDED(hres) && zone == URLZONE_INTERNET)
                    mode = COMPAT_MODE_IE11;
            }

            set_document_mode(This, mode, FALSE, FALSE);
            nsIDOMDocumentType_Release(nsdoctype);
        }
    }

    nsres = nsIContent_QueryInterface(aContent, &IID_nsIDOMElement, (void**)&nselem);
    if(NS_FAILED(nsres))
        return;

    check_event_attr(This, nselem);
    nsIDOMElement_Release(nselem);

    nsres = nsIContent_QueryInterface(aContent, &IID_nsIDOMHTMLIFrameElement, (void**)&nsiframe);
    if(NS_SUCCEEDED(nsres)) {
        TRACE("iframe node\n");

        add_script_runner(This, run_bind_to_tree, (nsISupports*)nsiframe, NULL);
        nsIDOMHTMLIFrameElement_Release(nsiframe);
        return;
    }

    nsres = nsIContent_QueryInterface(aContent, &IID_nsIDOMHTMLFrameElement, (void**)&nsframe);
    if(NS_SUCCEEDED(nsres)) {
        TRACE("frame node\n");

        add_script_runner(This, run_bind_to_tree, (nsISupports*)nsframe, NULL);
        nsIDOMHTMLFrameElement_Release(nsframe);
        return;
    }

    nsres = nsIContent_QueryInterface(aContent, &IID_nsIDOMHTMLScriptElement, (void**)&nsscript);
    if(NS_SUCCEEDED(nsres)) {
        TRACE("script element\n");

        add_script_runner(This, run_bind_to_tree, (nsISupports*)nsscript, NULL);
        nsIDOMHTMLScriptElement_Release(nsscript);
        return;
    }

    nsres = nsIContent_QueryInterface(aContent, &IID_nsIDOMHTMLMetaElement, (void**)&nsmeta);
    if(NS_SUCCEEDED(nsres)) {
        process_meta_element(This, nsmeta);
        nsIDOMHTMLMetaElement_Release(nsmeta);
    }
}

static void NSAPI nsDocumentObserver_AttemptToExecuteScript(nsIDocumentObserver *iface, nsIContent *aContent,
        nsIParser *aParser, cpp_bool *aBlock)
{
    HTMLDocumentNode *This = impl_from_nsIDocumentObserver(iface);
    nsIDOMHTMLScriptElement *nsscript;
    nsresult nsres;

    TRACE("(%p)->(%p %p %p)\n", This, aContent, aParser, aBlock);

    nsres = nsIContent_QueryInterface(aContent, &IID_nsIDOMHTMLScriptElement, (void**)&nsscript);
    if(NS_SUCCEEDED(nsres)) {
        TRACE("script node\n");

        lock_document_mode(This);
        add_script_runner(This, run_insert_script, (nsISupports*)nsscript, (nsISupports*)aParser);
        nsIDOMHTMLScriptElement_Release(nsscript);
    }
}

static const nsIDocumentObserverVtbl nsDocumentObserverVtbl = {
    nsDocumentObserver_QueryInterface,
    nsDocumentObserver_AddRef,
    nsDocumentObserver_Release,
    nsDocumentObserver_CharacterDataWillChange,
    nsDocumentObserver_CharacterDataChanged,
    nsDocumentObserver_AttributeWillChange,
    nsDocumentObserver_AttributeChanged,
    nsDocumentObserver_NativeAnonymousChildListChange,
    nsDocumentObserver_AttributeSetToCurrentValue,
    nsDocumentObserver_ContentAppended,
    nsDocumentObserver_ContentInserted,
    nsDocumentObserver_ContentRemoved,
    nsDocumentObserver_NodeWillBeDestroyed,
    nsDocumentObserver_ParentChainChanged,
    nsDocumentObserver_BeginUpdate,
    nsDocumentObserver_EndUpdate,
    nsDocumentObserver_BeginLoad,
    nsDocumentObserver_EndLoad,
    nsDocumentObserver_ContentStatesChanged,
    nsDocumentObserver_DocumentStatesChanged,
    nsDocumentObserver_StyleSheetAdded,
    nsDocumentObserver_StyleSheetRemoved,
    nsDocumentObserver_StyleSheetApplicableStateChanged,
    nsDocumentObserver_StyleRuleChanged,
    nsDocumentObserver_StyleRuleAdded,
    nsDocumentObserver_StyleRuleRemoved,
    nsDocumentObserver_BindToDocument,
    nsDocumentObserver_AttemptToExecuteScript
};

void init_document_mutation(HTMLDocumentNode *doc)
{
    nsIDocument *nsdoc;
    nsresult nsres;

    doc->nsIDocumentObserver_iface.lpVtbl = &nsDocumentObserverVtbl;

    nsres = nsIDOMDocument_QueryInterface(doc->dom_document, &IID_nsIDocument, (void**)&nsdoc);
    if(NS_FAILED(nsres)) {
        ERR("Could not get nsIDocument: %08lx\n", nsres);
        return;
    }

    nsIContentUtils_AddDocumentObserver(content_utils, nsdoc, &doc->nsIDocumentObserver_iface);
    nsIDocument_Release(nsdoc);
}

void release_document_mutation(HTMLDocumentNode *doc)
{
    nsIDocument *nsdoc;
    nsresult nsres;

    nsres = nsIDOMDocument_QueryInterface(doc->dom_document, &IID_nsIDocument, (void**)&nsdoc);
    if(NS_FAILED(nsres)) {
        ERR("Could not get nsIDocument: %08lx\n", nsres);
        return;
    }

    nsIContentUtils_RemoveDocumentObserver(content_utils, nsdoc, &doc->nsIDocumentObserver_iface);
    nsIDocument_Release(nsdoc);
}

JSContext *get_context_from_document(nsIDOMDocument *nsdoc)
{
    nsIDocument *doc;
    JSContext *ctx;
    nsresult nsres;

    nsres = nsIDOMDocument_QueryInterface(nsdoc, &IID_nsIDocument, (void**)&doc);
    assert(nsres == NS_OK);

    ctx = nsIContentUtils_GetContextFromDocument(content_utils, doc);
    nsIDocument_Release(doc);

    TRACE("ret %p\n", ctx);
    return ctx;
}

static ExternalCycleCollectionParticipant mutation_node_list_ccp;
static nsresult NSAPI mutation_node_list_traverse(void*,void*,nsCycleCollectionTraversalCallback*);
static nsresult NSAPI mutation_node_list_unlink(void*);
static void NSAPI mutation_node_list_delete_cycle_collectable(void*);

void init_mutation(nsIComponentManager *component_manager)
{
    static const CCObjCallback node_list_ccp_callback = {
        mutation_node_list_traverse,
        mutation_node_list_unlink,
        mutation_node_list_delete_cycle_collectable
    };
    nsIFactory *factory;
    nsresult nsres;

    if(!component_manager) {
        if(content_utils) {
            nsIContentUtils_Release(content_utils);
            content_utils = NULL;
        }
        return;
    }

    nsres = nsIComponentManager_GetClassObject(component_manager, &NS_ICONTENTUTILS_CID,
            &IID_nsIFactory, (void**)&factory);
    if(NS_FAILED(nsres)) {
        ERR("Could not create nsIContentUtils service: %08lx\n", nsres);
        return;
    }

    ccp_init(&mutation_node_list_ccp, &node_list_ccp_callback);
    nsres = nsIFactory_CreateInstance(factory, NULL, &IID_nsIContentUtils, (void**)&content_utils);
    nsIFactory_Release(factory);
    if(NS_FAILED(nsres))
        ERR("Could not create nsIContentUtils instance: %08lx\n", nsres);
}

struct mutation_observer_target {
    struct list entry;
    HTMLDOMNode *node;
    nsINode *native_node;
    BOOL native_registered;
    BOOL transient;
    BOOL character_data;
    BOOL child_list;
    BOOL subtree;
};

struct mutation_observer_record {
    struct list entry;
    HTMLDOMNode *target;
    BOOL child_list;
    nsIDOMNode *previous_sibling;
    nsIDOMNode *next_sibling;
    UINT32 added_count;
    nsIDOMNode **added_nodes;
    UINT32 removed_count;
    nsIDOMNode **removed_nodes;
};

struct mutation_observer_runner {
    nsISupports nsISupports_iface;
    LONG ref;
    ULONG generation;
};

struct mutation_node_list {
    nsIDOMNodeList nsIDOMNodeList_iface;
    nsCycleCollectingAutoRefCnt ccref;
    UINT32 count;
    nsIDOMNode **nodes;
};

struct mutation_observer {
    IWineMSHTMLMutationObserver IWineMSHTMLMutationObserver_iface;
    nsIMutationObserver nsIMutationObserver_iface;

    DispatchEx dispex;
    IDispatch *callback;
    struct list targets;
    struct list records;
    ULONG delivery_generation;
    BOOL delivery_pending;
    BOOL observing_ref;
};

static inline struct mutation_observer *impl_from_IWineMSHTMLMutationObserver(IWineMSHTMLMutationObserver *iface)
{
    return CONTAINING_RECORD(iface, struct mutation_observer, IWineMSHTMLMutationObserver_iface);
}

static inline struct mutation_observer *impl_from_nsIMutationObserver(nsIMutationObserver *iface)
{
    return CONTAINING_RECORD(iface, struct mutation_observer, nsIMutationObserver_iface);
}

static inline struct mutation_observer_runner *impl_from_mutation_runner(nsISupports *iface)
{
    return CONTAINING_RECORD(iface, struct mutation_observer_runner, nsISupports_iface);
}

static inline struct mutation_node_list *impl_from_mutation_node_list(nsIDOMNodeList *iface)
{
    return CONTAINING_RECORD(iface, struct mutation_node_list, nsIDOMNodeList_iface);
}

static void mutation_observer_clear_records(struct mutation_observer *This);
static void mutation_observer_disconnect_internal(struct mutation_observer *This);
static HRESULT mutation_observer_create_records(struct mutation_observer *This, IDispatch **ret);
static HRESULT mutation_observer_get_native_node(HTMLDOMNode *node, nsINode **ret);
static void mutation_observer_schedule(struct mutation_observer *This);

static nsresult NSAPI mutation_runner_QueryInterface(nsISupports *iface, nsIIDRef riid, void **result)
{
    if(!IsEqualGUID(riid, &IID_nsISupports)) {
        *result = NULL;
        return NS_NOINTERFACE;
    }

    *result = iface;
    nsISupports_AddRef(iface);
    return NS_OK;
}

static nsrefcnt NSAPI mutation_runner_AddRef(nsISupports *iface)
{
    struct mutation_observer_runner *This = impl_from_mutation_runner(iface);
    return InterlockedIncrement(&This->ref);
}

static nsrefcnt NSAPI mutation_runner_Release(nsISupports *iface)
{
    struct mutation_observer_runner *This = impl_from_mutation_runner(iface);
    LONG ref = InterlockedDecrement(&This->ref);

    if(!ref)
        free(This);
    return ref;
}

static const nsISupportsVtbl mutation_runner_vtbl = {
    mutation_runner_QueryInterface,
    mutation_runner_AddRef,
    mutation_runner_Release
};

static nsresult NSAPI mutation_node_list_QueryInterface(nsIDOMNodeList *iface, nsIIDRef riid, void **result)
{
    struct mutation_node_list *This = impl_from_mutation_node_list(iface);

    if(IsEqualGUID(riid, &IID_nsXPCOMCycleCollectionParticipant)) {
        *result = &mutation_node_list_ccp;
        return NS_OK;
    }
    if(IsEqualGUID(riid, &IID_nsCycleCollectionISupports)) {
        *result = iface;
        return NS_OK;
    }
    if(!IsEqualGUID(riid, &IID_nsISupports) && !IsEqualGUID(riid, &IID_nsIDOMNodeList)) {
        *result = NULL;
        return NS_NOINTERFACE;
    }

    *result = iface;
    ccref_incr(&This->ccref, (nsISupports *)iface);
    return NS_OK;
}

static nsrefcnt NSAPI mutation_node_list_AddRef(nsIDOMNodeList *iface)
{
    struct mutation_node_list *This = impl_from_mutation_node_list(iface);
    return ccref_incr(&This->ccref, (nsISupports *)iface);
}

static nsrefcnt NSAPI mutation_node_list_Release(nsIDOMNodeList *iface)
{
    struct mutation_node_list *This = impl_from_mutation_node_list(iface);
    return ccref_decr(&This->ccref, (nsISupports *)iface, &mutation_node_list_ccp);
}

static nsresult NSAPI mutation_node_list_traverse(void *ccp, void *object,
        nsCycleCollectionTraversalCallback *callback)
{
    struct mutation_node_list *This = impl_from_mutation_node_list(object);
    UINT32 i;

    describe_cc_node(&This->ccref, "MutationNodeList", callback);
    for(i = 0; i < This->count; i++)
        note_cc_edge((nsISupports *)This->nodes[i], "node", callback);
    return NS_OK;
}

static nsresult NSAPI mutation_node_list_unlink(void *object)
{
    struct mutation_node_list *This = impl_from_mutation_node_list(object);
    UINT32 i;

    for(i = 0; i < This->count; i++)
        nsIDOMNode_Release(This->nodes[i]);
    free(This->nodes);
    This->nodes = NULL;
    This->count = 0;
    return NS_OK;
}

static void NSAPI mutation_node_list_delete_cycle_collectable(void *object)
{
    struct mutation_node_list *This = impl_from_mutation_node_list(object);

    mutation_node_list_unlink(object);
    free(This);
}

static nsresult NSAPI mutation_node_list_Item(nsIDOMNodeList *iface, UINT32 index, nsIDOMNode **ret)
{
    struct mutation_node_list *This = impl_from_mutation_node_list(iface);

    if(!ret)
        return NS_ERROR_FAILURE;
    *ret = NULL;
    if(index >= This->count)
        return NS_OK;

    nsIDOMNode_AddRef(This->nodes[index]);
    *ret = This->nodes[index];
    return NS_OK;
}

static nsresult NSAPI mutation_node_list_GetLength(nsIDOMNodeList *iface, UINT32 *ret)
{
    struct mutation_node_list *This = impl_from_mutation_node_list(iface);

    if(!ret)
        return NS_ERROR_FAILURE;
    *ret = This->count;
    return NS_OK;
}

static const nsIDOMNodeListVtbl mutation_node_list_vtbl = {
    mutation_node_list_QueryInterface,
    mutation_node_list_AddRef,
    mutation_node_list_Release,
    mutation_node_list_Item,
    mutation_node_list_GetLength
};

static BOOL mutation_observer_array_size(UINT32 count, SIZE_T element_size, SIZE_T *size)
{
    if(count && element_size > ~(SIZE_T)0 / count)
        return FALSE;
    *size = (SIZE_T)count * element_size;
    return TRUE;
}

static HRESULT mutation_node_list_create(UINT32 count, nsIDOMNode *const *nodes, nsIDOMNodeList **ret)
{
    struct mutation_node_list *list;
    UINT32 i;

    *ret = NULL;
    list = calloc(1, sizeof(*list));
    if(!list)
        return E_OUTOFMEMORY;

    list->nsIDOMNodeList_iface.lpVtbl = &mutation_node_list_vtbl;
    ccref_init(&list->ccref, 1);
    list->count = count;
    if(count) {
        SIZE_T size;

        if(!mutation_observer_array_size(count, sizeof(*list->nodes), &size)) {
            free(list);
            return E_OUTOFMEMORY;
        }
        list->nodes = calloc(1, size);
        if(!list->nodes) {
            free(list);
            return E_OUTOFMEMORY;
        }
        for(i = 0; i < count; i++) {
            nsIDOMNode_AddRef(nodes[i]);
            list->nodes[i] = nodes[i];
        }
    }

    *ret = &list->nsIDOMNodeList_iface;
    return S_OK;
}

static void mutation_observer_free_record(struct mutation_observer_record *record)
{
    UINT32 i;

    if(record->target)
        node_release(record->target);
    if(record->previous_sibling)
        nsIDOMNode_Release(record->previous_sibling);
    if(record->next_sibling)
        nsIDOMNode_Release(record->next_sibling);
    for(i = 0; i < record->added_count; i++)
        nsIDOMNode_Release(record->added_nodes[i]);
    for(i = 0; i < record->removed_count; i++)
        nsIDOMNode_Release(record->removed_nodes[i]);
    free(record->added_nodes);
    free(record->removed_nodes);
    free(record);
}

static void mutation_observer_clear_records(struct mutation_observer *This)
{
    struct mutation_observer_record *record, *next;

    LIST_FOR_EACH_ENTRY_SAFE(record, next, &This->records, struct mutation_observer_record, entry) {
        list_remove(&record->entry);
        mutation_observer_free_record(record);
    }
}

static void mutation_observer_detach_records(struct mutation_observer *This, struct list *records)
{
    struct mutation_observer_record *record;

    list_init(records);
    while(!list_empty(&This->records)) {
        record = LIST_ENTRY(list_head(&This->records), struct mutation_observer_record, entry);
        list_remove(&record->entry);
        list_add_tail(records, &record->entry);
    }
}

static void mutation_observer_restore_records(struct mutation_observer *This, struct list *records)
{
    struct mutation_observer_record *record;

    while(!list_empty(records)) {
        record = LIST_ENTRY(list_tail(records), struct mutation_observer_record, entry);
        list_remove(&record->entry);
        list_add_head(&This->records, &record->entry);
    }
}

static HRESULT mutation_observer_create_array(struct mutation_observer *This, DWORD length,
        IWineJSDispatch **ret)
{
    HTMLInnerWindow *window;
    HRESULT hres;

    *ret = NULL;
    window = get_script_global(&This->dispex);
    if(!window || !window->jscript) {
        if(window)
            IHTMLWindow2_Release(&window->base.IHTMLWindow2_iface);
        return E_UNEXPECTED;
    }

    hres = IWineJScript_CreateArray(window->jscript, length, ret);
    IHTMLWindow2_Release(&window->base.IHTMLWindow2_iface);
    return hres;
}

static HRESULT mutation_observer_define(IWineJSDispatch *object, const WCHAR *name, VARIANT *value)
{
    return IWineJSDispatch_DefineProperty(object, name, PROPF_ENUMERABLE, value);
}

static HRESULT mutation_observer_define_array_element(IWineJSDispatch *object, const WCHAR *name,
        VARIANT *value)
{
    return IWineJSDispatch_DefineProperty(object, name,
            PROPF_ENUMERABLE | PROPF_WRITABLE | PROPF_CONFIGURABLE, value);
}

static HRESULT mutation_observer_define_null(IWineJSDispatch *object, const WCHAR *name)
{
    VARIANT value;
    HRESULT hres;

    VariantInit(&value);
    V_VT(&value) = VT_NULL;
    hres = mutation_observer_define(object, name, &value);
    VariantClear(&value);
    return hres;
}

static HRESULT mutation_observer_define_string(IWineJSDispatch *object, const WCHAR *name, const WCHAR *value)
{
    VARIANT var;
    HRESULT hres;

    VariantInit(&var);
    V_VT(&var) = VT_BSTR;
    V_BSTR(&var) = SysAllocString(value);
    if(!V_BSTR(&var))
        return E_OUTOFMEMORY;
    hres = mutation_observer_define(object, name, &var);
    VariantClear(&var);
    return hres;
}

static HRESULT mutation_observer_define_node(IWineJSDispatch *object, const WCHAR *name,
        nsIDOMNode *node)
{
    HTMLDOMNode *html_node;
    IDispatch *dispatch;
    VARIANT value;
    HRESULT hres;

    if(!node)
        return mutation_observer_define_null(object, name);

    hres = get_node(node, TRUE, &html_node);
    if(FAILED(hres))
        return hres;
    hres = IHTMLDOMNode_QueryInterface(&html_node->IHTMLDOMNode_iface, &IID_IDispatch,
            (void **)&dispatch);
    node_release(html_node);
    if(FAILED(hres))
        return hres;

    VariantInit(&value);
    V_VT(&value) = VT_DISPATCH;
    V_DISPATCH(&value) = dispatch;
    hres = mutation_observer_define(object, name, &value);
    VariantClear(&value);
    return hres;
}

static HRESULT mutation_observer_define_nodes(IWineJSDispatch *object, const WCHAR *name,
        DispatchEx *owner, UINT32 count, nsIDOMNode *const *nodes)
{
    IHTMLDOMChildrenCollection *collection;
    nsIDOMNodeList *node_list;
    VARIANT value;
    HRESULT hres;

    hres = mutation_node_list_create(count, nodes, &node_list);
    if(FAILED(hres))
        return hres;
    hres = create_child_collection(node_list, owner, &collection);
    nsIDOMNodeList_Release(node_list);
    if(FAILED(hres))
        return hres;

    VariantInit(&value);
    V_VT(&value) = VT_DISPATCH;
    V_DISPATCH(&value) = (IDispatch *)collection;
    hres = mutation_observer_define(object, name, &value);
    VariantClear(&value);
    return hres;
}

static HRESULT mutation_observer_create_record(struct mutation_observer *This,
        IWineJScript *script, struct mutation_observer_record *record, IDispatch **ret)
{
    IWineJSDispatch *object;
    VARIANT value;
    HRESULT hres;

    *ret = NULL;
    hres = IWineJScript_CreateObject(script, &object);
    if(FAILED(hres))
        return hres;

    hres = mutation_observer_define_string(object, L"type",
            record->child_list ? L"childList" : L"characterData");
    if(SUCCEEDED(hres)) {
        VariantInit(&value);
        V_VT(&value) = VT_DISPATCH;
        V_DISPATCH(&value) = (IDispatch *)&record->target->IHTMLDOMNode_iface;
        IDispatch_AddRef(V_DISPATCH(&value));
        hres = mutation_observer_define(object, L"target", &value);
        VariantClear(&value);
    }
    if(SUCCEEDED(hres))
        hres = mutation_observer_define_node(object, L"previousSibling", record->previous_sibling);
    if(SUCCEEDED(hres))
        hres = mutation_observer_define_node(object, L"nextSibling", record->next_sibling);
    if(SUCCEEDED(hres)) hres = mutation_observer_define_null(object, L"attributeName");
    if(SUCCEEDED(hres)) hres = mutation_observer_define_null(object, L"attributeNamespace");
    if(SUCCEEDED(hres)) hres = mutation_observer_define_null(object, L"oldValue");
    if(SUCCEEDED(hres))
        hres = mutation_observer_define_nodes(object, L"addedNodes", &This->dispex,
                record->added_count, record->added_nodes);
    if(SUCCEEDED(hres))
        hres = mutation_observer_define_nodes(object, L"removedNodes", &This->dispex,
                record->removed_count, record->removed_nodes);

    if(SUCCEEDED(hres))
        *ret = (IDispatch *)object;
    else
        IWineJSDispatch_Release(object);
    return hres;
}

static HRESULT mutation_observer_create_records(struct mutation_observer *This, IDispatch **ret)
{
    HTMLInnerWindow *window;
    IWineJSDispatch *array;
    struct list records;
    struct mutation_observer_record *record;
    ULONG index = 0;
    WCHAR name[32];
    VARIANT value;
    HRESULT hres;

    *ret = NULL;
    hres = mutation_observer_create_array(This, list_count(&This->records), &array);
    if(FAILED(hres))
        return hres;

    window = get_script_global(&This->dispex);
    if(!window || !window->jscript) {
        if(window)
            IHTMLWindow2_Release(&window->base.IHTMLWindow2_iface);
        IWineJSDispatch_Release(array);
        return E_UNEXPECTED;
    }

    hres = S_OK;
    mutation_observer_detach_records(This, &records);
    LIST_FOR_EACH_ENTRY(record, &records, struct mutation_observer_record, entry) {
        IDispatch *record_disp;

        hres = mutation_observer_create_record(This, window->jscript, record, &record_disp);
        if(FAILED(hres))
            break;

        swprintf(name, ARRAY_SIZE(name), L"%u", index++);
        VariantInit(&value);
        V_VT(&value) = VT_DISPATCH;
        V_DISPATCH(&value) = record_disp;
        hres = mutation_observer_define_array_element(array, name, &value);
        VariantClear(&value);
        if(FAILED(hres))
            break;
    }

    IHTMLWindow2_Release(&window->base.IHTMLWindow2_iface);
    if(FAILED(hres)) {
        mutation_observer_restore_records(This, &records);
        IWineJSDispatch_Release(array);
        return hres;
    }

    while(!list_empty(&records)) {
        record = LIST_ENTRY(list_head(&records), struct mutation_observer_record, entry);
        list_remove(&record->entry);
        mutation_observer_free_record(record);
    }

    *ret = (IDispatch *)array;
    return S_OK;
}

static nsresult NSAPI mutation_observer_ns_QueryInterface(nsIMutationObserver *iface,
        nsIIDRef riid, void **result)
{
    struct mutation_observer *This = impl_from_nsIMutationObserver(iface);

    if(!IsEqualGUID(riid, &IID_nsISupports) && !IsEqualGUID(riid, &IID_nsIMutationObserver)) {
        *result = NULL;
        return NS_NOINTERFACE;
    }

    *result = &This->nsIMutationObserver_iface;
    IWineMSHTMLMutationObserver_AddRef(&This->IWineMSHTMLMutationObserver_iface);
    return NS_OK;
}

static nsrefcnt NSAPI mutation_observer_ns_AddRef(nsIMutationObserver *iface)
{
    struct mutation_observer *This = impl_from_nsIMutationObserver(iface);
    return IWineMSHTMLMutationObserver_AddRef(&This->IWineMSHTMLMutationObserver_iface);
}

static nsrefcnt NSAPI mutation_observer_ns_Release(nsIMutationObserver *iface)
{
    struct mutation_observer *This = impl_from_nsIMutationObserver(iface);
    return IWineMSHTMLMutationObserver_Release(&This->IWineMSHTMLMutationObserver_iface);
}

static BOOL mutation_observer_target_matches(struct mutation_observer_target *target, nsIDOMNode *content)
{
    nsIDOMNode *current = content, *parent;
    BOOL ret = FALSE;

    nsIDOMNode_AddRef(current);
    for(;;) {
        if(current == target->node->nsnode) {
            ret = TRUE;
            break;
        }
        if(!target->subtree || NS_FAILED(nsIDOMNode_GetParentNode(current, &parent)) || !parent)
            break;
        nsIDOMNode_Release(current);
        current = parent;
    }
    nsIDOMNode_Release(current);
    return ret;
}

static BOOL mutation_observer_matches(struct mutation_observer *This, nsIDOMNode *content,
        BOOL child_list)
{
    struct mutation_observer_target *target;

    LIST_FOR_EACH_ENTRY(target, &This->targets, struct mutation_observer_target, entry) {
        if((child_list ? target->child_list : target->character_data)
                && mutation_observer_target_matches(target, content))
            return TRUE;
    }
    return FALSE;
}

static BOOL mutation_observer_transient_options(struct mutation_observer *This, nsIDOMNode *content,
        BOOL *character_data, BOOL *child_list)
{
    struct mutation_observer_target *target;
    BOOL matched = FALSE;

    *character_data = *child_list = FALSE;
    LIST_FOR_EACH_ENTRY(target, &This->targets, struct mutation_observer_target, entry) {
        if(!target->subtree || !mutation_observer_target_matches(target, content))
            continue;
        matched = TRUE;
        *character_data |= target->character_data;
        *child_list |= target->child_list;
    }
    return matched;
}

static void mutation_observer_remove_target(struct mutation_observer *This,
        struct mutation_observer_target *target)
{
    BOOL native_registered = target->native_registered;

    target->native_registered = FALSE;
    list_remove(&target->entry);
    if(native_registered && content_utils)
        nsIContentUtils_RemoveMutationObserver(content_utils, target->native_node,
                &This->nsIMutationObserver_iface);
    nsISupports_Release((nsISupports *)target->native_node);
    node_release(target->node);
    free(target);
}

static BOOL mutation_observer_has_transients(struct mutation_observer *This)
{
    struct mutation_observer_target *target;

    LIST_FOR_EACH_ENTRY(target, &This->targets, struct mutation_observer_target, entry) {
        if(target->transient)
            return TRUE;
    }
    return FALSE;
}

static void mutation_observer_clear_transients(struct mutation_observer *This)
{
    struct mutation_observer_target *target, *next;

    LIST_FOR_EACH_ENTRY_SAFE(target, next, &This->targets, struct mutation_observer_target, entry) {
        if(target->transient)
            mutation_observer_remove_target(This, target);
    }
    if(list_empty(&This->targets) && This->observing_ref) {
        This->observing_ref = FALSE;
        IWineMSHTMLMutationObserver_Release(&This->IWineMSHTMLMutationObserver_iface);
    }
}

static HRESULT mutation_observer_add_transient(struct mutation_observer *This,
        nsIDOMNode *node, BOOL character_data, BOOL child_list)
{
    struct mutation_observer_target *target, *entry;
    HTMLDOMNode *html_node;
    nsINode *native_node;
    BOOL native_registered = FALSE;
    nsresult nsres;
    HRESULT hres;

    hres = get_node(node, TRUE, &html_node);
    if(FAILED(hres))
        return hres;
    hres = mutation_observer_get_native_node(html_node, &native_node);
    if(FAILED(hres)) {
        node_release(html_node);
        return hres;
    }

    LIST_FOR_EACH_ENTRY(target, &This->targets, struct mutation_observer_target, entry) {
        if(target->native_node != native_node)
            continue;
        native_registered = TRUE;
        if(target->transient) {
            target->character_data |= character_data;
            target->child_list |= child_list;
            nsISupports_Release((nsISupports *)native_node);
            node_release(html_node);
            return S_OK;
        }
    }

    entry = calloc(1, sizeof(*entry));
    if(!entry) {
        nsISupports_Release((nsISupports *)native_node);
        node_release(html_node);
        return E_OUTOFMEMORY;
    }
    if(!native_registered) {
        nsres = nsIContentUtils_AddMutationObserver(content_utils, native_node,
                &This->nsIMutationObserver_iface);
        if(NS_FAILED(nsres)) {
            nsISupports_Release((nsISupports *)native_node);
            node_release(html_node);
            free(entry);
            return map_nsresult(nsres);
        }
        entry->native_registered = TRUE;
    }

    entry->node = html_node;
    entry->native_node = native_node;
    entry->transient = TRUE;
    entry->character_data = character_data;
    entry->child_list = child_list;
    entry->subtree = TRUE;
    list_add_tail(&This->targets, &entry->entry);
    return S_OK;
}

static nsresult mutation_observer_deliver(HTMLDocumentNode *doc, nsISupports *arg1, nsISupports *arg2)
{
    struct mutation_observer *This = impl_from_nsIMutationObserver((nsIMutationObserver *)arg1);
    struct mutation_observer_runner *runner = arg2 ? impl_from_mutation_runner(arg2) : NULL;
    DISPID named_arg = DISPID_THIS;
    VARIANT args[3], result;
    DISPPARAMS params = {args, &named_arg, ARRAY_SIZE(args), 1};
    IDispatch *records, *callback;
    HRESULT hres;

    if(!runner || !This->delivery_pending || runner->generation != This->delivery_generation)
        return NS_OK;

    This->delivery_pending = FALSE;
    mutation_observer_clear_transients(This);
    if(list_empty(&This->records) || !This->callback)
        return NS_OK;

    hres = mutation_observer_create_records(This, &records);
    if(FAILED(hres)) {
        WARN("Could not create MutationObserver records array: %08lx\n", hres);
        if(hres == E_UNEXPECTED)
            mutation_observer_clear_records(This);
        return NS_OK;
    }

    callback = This->callback;
    IDispatch_AddRef(callback);
    V_VT(args) = VT_DISPATCH;
    V_DISPATCH(args) = (IDispatch *)&This->IWineMSHTMLMutationObserver_iface;
    V_VT(args + 1) = VT_DISPATCH;
    V_DISPATCH(args + 1) = (IDispatch *)&This->IWineMSHTMLMutationObserver_iface;
    V_VT(args + 2) = VT_DISPATCH;
    V_DISPATCH(args + 2) = records;
    VariantInit(&result);
    hres = call_disp_func(callback, &params, &result);
    VariantClear(&result);
    IDispatch_Release(callback);
    IDispatch_Release(records);
    if(FAILED(hres))
        WARN("MutationObserver callback failed: %08lx\n", hres);
    return NS_OK;
}

static void mutation_observer_schedule(struct mutation_observer *This)
{
    struct mutation_observer_target *target;
    struct mutation_observer_runner *runner;

    if(This->delivery_pending || (list_empty(&This->records)
            && !mutation_observer_has_transients(This)) || list_empty(&This->targets))
        return;

    target = LIST_ENTRY(list_head(&This->targets), struct mutation_observer_target, entry);
    runner = calloc(1, sizeof(*runner));
    if(!runner)
        return;

    runner->nsISupports_iface.lpVtbl = &mutation_runner_vtbl;
    runner->ref = 1;
    runner->generation = ++This->delivery_generation;
    This->delivery_pending = TRUE;

    if(!add_script_runner(target->node->doc, mutation_observer_deliver,
            (nsISupports *)&This->nsIMutationObserver_iface, &runner->nsISupports_iface)) {
        This->delivery_pending = FALSE;
        ++This->delivery_generation;
        nsISupports_Release(&runner->nsISupports_iface);
        return;
    }
    nsISupports_Release(&runner->nsISupports_iface);
}

struct mutation_node_array {
    nsIDOMNode **nodes;
    UINT32 count;
    UINT32 capacity;
};

static void mutation_node_array_clear(struct mutation_node_array *array)
{
    UINT32 i;

    for(i = 0; i < array->count; i++)
        nsIDOMNode_Release(array->nodes[i]);
    free(array->nodes);
    array->nodes = NULL;
    array->count = array->capacity = 0;
}

static BOOL mutation_node_array_append(struct mutation_node_array *array, nsIDOMNode *node)
{
    nsIDOMNode **nodes;
    UINT32 capacity;
    SIZE_T size;

    if(!node || array->count == ~(UINT32)0)
        return FALSE;
    if(array->count == array->capacity) {
        if(array->capacity > ~(UINT32)0 / 2)
            capacity = ~(UINT32)0;
        else
            capacity = array->capacity ? array->capacity * 2 : 4;
        if(!mutation_observer_array_size(capacity, sizeof(*nodes), &size))
            return FALSE;
        nodes = realloc(array->nodes, size);
        if(!nodes)
            return FALSE;
        array->nodes = nodes;
        array->capacity = capacity;
    }

    nsIDOMNode_AddRef(node);
    array->nodes[array->count++] = node;
    return TRUE;
}

static void mutation_observer_queue(struct mutation_observer *This, BOOL child_list,
        nsIDOMNode *target, nsIDOMNode *const *added_nodes, UINT32 added_count,
        nsIDOMNode *const *removed_nodes, UINT32 removed_count,
        nsIDOMNode *previous_sibling, nsIDOMNode *next_sibling)
{
    struct mutation_observer_record *record;
    UINT32 i;
    SIZE_T added_size, removed_size;

    if(!target || (added_count && !added_nodes) || (removed_count && !removed_nodes)
            || !mutation_observer_array_size(added_count, sizeof(*record->added_nodes), &added_size)
            || !mutation_observer_array_size(removed_count, sizeof(*record->removed_nodes), &removed_size))
        return;
    if(!(record = calloc(1, sizeof(*record))))
        return;

    if(FAILED(get_node(target, TRUE, &record->target)))
        goto failed;
    record->child_list = child_list;
    if(previous_sibling) {
        nsIDOMNode_AddRef(previous_sibling);
        record->previous_sibling = previous_sibling;
    }
    if(next_sibling) {
        nsIDOMNode_AddRef(next_sibling);
        record->next_sibling = next_sibling;
    }
    if(added_count) {
        record->added_nodes = calloc(1, added_size);
        if(!record->added_nodes)
            goto failed;
        for(i = 0; i < added_count; i++) {
            nsIDOMNode_AddRef(added_nodes[i]);
            record->added_nodes[i] = added_nodes[i];
        }
        record->added_count = added_count;
    }
    if(removed_count) {
        record->removed_nodes = calloc(1, removed_size);
        if(!record->removed_nodes)
            goto failed;
        for(i = 0; i < removed_count; i++) {
            nsIDOMNode_AddRef(removed_nodes[i]);
            record->removed_nodes[i] = removed_nodes[i];
        }
        record->removed_count = removed_count;
    }

    list_add_tail(&This->records, &record->entry);
    mutation_observer_schedule(This);
    return;

failed:
    mutation_observer_free_record(record);
}

static void NSAPI mutation_observer_CharacterDataWillChange(nsIMutationObserver *iface,
        nsIDocument *document, nsIContent *content, void *info)
{
}

static void NSAPI mutation_observer_CharacterDataChanged(nsIMutationObserver *iface,
        nsIDocument *document, nsIContent *content, void *info)
{
    struct mutation_observer *This = impl_from_nsIMutationObserver(iface);
    nsIDOMNode *nsnode;
    HTMLDOMNode *node;
    nsresult nsres;
    HRESULT hres;
    struct mutation_observer_record *record;

    if(!content)
        return;
    nsres = nsISupports_QueryInterface((nsISupports *)content, &IID_nsIDOMNode, (void **)&nsnode);
    if(NS_FAILED(nsres) || !mutation_observer_matches(This, nsnode, FALSE)) {
        if(NS_SUCCEEDED(nsres))
            nsIDOMNode_Release(nsnode);
        return;
    }

    hres = get_node(nsnode, TRUE, &node);
    nsIDOMNode_Release(nsnode);
    if(FAILED(hres))
        return;

    record = calloc(1, sizeof(*record));
    if(!record) {
        node_release(node);
        return;
    }
    record->target = node;
    list_add_tail(&This->records, &record->entry);
    mutation_observer_schedule(This);
}

static void NSAPI mutation_observer_AttributeWillChange(nsIMutationObserver *iface, nsIDocument *document,
        void *element, LONG namespace_id, nsIAtom *attribute, LONG mod_type, const nsAttrValue *new_value)
{
}

static void NSAPI mutation_observer_AttributeChanged(nsIMutationObserver *iface, nsIDocument *document,
        void *element, LONG namespace_id, nsIAtom *attribute, LONG mod_type, const nsAttrValue *old_value)
{
}

static void NSAPI mutation_observer_NativeAnonymousChildListChange(nsIMutationObserver *iface,
        nsIDocument *document, nsIContent *content, cpp_bool is_remove)
{
}

static void NSAPI mutation_observer_AttributeSetToCurrentValue(nsIMutationObserver *iface,
        nsIDocument *document, void *element, LONG namespace_id, nsIAtom *attribute)
{
}

static void NSAPI mutation_observer_ContentAppended(nsIMutationObserver *iface, nsIDocument *document,
        nsIContent *container, nsIContent *first_new_content, LONG new_index)
{
    struct mutation_observer *This = impl_from_nsIMutationObserver(iface);
    struct mutation_node_array added = {0};
    nsIDOMNode *container_node, *node = NULL, *next, *previous = NULL;
    nsresult nsres;
    BOOL success = TRUE;

    if(!container || !first_new_content)
        return;
    if(NS_FAILED(nsIContent_QueryInterface(container, &IID_nsIDOMNode, (void **)&container_node)))
        return;
    if(!mutation_observer_matches(This, container_node, TRUE)) {
        nsIDOMNode_Release(container_node);
        return;
    }
    if(NS_FAILED(nsIContent_QueryInterface(first_new_content, &IID_nsIDOMNode, (void **)&node)))
        goto done;
    nsres = nsIDOMNode_GetPreviousSibling(node, &previous);
    if(NS_FAILED(nsres))
        goto done;

    /* ContentAppended supplies the first new child of an append operation; the
       appended range is contiguous and ends at the container's last child. */
    while(node) {
        if(!mutation_node_array_append(&added, node)) {
            success = FALSE;
            nsIDOMNode_Release(node);
            node = NULL;
            break;
        }
        next = NULL;
        nsres = nsIDOMNode_GetNextSibling(node, &next);
        nsIDOMNode_Release(node);
        node = next;
        if(NS_FAILED(nsres)) {
            if(node)
                nsIDOMNode_Release(node);
            node = NULL;
            success = FALSE;
            break;
        }
    }
    if(success && added.count)
        mutation_observer_queue(This, TRUE, container_node, added.nodes, added.count,
                NULL, 0, previous, NULL);

done:
    if(node)
        nsIDOMNode_Release(node);
    if(previous)
        nsIDOMNode_Release(previous);
    mutation_node_array_clear(&added);
    nsIDOMNode_Release(container_node);
}

static void NSAPI mutation_observer_ContentInserted(nsIMutationObserver *iface, nsIDocument *document,
        nsIContent *container, nsIContent *child, LONG index)
{
    struct mutation_observer *This = impl_from_nsIMutationObserver(iface);
    nsIDOMNode *container_node, *child_node, *previous = NULL, *next = NULL;
    nsresult nsres;

    if(!container || !child)
        return;
    if(NS_FAILED(nsIContent_QueryInterface(container, &IID_nsIDOMNode, (void **)&container_node)))
        return;
    if(!mutation_observer_matches(This, container_node, TRUE))
        goto done;
    if(NS_FAILED(nsIContent_QueryInterface(child, &IID_nsIDOMNode, (void **)&child_node)))
        goto done;
    nsres = nsIDOMNode_GetPreviousSibling(child_node, &previous);
    if(NS_SUCCEEDED(nsres))
        nsres = nsIDOMNode_GetNextSibling(child_node, &next);
    if(NS_SUCCEEDED(nsres))
        mutation_observer_queue(This, TRUE, container_node, &child_node, 1,
                NULL, 0, previous, next);
    nsIDOMNode_Release(child_node);
    if(previous)
        nsIDOMNode_Release(previous);
    if(next)
        nsIDOMNode_Release(next);

done:
    nsIDOMNode_Release(container_node);
}

static void NSAPI mutation_observer_ContentRemoved(nsIMutationObserver *iface, nsIDocument *document,
        nsIContent *container, nsIContent *child, LONG index, nsIContent *previous_sibling)
{
    struct mutation_observer *This = impl_from_nsIMutationObserver(iface);
    nsIDOMNode *container_node, *child_node, *previous = NULL, *next = NULL;
    BOOL character_data, child_list, queue_child_list;
    nsresult nsres;

    if(!container || !child)
        return;
    if(NS_FAILED(nsIContent_QueryInterface(container, &IID_nsIDOMNode, (void **)&container_node)))
        return;
    queue_child_list = mutation_observer_matches(This, container_node, TRUE);
    mutation_observer_transient_options(This, container_node, &character_data, &child_list);
    if(!queue_child_list && !character_data && !child_list)
        goto done;
    if(NS_FAILED(nsIContent_QueryInterface(child, &IID_nsIDOMNode, (void **)&child_node)))
        goto done;
    if(queue_child_list) {
        if(previous_sibling) {
            nsres = nsIContent_QueryInterface(previous_sibling, &IID_nsIDOMNode, (void **)&previous);
            if(NS_SUCCEEDED(nsres))
                nsres = nsIDOMNode_GetNextSibling(previous, &next);
        }else {
            nsres = nsIDOMNode_GetFirstChild(container_node, &next);
        }
        if(NS_SUCCEEDED(nsres))
            mutation_observer_queue(This, TRUE, container_node, NULL, 0,
                    &child_node, 1, previous, next);
    }
    if((character_data || child_list) &&
            SUCCEEDED(mutation_observer_add_transient(This, child_node, character_data, child_list)))
        mutation_observer_schedule(This);
    nsIDOMNode_Release(child_node);
    if(previous)
        nsIDOMNode_Release(previous);
    if(next)
        nsIDOMNode_Release(next);

done:
    nsIDOMNode_Release(container_node);
}

static void NSAPI mutation_observer_NodeWillBeDestroyed(nsIMutationObserver *iface, const nsINode *node)
{
    struct mutation_observer *This = impl_from_nsIMutationObserver(iface);
    struct mutation_observer_target *target, *next;
    BOOL release_observer = FALSE;

    IWineMSHTMLMutationObserver_AddRef(&This->IWineMSHTMLMutationObserver_iface);
    LIST_FOR_EACH_ENTRY_SAFE(target, next, &This->targets, struct mutation_observer_target, entry) {
        if(target->native_node != node)
            continue;
        target->native_registered = FALSE;
        list_remove(&target->entry);
        nsISupports_Release((nsISupports *)target->native_node);
        node_release(target->node);
        free(target);
    }
    if(list_empty(&This->targets) && This->observing_ref) {
        This->observing_ref = FALSE;
        release_observer = TRUE;
    }
    if(release_observer)
        IWineMSHTMLMutationObserver_Release(&This->IWineMSHTMLMutationObserver_iface);
    IWineMSHTMLMutationObserver_Release(&This->IWineMSHTMLMutationObserver_iface);
}

static void NSAPI mutation_observer_ParentChainChanged(nsIMutationObserver *iface, nsIContent *content)
{
}

static const nsIMutationObserverVtbl mutation_observer_ns_vtbl = {
    mutation_observer_ns_QueryInterface,
    mutation_observer_ns_AddRef,
    mutation_observer_ns_Release,
    mutation_observer_CharacterDataWillChange,
    mutation_observer_CharacterDataChanged,
    mutation_observer_AttributeWillChange,
    mutation_observer_AttributeChanged,
    mutation_observer_NativeAnonymousChildListChange,
    mutation_observer_AttributeSetToCurrentValue,
    mutation_observer_ContentAppended,
    mutation_observer_ContentInserted,
    mutation_observer_ContentRemoved,
    mutation_observer_NodeWillBeDestroyed,
    mutation_observer_ParentChainChanged
};

static inline struct mutation_observer *mutation_observer_from_DispatchEx(DispatchEx *iface)
{
    return CONTAINING_RECORD(iface, struct mutation_observer, dispex);
}

DISPEX_IDISPATCH_IMPL(MutationObserver, IWineMSHTMLMutationObserver,
                      impl_from_IWineMSHTMLMutationObserver(iface)->dispex)

static void mutation_observer_disconnect_internal(struct mutation_observer *This)
{
    struct mutation_observer_target *target, *next;

    ++This->delivery_generation;
    This->delivery_pending = FALSE;
    LIST_FOR_EACH_ENTRY_SAFE(target, next, &This->targets, struct mutation_observer_target, entry)
        mutation_observer_remove_target(This, target);
    mutation_observer_clear_records(This);
    if(This->observing_ref) {
        This->observing_ref = FALSE;
        IWineMSHTMLMutationObserver_Release(&This->IWineMSHTMLMutationObserver_iface);
    }
}

static HRESULT WINAPI MutationObserver_disconnect(IWineMSHTMLMutationObserver *iface)
{
    struct mutation_observer *This = impl_from_IWineMSHTMLMutationObserver(iface);

    TRACE("(%p)\n", This);
    IWineMSHTMLMutationObserver_AddRef(iface);
    mutation_observer_disconnect_internal(This);
    IWineMSHTMLMutationObserver_Release(iface);
    return S_OK;
}

static HRESULT mutation_observer_get_option(IDispatch *options, const WCHAR *name,
        VARIANT *value, BOOL *present)
{
    DISPPARAMS params = {0};
    LPOLESTR names[] = {(WCHAR *)name};
    DISPID id;
    HRESULT hres;

    VariantInit(value);
    *present = FALSE;
    hres = IDispatch_GetIDsOfNames(options, &IID_NULL, names, 1, LOCALE_SYSTEM_DEFAULT, &id);
    if(hres == DISP_E_UNKNOWNNAME)
        return S_OK;
    if(FAILED(hres))
        return hres;

    hres = IDispatch_Invoke(options, id, &IID_NULL, LOCALE_SYSTEM_DEFAULT,
            DISPATCH_PROPERTYGET, &params, value, NULL, NULL);
    if(SUCCEEDED(hres))
        *present = V_VT(value) != VT_EMPTY;
    return hres;
}

static HRESULT mutation_observer_option_present(IDispatch *options, const WCHAR *name, BOOL *present)
{
    VARIANT value;
    HRESULT hres;

    hres = mutation_observer_get_option(options, name, &value, present);
    VariantClear(&value);
    return hres;
}

static HRESULT mutation_observer_to_boolean(VARIANT *value, BOOL *ret)
{
    VARIANT v;
    HRESULT hres;

    VariantInit(&v);
    hres = VariantCopyInd(&v, value);
    if(FAILED(hres))
        return hres;

    switch(V_VT(&v)) {
    case VT_EMPTY:
    case VT_NULL:
        *ret = FALSE;
        break;
    case VT_BOOL:
        *ret = V_BOOL(&v) != VARIANT_FALSE;
        break;
    case VT_I1:
        *ret = V_I1(&v) != 0;
        break;
    case VT_UI1:
        *ret = V_UI1(&v) != 0;
        break;
    case VT_I2:
        *ret = V_I2(&v) != 0;
        break;
    case VT_UI2:
        *ret = V_UI2(&v) != 0;
        break;
    case VT_I4:
        *ret = V_I4(&v) != 0;
        break;
    case VT_UI4:
        *ret = V_UI4(&v) != 0;
        break;
    case VT_I8:
        *ret = V_I8(&v) != 0;
        break;
    case VT_UI8:
        *ret = V_UI8(&v) != 0;
        break;
    case VT_INT:
        *ret = V_INT(&v) != 0;
        break;
    case VT_UINT:
        *ret = V_UINT(&v) != 0;
        break;
    case VT_R4:
        *ret = V_R4(&v) != 0 && V_R4(&v) == V_R4(&v);
        break;
    case VT_R8:
    case VT_DATE:
        *ret = V_R8(&v) != 0 && V_R8(&v) == V_R8(&v);
        break;
    case VT_CY:
        *ret = V_CY(&v).int64 != 0;
        break;
    case VT_BSTR:
        *ret = V_BSTR(&v) && SysStringLen(V_BSTR(&v));
        break;
    case VT_DISPATCH:
        *ret = V_DISPATCH(&v) != NULL;
        break;
    case VT_UNKNOWN:
        *ret = V_UNKNOWN(&v) != NULL;
        break;
    default:
        hres = DISP_E_TYPEMISMATCH;
    }

    VariantClear(&v);
    return hres;
}

static HRESULT mutation_observer_get_bool(IDispatch *options, const WCHAR *name, BOOL *ret, BOOL *present)
{
    VARIANT value;
    HRESULT hres;

    *ret = FALSE;
    hres = mutation_observer_get_option(options, name, &value, present);
    if(SUCCEEDED(hres))
        hres = mutation_observer_to_boolean(&value, ret);
    VariantClear(&value);
    return hres;
}

static HRESULT mutation_observer_get_native_node(HTMLDOMNode *node, nsINode **ret)
{
    UINT16 node_type;
    nsresult nsres;

    *ret = NULL;
    nsres = nsIDOMNode_GetNodeType(node->nsnode, &node_type);
    if(NS_FAILED(nsres))
        return map_nsresult(nsres);
    if(node_type == DOCUMENT_NODE)
        nsres = nsIDOMNode_QueryInterface(node->nsnode, &IID_nsIDocument, (void **)ret);
    else
        nsres = nsIDOMNode_QueryInterface(node->nsnode, &IID_nsIContent, (void **)ret);
    return NS_FAILED(nsres) ? map_nsresult(nsres) : S_OK;
}

static HRESULT WINAPI MutationObserver_observe(IWineMSHTMLMutationObserver *iface, IHTMLDOMNode *target,
                                               IDispatch *options)
{
    struct mutation_observer *This = impl_from_IWineMSHTMLMutationObserver(iface);
    struct mutation_observer_target *entry;
    HTMLDOMNode *node;
    nsINode *native_node;
    BOOL attributes, attribute_old, character_data, character_data_old, child_list, subtree;
    BOOL attributes_present, attribute_old_present, character_data_present;
    BOOL character_data_old_present, ignored, filter_present;
    nsresult nsres;
    HRESULT hres;

    TRACE("(%p)->(%p %p)\n", This, target, options);

    if(!target || !options || !(node = unsafe_impl_from_IHTMLDOMNode(target)))
        return E_INVALIDARG;
    if(FAILED(hres = mutation_observer_get_bool(options, L"attributes", &attributes, &attributes_present))
            || FAILED(hres = mutation_observer_get_bool(options, L"attributeOldValue", &attribute_old,
                    &attribute_old_present))
            || FAILED(hres = mutation_observer_get_bool(options, L"characterData", &character_data,
                    &character_data_present))
            || FAILED(hres = mutation_observer_get_bool(options, L"characterDataOldValue", &character_data_old,
                    &character_data_old_present))
            || FAILED(hres = mutation_observer_get_bool(options, L"childList", &child_list, &ignored))
            || FAILED(hres = mutation_observer_get_bool(options, L"subtree", &subtree, &ignored))
            || FAILED(hres = mutation_observer_option_present(options, L"attributeFilter", &filter_present)))
        return hres;

    if(!attributes_present && (attribute_old_present || filter_present))
        attributes = TRUE;
    if(!character_data_present && character_data_old_present)
        character_data = TRUE;
    if(attributes || attribute_old || character_data_old || filter_present)
        return E_NOTIMPL;
    if(!character_data && !child_list)
        return E_INVALIDARG;

    LIST_FOR_EACH_ENTRY(entry, &This->targets, struct mutation_observer_target, entry) {
        if(entry->node == node) {
            entry->transient = FALSE;
            entry->character_data = character_data;
            entry->child_list = child_list;
            entry->subtree = subtree;
            return S_OK;
        }
    }

    if(!content_utils || !node->nsnode)
        return E_UNEXPECTED;
    hres = mutation_observer_get_native_node(node, &native_node);
    if(FAILED(hres))
        return hres;

    entry = calloc(1, sizeof(*entry));
    if(!entry) {
        nsISupports_Release((nsISupports *)native_node);
        return E_OUTOFMEMORY;
    }
    nsres = nsIContentUtils_AddMutationObserver(content_utils, native_node,
            &This->nsIMutationObserver_iface);
    if(NS_FAILED(nsres)) {
        nsISupports_Release((nsISupports *)native_node);
        free(entry);
        return map_nsresult(nsres);
    }

    node_addref(node);
    entry->node = node;
    entry->native_node = native_node;
    entry->native_registered = TRUE;
    entry->character_data = character_data;
    entry->child_list = child_list;
    entry->subtree = subtree;
    list_add_tail(&This->targets, &entry->entry);
    if(!This->observing_ref) {
        This->observing_ref = TRUE;
        IWineMSHTMLMutationObserver_AddRef(iface);
    }
    return S_OK;
}

static HRESULT WINAPI MutationObserver_takeRecords(IWineMSHTMLMutationObserver *iface, IDispatch **ret)
{
    struct mutation_observer *This = impl_from_IWineMSHTMLMutationObserver(iface);
    HRESULT hres;

    TRACE("(%p)->(%p)\n", This, ret);
    if(!ret)
        return E_POINTER;
    *ret = NULL;
    ++This->delivery_generation;
    This->delivery_pending = FALSE;
    hres = mutation_observer_create_records(This, ret);
    if(mutation_observer_has_transients(This))
        mutation_observer_schedule(This);
    return hres;
}

static const IWineMSHTMLMutationObserverVtbl WineMSHTMLMutationObserverVtbl = {
    MutationObserver_QueryInterface,
    MutationObserver_AddRef,
    MutationObserver_Release,
    MutationObserver_GetTypeInfoCount,
    MutationObserver_GetTypeInfo,
    MutationObserver_GetIDsOfNames,
    MutationObserver_Invoke,
    MutationObserver_disconnect,
    MutationObserver_observe,
    MutationObserver_takeRecords
};

static void *mutation_observer_query_interface(DispatchEx *dispex, REFIID riid)
{
    struct mutation_observer *This = mutation_observer_from_DispatchEx(dispex);

    if(IsEqualGUID(&IID_IWineMSHTMLMutationObserver, riid))
        return &This->IWineMSHTMLMutationObserver_iface;
    return NULL;
}

static void mutation_observer_traverse(DispatchEx *dispex, nsCycleCollectionTraversalCallback *cb)
{
    struct mutation_observer *This = mutation_observer_from_DispatchEx(dispex);
    struct mutation_observer_target *target;
    struct mutation_observer_record *record;

    if(This->callback)
        note_cc_edge((nsISupports*)This->callback, "callback", cb);
    LIST_FOR_EACH_ENTRY(target, &This->targets, struct mutation_observer_target, entry) {
        note_cc_edge((nsISupports*)&target->node->IHTMLDOMNode_iface, "target", cb);
        note_cc_edge((nsISupports*)target->native_node, "native_target", cb);
    }
    LIST_FOR_EACH_ENTRY(record, &This->records, struct mutation_observer_record, entry) {
        UINT32 i;

        note_cc_edge((nsISupports*)&record->target->IHTMLDOMNode_iface, "record_target", cb);
        if(record->previous_sibling)
            note_cc_edge((nsISupports *)record->previous_sibling, "previous_sibling", cb);
        if(record->next_sibling)
            note_cc_edge((nsISupports *)record->next_sibling, "next_sibling", cb);
        for(i = 0; i < record->added_count; i++)
            note_cc_edge((nsISupports *)record->added_nodes[i], "added_node", cb);
        for(i = 0; i < record->removed_count; i++)
            note_cc_edge((nsISupports *)record->removed_nodes[i], "removed_node", cb);
    }
}

static void mutation_observer_unlink(DispatchEx *dispex)
{
    struct mutation_observer *This = mutation_observer_from_DispatchEx(dispex);

    IWineMSHTMLMutationObserver_AddRef(&This->IWineMSHTMLMutationObserver_iface);
    mutation_observer_disconnect_internal(This);
    IWineMSHTMLMutationObserver_Release(&This->IWineMSHTMLMutationObserver_iface);
    unlink_ref(&This->callback);
}

static void mutation_observer_destructor(DispatchEx *dispex)
{
    struct mutation_observer *This = mutation_observer_from_DispatchEx(dispex);

    mutation_observer_disconnect_internal(This);
    free(This);
}

static HRESULT init_mutation_observer_ctor(struct constructor*);

static const dispex_static_data_vtbl_t mutation_observer_dispex_vtbl = {
    .query_interface  = mutation_observer_query_interface,
    .destructor       = mutation_observer_destructor,
    .traverse         = mutation_observer_traverse,
    .unlink           = mutation_observer_unlink
};

static const tid_t mutation_observer_iface_tids[] = {
    IWineMSHTMLMutationObserver_tid,
    0
};
dispex_static_data_t MutationObserver_dispex = {
    .id               = OBJID_MutationObserver,
    .init_constructor = init_mutation_observer_ctor,
    .vtbl             = &mutation_observer_dispex_vtbl,
    .disp_tid         = IWineMSHTMLMutationObserver_tid,
    .iface_tids       = mutation_observer_iface_tids,
    .min_compat_mode  = COMPAT_MODE_IE11,
};

static HRESULT create_mutation_observer(DispatchEx *owner, IDispatch *callback,
                                        IWineMSHTMLMutationObserver **ret)
{
    struct mutation_observer *obj;

    TRACE("(callback = %p, ret = %p)\n", callback, ret);
    obj = calloc(1, sizeof(*obj));
    if(!obj)
        return E_OUTOFMEMORY;

    obj->IWineMSHTMLMutationObserver_iface.lpVtbl = &WineMSHTMLMutationObserverVtbl;
    obj->nsIMutationObserver_iface.lpVtbl = &mutation_observer_ns_vtbl;
    list_init(&obj->targets);
    list_init(&obj->records);
    init_dispatch_with_owner(&obj->dispex, &MutationObserver_dispex, owner);
    IDispatch_AddRef(callback);
    obj->callback = callback;
    *ret = &obj->IWineMSHTMLMutationObserver_iface;
    return S_OK;
}

static HRESULT mutation_observer_ctor_value(DispatchEx *dispex, LCID lcid,
        WORD flags, DISPPARAMS *params, VARIANT *res, EXCEPINFO *ei,
        IServiceProvider *caller)
{
    struct constructor *This = constructor_from_DispatchEx(dispex);
    VARIANT *callback;
    IWineMSHTMLMutationObserver *mutation_observer;
    BOOL callable;
    HRESULT hres;
    int argc = params->cArgs - params->cNamedArgs;

    TRACE("(%p)->(%lx %x %p %p %p %p)\n", This, lcid, flags, params, res, ei, caller);

    switch (flags) {
    case DISPATCH_METHOD | DISPATCH_PROPERTYGET:
        if (!res)
            return E_INVALIDARG;
    case DISPATCH_CONSTRUCT:
    case DISPATCH_METHOD:
        break;
    default:
        FIXME("flags %x is not supported\n", flags);
        return E_NOTIMPL;
    }

    if (argc < 1)
        return E_UNEXPECTED;

    callback = params->rgvarg + (params->cArgs - 1);
    if (V_VT(callback) != VT_DISPATCH || !V_DISPATCH(callback))
        return E_FAIL;
    if (!This->window->jscript)
        return E_UNEXPECTED;
    hres = IWineJScript_IsCallable(This->window->jscript, V_DISPATCH(callback), &callable);
    if (FAILED(hres))
        return hres;
    if (!callable)
        return E_FAIL;
    if (!res)
        return S_OK;

    hres = create_mutation_observer(&This->dispex, V_DISPATCH(callback), &mutation_observer);
    if (FAILED(hres))
        return hres;
    V_VT(res) = VT_DISPATCH;
    V_DISPATCH(res) = (IDispatch*)mutation_observer;
    return S_OK;
}

static const dispex_static_data_vtbl_t mutation_observer_ctor_dispex_vtbl = {
    .destructor       = constructor_destructor,
    .traverse         = constructor_traverse,
    .unlink           = constructor_unlink,
    .value            = mutation_observer_ctor_value
};

static dispex_static_data_t mutation_observer_ctor_dispex = {
    .name           = "MutationObserver",
    .constructor_id = OBJID_MutationObserver,
    .vtbl           = &mutation_observer_ctor_dispex_vtbl,
};

static HRESULT init_mutation_observer_ctor(struct constructor *constr)
{
    init_dispatch(&constr->dispex, &mutation_observer_ctor_dispex, constr->window,
                  dispex_compat_mode(&constr->window->event_target.dispex));
    return S_OK;
}
