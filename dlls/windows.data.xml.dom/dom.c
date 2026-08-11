/* Windows.Data.Xml.Dom implementation for reusable WinRT XML objects. */

#include <stdarg.h>
#include <limits.h>
#include <string.h>
#define COBJMACROS
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "objbase.h"
#include "initguid.h"
#include "roapi.h"
#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#define WIDL_using_Windows_Data_Xml_Dom
#include "windows.data.xml.dom.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/valid.h>
#include "dom.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(xml_dom);

enum dom_kind
{
    DOM_DOCUMENT,
    DOM_ELEMENT,
    DOM_TEXT,
    DOM_ATTRIBUTE,
    DOM_FRAGMENT,
    DOM_COMMENT,
    DOM_PROCESSING_INSTRUCTION,
    DOM_ENTITY_REFERENCE,
    DOM_CDATA,
    DOM_OTHER
};
struct dom_object;
struct dom_entry
{
    xmlNodePtr xml;
    xmlNsPtr detached_ns;
    struct dom_object *object;
    BOOL detached_root;
    struct dom_entry *next;
};
struct dom_tree
{
    LONG ref;
    SRWLOCK lock;
    SRWLOCK cache_lock;
    xmlDocPtr doc;
    struct dom_entry *entries;
};
struct dom_object
{
    IXmlDocument document;
    IXmlDocumentIO io;
    IXmlDocumentIO2 io2;
    IXmlNode node;
    IXmlNodeSerializer serializer;
    IXmlNodeSelector selector;
    IXmlElement element;
    IXmlText text;
    IXmlCharacterData character;
    IXmlAttribute attribute;
    IXmlDocumentFragment fragment;
    IXmlComment comment;
    IXmlProcessingInstruction processing_instruction;
    IXmlEntityReference entity_reference;
    IXmlCDataSection cdata;
    LONG ref;
    enum dom_kind kind;
    struct dom_tree *tree;
    xmlNodePtr xml;
    SRWLOCK lock;
};
struct dom_list
{
    IXmlNodeList iface;
    LONG ref;
    struct dom_tree *tree;
    IXmlNode **items;
    UINT32 count;
};
struct dom_implementation
{
    IXmlDomImplementation iface;
    LONG ref;
};

static const IXmlDocumentVtbl document_vtbl;
static const IXmlDocumentIOVtbl io_vtbl;
static const IXmlDocumentIO2Vtbl io2_vtbl;
static const IXmlNodeVtbl node_vtbl;
static const IXmlNodeSerializerVtbl serializer_vtbl;
static const IXmlNodeSelectorVtbl selector_vtbl;
static const IXmlElementVtbl element_vtbl;
static const IXmlTextVtbl text_vtbl;
static const IXmlCharacterDataVtbl character_vtbl;
static const IXmlAttributeVtbl attribute_vtbl;
static const IXmlDocumentFragmentVtbl fragment_vtbl;
static const IXmlCommentVtbl comment_vtbl;
static const IXmlProcessingInstructionVtbl processing_instruction_vtbl;
static const IXmlEntityReferenceVtbl entity_reference_vtbl;
static const IXmlCDataSectionVtbl cdata_vtbl;
static const IXmlNodeListVtbl list_vtbl;
static const IXmlDomImplementationVtbl implementation_vtbl;

#define IMPL(iface, type, member) CONTAINING_RECORD(iface, type, member)
static inline struct dom_object *from_document(IXmlDocument *v) { return IMPL(v, struct dom_object, document); }
static inline struct dom_object *from_io(IXmlDocumentIO *v) { return IMPL(v, struct dom_object, io); }
static inline struct dom_object *from_io2(IXmlDocumentIO2 *v) { return IMPL(v, struct dom_object, io2); }
static inline struct dom_object *from_node(IXmlNode *v) { return IMPL(v, struct dom_object, node); }
static inline struct dom_object *from_serializer(IXmlNodeSerializer *v) { return IMPL(v, struct dom_object, serializer); }
static inline struct dom_object *from_selector(IXmlNodeSelector *v) { return IMPL(v, struct dom_object, selector); }
static inline struct dom_object *from_element(IXmlElement *v) { return IMPL(v, struct dom_object, element); }
static inline struct dom_object *from_text(IXmlText *v) { return IMPL(v, struct dom_object, text); }
static inline struct dom_object *from_character(IXmlCharacterData *v) { return IMPL(v, struct dom_object, character); }
static inline struct dom_object *from_attribute(IXmlAttribute *v) { return IMPL(v, struct dom_object, attribute); }
static inline struct dom_object *from_fragment(IXmlDocumentFragment *v) { return IMPL(v, struct dom_object, fragment); }
static inline struct dom_object *from_comment(IXmlComment *v) { return IMPL(v, struct dom_object, comment); }
static inline struct dom_object *from_processing_instruction(IXmlProcessingInstruction *v) { return IMPL(v, struct dom_object, processing_instruction); }
static inline struct dom_object *from_entity_reference(IXmlEntityReference *v) { return IMPL(v, struct dom_object, entity_reference); }
static inline struct dom_object *from_cdata(IXmlCDataSection *v) { return IMPL(v, struct dom_object, cdata); }
static inline struct dom_list *from_list(IXmlNodeList *v) { return IMPL(v, struct dom_list, iface); }

static void tree_addref(struct dom_tree *tree) { InterlockedIncrement(&tree->ref); }
static void tree_release(struct dom_tree *tree)
{
    struct dom_entry *entry, *next;
    if (InterlockedDecrement(&tree->ref)) return;

    for (entry = tree->entries; entry; entry = entry->next)
        entry->detached_root = entry->xml != (xmlNodePtr)tree->doc && !entry->xml->parent;
    for (entry = tree->entries; entry; entry = entry->next)
    {
        if (!entry->detached_root) continue;
        if (entry->xml->type == XML_ATTRIBUTE_NODE) xmlFreeProp((xmlAttrPtr)entry->xml);
        else xmlFreeNode(entry->xml);
    }
    for (entry = tree->entries; entry; entry = next)
    {
        next = entry->next;
        if (entry->detached_ns) xmlFreeNs(entry->detached_ns);
        free(entry);
    }
    xmlFreeDoc(tree->doc);
    free(tree);
}
static struct dom_tree *tree_new(xmlDocPtr doc)
{
    struct dom_tree *tree = calloc(1, sizeof(*tree));
    if (!tree) return NULL;
    tree->ref = 1;
    tree->lock = (SRWLOCK)SRWLOCK_INIT;
    tree->cache_lock = (SRWLOCK)SRWLOCK_INIT;
    tree->doc = doc;
    return tree;
}

static HRESULT hstring_utf8(HSTRING input, char **out, int *length)
{
    const WCHAR *value; UINT32 len; int size; char *buffer;
    *out = NULL; *length = 0;
    value = WindowsGetStringRawBuffer(input, &len);
    if (!len) { *out = calloc(1, 1); return *out ? S_OK : E_OUTOFMEMORY; }
    if (len > INT_MAX / 4) return E_INVALIDARG;
    size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, len, NULL, 0, NULL, NULL);
    if (!size) return E_INVALIDARG;
    if (!(buffer = malloc(size + 1))) return E_OUTOFMEMORY;
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, len, buffer, size, NULL, NULL))
    { free(buffer); return E_INVALIDARG; }
    buffer[size] = 0; *out = buffer; *length = size; return S_OK;
}
static HRESULT utf8_hstring(const xmlChar *input, HSTRING *out)
{
    int len, size; WCHAR *buffer;
    *out = NULL; if (!input) input = (const xmlChar *)"";
    len = strlen((const char *)input);
    size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)input, len, NULL, 0);
    if (!size && len) return E_FAIL;
    if (!(buffer = malloc((size + 1) * sizeof(*buffer)))) return E_OUTOFMEMORY;
    if (size) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)input, len, buffer, size);
    buffer[size] = 0;
    len = WindowsCreateString(buffer, size, out); free(buffer); return len;
}
static BOOL prefix_ci(const char *s, const char *prefix, unsigned int length)
{
    unsigned int i;
    for (i = 0; i < length; ++i)
    {
        char a = s[i], b = prefix[i];
        if (a >= 'A' && a <= 'Z') a += 'a' - 'A';
        if (a != b) return FALSE;
    }
    return TRUE;
}

static BOOL forbidden_declaration(const char *s, int len)
{
    int i;
    for (i = 0; i + 8 < len; ++i)
    {
        if (s[i] != '<' || s[i + 1] != '!') continue;
        if (prefix_ci(s + i + 2, "doctype", 7) || prefix_ci(s + i + 2, "entity", 6)) return TRUE;
    }
    return FALSE;
}
static BOOL validate_xml_ids(xmlDocPtr doc, xmlNodePtr parent)
{
    static const xmlChar xml_namespace[] = "http://www.w3.org/XML/1998/namespace";
    xmlNodePtr node;
    xmlAttrPtr attr, indexed;
    xmlChar *value;

    for (node = parent ? parent->children : NULL; node; node = node->next)
    {
        if (node->type != XML_ELEMENT_NODE)
        {
            if (!validate_xml_ids(doc, node)) return FALSE;
            continue;
        }
        for (attr = node->properties; attr; attr = attr->next)
        {
            if (!attr->ns || xmlStrcmp(attr->ns->href, xml_namespace) || xmlStrcmp(attr->name, (xmlChar *)"id"))
                continue;
            value = xmlNodeGetContent((xmlNodePtr)attr);
            if (!value || xmlValidateNCName(value, 0) || ((indexed = xmlGetID(doc, value)) && indexed != attr))
            {
                if (value) xmlFree(value);
                return FALSE;
            }
            if (!indexed && !xmlAddID(NULL, doc, value, attr))
            {
                xmlFree(value);
                return FALSE;
            }
            attr->atype = XML_ATTRIBUTE_ID;
            xmlFree(value);
        }
        if (!validate_xml_ids(doc, node)) return FALSE;
    }
    return TRUE;
}
static HRESULT parse_source(HSTRING source, struct dom_tree **out)
{
    char *utf8; int length; xmlDocPtr doc; struct dom_tree *tree; HRESULT hr;
    *out = NULL;
    if (FAILED(hr = hstring_utf8(source, &utf8, &length))) return hr;
    if (forbidden_declaration(utf8, length)) { free(utf8); return E_ACCESSDENIED; }
    doc = xmlReadMemory(utf8, length, NULL, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    free(utf8);
    if (!doc || !xmlDocGetRootElement(doc) || doc->intSubset ||
            !validate_xml_ids(doc, (xmlNodePtr)doc))
    { if (doc) xmlFreeDoc(doc); return E_FAIL; }
    if (!(tree = tree_new(doc))) { xmlFreeDoc(doc); return E_OUTOFMEMORY; }
    *out = tree; return S_OK;
}

static enum dom_kind node_kind(xmlNodePtr node)
{
    if (!node) return DOM_OTHER;
    switch (node->type)
    {
    case XML_DOCUMENT_NODE: return DOM_DOCUMENT;
    case XML_ELEMENT_NODE: return DOM_ELEMENT;
    case XML_TEXT_NODE: return DOM_TEXT;
    case XML_ATTRIBUTE_NODE: return DOM_ATTRIBUTE;
    case XML_DOCUMENT_FRAG_NODE: return DOM_FRAGMENT;
    case XML_COMMENT_NODE: return DOM_COMMENT;
    case XML_PI_NODE: return DOM_PROCESSING_INSTRUCTION;
    case XML_ENTITY_REF_NODE: return DOM_ENTITY_REFERENCE;
    case XML_CDATA_SECTION_NODE: return DOM_CDATA;
    default: return DOM_OTHER;
    }
}

static struct dom_tree *object_tree(struct dom_object *object)
{
    struct dom_tree *tree;
    if (object->kind == DOM_DOCUMENT)
    {
        AcquireSRWLockShared(&object->lock);
        tree = object->tree;
        tree_addref(tree);
        ReleaseSRWLockShared(&object->lock);
        return tree;
    }
    tree = object->tree;
    tree_addref(tree);
    return tree;
}
static struct dom_entry *tree_entry(struct dom_tree *tree, xmlNodePtr xml, BOOL create)
{
    struct dom_entry *entry;

    for (entry = tree->entries; entry; entry = entry->next)
        if (entry->xml == xml) return entry;
    if (!create || !(entry = calloc(1, sizeof(*entry)))) return NULL;
    entry->xml = xml;
    entry->next = tree->entries;
    tree->entries = entry;
    return entry;
}

static struct dom_object *wrap_node(struct dom_tree *tree, xmlNodePtr xml)
{
    struct dom_object *object;
    struct dom_entry *entry;

    if (!xml) return NULL;
    AcquireSRWLockExclusive(&tree->cache_lock);
    if (!(entry = tree_entry(tree, xml, TRUE)))
    {
        ReleaseSRWLockExclusive(&tree->cache_lock);
        return NULL;
    }
    if ((object = entry->object))
    {
        InterlockedIncrement(&object->ref);
        ReleaseSRWLockExclusive(&tree->cache_lock);
        return object;
    }
    if (!(object = calloc(1, sizeof(*object))))
    {
        ReleaseSRWLockExclusive(&tree->cache_lock);
        return NULL;
    }
    object->ref = 1;
    object->kind = node_kind(xml);
    object->tree = tree;
    object->xml = xml;
    object->lock = (SRWLOCK)SRWLOCK_INIT;
    tree_addref(tree);
    object->document.lpVtbl = &document_vtbl;
    object->io.lpVtbl = &io_vtbl;
    object->io2.lpVtbl = &io2_vtbl;
    object->node.lpVtbl = &node_vtbl;
    object->serializer.lpVtbl = &serializer_vtbl;
    object->selector.lpVtbl = &selector_vtbl;
    object->element.lpVtbl = &element_vtbl;
    object->text.lpVtbl = &text_vtbl;
    object->character.lpVtbl = &character_vtbl;
    object->attribute.lpVtbl = &attribute_vtbl;
    object->fragment.lpVtbl = &fragment_vtbl;
    object->comment.lpVtbl = &comment_vtbl;
    object->processing_instruction.lpVtbl = &processing_instruction_vtbl;
    object->entity_reference.lpVtbl = &entity_reference_vtbl;
    object->cdata.lpVtbl = &cdata_vtbl;
    entry->object = object;
    ReleaseSRWLockExclusive(&tree->cache_lock);
    return object;
}

static HRESULT object_qi(struct dom_object *object, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable))
        *out = object->kind == DOM_DOCUMENT ? (void *)&object->document : (void *)&object->node;
    else if (object->kind == DOM_DOCUMENT && IsEqualGUID(iid, &IID_IXmlDocument)) *out = &object->document;
    else if (object->kind == DOM_DOCUMENT && IsEqualGUID(iid, &IID_IXmlDocumentIO)) *out = &object->io;
    else if (object->kind == DOM_DOCUMENT && IsEqualGUID(iid, &IID_IXmlDocumentIO2)) *out = &object->io2;
    else if (IsEqualGUID(iid, &IID_IXmlNode)) *out = &object->node;
    else if (IsEqualGUID(iid, &IID_IXmlNodeSerializer)) *out = &object->serializer;
    else if (IsEqualGUID(iid, &IID_IXmlNodeSelector)) *out = &object->selector;
    else if (object->kind == DOM_ELEMENT && IsEqualGUID(iid, &IID_IXmlElement)) *out = &object->element;
    else if ((object->kind == DOM_TEXT || object->kind == DOM_CDATA) && IsEqualGUID(iid, &IID_IXmlText)) *out = &object->text;
    else if ((object->kind == DOM_TEXT || object->kind == DOM_CDATA || object->kind == DOM_COMMENT) &&
            IsEqualGUID(iid, &IID_IXmlCharacterData)) *out = &object->character;
    else if (object->kind == DOM_ATTRIBUTE && IsEqualGUID(iid, &IID_IXmlAttribute)) *out = &object->attribute;
    else if (object->kind == DOM_FRAGMENT && IsEqualGUID(iid, &IID_IXmlDocumentFragment)) *out = &object->fragment;
    else if (object->kind == DOM_COMMENT && IsEqualGUID(iid, &IID_IXmlComment)) *out = &object->comment;
    else if (object->kind == DOM_PROCESSING_INSTRUCTION &&
            IsEqualGUID(iid, &IID_IXmlProcessingInstruction)) *out = &object->processing_instruction;
    else if (object->kind == DOM_ENTITY_REFERENCE &&
            IsEqualGUID(iid, &IID_IXmlEntityReference)) *out = &object->entity_reference;
    else if (object->kind == DOM_CDATA && IsEqualGUID(iid, &IID_IXmlCDataSection)) *out = &object->cdata;
    else return E_NOINTERFACE;
    IInspectable_AddRef((IInspectable *)*out);
    return S_OK;
}
static HRESULT object_iids(ULONG *count, IID **iids)
{ if (!count || !iids) return E_POINTER; *count = 0; *iids = NULL; return S_OK; }
static HRESULT object_name(struct dom_object *object, HSTRING *out)
{
    static const WCHAR *names[] =
    {
        L"Windows.Data.Xml.Dom.XmlDocument", L"Windows.Data.Xml.Dom.XmlElement",
        L"Windows.Data.Xml.Dom.XmlText", L"Windows.Data.Xml.Dom.XmlAttribute",
        L"Windows.Data.Xml.Dom.XmlDocumentFragment", L"Windows.Data.Xml.Dom.XmlComment",
        L"Windows.Data.Xml.Dom.XmlProcessingInstruction", L"Windows.Data.Xml.Dom.XmlEntityReference",
        L"Windows.Data.Xml.Dom.XmlCDataSection", L"Windows.Data.Xml.Dom.XmlNode"
    };
    if (!out) return E_POINTER;
    *out = NULL;
    return WindowsCreateString(names[object->kind], wcslen(names[object->kind]), out);
}
static ULONG object_release(struct dom_object *object)
{
    struct dom_tree *tree;
    struct dom_entry *entry;
    ULONG ref;

    AcquireSRWLockShared(&object->lock);
    tree = object->tree;
    AcquireSRWLockExclusive(&tree->cache_lock);
    ref = InterlockedDecrement(&object->ref);
    if (!ref)
    {
        entry = tree_entry(tree, object->xml, FALSE);
        if (entry && entry->object == object) entry->object = NULL;
    }
    ReleaseSRWLockExclusive(&tree->cache_lock);
    ReleaseSRWLockShared(&object->lock);
    if (!ref)
    {
        tree_release(tree);
        free(object);
    }
    return ref;
}
#define BASE(prefix, iface, field) \
static HRESULT WINAPI prefix##_QueryInterface(iface *v, REFIID iid, void **out) { return object_qi(IMPL(v, struct dom_object, field), iid, out); } \
static ULONG WINAPI prefix##_AddRef(iface *v) { return InterlockedIncrement(&IMPL(v, struct dom_object, field)->ref); } \
static ULONG WINAPI prefix##_Release(iface *v) { return object_release(IMPL(v, struct dom_object, field)); } \
static HRESULT WINAPI prefix##_GetIids(iface *v, ULONG *count, IID **iids) { return object_iids(count, iids); } \
static HRESULT WINAPI prefix##_GetRuntimeClassName(iface *v, HSTRING *name) { return object_name(IMPL(v, struct dom_object, field), name); } \
static HRESULT WINAPI prefix##_GetTrustLevel(iface *v, TrustLevel *level) { if (!level) return E_POINTER; *level = BaseTrust; return S_OK; }
BASE(document, IXmlDocument, document)
BASE(io, IXmlDocumentIO, io)
BASE(io2, IXmlDocumentIO2, io2)
BASE(node, IXmlNode, node)
BASE(serializer, IXmlNodeSerializer, serializer)
BASE(selector, IXmlNodeSelector, selector)
BASE(element, IXmlElement, element)
BASE(text, IXmlText, text)
BASE(character, IXmlCharacterData, character)
BASE(attribute, IXmlAttribute, attribute)
BASE(fragment, IXmlDocumentFragment, fragment)
BASE(comment, IXmlComment, comment)
BASE(processing_instruction, IXmlProcessingInstruction, processing_instruction)
BASE(entity_reference, IXmlEntityReference, entity_reference)
BASE(cdata, IXmlCDataSection, cdata)
#undef BASE
static HRESULT wrap_iid(struct dom_tree *tree, xmlNodePtr node, REFIID iid, void **out)
{
    struct dom_object *object; HRESULT hr;
    *out = NULL; if (!node) return S_FALSE;
    if (!(object = wrap_node(tree, node))) return E_OUTOFMEMORY;
    hr = object_qi(object, iid, out); IXmlNode_Release(&object->node); return hr;
}
static HRESULT list_new(struct dom_tree *tree, IXmlNode **items, UINT32 count, IXmlNodeList **out)
{
    struct dom_list *list; UINT32 i;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!(list = calloc(1, sizeof(*list)))) return E_OUTOFMEMORY;
    list->iface.lpVtbl = &list_vtbl;
    list->ref = 1;
    list->tree = tree;
    list->count = count;
    tree_addref(tree);
    if (count && !(list->items = calloc(count, sizeof(*list->items))))
    {
        tree_release(tree);
        free(list);
        return E_OUTOFMEMORY;
    }
    for (i = 0; i < count; ++i) IXmlNode_AddRef(list->items[i] = items[i]);
    *out = &list->iface; return S_OK;
}
static BOOL node_name_matches(xmlNodePtr node, const char *name)
{
    size_t prefix_length;

    if (!strcmp(name, "*")) return TRUE;
    if (!node->ns || !node->ns->prefix) return !strcmp(name, (const char *)node->name);
    prefix_length = strlen((const char *)node->ns->prefix);
    return !strncmp(name, (const char *)node->ns->prefix, prefix_length) &&
            name[prefix_length] == ':' && !strcmp(name + prefix_length + 1, (const char *)node->name);
}

static void collect(struct dom_tree *tree, xmlNodePtr parent, const char *name, IXmlNode ***items, UINT32 *count)
{
    xmlNodePtr node; IXmlNode **new_items; struct dom_object *object;
    for (node = parent ? parent->children : NULL; node; node = node->next)
    {
        if (node->type == XML_ELEMENT_NODE && node_name_matches(node, name))
        {
            if (*count == UINT_MAX || !(object = wrap_node(tree, node))) continue;
            if (!(new_items = realloc(*items, (*count + 1) * sizeof(**items)))) { IXmlNode_Release(&object->node); continue; }
            *items = new_items; (*items)[(*count)++] = &object->node;
        }
        collect(tree, node, name, items, count);
    }
}
static HRESULT elements_by_tag(struct dom_tree *tree, xmlNodePtr parent, HSTRING tag, IXmlNodeList **out)
{
    char *name; int len; IXmlNode **items = NULL; UINT32 count = 0, i; HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (FAILED(hr = hstring_utf8(tag, &name, &len))) return hr;
    collect(tree, parent, name, &items, &count); free(name);
    hr = list_new(tree, items, count, out);
    for (i = 0; i < count; ++i) IXmlNode_Release(items[i]);
    free(items); return hr;
}
static HRESULT inspectable_string(IInspectable *value, HSTRING *out)
{
    IPropertyValue *property;
    HRESULT hr;

    *out = NULL;
    if (!value) return S_OK;
    if (FAILED(hr = IInspectable_QueryInterface(value, &IID_IPropertyValue, (void **)&property))) return E_INVALIDARG;
    hr = IPropertyValue_GetString(property, out);
    IPropertyValue_Release(property);
    return FAILED(hr) ? E_INVALIDARG : hr;
}

static HRESULT qualified_name(HSTRING value, char **storage, char **prefix, char **local)
{
    int length;
    char *colon;
    HRESULT hr;

    *storage = *prefix = *local = NULL;
    if (FAILED(hr = hstring_utf8(value, storage, &length))) return hr;
    if (!length || xmlValidateQName((xmlChar *)*storage, 0))
    {
        free(*storage);
        *storage = NULL;
        return E_INVALIDARG;
    }
    if ((colon = strchr(*storage, ':')))
    {
        *colon = 0;
        *prefix = *storage;
        *local = colon + 1;
    }
    else *local = *storage;
    return S_OK;
}

static HRESULT namespace_uri(IInspectable *value, char **uri)
{
    HSTRING string = NULL;
    int length;
    HRESULT hr;

    *uri = NULL;
    if (FAILED(hr = inspectable_string(value, &string))) return hr;
    if (!string) return S_OK;
    hr = hstring_utf8(string, uri, &length);
    WindowsDeleteString(string);
    if (SUCCEEDED(hr) && !length)
    {
        free(*uri);
        *uri = NULL;
    }
    return hr;
}

static BOOL valid_xml_name(const char *name)
{
    return name && *name && xmlValidateNameValue((const xmlChar *)name);
}

static struct dom_implementation implementation = {{&implementation_vtbl}, 1};
static HRESULT WINAPI implementation_QueryInterface(IXmlDomImplementation *v, REFIID iid, void **out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!IsEqualGUID(iid, &IID_IUnknown) && !IsEqualGUID(iid, &IID_IInspectable) &&
            !IsEqualGUID(iid, &IID_IXmlDomImplementation)) return E_NOINTERFACE;
    *out = v;
    IXmlDomImplementation_AddRef(v);
    return S_OK;
}
static ULONG WINAPI implementation_AddRef(IXmlDomImplementation *v) { return 2; }
static ULONG WINAPI implementation_Release(IXmlDomImplementation *v) { return 1; }
static HRESULT WINAPI implementation_GetIids(IXmlDomImplementation *v, ULONG *count, IID **iids)
{ return object_iids(count, iids); }
static HRESULT WINAPI implementation_GetRuntimeClassName(IXmlDomImplementation *v, HSTRING *out)
{
    static const WCHAR name[] = L"Windows.Data.Xml.Dom.XmlDomImplementation";
    if (!out) return E_POINTER;
    *out = NULL;
    return WindowsCreateString(name, ARRAY_SIZE(name) - 1, out);
}
static HRESULT WINAPI implementation_GetTrustLevel(IXmlDomImplementation *v, TrustLevel *out)
{ if (!out) return E_POINTER; *out = BaseTrust; return S_OK; }
static HRESULT WINAPI implementation_HasFeature(IXmlDomImplementation *v, HSTRING feature,
        IInspectable *version_value, boolean *out)
{
    HSTRING version = NULL;
    const WCHAR *feature_string, *version_string = NULL;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = FALSE;
    feature_string = WindowsGetStringRawBuffer(feature, NULL);
    if (version_value && FAILED(hr = inspectable_string(version_value, &version))) return hr;
    if (version) version_string = WindowsGetStringRawBuffer(version, NULL);
    if ((!wcsicmp(feature_string, L"XML") || !wcsicmp(feature_string, L"Core")) &&
            (!version_string || !*version_string || !wcscmp(version_string, L"1.0") ||
             !wcscmp(version_string, L"2.0") || !wcscmp(version_string, L"3.0")))
        *out = TRUE;
    WindowsDeleteString(version);
    return S_OK;
}
static HRESULT boxed_string(const xmlChar *value, IInspectable **out)
{
    IPropertyValueStatics *statics;
    HSTRING class_name = NULL, string = NULL;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!value) return S_OK;
    if (FAILED(hr = WindowsCreateString(L"Windows.Foundation.PropertyValue", 32, &class_name))) return hr;
    hr = RoGetActivationFactory(class_name, &IID_IPropertyValueStatics, (void **)&statics);
    WindowsDeleteString(class_name);
    if (FAILED(hr)) return hr;
    if (SUCCEEDED(hr = utf8_hstring(value, &string)))
    {
        hr = IPropertyValueStatics_CreateString(statics, string, out);
        WindowsDeleteString(string);
    }
    IPropertyValueStatics_Release(statics);
    return hr;
}

static HRESULT qualified_hstring(xmlNodePtr node, HSTRING *out)
{
    xmlChar *qualified;
    HRESULT hr;

    if (!node->ns || !node->ns->prefix) return utf8_hstring(node->name, out);
    if (!(qualified = xmlBuildQName(node->name, node->ns->prefix, NULL, 0))) return E_OUTOFMEMORY;
    hr = utf8_hstring(qualified, out);
    xmlFree(qualified);
    return hr;
}

static HRESULT document_root(IXmlDocument *v, IXmlElement **out)
{
    struct dom_object *o = from_document(v); struct dom_tree *tree; xmlNodePtr root; HRESULT hr;
    if (!out) return E_POINTER; *out = NULL; tree = object_tree(o);
    AcquireSRWLockShared(&tree->lock); root = xmlDocGetRootElement(tree->doc);
    hr = wrap_iid(tree, root, &IID_IXmlElement, (void **)out);
    ReleaseSRWLockShared(&tree->lock); tree_release(tree); return hr;
}
static HRESULT document_create_element(IXmlDocument *v, HSTRING name, IXmlElement **out)
{
    struct dom_object *o = from_document(v);
    struct dom_tree *tree;
    char *text;
    int len;
    xmlNodePtr node;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (FAILED(hr = hstring_utf8(name, &text, &len))) return hr;
    if (!valid_xml_name(text) || strchr(text, ':')) { free(text); return E_INVALIDARG; }
    tree = object_tree(o);
    AcquireSRWLockExclusive(&tree->lock);
    node = xmlNewDocNode(tree->doc, NULL, (xmlChar *)text, NULL);
    hr = node ? wrap_iid(tree, node, &IID_IXmlElement, (void **)out) : E_OUTOFMEMORY;
    ReleaseSRWLockExclusive(&tree->lock);
    tree_release(tree);
    free(text);
    return hr;
}
static HRESULT document_create_text(IXmlDocument *v, HSTRING value, IXmlText **out)
{
    struct dom_object *o = from_document(v);
    struct dom_tree *tree;
    char *text;
    int len;
    xmlNodePtr node;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (FAILED(hr = hstring_utf8(value, &text, &len))) return hr;
    tree = object_tree(o);
    AcquireSRWLockExclusive(&tree->lock);
    node = xmlNewDocText(tree->doc, (xmlChar *)text);
    hr = node ? wrap_iid(tree, node, &IID_IXmlText, (void **)out) : E_OUTOFMEMORY;
    ReleaseSRWLockExclusive(&tree->lock);
    tree_release(tree);
    free(text);
    return hr;
}
static HRESULT document_create_attr(IXmlDocument *v, HSTRING name, IXmlAttribute **out)
{
    struct dom_object *o = from_document(v);
    struct dom_tree *tree;
    char *text;
    int len;
    xmlNodePtr node;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (FAILED(hr = hstring_utf8(name, &text, &len))) return hr;
    if (!valid_xml_name(text) || strchr(text, ':')) { free(text); return E_INVALIDARG; }
    tree = object_tree(o);
    AcquireSRWLockExclusive(&tree->lock);
    node = (xmlNodePtr)xmlNewDocProp(tree->doc, (xmlChar *)text, NULL);
    hr = node ? wrap_iid(tree, node, &IID_IXmlAttribute, (void **)out) : E_OUTOFMEMORY;
    ReleaseSRWLockExclusive(&tree->lock);
    tree_release(tree);
    free(text);
    return hr;
}

static HRESULT document_create_node(IXmlDocument *v, enum dom_kind kind, HSTRING first, HSTRING second,
        REFIID iid, void **out)
{
    struct dom_object *o = from_document(v);
    struct dom_tree *tree;
    char *a = NULL, *b = NULL;
    int alen, blen;
    xmlNodePtr node = NULL;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (kind != DOM_FRAGMENT && FAILED(hr = hstring_utf8(first, &a, &alen))) return hr;
    if (kind == DOM_PROCESSING_INSTRUCTION && FAILED(hr = hstring_utf8(second, &b, &blen)))
    { free(a); return hr; }
    if ((kind == DOM_COMMENT && (strstr(a, "--") || (alen && a[alen - 1] == '-'))) ||
            (kind == DOM_CDATA && strstr(a, "]]>")) ||
            (kind == DOM_PROCESSING_INSTRUCTION &&
             (!valid_xml_name(a) || !stricmp(a, "xml") || (b && strstr(b, "?>")))) ||
            (kind == DOM_ENTITY_REFERENCE && !valid_xml_name(a)))
    {
        free(a);
        free(b);
        return E_INVALIDARG;
    }
    tree = object_tree(o);
    AcquireSRWLockExclusive(&tree->lock);
    switch (kind)
    {
    case DOM_FRAGMENT: node = xmlNewDocFragment(tree->doc); break;
    case DOM_COMMENT: node = xmlNewDocComment(tree->doc, (xmlChar *)a); break;
    case DOM_PROCESSING_INSTRUCTION: node = xmlNewDocPI(tree->doc, (xmlChar *)a, (xmlChar *)b); break;
    case DOM_ENTITY_REFERENCE: node = xmlNewReference(tree->doc, (xmlChar *)a); break;
    case DOM_CDATA: node = xmlNewCDataBlock(tree->doc, (xmlChar *)a, alen); break;
    default: break;
    }
    hr = node ? wrap_iid(tree, node, iid, out) : E_OUTOFMEMORY;
    ReleaseSRWLockExclusive(&tree->lock);
    tree_release(tree);
    free(a);
    free(b);
    return hr;
}

static BOOL valid_namespace_binding(const char *uri, const char *prefix, const char *local, BOOL attribute)
{
    static const char xml_namespace[] = "http://www.w3.org/XML/1998/namespace";
    static const char xmlns_namespace[] = "http://www.w3.org/2000/xmlns/";
    BOOL xml_prefix = prefix && !strcmp(prefix, "xml");
    BOOL xmlns_name = (prefix && !strcmp(prefix, "xmlns")) || (!prefix && !strcmp(local, "xmlns"));

    if (prefix && !uri) return FALSE;
    if (xml_prefix != (uri && !strcmp(uri, xml_namespace))) return FALSE;
    if (xmlns_name != (uri && !strcmp(uri, xmlns_namespace))) return FALSE;
    if (uri && !strcmp(uri, xmlns_namespace) && !attribute) return FALSE;
    return TRUE;
}

static HRESULT document_create_ns(IXmlDocument *v, IInspectable *namespace_value, HSTRING qualified,
        BOOL attribute, void **out)
{
    struct dom_object *o = from_document(v);
    struct dom_tree *tree;
    struct dom_entry *entry;
    char *name = NULL, *prefix = NULL, *local = NULL, *uri = NULL;
    xmlNodePtr node = NULL;
    xmlNsPtr ns = NULL;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (FAILED(hr = qualified_name(qualified, &name, &prefix, &local))) return hr;
    if (FAILED(hr = namespace_uri(namespace_value, &uri))) { free(name); return hr; }
    if (!valid_namespace_binding(uri, prefix, local, attribute))
    {
        free(name);
        free(uri);
        return E_INVALIDARG;
    }
    tree = object_tree(o);
    AcquireSRWLockExclusive(&tree->lock);
    if (attribute)
    {
        node = (xmlNodePtr)xmlNewDocProp(tree->doc, (xmlChar *)local, NULL);
        if (node && uri)
        {
            if (!strcmp(uri, "http://www.w3.org/XML/1998/namespace"))
                ns = xmlSearchNsByHref(tree->doc, node, (xmlChar *)uri);
            else
                ns = xmlNewNs(NULL, (xmlChar *)uri, (xmlChar *)prefix);
            if (ns) ((xmlAttrPtr)node)->ns = ns;
        }
    }
    else
    {
        node = xmlNewDocNode(tree->doc, NULL, (xmlChar *)local, NULL);
        if (node && uri)
        {
            if (!strcmp(uri, "http://www.w3.org/XML/1998/namespace"))
                ns = xmlSearchNsByHref(tree->doc, node, (xmlChar *)uri);
            else
                ns = xmlNewNs(node, (xmlChar *)uri, (xmlChar *)prefix);
            if (ns) xmlSetNs(node, ns);
        }
    }
    if (!node || (uri && !ns)) hr = E_OUTOFMEMORY;
    else hr = wrap_iid(tree, node, attribute ? &IID_IXmlAttribute : &IID_IXmlElement, out);
    if (SUCCEEDED(hr) && attribute && ns &&
            strcmp(uri, "http://www.w3.org/XML/1998/namespace"))
    {
        AcquireSRWLockExclusive(&tree->cache_lock);
        entry = tree_entry(tree, node, FALSE);
        if (entry) entry->detached_ns = ns;
        ReleaseSRWLockExclusive(&tree->cache_lock);
    }
    ReleaseSRWLockExclusive(&tree->lock);
    tree_release(tree);
    free(name);
    free(uri);
    return hr;
}
static HRESULT WINAPI document_get_Doctype(IXmlDocument *v, IXmlDocumentType **out) { if (!out) return E_POINTER; *out = NULL; return S_OK; }
static HRESULT WINAPI document_get_Implementation(IXmlDocument *v, IXmlDomImplementation **out)
{
    if (!out) return E_POINTER;
    *out = &implementation.iface;
    IXmlDomImplementation_AddRef(*out);
    return S_OK;
}
static HRESULT WINAPI document_get_DocumentElement(IXmlDocument *v, IXmlElement **out) { return document_root(v, out); }
static HRESULT WINAPI document_CreateElement(IXmlDocument *v, HSTRING n, IXmlElement **out) { return document_create_element(v, n, out); }
static HRESULT WINAPI document_CreateDocumentFragment(IXmlDocument *v, IXmlDocumentFragment **out)
{ return document_create_node(v, DOM_FRAGMENT, NULL, NULL, &IID_IXmlDocumentFragment, (void **)out); }
static HRESULT WINAPI document_CreateTextNode(IXmlDocument *v, HSTRING n, IXmlText **out) { return document_create_text(v, n, out); }
static HRESULT WINAPI document_CreateComment(IXmlDocument *v, HSTRING n, IXmlComment **out)
{ return document_create_node(v, DOM_COMMENT, n, NULL, &IID_IXmlComment, (void **)out); }
static HRESULT WINAPI document_CreateProcessingInstruction(IXmlDocument *v, HSTRING a, HSTRING b, IXmlProcessingInstruction **out)
{ return document_create_node(v, DOM_PROCESSING_INSTRUCTION, a, b, &IID_IXmlProcessingInstruction, (void **)out); }
static HRESULT WINAPI document_CreateAttribute(IXmlDocument *v, HSTRING n, IXmlAttribute **out) { return document_create_attr(v, n, out); }
static HRESULT WINAPI document_CreateEntityReference(IXmlDocument *v, HSTRING n, IXmlEntityReference **out)
{ return document_create_node(v, DOM_ENTITY_REFERENCE, n, NULL, &IID_IXmlEntityReference, (void **)out); }
static HRESULT WINAPI document_GetElementsByTagName(IXmlDocument *v, HSTRING n, IXmlNodeList **out)
{ struct dom_object *o = from_document(v); struct dom_tree *tree = object_tree(o); HRESULT hr; AcquireSRWLockShared(&tree->lock); hr = elements_by_tag(tree, (xmlNodePtr)tree->doc, n, out); ReleaseSRWLockShared(&tree->lock); tree_release(tree); return hr; }
static HRESULT WINAPI document_CreateCDataSection(IXmlDocument *v, HSTRING n, IXmlCDataSection **out)
{ return document_create_node(v, DOM_CDATA, n, NULL, &IID_IXmlCDataSection, (void **)out); }
static HRESULT WINAPI document_get_DocumentUri(IXmlDocument *v, HSTRING *out) { if (!out) return E_POINTER; *out = NULL; return S_OK; }
static HRESULT WINAPI document_CreateAttributeNS(IXmlDocument *v, IInspectable *a, HSTRING b, IXmlAttribute **out)
{ return document_create_ns(v, a, b, TRUE, (void **)out); }
static HRESULT WINAPI document_CreateElementNS(IXmlDocument *v, IInspectable *a, HSTRING b, IXmlElement **out)
{ return document_create_ns(v, a, b, FALSE, (void **)out); }
static HRESULT WINAPI document_GetElementById(IXmlDocument *v, HSTRING value, IXmlElement **out)
{
    struct dom_object *o = from_document(v);
    struct dom_tree *tree;
    xmlAttrPtr attr;
    char *id;
    int length;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (FAILED(hr = hstring_utf8(value, &id, &length))) return hr;
    tree = object_tree(o);
    AcquireSRWLockShared(&tree->lock);
    attr = xmlGetID(tree->doc, (xmlChar *)id);
    hr = attr ? wrap_iid(tree, attr->parent, &IID_IXmlElement, (void **)out) : S_OK;
    ReleaseSRWLockShared(&tree->lock);
    tree_release(tree);
    free(id);
    return hr;
}
static HRESULT WINAPI document_ImportNode(IXmlDocument *v, IXmlNode *source, boolean deep, IXmlNode **out)
{
    struct dom_object *o = from_document(v), *src;
    xmlNodePtr copy;
    struct dom_tree *tree;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!source) return E_INVALIDARG;
    src = from_node(source);
    tree = object_tree(o);
    AcquireSRWLockExclusive(&tree->lock);
    copy = xmlDocCopyNode(src->xml, tree->doc, deep);
    if (!copy) hr = E_OUTOFMEMORY;
    else hr = wrap_iid(tree, copy, &IID_IXmlNode, (void **)out);
    ReleaseSRWLockExclusive(&tree->lock);
    tree_release(tree);
    return hr;
}

static HRESULT WINAPI io_LoadXml(IXmlDocumentIO *v, HSTRING source)
{
    struct dom_object *o = from_io(v);
    struct dom_entry *entry;
    struct dom_tree *old, *tree;
    HRESULT hr;

    if (FAILED(hr = parse_source(source, &tree))) return hr;
    AcquireSRWLockExclusive(&o->lock);
    old = o->tree;
    AcquireSRWLockExclusive(&tree->cache_lock);
    entry = tree_entry(tree, (xmlNodePtr)tree->doc, TRUE);
    ReleaseSRWLockExclusive(&tree->cache_lock);
    if (!entry)
    {
        ReleaseSRWLockExclusive(&o->lock);
        tree_release(tree);
        return E_OUTOFMEMORY;
    }
    AcquireSRWLockExclusive(&old->cache_lock);
    entry = tree_entry(old, o->xml, FALSE);
    if (entry && entry->object == o) entry->object = NULL;
    ReleaseSRWLockExclusive(&old->cache_lock);
    AcquireSRWLockExclusive(&tree->cache_lock);
    entry = tree_entry(tree, (xmlNodePtr)tree->doc, FALSE);
    entry->object = o;
    tree_addref(tree);
    ReleaseSRWLockExclusive(&tree->cache_lock);
    o->tree = tree;
    o->xml = (xmlNodePtr)tree->doc;
    ReleaseSRWLockExclusive(&o->lock);
    tree_release(old);
    tree_release(tree);
    return S_OK;
}
static HRESULT WINAPI io_LoadXmlWithSettings(IXmlDocumentIO *v, HSTRING source, IXmlLoadSettings *settings) { return io_LoadXml(v, source); }
static HRESULT WINAPI io_SaveToFileAsync(IXmlDocumentIO *v, __x_ABI_CWindows_CStorage_CIStorageFile *file, __x_ABI_CWindows_CFoundation_CIAsyncAction **out) { if (!out) return E_POINTER; *out = NULL; return E_NOTIMPL; }
static HRESULT WINAPI io2_LoadXmlFromBuffer(IXmlDocumentIO2 *v, __x_ABI_CWindows_CStorage_CStreams_CIBuffer *buffer) { return E_NOTIMPL; }
static HRESULT WINAPI io2_LoadXmlFromBufferWithSettings(IXmlDocumentIO2 *v, __x_ABI_CWindows_CStorage_CStreams_CIBuffer *buffer, IXmlLoadSettings *s) { return E_NOTIMPL; }

static struct dom_tree *node_tree(struct dom_object *o) { return object_tree(o); }
static HRESULT WINAPI node_get_NodeValue(IXmlNode *v, IInspectable **out) { if (!out) return E_POINTER; *out = NULL; return E_NOTIMPL; }
static HRESULT WINAPI node_put_NodeValue(IXmlNode *v, IInspectable *value) { return E_NOTIMPL; }
static HRESULT WINAPI node_get_NodeType(IXmlNode *v, NodeType *out)
{
    struct dom_object *o = from_node(v);
    if (!out) return E_POINTER;
    switch (o->kind)
    {
    case DOM_DOCUMENT: *out = NodeType_DocumentNode; break;
    case DOM_ELEMENT: *out = NodeType_ElementNode; break;
    case DOM_ATTRIBUTE: *out = NodeType_AttributeNode; break;
    case DOM_TEXT: *out = NodeType_TextNode; break;
    case DOM_CDATA: *out = NodeType_DataSectionNode; break;
    case DOM_ENTITY_REFERENCE: *out = NodeType_EntityReferenceNode; break;
    case DOM_PROCESSING_INSTRUCTION: *out = NodeType_ProcessingInstructionNode; break;
    case DOM_COMMENT: *out = NodeType_CommentNode; break;
    case DOM_FRAGMENT: *out = NodeType_DocumentFragmentNode; break;
    default: *out = NodeType_Invalid; break;
    }
    return S_OK;
}
static HRESULT WINAPI node_get_NodeName(IXmlNode *v, HSTRING *out)
{
    struct dom_object *o = from_node(v);
    static const WCHAR *special[] =
    {
        L"#document", NULL, L"#text", NULL, L"#document-fragment", L"#comment",
        NULL, NULL, L"#cdata-section", NULL
    };
    if (!out) return E_POINTER;
    *out = NULL;
    if (special[o->kind]) return WindowsCreateString(special[o->kind], wcslen(special[o->kind]), out);
    return qualified_hstring(o->xml, out);
}
static HRESULT node_wrap(struct dom_tree *tree, xmlNodePtr node, IXmlNode **out)
{ struct dom_object *o; *out = NULL; if (!node) return S_FALSE; if (!(o = wrap_node(tree, node))) return E_OUTOFMEMORY; *out = &o->node; return S_OK; }
static HRESULT WINAPI node_get_ParentNode(IXmlNode *v, IXmlNode **out)
{
    struct dom_object *o = from_node(v);
    struct dom_tree *tree = node_tree(o);
    HRESULT hr;
    if (!out) { tree_release(tree); return E_POINTER; }
    AcquireSRWLockShared(&tree->lock);
    hr = node_wrap(tree, o->kind == DOM_DOCUMENT ? NULL : o->xml->parent, out);
    ReleaseSRWLockShared(&tree->lock);
    tree_release(tree);
    return hr;
}
static HRESULT WINAPI node_get_ChildNodes(IXmlNode *v, IXmlNodeList **out);
static HRESULT WINAPI node_get_FirstChild(IXmlNode *v, IXmlNode **out) { struct dom_object *o = from_node(v); struct dom_tree *tree = node_tree(o); HRESULT hr; if (!out) { tree_release(tree); return E_POINTER; } AcquireSRWLockShared(&tree->lock); hr = node_wrap(tree, o->xml->children, out); ReleaseSRWLockShared(&tree->lock); tree_release(tree); return hr; }
static HRESULT WINAPI node_get_LastChild(IXmlNode *v, IXmlNode **out) { struct dom_object *o = from_node(v); struct dom_tree *tree = node_tree(o); HRESULT hr; if (!out) { tree_release(tree); return E_POINTER; } AcquireSRWLockShared(&tree->lock); hr = node_wrap(tree, o->xml->last, out); ReleaseSRWLockShared(&tree->lock); tree_release(tree); return hr; }
static HRESULT WINAPI node_get_PreviousSibling(IXmlNode *v, IXmlNode **out) { struct dom_object *o = from_node(v); struct dom_tree *tree = node_tree(o); HRESULT hr; if (!out) { tree_release(tree); return E_POINTER; } AcquireSRWLockShared(&tree->lock); hr = node_wrap(tree, o->xml->prev, out); ReleaseSRWLockShared(&tree->lock); tree_release(tree); return hr; }
static HRESULT WINAPI node_get_NextSibling(IXmlNode *v, IXmlNode **out) { struct dom_object *o = from_node(v); struct dom_tree *tree = node_tree(o); HRESULT hr; if (!out) { tree_release(tree); return E_POINTER; } AcquireSRWLockShared(&tree->lock); hr = node_wrap(tree, o->xml->next, out); ReleaseSRWLockShared(&tree->lock); tree_release(tree); return hr; }
static HRESULT WINAPI node_get_Attributes(IXmlNode *v, IXmlNamedNodeMap **out) { if (!out) return E_POINTER; *out = NULL; return E_NOTIMPL; }
static HRESULT WINAPI node_HasChildNodes(IXmlNode *v, boolean *out) { struct dom_object *o = from_node(v); if (!out) return E_POINTER; *out = !!o->xml->children; return S_OK; }
static HRESULT WINAPI node_get_OwnerDocument(IXmlNode *v, IXmlDocument **out)
{
    struct dom_object *o = from_node(v), *doc;
    if (!out) return E_POINTER;
    *out = NULL;
    if (o->kind == DOM_DOCUMENT) return S_OK;
    if (!(doc = wrap_node(o->tree, (xmlNodePtr)o->tree->doc))) return E_OUTOFMEMORY;
    *out = &doc->document;
    return S_OK;
}
static HRESULT node_child_check(struct dom_object *parent, IXmlNode *child, xmlNodePtr *xml)
{
    struct dom_object *o;
    xmlNodePtr ancestor;

    if (!child || !(o = from_node(child)) || o->tree != parent->tree ||
            o->kind == DOM_DOCUMENT || o->kind == DOM_ATTRIBUTE) return E_INVALIDARG;
    for (ancestor = parent->xml; ancestor; ancestor = ancestor->parent)
        if (ancestor == o->xml) return E_INVALIDARG;
    *xml = o->xml;
    return S_OK;
}
static BOOL id_attribute(xmlAttrPtr attr)
{
    static const xmlChar xml_namespace[] = "http://www.w3.org/XML/1998/namespace";
    return attr->atype == XML_ATTRIBUTE_ID || (attr->ns && !xmlStrcmp(attr->ns->href, xml_namespace) &&
            !xmlStrcmp(attr->name, (xmlChar *)"id"));
}

static BOOL node_in_document(xmlNodePtr node)
{
    while (node && node->type != XML_DOCUMENT_NODE) node = node->parent;
    return !!node;
}

static UINT32 subtree_id_count_walk(xmlNodePtr root, const xmlChar *value, BOOL siblings)
{
    xmlNodePtr node;
    xmlAttrPtr attr;
    xmlChar *current;
    UINT32 count = 0;

    for (node = root; node; node = siblings ? node->next : NULL)
    {
        if (node->type == XML_ELEMENT_NODE)
        {
            for (attr = node->properties; attr; attr = attr->next)
            {
                if (!id_attribute(attr)) continue;
                current = xmlNodeGetContent((xmlNodePtr)attr);
                if (current && !xmlStrcmp(current, value)) ++count;
                if (current) xmlFree(current);
            }
        }
        if (node->children) count += subtree_id_count_walk(node->children, value, TRUE);
    }
    return count;
}

static HRESULT validate_subtree_ids_walk(struct dom_tree *tree, xmlNodePtr root, BOOL siblings)
{
    xmlNodePtr node;
    xmlAttrPtr attr, indexed;
    xmlChar *value;
    HRESULT hr;

    for (node = root; node; node = siblings ? node->next : NULL)
    {
        if (node->type == XML_ELEMENT_NODE)
        {
            for (attr = node->properties; attr; attr = attr->next)
            {
                if (!id_attribute(attr)) continue;
                value = xmlNodeGetContent((xmlNodePtr)attr);
                indexed = value ? xmlGetID(tree->doc, value) : NULL;
                if (!value || !*value || xmlValidateNCName(value, 0) ||
                        (indexed && indexed != attr) || subtree_id_count_walk(root, value, siblings) != 1)
                {
                    if (value) xmlFree(value);
                    return E_INVALIDARG;
                }
                xmlFree(value);
            }
        }
        if (node->children &&
                FAILED(hr = validate_subtree_ids_walk(tree, node->children, TRUE))) return hr;
    }
    return S_OK;
}

static HRESULT update_subtree_ids_walk(struct dom_tree *tree, xmlNodePtr root, BOOL siblings, BOOL add)
{
    xmlNodePtr node;
    xmlAttrPtr attr, indexed;
    xmlChar *value;
    HRESULT hr;

    for (node = root; node; node = siblings ? node->next : NULL)
    {
        if (node->type == XML_ELEMENT_NODE)
        {
            for (attr = node->properties; attr; attr = attr->next)
            {
                if (!id_attribute(attr)) continue;
                attr->atype = XML_ATTRIBUTE_ID;
                if (!add) xmlRemoveID(tree->doc, attr);
                else
                {
                    value = xmlNodeGetContent((xmlNodePtr)attr);
                    if (!value) return E_OUTOFMEMORY;
                    indexed = xmlGetID(tree->doc, value);
                    if (!indexed && !xmlAddID(NULL, tree->doc, value, attr))
                    {
                        xmlFree(value);
                        return E_FAIL;
                    }
                    xmlFree(value);
                }
            }
        }
        if (node->children &&
                FAILED(hr = update_subtree_ids_walk(tree, node->children, TRUE, add))) return hr;
    }
    return S_OK;
}

static HRESULT WINAPI node_AppendChild(IXmlNode *v, IXmlNode *child, IXmlNode **out)
{
    struct dom_object *parent = from_node(v), *child_object;
    xmlNodePtr xml, current, next;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (parent->kind == DOM_ATTRIBUTE || FAILED(hr = node_child_check(parent, child, &xml))) return hr;
    child_object = from_node(child);
    AcquireSRWLockExclusive(&parent->tree->lock);
    if (node_in_document(parent->xml) &&
            FAILED(hr = validate_subtree_ids_walk(parent->tree,
            child_object->kind == DOM_FRAGMENT ? xml->children : xml,
            child_object->kind == DOM_FRAGMENT)))
    {
        ReleaseSRWLockExclusive(&parent->tree->lock);
        return hr;
    }
    if (child_object->kind == DOM_FRAGMENT)
    {
        for (current = xml->children; current; current = next)
        {
            next = current->next;
            xmlUnlinkNode(current);
            if (!xmlAddChild(parent->xml, current) ||
                    (node_in_document(parent->xml) &&
                     FAILED(hr = update_subtree_ids_walk(parent->tree, current, FALSE, TRUE))))
            {
                hr = E_FAIL;
                break;
            }
        }
        if (!current) hr = S_OK;
    }
    else if (!xmlAddChild(parent->xml, xml)) hr = E_FAIL;
    else if (node_in_document(parent->xml))
        hr = update_subtree_ids_walk(parent->tree, xml, FALSE, TRUE);
    else hr = S_OK;
    if (SUCCEEDED(hr)) IXmlNode_AddRef(*out = child);
    ReleaseSRWLockExclusive(&parent->tree->lock);
    return hr;
}

static HRESULT WINAPI node_InsertBefore(IXmlNode *v, IXmlNode *child, IXmlNode *ref, IXmlNode **out)
{
    struct dom_object *parent = from_node(v), *ref_object;
    xmlNodePtr child_xml, ref_xml;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (FAILED(hr = node_child_check(parent, child, &child_xml))) return hr;
    if (!ref) return node_AppendChild(v, child, out);
    ref_object = from_node(ref);
    if (ref_object->tree != parent->tree || (ref_xml = ref_object->xml)->parent != parent->xml)
        return E_INVALIDARG;
    AcquireSRWLockExclusive(&parent->tree->lock);
    if (node_in_document(parent->xml) &&
            FAILED(hr = validate_subtree_ids_walk(parent->tree, child_xml, FALSE)))
    {
        ReleaseSRWLockExclusive(&parent->tree->lock);
        return hr;
    }
    if (child_xml->parent) xmlUnlinkNode(child_xml);
    if (!xmlAddPrevSibling(ref_xml, child_xml)) hr = E_FAIL;
    else if (node_in_document(parent->xml))
        hr = update_subtree_ids_walk(parent->tree, child_xml, FALSE, TRUE);
    else hr = S_OK;
    if (SUCCEEDED(hr)) IXmlNode_AddRef(*out = child);
    ReleaseSRWLockExclusive(&parent->tree->lock);
    return hr;
}

static HRESULT WINAPI node_ReplaceChild(IXmlNode *v, IXmlNode *child, IXmlNode *ref, IXmlNode **out)
{
    struct dom_object *parent = from_node(v), *ref_object;
    xmlNodePtr child_xml, ref_xml;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!ref || FAILED(hr = node_child_check(parent, child, &child_xml))) return E_INVALIDARG;
    ref_object = from_node(ref);
    if (ref_object->tree != parent->tree || (ref_xml = ref_object->xml)->parent != parent->xml)
        return E_INVALIDARG;
    if (child_xml == ref_xml) { IXmlNode_AddRef(*out = ref); return S_OK; }
    AcquireSRWLockExclusive(&parent->tree->lock);
    if (node_in_document(parent->xml) &&
            FAILED(hr = validate_subtree_ids_walk(parent->tree, child_xml, FALSE)))
    {
        ReleaseSRWLockExclusive(&parent->tree->lock);
        return hr;
    }
    if (child_xml->parent) xmlUnlinkNode(child_xml);
    if (node_in_document(ref_xml)) update_subtree_ids_walk(parent->tree, ref_xml, FALSE, FALSE);
    if (!xmlReplaceNode(ref_xml, child_xml)) hr = E_FAIL;
    else if (node_in_document(parent->xml))
        hr = update_subtree_ids_walk(parent->tree, child_xml, FALSE, TRUE);
    else hr = S_OK;
    if (SUCCEEDED(hr)) IXmlNode_AddRef(*out = ref);
    ReleaseSRWLockExclusive(&parent->tree->lock);
    return hr;
}

static HRESULT WINAPI node_RemoveChild(IXmlNode *v, IXmlNode *child, IXmlNode **out)
{
    struct dom_object *parent = from_node(v), *object;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!child || (object = from_node(child))->tree != parent->tree ||
            object->xml->parent != parent->xml) return E_INVALIDARG;
    AcquireSRWLockExclusive(&parent->tree->lock);
    if (node_in_document(object->xml))
        update_subtree_ids_walk(parent->tree, object->xml, FALSE, FALSE);
    xmlUnlinkNode(object->xml);
    IXmlNode_AddRef(*out = child);
    ReleaseSRWLockExclusive(&parent->tree->lock);
    return S_OK;
}

static HRESULT WINAPI node_CloneNode(IXmlNode *v, boolean deep, IXmlNode **out)
{
    struct dom_object *o = from_node(v);
    xmlNodePtr xml;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    AcquireSRWLockExclusive(&o->tree->lock);
    xml = xmlDocCopyNode(o->xml, o->tree->doc, deep);
    hr = xml ? node_wrap(o->tree, xml, out) : E_OUTOFMEMORY;
    ReleaseSRWLockExclusive(&o->tree->lock);
    return hr;
}
static HRESULT WINAPI node_get_NamespaceUri(IXmlNode *v, IInspectable **out)
{ struct dom_object *o = from_node(v); return boxed_string(o->xml->ns ? o->xml->ns->href : NULL, out); }
static HRESULT WINAPI node_get_LocalName(IXmlNode *v, IInspectable **out)
{
    struct dom_object *o = from_node(v);
    if (o->kind != DOM_ELEMENT && o->kind != DOM_ATTRIBUTE) { if (!out) return E_POINTER; *out = NULL; return S_OK; }
    return boxed_string(o->xml->name, out);
}
static HRESULT WINAPI node_get_Prefix(IXmlNode *v, IInspectable **out)
{ struct dom_object *o = from_node(v); return boxed_string(o->xml->ns ? o->xml->ns->prefix : NULL, out); }
static void normalize_node(xmlNodePtr parent)
{
    xmlNodePtr node, next;

    for (node = parent->children; node; node = next)
    {
        next = node->next;
        if (node->type == XML_ELEMENT_NODE || node->type == XML_DOCUMENT_FRAG_NODE) normalize_node(node);
        if (node->type != XML_TEXT_NODE) continue;
        while (next && next->type == XML_TEXT_NODE)
        {
            xmlNodeAddContent(node, next->content);
            xmlUnlinkNode(next);
            next = node->next;
        }
        if (!node->content || !*node->content) xmlUnlinkNode(node);
    }
}
static HRESULT WINAPI node_Normalize(IXmlNode *v)
{
    struct dom_object *o = from_node(v);
    AcquireSRWLockExclusive(&o->tree->lock);
    normalize_node(o->xml);
    ReleaseSRWLockExclusive(&o->tree->lock);
    return S_OK;
}
static HRESULT WINAPI node_put_Prefix(IXmlNode *v, IInspectable *value) { return E_NOTIMPL; }

static HRESULT WINAPI serializer_GetXml(IXmlNodeSerializer *v, HSTRING *out)
{
    struct dom_object *o = from_serializer(v);
    struct dom_tree *tree = object_tree(o);
    xmlBufferPtr buffer;
    xmlChar *docbuf = NULL;
    int len = 0;
    HRESULT hr;
    if (!out) { tree_release(tree); return E_POINTER; }
    *out = NULL;
    AcquireSRWLockShared(&tree->lock);
    if (o->kind == DOM_DOCUMENT)
    {
        xmlDocDumpMemoryEnc(tree->doc, &docbuf, &len, "UTF-8");
        hr = utf8_hstring(docbuf, out);
        if (docbuf) xmlFree(docbuf);
    }
    else if ((buffer = xmlBufferCreate()))
    {
        xmlNodeDump(buffer, tree->doc, o->xml, 0, 0);
        hr = utf8_hstring(xmlBufferContent(buffer), out);
        xmlBufferFree(buffer);
    }
    else hr = E_OUTOFMEMORY;
    ReleaseSRWLockShared(&tree->lock);
    tree_release(tree);
    return hr;
}
static HRESULT WINAPI serializer_get_InnerText(IXmlNodeSerializer *v, HSTRING *out)
{
    struct dom_object *o = from_serializer(v);
    struct dom_tree *tree = object_tree(o);
    xmlChar *text;
    HRESULT hr;
    if (!out) { tree_release(tree); return E_POINTER; }
    *out = NULL;
    AcquireSRWLockShared(&tree->lock);
    text = xmlNodeGetContent(o->xml);
    hr = utf8_hstring(text, out);
    if (text) xmlFree(text);
    ReleaseSRWLockShared(&tree->lock);
    tree_release(tree);
    return hr;
}
static HRESULT WINAPI serializer_put_InnerText(IXmlNodeSerializer *v, HSTRING value)
{
    struct dom_object *o = from_serializer(v);
    char *text;
    int len;
    HRESULT hr;
    if (o->kind != DOM_ELEMENT && o->kind != DOM_TEXT && o->kind != DOM_ATTRIBUTE &&
            o->kind != DOM_COMMENT && o->kind != DOM_CDATA && o->kind != DOM_PROCESSING_INSTRUCTION)
        return E_NOTIMPL;
    if (FAILED(hr = hstring_utf8(value, &text, &len))) return hr;
    if (o->kind == DOM_CDATA && strstr(text, "]]>")) { free(text); return E_INVALIDARG; }
    if (o->kind == DOM_COMMENT && (strstr(text, "--") || (len && text[len - 1] == '-')))
    { free(text); return E_INVALIDARG; }
    AcquireSRWLockExclusive(&o->tree->lock);
    if (o->kind == DOM_ATTRIBUTE && ((xmlAttrPtr)o->xml)->atype == XML_ATTRIBUTE_ID)
    {
        xmlAttrPtr indexed = xmlGetID(o->tree->doc, (xmlChar *)text);
        if (!*text || xmlValidateNCName((xmlChar *)text, 0) || (indexed && indexed != (xmlAttrPtr)o->xml))
            hr = E_INVALIDARG;
        else
        {
            xmlRemoveID(o->tree->doc, (xmlAttrPtr)o->xml);
            xmlNodeSetContent(o->xml, (xmlChar *)text);
            hr = xmlAddID(NULL, o->tree->doc, (xmlChar *)text, (xmlAttrPtr)o->xml) ? S_OK : E_FAIL;
        }
    }
    else
    {
        xmlNodeSetContent(o->xml, (xmlChar *)text);
        hr = S_OK;
    }
    ReleaseSRWLockExclusive(&o->tree->lock);
    free(text);
    return hr;
}
static HRESULT WINAPI selector_SelectSingleNode(IXmlNodeSelector *v, HSTRING x, IXmlNode **out) { if (!out) return E_POINTER; *out = NULL; return E_NOTIMPL; }
static HRESULT WINAPI selector_SelectNodes(IXmlNodeSelector *v, HSTRING x, IXmlNodeList **out) { if (!out) return E_POINTER; *out = NULL; return E_NOTIMPL; }
static HRESULT WINAPI selector_SelectSingleNodeNS(IXmlNodeSelector *v, HSTRING x, IInspectable *n, IXmlNode **out) { if (!out) return E_POINTER; *out = NULL; return E_NOTIMPL; }
static HRESULT WINAPI selector_SelectNodesNS(IXmlNodeSelector *v, HSTRING x, IInspectable *n, IXmlNodeList **out) { if (!out) return E_POINTER; *out = NULL; return E_NOTIMPL; }

static HRESULT element_name(IXmlElement *v, HSTRING *out) { struct dom_object *o = from_element(v); if (!out) return E_POINTER; *out = NULL; return qualified_hstring(o->xml, out); }
static HRESULT element_get_attr(IXmlElement *v, HSTRING name, HSTRING *out)
{ struct dom_object *o = from_element(v); char *n; int len; xmlChar *value; HRESULT hr; if (!out) return E_POINTER; *out = NULL; if (FAILED(hr = hstring_utf8(name, &n, &len))) return hr; AcquireSRWLockShared(&o->tree->lock); value = xmlGetProp(o->xml, (xmlChar *)n); hr = utf8_hstring(value, out); if (value) xmlFree(value); ReleaseSRWLockShared(&o->tree->lock); free(n); return hr; }
static void detach_attribute(struct dom_tree *tree, xmlAttrPtr attr);
static HRESULT element_set_attr(IXmlElement *v, HSTRING name, HSTRING value)
{
    struct dom_object *o = from_element(v);
    char *n, *val;
    int nl, vl;
    HRESULT hr;
    if (FAILED(hr = hstring_utf8(name, &n, &nl))) return hr;
    if (!valid_xml_name(n) || strchr(n, ':')) { free(n); return E_INVALIDARG; }
    if (FAILED(hr = hstring_utf8(value, &val, &vl))) { free(n); return hr; }
    AcquireSRWLockExclusive(&o->tree->lock);
    hr = xmlSetProp(o->xml, (xmlChar *)n, (xmlChar *)val) ? S_OK : E_FAIL;
    ReleaseSRWLockExclusive(&o->tree->lock);
    free(n);
    free(val);
    return hr;
}
static HRESULT element_remove_attr(IXmlElement *v, HSTRING name)
{
    struct dom_object *o = from_element(v);
    char *n;
    int len;
    xmlAttrPtr attr;
    HRESULT hr;
    if (FAILED(hr = hstring_utf8(name, &n, &len))) return hr;
    AcquireSRWLockExclusive(&o->tree->lock);
    attr = xmlHasProp(o->xml, (xmlChar *)n);
    if (attr) { detach_attribute(o->tree, attr); hr = S_OK; }
    else hr = S_FALSE;
    ReleaseSRWLockExclusive(&o->tree->lock);
    free(n);
    return hr;
}
static HRESULT element_attr_node(IXmlElement *v, HSTRING name, IXmlAttribute **out)
{ struct dom_object *o = from_element(v); char *n; int len; xmlAttrPtr attr; HRESULT hr; if (!out) return E_POINTER; *out = NULL; if (FAILED(hr = hstring_utf8(name, &n, &len))) return hr; AcquireSRWLockShared(&o->tree->lock); attr = xmlHasProp(o->xml, (xmlChar *)n); hr = wrap_iid(o->tree, (xmlNodePtr)attr, &IID_IXmlAttribute, (void **)out); ReleaseSRWLockShared(&o->tree->lock); free(n); return hr; }
static void detach_attribute(struct dom_tree *tree, xmlAttrPtr attr)
{
    if (!attr->parent) return;
    if (attr->atype == XML_ATTRIBUTE_ID) xmlRemoveID(tree->doc, attr);
    if (attr->prev) attr->prev->next = attr->next;
    else attr->parent->properties = attr->next;
    if (attr->next) attr->next->prev = attr->prev;
    attr->parent = NULL;
    attr->prev = NULL;
    attr->next = NULL;
    AcquireSRWLockExclusive(&tree->cache_lock);
    tree_entry(tree, (xmlNodePtr)attr, TRUE);
    ReleaseSRWLockExclusive(&tree->cache_lock);
}

static HRESULT attribute_id_add(struct dom_tree *tree, xmlAttrPtr attr)
{
    static const xmlChar xml_namespace[] = "http://www.w3.org/XML/1998/namespace";
    xmlAttrPtr indexed;
    xmlChar *value;

    if (!attr->ns || xmlStrcmp(attr->ns->href, xml_namespace) || xmlStrcmp(attr->name, (xmlChar *)"id"))
        return S_OK;
    value = xmlNodeGetContent((xmlNodePtr)attr);
    if (!value) return E_OUTOFMEMORY;
    indexed = xmlGetID(tree->doc, value);
    if (!*value || xmlValidateNCName(value, 0) ||
            (node_in_document((xmlNodePtr)attr) && indexed && indexed != attr))
    {
        xmlFree(value);
        return E_INVALIDARG;
    }
    attr->atype = XML_ATTRIBUTE_ID;
    if (node_in_document((xmlNodePtr)attr) && !indexed && !xmlAddID(NULL, tree->doc, value, attr))
    {
        xmlFree(value);
        return E_FAIL;
    }
    xmlFree(value);
    return S_OK;
}

static HRESULT attach_attribute(struct dom_object *element, IXmlAttribute *attribute, IXmlAttribute **out)
{
    struct dom_object *object;
    struct dom_entry *entry;
    xmlAttrPtr attr, previous, indexed;
    xmlChar *id_value = NULL;
    xmlNsPtr ns;
    HRESULT hr;

    if (!out) return E_POINTER;
    *out = NULL;
    if (!attribute || (object = from_attribute(attribute))->tree != element->tree) return E_INVALIDARG;
    attr = (xmlAttrPtr)object->xml;
    if (attr->parent && attr->parent != element->xml) return E_INVALIDARG;
    AcquireSRWLockExclusive(&element->tree->lock);
    previous = attr->ns ? xmlHasNsProp(element->xml, attr->name, attr->ns->href) :
            xmlHasProp(element->xml, attr->name);
    if (previous == attr)
    {
        ReleaseSRWLockExclusive(&element->tree->lock);
        return S_FALSE;
    }
    if (id_attribute(attr))
    {
        id_value = xmlNodeGetContent((xmlNodePtr)attr);
        indexed = id_value ? xmlGetID(element->tree->doc, id_value) : NULL;
        if (!id_value || !*id_value || xmlValidateNCName(id_value, 0) ||
                (node_in_document(element->xml) && indexed && indexed != previous))
        {
            if (id_value) xmlFree(id_value);
            ReleaseSRWLockExclusive(&element->tree->lock);
            return E_INVALIDARG;
        }
        xmlFree(id_value);
    }
    if (previous)
    {
        if (FAILED(hr = wrap_iid(element->tree, (xmlNodePtr)previous, &IID_IXmlAttribute, (void **)out)))
        {
            if (attr->atype == XML_ATTRIBUTE_ID) xmlRemoveID(element->tree->doc, attr);
            ReleaseSRWLockExclusive(&element->tree->lock);
            return hr;
        }
        detach_attribute(element->tree, previous);
    }
    AcquireSRWLockExclusive(&element->tree->cache_lock);
    entry = tree_entry(element->tree, (xmlNodePtr)attr, FALSE);
    if (entry && entry->detached_ns)
    {
        ns = xmlSearchNs(element->tree->doc, element->xml, entry->detached_ns->prefix);
        if (!ns || xmlStrcmp(ns->href, entry->detached_ns->href))
            ns = xmlNewNs(element->xml, entry->detached_ns->href, entry->detached_ns->prefix);
        if (!ns)
        {
            ReleaseSRWLockExclusive(&element->tree->cache_lock);
            ReleaseSRWLockExclusive(&element->tree->lock);
            if (*out) { IXmlAttribute_Release(*out); *out = NULL; }
            return E_OUTOFMEMORY;
        }
        attr->ns = ns;
        xmlFreeNs(entry->detached_ns);
        entry->detached_ns = NULL;
    }
    ReleaseSRWLockExclusive(&element->tree->cache_lock);
    if (!xmlAddChild(element->xml, (xmlNodePtr)attr)) hr = E_FAIL;
    else hr = attribute_id_add(element->tree, attr);
    ReleaseSRWLockExclusive(&element->tree->lock);
    return hr;
}

static HRESULT namespace_lookup(IXmlElement *v, IInspectable *namespace_value, HSTRING local_name,
        xmlAttrPtr *out)
{
    struct dom_object *o = from_element(v);
    char *uri = NULL, *local = NULL;
    int length;
    HRESULT hr;

    *out = NULL;
    if (FAILED(hr = namespace_uri(namespace_value, &uri))) return hr;
    if (FAILED(hr = hstring_utf8(local_name, &local, &length))) { free(uri); return hr; }
    if (!valid_xml_name(local) || strchr(local, ':')) hr = E_INVALIDARG;
    else
    {
        *out = xmlHasNsProp(o->xml, (xmlChar *)local, (xmlChar *)uri);
        hr = S_OK;
    }
    free(uri);
    free(local);
    return hr;
}
static HRESULT WINAPI element_get_TagName(IXmlElement *v, HSTRING *out) { return element_name(v, out); }
static HRESULT WINAPI element_GetAttribute(IXmlElement *v, HSTRING n, HSTRING *out) { return element_get_attr(v, n, out); }
static HRESULT WINAPI element_SetAttribute(IXmlElement *v, HSTRING n, HSTRING value) { return element_set_attr(v, n, value); }
static HRESULT WINAPI element_RemoveAttribute(IXmlElement *v, HSTRING n) { return element_remove_attr(v, n); }
static HRESULT WINAPI element_GetAttributeNode(IXmlElement *v, HSTRING n, IXmlAttribute **out) { return element_attr_node(v, n, out); }
static HRESULT WINAPI element_SetAttributeNode(IXmlElement *v, IXmlAttribute *a, IXmlAttribute **out)
{ return attach_attribute(from_element(v), a, out); }
static HRESULT WINAPI element_RemoveAttributeNode(IXmlElement *v, IXmlAttribute *a, IXmlAttribute **out)
{
    struct dom_object *element = from_element(v), *attribute;
    if (!out) return E_POINTER;
    *out = NULL;
    if (!a || (attribute = from_attribute(a))->tree != element->tree ||
            attribute->xml->parent != element->xml) return E_INVALIDARG;
    AcquireSRWLockExclusive(&element->tree->lock);
    detach_attribute(element->tree, (xmlAttrPtr)attribute->xml);
    IXmlAttribute_AddRef(*out = a);
    ReleaseSRWLockExclusive(&element->tree->lock);
    return S_OK;
}
static HRESULT WINAPI element_GetElementsByTagName(IXmlElement *v, HSTRING n, IXmlNodeList **out) { struct dom_object *o = from_element(v); return elements_by_tag(o->tree, o->xml, n, out); }
static HRESULT WINAPI element_SetAttributeNS(IXmlElement *v, IInspectable *namespace_value,
        HSTRING qualified, HSTRING value)
{
    struct dom_object *o = from_element(v);
    char *name = NULL, *prefix = NULL, *local = NULL, *uri = NULL, *text = NULL;
    xmlAttrPtr attr, indexed;
    xmlNsPtr ns = NULL;
    int length;
    HRESULT hr;

    if (FAILED(hr = qualified_name(qualified, &name, &prefix, &local))) return hr;
    if (FAILED(hr = namespace_uri(namespace_value, &uri))) { free(name); return hr; }
    if (FAILED(hr = hstring_utf8(value, &text, &length))) { free(uri); free(name); return hr; }
    if (!valid_namespace_binding(uri, prefix, local, TRUE)) hr = E_INVALIDARG;
    else if (uri && !strcmp(uri, "http://www.w3.org/XML/1998/namespace") && !strcmp(local, "id") &&
            (!*text || xmlValidateNCName((xmlChar *)text, 0))) hr = E_INVALIDARG;
    else
    {
        AcquireSRWLockExclusive(&o->tree->lock);
        attr = xmlHasNsProp(o->xml, (xmlChar *)local, (xmlChar *)uri);
        indexed = uri && !strcmp(uri, "http://www.w3.org/XML/1998/namespace") && !strcmp(local, "id") ?
                xmlGetID(o->tree->doc, (xmlChar *)text) : NULL;
        if (indexed && indexed != attr) hr = E_INVALIDARG;
        else
        {
            if (attr && attr->atype == XML_ATTRIBUTE_ID) xmlRemoveID(o->tree->doc, attr);
            if (uri)
            {
                if (!strcmp(uri, "http://www.w3.org/XML/1998/namespace"))
                    ns = xmlSearchNsByHref(o->tree->doc, o->xml, (xmlChar *)uri);
                else
                {
                    ns = xmlSearchNs(o->tree->doc, o->xml, (xmlChar *)prefix);
                    if (!ns || xmlStrcmp(ns->href, (xmlChar *)uri))
                        ns = xmlNewNs(o->xml, (xmlChar *)uri, (xmlChar *)prefix);
                }
            }
            attr = uri && !ns ? NULL : xmlSetNsProp(o->xml, ns, (xmlChar *)local, (xmlChar *)text);
            hr = attr ? attribute_id_add(o->tree, attr) : E_OUTOFMEMORY;
        }
        ReleaseSRWLockExclusive(&o->tree->lock);
    }
    free(text);
    free(uri);
    free(name);
    return hr;
}
static HRESULT WINAPI element_GetAttributeNS(IXmlElement *v, IInspectable *namespace_value,
        HSTRING local, HSTRING *out)
{
    struct dom_object *o = from_element(v);
    xmlAttrPtr attr;
    xmlChar *value;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    AcquireSRWLockShared(&o->tree->lock);
    if (SUCCEEDED(hr = namespace_lookup(v, namespace_value, local, &attr)))
    {
        value = attr ? xmlNodeGetContent((xmlNodePtr)attr) : NULL;
        hr = utf8_hstring(value, out);
        if (value) xmlFree(value);
    }
    ReleaseSRWLockShared(&o->tree->lock);
    return hr;
}
static HRESULT WINAPI element_RemoveAttributeNS(IXmlElement *v, IInspectable *namespace_value, HSTRING local)
{
    struct dom_object *o = from_element(v);
    xmlAttrPtr attr;
    HRESULT hr;
    AcquireSRWLockExclusive(&o->tree->lock);
    if (SUCCEEDED(hr = namespace_lookup(v, namespace_value, local, &attr)) && attr)
    {
        detach_attribute(o->tree, attr);
        hr = S_OK;
    }
    else if (SUCCEEDED(hr)) hr = S_FALSE;
    ReleaseSRWLockExclusive(&o->tree->lock);
    return hr;
}
static HRESULT WINAPI element_SetAttributeNodeNS(IXmlElement *v, IXmlAttribute *a, IXmlAttribute **out)
{ return attach_attribute(from_element(v), a, out); }
static HRESULT WINAPI element_GetAttributeNodeNS(IXmlElement *v, IInspectable *namespace_value,
        HSTRING local, IXmlAttribute **out)
{
    struct dom_object *o = from_element(v);
    xmlAttrPtr attr;
    HRESULT hr;
    if (!out) return E_POINTER;
    *out = NULL;
    AcquireSRWLockShared(&o->tree->lock);
    if (SUCCEEDED(hr = namespace_lookup(v, namespace_value, local, &attr)))
        hr = wrap_iid(o->tree, (xmlNodePtr)attr, &IID_IXmlAttribute, (void **)out);
    ReleaseSRWLockShared(&o->tree->lock);
    return hr;
}

static HRESULT WINAPI character_get_Data(IXmlCharacterData *v, HSTRING *out) { return serializer_get_InnerText((IXmlNodeSerializer *)&from_character(v)->serializer, out); }
static HRESULT WINAPI character_put_Data(IXmlCharacterData *v, HSTRING value) { return serializer_put_InnerText((IXmlNodeSerializer *)&from_character(v)->serializer, value); }
static HRESULT WINAPI character_get_Length(IXmlCharacterData *v, UINT32 *out) { HSTRING value = NULL; HRESULT hr; if (!out) return E_POINTER; *out = 0; if (FAILED(hr = character_get_Data(v, &value))) return hr; *out = WindowsGetStringLen(value); WindowsDeleteString(value); return S_OK; }
static HRESULT WINAPI character_SubstringData(IXmlCharacterData *v, UINT32 offset, UINT32 count, HSTRING *out) { HSTRING value = NULL; UINT32 len; HRESULT hr; if (!out) return E_POINTER; *out = NULL; if (FAILED(hr = character_get_Data(v, &value))) return hr; len = WindowsGetStringLen(value); if (offset > len) { WindowsDeleteString(value); return E_INVALIDARG; } if (count > len - offset) count = len - offset; hr = WindowsCreateString(WindowsGetStringRawBuffer(value, NULL) + offset, count, out); WindowsDeleteString(value); return hr; }
static HRESULT WINAPI character_AppendData(IXmlCharacterData *v, HSTRING value) { return E_NOTIMPL; }
static HRESULT WINAPI character_InsertData(IXmlCharacterData *v, UINT32 a, HSTRING b) { return E_NOTIMPL; }
static HRESULT WINAPI character_DeleteData(IXmlCharacterData *v, UINT32 a, UINT32 b) { return E_NOTIMPL; }
static HRESULT WINAPI character_ReplaceData(IXmlCharacterData *v, UINT32 a, UINT32 b, HSTRING c) { return E_NOTIMPL; }
static HRESULT WINAPI text_SplitText(IXmlText *v, UINT32 offset, IXmlText **out) { if (!out) return E_POINTER; *out = NULL; return E_NOTIMPL; }
static HRESULT WINAPI attribute_get_Name(IXmlAttribute *v, HSTRING *out) { struct dom_object *o = from_attribute(v); if (!out) return E_POINTER; *out = NULL; return qualified_hstring(o->xml, out); }
static HRESULT WINAPI attribute_get_Specified(IXmlAttribute *v, boolean *out) { if (!out) return E_POINTER; *out = TRUE; return S_OK; }
static HRESULT WINAPI attribute_get_Value(IXmlAttribute *v, HSTRING *out) { return serializer_get_InnerText((IXmlNodeSerializer *)&from_attribute(v)->serializer, out); }
static HRESULT WINAPI attribute_put_Value(IXmlAttribute *v, HSTRING value) { return serializer_put_InnerText((IXmlNodeSerializer *)&from_attribute(v)->serializer, value); }

static HRESULT WINAPI processing_instruction_get_Target(IXmlProcessingInstruction *v, HSTRING *out)
{ struct dom_object *o = from_processing_instruction(v); if (!out) return E_POINTER; *out = NULL; return utf8_hstring(o->xml->name, out); }
static HRESULT WINAPI processing_instruction_get_Data(IXmlProcessingInstruction *v, HSTRING *out)
{ return serializer_get_InnerText(&from_processing_instruction(v)->serializer, out); }
static HRESULT WINAPI processing_instruction_put_Data(IXmlProcessingInstruction *v, HSTRING value)
{
    const WCHAR *data = WindowsGetStringRawBuffer(value, NULL);
    if (data && wcsstr(data, L"?>")) return E_INVALIDARG;
    return serializer_put_InnerText(&from_processing_instruction(v)->serializer, value);
}

static HRESULT WINAPI node_get_ChildNodes(IXmlNode *v, IXmlNodeList **out)
{ struct dom_object *o = from_node(v); IXmlNode **items = NULL; UINT32 count = 0, i; xmlNodePtr n; HRESULT hr; if (!out) return E_POINTER; *out = NULL; for (n = o->xml->children; n; n = n->next) count++; if (count && !(items = calloc(count, sizeof(*items)))) return E_OUTOFMEMORY; for (n = o->xml->children, i = 0; n; n = n->next, ++i) { struct dom_object *child = wrap_node(o->tree, n); if (!child) { hr = E_OUTOFMEMORY; goto done; } items[i] = &child->node; } hr = list_new(o->tree, items, count, out); done: for (i = 0; i < count; ++i) if (items && items[i]) IXmlNode_Release(items[i]); free(items); return hr; }

static HRESULT WINAPI list_QueryInterface(IXmlNodeList *v, REFIID iid, void **out) { if (!out) return E_POINTER; *out = NULL; if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IXmlNodeList)) { *out = v; IXmlNodeList_AddRef(v); return S_OK; } return E_NOINTERFACE; }
static ULONG WINAPI list_AddRef(IXmlNodeList *v) { return InterlockedIncrement(&from_list(v)->ref); }
static ULONG WINAPI list_Release(IXmlNodeList *v) { struct dom_list *l = from_list(v); ULONG ref = InterlockedDecrement(&l->ref); UINT32 i; if (!ref) { for (i = 0; i < l->count; ++i) IXmlNode_Release(l->items[i]); free(l->items); tree_release(l->tree); free(l); } return ref; }
static HRESULT WINAPI list_GetIids(IXmlNodeList *v, ULONG *c, IID **i) { return object_iids(c, i); }
static HRESULT WINAPI list_GetRuntimeClassName(IXmlNodeList *v, HSTRING *n) { if (!n) return E_POINTER; *n = NULL; return WindowsCreateString(L"Windows.Data.Xml.Dom.XmlNodeList", 33, n); }
static HRESULT WINAPI list_GetTrustLevel(IXmlNodeList *v, TrustLevel *l) { if (!l) return E_POINTER; *l = BaseTrust; return S_OK; }
static HRESULT WINAPI list_get_Length(IXmlNodeList *v, UINT32 *out) { if (!out) return E_POINTER; *out = from_list(v)->count; return S_OK; }
static HRESULT WINAPI list_Item(IXmlNodeList *v, UINT32 index, IXmlNode **out) { struct dom_list *l = from_list(v); if (!out) return E_POINTER; *out = NULL; if (index >= l->count) return E_BOUNDS; IXmlNode_AddRef(*out = l->items[index]); return S_OK; }

static const IXmlDocumentVtbl document_vtbl = { document_QueryInterface, document_AddRef, document_Release, document_GetIids, document_GetRuntimeClassName, document_GetTrustLevel, document_get_Doctype, document_get_Implementation, document_get_DocumentElement, document_CreateElement, document_CreateDocumentFragment, document_CreateTextNode, document_CreateComment, document_CreateProcessingInstruction, document_CreateAttribute, document_CreateEntityReference, document_GetElementsByTagName, document_CreateCDataSection, document_get_DocumentUri, document_CreateAttributeNS, document_CreateElementNS, document_GetElementById, document_ImportNode };
static const IXmlDocumentIOVtbl io_vtbl = { io_QueryInterface, io_AddRef, io_Release, io_GetIids, io_GetRuntimeClassName, io_GetTrustLevel, io_LoadXml, io_LoadXmlWithSettings, io_SaveToFileAsync };
static const IXmlDocumentIO2Vtbl io2_vtbl = { io2_QueryInterface, io2_AddRef, io2_Release, io2_GetIids, io2_GetRuntimeClassName, io2_GetTrustLevel, io2_LoadXmlFromBuffer, io2_LoadXmlFromBufferWithSettings };
static const IXmlNodeVtbl node_vtbl = { node_QueryInterface, node_AddRef, node_Release, node_GetIids, node_GetRuntimeClassName, node_GetTrustLevel, node_get_NodeValue, node_put_NodeValue, node_get_NodeType, node_get_NodeName, node_get_ParentNode, node_get_ChildNodes, node_get_FirstChild, node_get_LastChild, node_get_PreviousSibling, node_get_NextSibling, node_get_Attributes, node_HasChildNodes, node_get_OwnerDocument, node_InsertBefore, node_ReplaceChild, node_RemoveChild, node_AppendChild, node_CloneNode, node_get_NamespaceUri, node_get_LocalName, node_get_Prefix, node_Normalize, node_put_Prefix };
static const IXmlNodeSerializerVtbl serializer_vtbl = { serializer_QueryInterface, serializer_AddRef, serializer_Release, serializer_GetIids, serializer_GetRuntimeClassName, serializer_GetTrustLevel, serializer_GetXml, serializer_get_InnerText, serializer_put_InnerText };
static const IXmlNodeSelectorVtbl selector_vtbl = { selector_QueryInterface, selector_AddRef, selector_Release, selector_GetIids, selector_GetRuntimeClassName, selector_GetTrustLevel, selector_SelectSingleNode, selector_SelectNodes, selector_SelectSingleNodeNS, selector_SelectNodesNS };
static const IXmlElementVtbl element_vtbl = { element_QueryInterface, element_AddRef, element_Release, element_GetIids, element_GetRuntimeClassName, element_GetTrustLevel, element_get_TagName, element_GetAttribute, element_SetAttribute, element_RemoveAttribute, element_GetAttributeNode, element_SetAttributeNode, element_RemoveAttributeNode, element_GetElementsByTagName, element_SetAttributeNS, element_GetAttributeNS, element_RemoveAttributeNS, element_SetAttributeNodeNS, element_GetAttributeNodeNS };
static const IXmlCharacterDataVtbl character_vtbl = { character_QueryInterface, character_AddRef, character_Release, character_GetIids, character_GetRuntimeClassName, character_GetTrustLevel, character_get_Data, character_put_Data, character_get_Length, character_SubstringData, character_AppendData, character_InsertData, character_DeleteData, character_ReplaceData };
static const IXmlTextVtbl text_vtbl = { text_QueryInterface, text_AddRef, text_Release, text_GetIids, text_GetRuntimeClassName, text_GetTrustLevel, text_SplitText };
static const IXmlAttributeVtbl attribute_vtbl = { attribute_QueryInterface, attribute_AddRef, attribute_Release, attribute_GetIids, attribute_GetRuntimeClassName, attribute_GetTrustLevel, attribute_get_Name, attribute_get_Specified, attribute_get_Value, attribute_put_Value };
static const IXmlDocumentFragmentVtbl fragment_vtbl = { fragment_QueryInterface, fragment_AddRef,
        fragment_Release, fragment_GetIids, fragment_GetRuntimeClassName, fragment_GetTrustLevel };
static const IXmlCommentVtbl comment_vtbl = { comment_QueryInterface, comment_AddRef, comment_Release,
        comment_GetIids, comment_GetRuntimeClassName, comment_GetTrustLevel };
static const IXmlProcessingInstructionVtbl processing_instruction_vtbl =
{
    processing_instruction_QueryInterface, processing_instruction_AddRef, processing_instruction_Release,
    processing_instruction_GetIids, processing_instruction_GetRuntimeClassName,
    processing_instruction_GetTrustLevel, processing_instruction_get_Target,
    processing_instruction_get_Data, processing_instruction_put_Data
};
static const IXmlEntityReferenceVtbl entity_reference_vtbl = { entity_reference_QueryInterface,
        entity_reference_AddRef, entity_reference_Release, entity_reference_GetIids,
        entity_reference_GetRuntimeClassName, entity_reference_GetTrustLevel };
static const IXmlCDataSectionVtbl cdata_vtbl = { cdata_QueryInterface, cdata_AddRef, cdata_Release,
        cdata_GetIids, cdata_GetRuntimeClassName, cdata_GetTrustLevel };
static const IXmlNodeListVtbl list_vtbl = { list_QueryInterface, list_AddRef, list_Release, list_GetIids, list_GetRuntimeClassName, list_GetTrustLevel, list_get_Length, list_Item };
static const IXmlDomImplementationVtbl implementation_vtbl =
{
    implementation_QueryInterface, implementation_AddRef, implementation_Release,
    implementation_GetIids, implementation_GetRuntimeClassName, implementation_GetTrustLevel,
    implementation_HasFeature
};
HRESULT xml_document_create(IInspectable **out)
{ xmlDocPtr xml; struct dom_tree *tree; struct dom_object *object; if (!out) return E_POINTER; *out = NULL; if (!(xml = xmlNewDoc((xmlChar *)"1.0"))) return E_OUTOFMEMORY; if (!(tree = tree_new(xml))) { xmlFreeDoc(xml); return E_OUTOFMEMORY; } if (!(object = wrap_node(tree, (xmlNodePtr)xml))) { tree_release(tree); return E_OUTOFMEMORY; } tree_release(tree); *out = (IInspectable *)&object->document; return S_OK; }

struct dom_factory { IActivationFactory iface; };
static struct dom_factory factory;
static HRESULT WINAPI factory_QueryInterface(IActivationFactory *v, REFIID iid, void **out) { if (!out) return E_POINTER; *out = NULL; if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IInspectable) || IsEqualGUID(iid, &IID_IActivationFactory)) { *out = v; IActivationFactory_AddRef(v); return S_OK; } return E_NOINTERFACE; }
static ULONG WINAPI factory_AddRef(IActivationFactory *v) { return 2; }
static ULONG WINAPI factory_Release(IActivationFactory *v) { return 1; }
static HRESULT WINAPI factory_GetIids(IActivationFactory *v, ULONG *c, IID **i) { return object_iids(c, i); }
static HRESULT WINAPI factory_GetRuntimeClassName(IActivationFactory *v, HSTRING *n)
{ static const WCHAR name[] = L"Windows.Data.Xml.Dom.XmlDocument"; if (!n) return E_POINTER; *n = NULL; return WindowsCreateString(name, ARRAY_SIZE(name) - 1, n); }
static HRESULT WINAPI factory_GetTrustLevel(IActivationFactory *v, TrustLevel *l) { if (!l) return E_POINTER; *l = BaseTrust; return S_OK; }
static HRESULT WINAPI factory_ActivateInstance(IActivationFactory *v, IInspectable **out) { return xml_document_create(out); }
static const IActivationFactoryVtbl factory_vtbl = { factory_QueryInterface, factory_AddRef, factory_Release, factory_GetIids, factory_GetRuntimeClassName, factory_GetTrustLevel, factory_ActivateInstance };
HRESULT WINAPI DllGetActivationFactory(HSTRING classid, IActivationFactory **out)
{
    const WCHAR *name;
    if (!out) return E_POINTER;
    *out = NULL;
    name = WindowsGetStringRawBuffer(classid, NULL);
    if (!name || wcscmp(name, L"Windows.Data.Xml.Dom.XmlDocument")) return CLASS_E_CLASSNOTAVAILABLE;
    factory.iface.lpVtbl = &factory_vtbl;
    *out = &factory.iface;
    IActivationFactory_AddRef(*out);
    return S_OK;
}
HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out) { if (out) *out = NULL; return CLASS_E_CLASSNOTAVAILABLE; }
HRESULT WINAPI DllCanUnloadNow(void) { return S_FALSE; }
