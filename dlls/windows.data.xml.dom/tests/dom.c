#define COBJMACROS
#include <stdarg.h>
#include "initguid.h"
#include "windef.h"
#include "winbase.h"
#include "winstring.h"
#include "roapi.h"
#define WIDL_using_Windows_Foundation
#include "windows.foundation.h"
#define WIDL_using_Windows_Data_Xml_Dom
#include "windows.data.xml.dom.h"
#include "wine/test.h"

static HSTRING str(const WCHAR *value)
{
    HSTRING ret;
    WindowsCreateString(value, lstrlenW(value), &ret);
    return ret;
}

static HRESULT call_DllGetActivationFactory(HSTRING class, IActivationFactory **factory)
{
    static HRESULT (WINAPI *dll_get_activation_factory)(HSTRING, IActivationFactory **);
    static HMODULE module;

    if (!module && !(module = LoadLibraryW(L"windows.data.xml.dom.dll")))
        return HRESULT_FROM_WIN32(GetLastError());
    if (!dll_get_activation_factory &&
        !(dll_get_activation_factory = (void *)GetProcAddress(module, "DllGetActivationFactory")))
        return HRESULT_FROM_WIN32(GetLastError());
    return dll_get_activation_factory(class, factory);
}

static IXmlDocument *new_document(void)
{
    HSTRING name = str(L"Windows.Data.Xml.Dom.XmlDocument");
    IInspectable *inspectable = NULL;
    IActivationFactory *factory = NULL;
    IXmlDocument *document = NULL;
    HRESULT hr;

    hr = call_DllGetActivationFactory(name, &factory);
    WindowsDeleteString(name);
    ok(hr == S_OK, "DllGetActivationFactory returned %#lx\n", hr);
    if (FAILED(hr)) return NULL;

    hr = IActivationFactory_ActivateInstance(factory, &inspectable);
    IActivationFactory_Release(factory);
    ok(hr == S_OK, "IActivationFactory_ActivateInstance returned %#lx\n", hr);
    if (FAILED(hr)) return NULL;

    hr = IInspectable_QueryInterface(inspectable, &IID_IXmlDocument, (void **)&document);
    IInspectable_Release(inspectable);
    ok(hr == S_OK, "IXmlDocument QI returned %#lx\n", hr);
    return document;
}


static HSTRING document_xml(IXmlDocument *document)
{
    IXmlNodeSerializer *serializer = NULL;
    HSTRING value = NULL;
    HRESULT hr = IXmlDocument_QueryInterface(document, &IID_IXmlNodeSerializer, (void **)&serializer);
    ok(hr == S_OK, "serializer QI returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IXmlNodeSerializer_GetXml(serializer, &value);
        ok(hr == S_OK, "GetXml returned %#lx\n", hr);
        IXmlNodeSerializer_Release(serializer);
    }
    return value;
}

static void test_secure_load(IXmlDocument *document)
{
    IXmlDocumentIO *io = NULL;
    IXmlElement *element = NULL;
    IXmlDocumentType *doctype = (void *)0xdeadbeef;
    HSTRING xml, malformed, before, after, tag = NULL, uri = (void *)0xdeadbeef;
    HRESULT hr;

    hr = IXmlDocument_QueryInterface(document, &IID_IXmlDocumentIO, (void **)&io);
    ok(hr == S_OK, "IO QI returned %#lx\n", hr);
    xml = str(L"<toast><text>hello</text></toast>");
    ok(IXmlDocumentIO_LoadXml(io, xml) == S_OK, "valid XML was rejected\n");
    WindowsDeleteString(xml);
    hr = IXmlDocument_get_Doctype(document, &doctype);
    ok(hr == S_OK && !doctype, "get_Doctype returned %#lx, doctype %p\n", hr, doctype);
    hr = IXmlDocument_get_DocumentUri(document, &uri);
    ok(hr == S_OK && !uri, "get_DocumentUri returned %#lx, URI %s\n", hr, wine_dbgstr_hstring(uri));
    ok(IXmlDocument_get_DocumentElement(document, &element) == S_OK, "missing document element\n");
    IXmlElement_get_TagName(element, &tag);
    ok(!lstrcmpW(WindowsGetStringRawBuffer(tag, NULL), L"toast"), "unexpected root element\n");
    WindowsDeleteString(tag);
    IXmlElement_Release(element);

    before = document_xml(document);
    malformed = str(L"<toast><text>broken</toast>");
    hr = IXmlDocumentIO_LoadXml(io, malformed);
    ok(FAILED(hr), "malformed XML was accepted\n");
    WindowsDeleteString(malformed);
    after = document_xml(document);
    ok(!lstrcmpW(WindowsGetStringRawBuffer(before, NULL), WindowsGetStringRawBuffer(after, NULL)),
            "failed LoadXml replaced the previous tree\n");
    WindowsDeleteString(before);
    WindowsDeleteString(after);

    malformed = str(L"<!DOCTYPE toast [<!ENTITY x SYSTEM 'file:///tmp/secret'>]><toast>&x;</toast>");
    hr = IXmlDocumentIO_LoadXml(io, malformed);
    ok(FAILED(hr), "DTD/external entity was accepted\n");
    WindowsDeleteString(malformed);
    IXmlDocumentIO_Release(io);
}

static void test_nodes_and_lists(IXmlDocument *document)
{
    IXmlDocumentIO *io = NULL;
    IXmlNodeList *list = NULL;
    IXmlNode *node = NULL, *held = NULL, *root_node = NULL, *element_node = NULL;
    IXmlNodeSerializer *serializer = NULL;
    IXmlElement *root = NULL, *element = NULL;
    IXmlText *text = NULL;
    HSTRING xml, tag, value, name, output, inner = NULL;
    UINT32 count;
    HRESULT hr;

    IXmlDocument_QueryInterface(document, &IID_IXmlDocumentIO, (void **)&io);
    xml = str(L"<root><!-- <text>comment</text> --><textual>wrong</textual><text>right</text></root>");
    ok(IXmlDocumentIO_LoadXml(io, xml) == S_OK, "toast-shaped XML was rejected\n");
    WindowsDeleteString(xml);
    IXmlDocumentIO_Release(io);

    tag = str(L"text");
    ok(IXmlDocument_GetElementsByTagName(document, tag, &list) == S_OK, "GetElementsByTagName failed\n");
    WindowsDeleteString(tag);
    ok(IXmlNodeList_get_Length(list, &count) == S_OK && count == 1, "got %u exact text elements\n", count);
    hr = IXmlNodeList_Item(list, count, &node);
    ok(hr == E_BOUNDS && !node, "out-of-bounds Item returned %#lx\n", hr);
    ok(IXmlNodeList_Item(list, 0, &held) == S_OK, "Item failed\n");
    IXmlNodeList_AddRef(list);
    IXmlNodeList_Release(list);
    IXmlNodeList_Release(list);
    IXmlNode_QueryInterface(held, &IID_IXmlNodeSerializer, (void **)&serializer);
    IXmlNodeSerializer_get_InnerText(serializer, &inner);
    ok(!lstrcmpW(WindowsGetStringRawBuffer(inner, NULL), L"right"), "comment or textual element selected\n");
    WindowsDeleteString(inner);
    IXmlNodeSerializer_Release(serializer);
    IXmlNode_Release(held);

    IXmlDocument_get_DocumentElement(document, &root);
    IXmlElement_QueryInterface(root, &IID_IXmlNode, (void **)&root_node);
    name = str(L"answer");
    value = str(L"value");
    ok(IXmlElement_SetAttribute(root, name, value) == S_OK, "SetAttribute failed\n");
    WindowsDeleteString(name);
    WindowsDeleteString(value);
    name = str(L"child");
    ok(IXmlDocument_CreateElement(document, name, &element) == S_OK, "CreateElement failed\n");
    WindowsDeleteString(name);
    value = str(L"payload");
    ok(IXmlDocument_CreateTextNode(document, value, &text) == S_OK, "CreateTextNode failed\n");
    WindowsDeleteString(value);
    IXmlText_QueryInterface(text, &IID_IXmlNode, (void **)&node);
    hr = IXmlNode_AppendChild(node, node, NULL);
    ok(hr == E_POINTER, "AppendChild did not initialize/check output\n");
    IXmlElement_QueryInterface(element, &IID_IXmlNode, (void **)&element_node);
    ok(IXmlNode_AppendChild(element_node, node, &held) == S_OK, "text AppendChild failed\n");
    IXmlNode_Release(held);
    IXmlNode_Release(node);
    ok(IXmlNode_AppendChild(root_node, element_node, &held) == S_OK, "AppendChild failed\n");
    IXmlNode_Release(held);
    IXmlNode_Release(element_node);
    IXmlText_Release(text);
    IXmlElement_Release(element);
    IXmlNode_Release(root_node);
    IXmlElement_Release(root);

    output = document_xml(document);
    ok(wcsstr(WindowsGetStringRawBuffer(output, NULL), L"answer=\"value\"") != NULL, "attribute not serialized\n");
    ok(wcsstr(WindowsGetStringRawBuffer(output, NULL), L"<child>payload</child>") != NULL, "child/text not serialized\n");
    WindowsDeleteString(output);
}

static void test_attribute_and_import(IXmlDocument *document)
{
    IXmlDocument *source = NULL;
    IXmlDocumentIO *source_io = NULL;
    IXmlAttribute *attribute = NULL;
    IXmlElement *source_root = NULL, *target_root = NULL;
    IXmlNode *source_node = NULL, *target_node = NULL, *imported = NULL, *appended = NULL;
    HSTRING name = NULL, value = NULL, actual = NULL, xml = NULL, output = NULL;
    HRESULT hr;

    name = str(L"detached");
    hr = IXmlDocument_CreateAttribute(document, name, &attribute);
    ok(hr == S_OK && attribute, "CreateAttribute returned %#lx, attribute %p\n", hr, attribute);
    if (attribute)
    {
        hr = IXmlAttribute_get_Name(attribute, &actual);
        ok(hr == S_OK && !lstrcmpW(WindowsGetStringRawBuffer(actual, NULL), L"detached"),
                "get_Name returned %#lx, name %s\n", hr, wine_dbgstr_hstring(actual));
        WindowsDeleteString(actual);
        actual = NULL;
        value = str(L"value");
        hr = IXmlAttribute_put_Value(attribute, value);
        ok(hr == S_OK, "put_Value returned %#lx\n", hr);
        hr = IXmlAttribute_get_Value(attribute, &actual);
        ok(hr == S_OK && !lstrcmpW(WindowsGetStringRawBuffer(actual, NULL), L"value"),
                "get_Value returned %#lx, value %s\n", hr, wine_dbgstr_hstring(actual));
    }
    WindowsDeleteString(actual);
    WindowsDeleteString(value);
    WindowsDeleteString(name);
    if (attribute) IXmlAttribute_Release(attribute);

    source = new_document();
    if (!source) return;
    hr = IXmlDocument_QueryInterface(source, &IID_IXmlDocumentIO, (void **)&source_io);
    ok(hr == S_OK, "source IXmlDocumentIO QI returned %#lx\n", hr);
    xml = str(L"<foreign><nested>payload</nested></foreign>");
    if (source_io)
    {
        hr = IXmlDocumentIO_LoadXml(source_io, xml);
        ok(hr == S_OK, "source LoadXml returned %#lx\n", hr);
    }
    WindowsDeleteString(xml);
    hr = IXmlDocument_get_DocumentElement(source, &source_root);
    ok(hr == S_OK && source_root, "source DocumentElement returned %#lx, element %p\n", hr, source_root);
    if (source_root)
        hr = IXmlElement_QueryInterface(source_root, &IID_IXmlNode, (void **)&source_node);
    ok(source_node != NULL, "source IXmlNode unavailable, hr %#lx\n", hr);

    imported = (void *)0xdeadbeef;
    hr = IXmlDocument_ImportNode(document, NULL, TRUE, &imported);
    ok(hr == E_INVALIDARG && !imported, "ImportNode(NULL) returned %#lx, node %p\n", hr, imported);
    if (source_node)
    {
        hr = IXmlDocument_ImportNode(document, source_node, TRUE, &imported);
        ok(hr == S_OK && imported, "ImportNode returned %#lx, node %p\n", hr, imported);
    }
    hr = IXmlDocument_get_DocumentElement(document, &target_root);
    ok(hr == S_OK && target_root, "target DocumentElement returned %#lx, element %p\n", hr, target_root);
    if (target_root)
        hr = IXmlElement_QueryInterface(target_root, &IID_IXmlNode, (void **)&target_node);
    ok(target_node != NULL, "target IXmlNode unavailable, hr %#lx\n", hr);
    if (target_node && imported)
    {
        hr = IXmlNode_AppendChild(target_node, imported, &appended);
        ok(hr == S_OK && appended == imported, "AppendChild(imported) returned %#lx, node %p\n",
                hr, appended);
    }
    output = document_xml(document);
    ok(output && wcsstr(WindowsGetStringRawBuffer(output, NULL),
            L"<foreign><nested>payload</nested></foreign>") != NULL,
            "deep imported subtree not serialized: %s\n", wine_dbgstr_hstring(output));

    WindowsDeleteString(output);
    if (appended) IXmlNode_Release(appended);
    if (imported) IXmlNode_Release(imported);
    if (target_node) IXmlNode_Release(target_node);
    if (target_root) IXmlElement_Release(target_root);
    if (source_node) IXmlNode_Release(source_node);
    if (source_root) IXmlElement_Release(source_root);
    if (source_io) IXmlDocumentIO_Release(source_io);
    IXmlDocument_Release(source);
}
static IInspectable *box_string(const WCHAR *value)
{
    IPropertyValueStatics *statics = NULL;
    IInspectable *boxed = NULL;
    HSTRING class_name = str(L"Windows.Foundation.PropertyValue"), string = str(value);
    HRESULT hr;

    hr = RoGetActivationFactory(class_name, &IID_IPropertyValueStatics, (void **)&statics);
    ok(hr == S_OK, "PropertyValue factory returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IPropertyValueStatics_CreateString(statics, string, &boxed);
        ok(hr == S_OK, "CreateString returned %#lx\n", hr);
        IPropertyValueStatics_Release(statics);
    }
    WindowsDeleteString(string);
    WindowsDeleteString(class_name);
    return boxed;
}

static void check_boxed_string(IInspectable *boxed, const WCHAR *expected)
{
    IPropertyValue *value = NULL;
    HSTRING string = NULL;
    HRESULT hr;

    hr = IInspectable_QueryInterface(boxed, &IID_IPropertyValue, (void **)&value);
    ok(hr == S_OK, "IPropertyValue QI returned %#lx\n", hr);
    if (SUCCEEDED(hr))
    {
        hr = IPropertyValue_GetString(value, &string);
        ok(hr == S_OK && !lstrcmpW(WindowsGetStringRawBuffer(string, NULL), expected),
                "GetString returned %#lx, value %s\n", hr, wine_dbgstr_hstring(string));
        WindowsDeleteString(string);
        IPropertyValue_Release(value);
    }
}

static void test_exact_document_operations(IXmlDocument *document)
{
    IXmlDocumentIO *io = NULL;
    IXmlDocument *owner = NULL, *source = NULL;
    IXmlDomImplementation *implementation = NULL, *implementation2 = NULL;
    IXmlDocumentFragment *fragment = NULL;
    IXmlComment *comment = NULL;
    IXmlProcessingInstruction *instruction = NULL;
    IXmlEntityReference *reference = NULL;
    IXmlCDataSection *cdata = NULL;
    IXmlCharacterData *character = NULL;
    IXmlAttribute *attribute = NULL, *previous = NULL;
    IXmlElement *root = NULL, *by_id = NULL, *namespaced = NULL, *missing = NULL, *item_by_id = NULL;
    IXmlElement *source_element = NULL;
    IXmlNodeList *namespace_nodes = NULL;
    IXmlNode *root_node = NULL, *fragment_node = NULL, *node = NULL, *appended = NULL;
    IXmlNode *namespaced_node = NULL, *parent = NULL, *source_node = NULL, *imported = NULL;
    IUnknown *root_identity = NULL, *found_identity = NULL, *parent_identity = NULL;
    IInspectable *namespace_value = NULL, *xml_namespace_value = NULL, *namespace_result = NULL;
    HSTRING value = NULL, output = NULL, before = NULL, after = NULL;
    UINT32 count;
    boolean supported, has_children;
    HRESULT hr;

    hr = IXmlDocument_QueryInterface(document, &IID_IXmlDocumentIO, (void **)&io);
    ok(hr == S_OK, "IXmlDocumentIO QI returned %#lx\n", hr);
    value = str(L"<root xml:id=\"root-id\"/>");
    hr = IXmlDocumentIO_LoadXml(io, value);
    ok(hr == S_OK, "xml:id document LoadXml returned %#lx\n", hr);
    WindowsDeleteString(value);

    hr = IXmlDocument_get_Implementation(document, &implementation);
    ok(hr == S_OK && implementation, "get_Implementation returned %#lx, %p\n", hr, implementation);
    hr = IXmlDocument_get_Implementation(document, &implementation2);
    ok(hr == S_OK && implementation2 == implementation, "implementation identity changed, hr %#lx\n", hr);
    value = str(L"XML");
    hr = IXmlDomImplementation_HasFeature(implementation, value, NULL, &supported);
    ok(hr == S_OK && supported, "HasFeature(XML) returned %#lx, %u\n", hr, supported);
    WindowsDeleteString(value);

    hr = IXmlDocument_get_DocumentElement(document, &root);
    ok(hr == S_OK && root, "DocumentElement returned %#lx, %p\n", hr, root);
    value = str(L"root-id");
    hr = IXmlDocument_GetElementById(document, value, &by_id);
    ok(hr == S_OK && by_id, "GetElementById returned %#lx, %p\n", hr, by_id);
    WindowsDeleteString(value);
    IXmlElement_QueryInterface(root, &IID_IUnknown, (void **)&root_identity);
    IXmlElement_QueryInterface(by_id, &IID_IUnknown, (void **)&found_identity);
    ok(root_identity == found_identity, "GetElementById did not preserve node identity\n");
    value = str(L"missing");
    hr = IXmlDocument_GetElementById(document, value, &missing);
    ok(hr == S_OK && !missing, "missing GetElementById returned %#lx, %p\n", hr, missing);
    WindowsDeleteString(value);

    hr = IXmlDocument_CreateDocumentFragment(document, &fragment);
    ok(hr == S_OK && fragment, "CreateDocumentFragment returned %#lx, %p\n", hr, fragment);
    IXmlDocumentFragment_QueryInterface(fragment, &IID_IXmlNode, (void **)&fragment_node);

    value = str(L"note");
    hr = IXmlDocument_CreateComment(document, value, &comment);
    ok(hr == S_OK && comment, "CreateComment returned %#lx, %p\n", hr, comment);
    WindowsDeleteString(value);
    IXmlComment_QueryInterface(comment, &IID_IXmlNode, (void **)&node);
    hr = IXmlNode_AppendChild(fragment_node, node, &appended);
    ok(hr == S_OK && appended == node, "fragment comment append returned %#lx\n", hr);
    IXmlNode_Release(appended);
    IXmlNode_Release(node);
    IXmlComment_Release(comment);
    node = appended = NULL;

    value = str(L"office");
    output = str(L"ready");
    hr = IXmlDocument_CreateProcessingInstruction(document, value, output, &instruction);
    ok(hr == S_OK && instruction, "CreateProcessingInstruction returned %#lx, %p\n", hr, instruction);
    WindowsDeleteString(value);
    WindowsDeleteString(output);
    value = NULL;
    hr = IXmlProcessingInstruction_get_Target(instruction, &value);
    ok(hr == S_OK && !lstrcmpW(WindowsGetStringRawBuffer(value, NULL), L"office"),
            "PI Target returned %#lx, %s\n", hr, wine_dbgstr_hstring(value));
    WindowsDeleteString(value);
    IXmlProcessingInstruction_QueryInterface(instruction, &IID_IXmlNode, (void **)&node);
    IXmlNode_AppendChild(fragment_node, node, &appended);
    IXmlNode_Release(appended);
    IXmlNode_Release(node);
    IXmlProcessingInstruction_Release(instruction);
    node = appended = NULL;

    value = str(L"<raw>&");
    hr = IXmlDocument_CreateCDataSection(document, value, &cdata);
    ok(hr == S_OK && cdata, "CreateCDataSection returned %#lx, %p\n", hr, cdata);
    WindowsDeleteString(value);
    hr = IXmlCDataSection_QueryInterface(cdata, &IID_IXmlCharacterData, (void **)&character);
    ok(hr == S_OK, "CDATA character-data QI returned %#lx\n", hr);
    IXmlCDataSection_QueryInterface(cdata, &IID_IXmlNode, (void **)&node);
    IXmlNode_AppendChild(fragment_node, node, &appended);
    IXmlNode_Release(appended);
    IXmlNode_Release(node);
    IXmlCharacterData_Release(character);
    IXmlCDataSection_Release(cdata);
    node = appended = NULL;

    value = str(L"safe");
    hr = IXmlDocument_CreateEntityReference(document, value, &reference);
    ok(hr == S_OK && reference, "CreateEntityReference returned %#lx, %p\n", hr, reference);
    WindowsDeleteString(value);
    IXmlEntityReference_QueryInterface(reference, &IID_IXmlNode, (void **)&node);
    IXmlNode_AppendChild(fragment_node, node, &appended);
    IXmlNode_Release(appended);
    IXmlNode_Release(node);
    IXmlEntityReference_Release(reference);
    node = appended = NULL;

    namespace_value = box_string(L"urn:office");
    value = str(L"o:item");
    hr = IXmlDocument_CreateElementNS(document, namespace_value, value, &namespaced);
    ok(hr == S_OK && namespaced, "CreateElementNS returned %#lx, %p\n", hr, namespaced);
    WindowsDeleteString(value);
    value = str(L"o:code");
    hr = IXmlDocument_CreateAttributeNS(document, namespace_value, value, &attribute);
    ok(hr == S_OK && attribute, "CreateAttributeNS returned %#lx, %p\n", hr, attribute);
    WindowsDeleteString(value);
    value = str(L"42");
    hr = IXmlAttribute_put_Value(attribute, value);
    ok(hr == S_OK, "namespaced attribute put_Value returned %#lx\n", hr);
    WindowsDeleteString(value);
    hr = IXmlElement_SetAttributeNodeNS(namespaced, attribute, &previous);
    ok(hr == S_OK && !previous, "SetAttributeNodeNS returned %#lx, previous %p\n", hr, previous);
    value = str(L"code");
    hr = IXmlElement_GetAttributeNS(namespaced, namespace_value, value, &output);
    ok(hr == S_OK && !lstrcmpW(WindowsGetStringRawBuffer(output, NULL), L"42"),
            "GetAttributeNS returned %#lx, %s\n", hr, wine_dbgstr_hstring(output));
    WindowsDeleteString(value);
    WindowsDeleteString(output);
    output = NULL;
    IXmlElement_QueryInterface(namespaced, &IID_IXmlNode, (void **)&namespaced_node);
    hr = IXmlNode_get_NamespaceUri(namespaced_node, &namespace_result);
    ok(hr == S_OK && namespace_result, "get_NamespaceUri returned %#lx, %p\n", hr, namespace_result);
    if (namespace_result) check_boxed_string(namespace_result, L"urn:office");
    if (namespace_result) IInspectable_Release(namespace_result);
    hr = IXmlNode_AppendChild(fragment_node, namespaced_node, &appended);
    ok(hr == S_OK, "fragment namespace element append returned %#lx\n", hr);
    IXmlNode_Release(appended);
    appended = NULL;

    IXmlElement_QueryInterface(root, &IID_IXmlNode, (void **)&root_node);
    hr = IXmlNode_AppendChild(root_node, fragment_node, &appended);
    ok(hr == S_OK && appended == fragment_node, "document-fragment append returned %#lx\n", hr);
    IXmlNode_Release(appended);
    appended = NULL;
    hr = IXmlNode_HasChildNodes(fragment_node, &has_children);
    ok(hr == S_OK && !has_children, "appended fragment retained children\n");
    xml_namespace_value = box_string(L"http://www.w3.org/XML/1998/namespace");
    value = str(L"xml:id");
    output = str(L"root-id");
    hr = IXmlElement_SetAttributeNS(namespaced, xml_namespace_value, value, output);
    ok(hr == E_INVALIDARG, "duplicate typed ID mutation returned %#lx\n", hr);
    WindowsDeleteString(output);
    output = str(L"item-id");
    hr = IXmlElement_SetAttributeNS(namespaced, xml_namespace_value, value, output);
    ok(hr == S_OK, "typed ID mutation returned %#lx\n", hr);
    WindowsDeleteString(value);
    WindowsDeleteString(output);
    value = str(L"item-id");
    hr = IXmlDocument_GetElementById(document, value, &item_by_id);
    ok(hr == S_OK && item_by_id == namespaced,
            "mutated GetElementById returned %#lx, %p, expected %p\n", hr, item_by_id, namespaced);
    WindowsDeleteString(value);
    value = output = NULL;
    value = str(L"o:item");
    hr = IXmlDocument_GetElementsByTagName(document, value, &namespace_nodes);
    ok(hr == S_OK, "qualified GetElementsByTagName returned %#lx\n", hr);
    WindowsDeleteString(value);
    hr = IXmlNodeList_get_Length(namespace_nodes, &count);
    ok(hr == S_OK && count == 1, "qualified namespace query returned %#lx, count %u\n", hr, count);
    hr = IXmlNode_get_ParentNode(namespaced_node, &parent);
    ok(hr == S_OK && parent, "namespaced ParentNode returned %#lx, %p\n", hr, parent);
    IXmlNode_QueryInterface(parent, &IID_IUnknown, (void **)&parent_identity);
    ok(parent_identity == root_identity, "ParentNode did not preserve root identity\n");
    hr = IXmlNode_get_OwnerDocument(namespaced_node, &owner);
    ok(hr == S_OK && owner == document, "OwnerDocument returned %#lx, %p, expected %p\n", hr, owner, document);

    output = document_xml(document);
    ok(wcsstr(WindowsGetStringRawBuffer(output, NULL), L"<!--note-->") != NULL,
            "comment missing from serialization: %s\n", wine_dbgstr_hstring(output));
    ok(wcsstr(WindowsGetStringRawBuffer(output, NULL), L"<?office ready?>") != NULL,
            "PI missing from serialization: %s\n", wine_dbgstr_hstring(output));
    ok(wcsstr(WindowsGetStringRawBuffer(output, NULL), L"<![CDATA[<raw>&]]>") != NULL,
            "CDATA missing from serialization: %s\n", wine_dbgstr_hstring(output));
    ok(wcsstr(WindowsGetStringRawBuffer(output, NULL), L"&safe;") != NULL,
            "entity reference missing from serialization: %s\n", wine_dbgstr_hstring(output));
    ok(wcsstr(WindowsGetStringRawBuffer(output, NULL), L"<o:item") != NULL &&
            wcsstr(WindowsGetStringRawBuffer(output, NULL), L"o:code=\"42\"") != NULL,
            "namespace element/attribute missing: %s\n", wine_dbgstr_hstring(output));
    WindowsDeleteString(output);
    output = NULL;

    source = new_document();
    value = str(L"o:imported");
    hr = IXmlDocument_CreateElementNS(source, namespace_value, value, &source_element);
    ok(hr == S_OK && source_element, "source CreateElementNS returned %#lx\n", hr);
    WindowsDeleteString(value);
    IXmlElement_QueryInterface(source_element, &IID_IXmlNode, (void **)&source_node);
    hr = IXmlDocument_ImportNode(document, source_node, TRUE, &imported);
    ok(hr == S_OK && imported, "namespaced ImportNode returned %#lx, %p\n", hr, imported);
    hr = IXmlNode_AppendChild(root_node, imported, &appended);
    ok(hr == S_OK, "imported namespace node append returned %#lx\n", hr);
    IXmlNode_Release(appended);
    appended = NULL;
    output = document_xml(document);
    ok(wcsstr(WindowsGetStringRawBuffer(output, NULL), L"<o:imported") != NULL,
            "imported namespace node missing: %s\n", wine_dbgstr_hstring(output));
    WindowsDeleteString(output);
    output = NULL;

    value = str(L"bad--comment");
    hr = IXmlDocument_CreateComment(document, value, &comment);
    ok(hr == E_INVALIDARG && !comment, "malformed comment returned %#lx, %p\n", hr, comment);
    WindowsDeleteString(value);
    value = str(L"bad]]>");
    hr = IXmlDocument_CreateCDataSection(document, value, &cdata);
    ok(hr == E_INVALIDARG && !cdata, "malformed CDATA returned %#lx, %p\n", hr, cdata);
    WindowsDeleteString(value);
    value = str(L"bad name");
    hr = IXmlDocument_CreateEntityReference(document, value, &reference);
    ok(hr == E_INVALIDARG && !reference, "malformed entity name returned %#lx, %p\n", hr, reference);
    WindowsDeleteString(value);
    value = str(L"xml");
    output = str(L"data");
    hr = IXmlDocument_CreateProcessingInstruction(document, value, output, &instruction);
    ok(hr == E_INVALIDARG && !instruction, "reserved PI target returned %#lx, %p\n", hr, instruction);
    WindowsDeleteString(value);
    WindowsDeleteString(output);
    value = str(L"p:invalid");
    hr = IXmlDocument_CreateAttributeNS(document, NULL, value, &previous);
    ok(hr == E_INVALIDARG && !previous, "attribute prefix without namespace returned %#lx, %p\n", hr, previous);
    WindowsDeleteString(value);
    value = str(L"p:invalid");
    hr = IXmlDocument_CreateElementNS(document, NULL, value, &missing);
    ok(hr == E_INVALIDARG && !missing, "prefix without namespace returned %#lx, %p\n", hr, missing);
    WindowsDeleteString(value);

    before = document_xml(document);
    value = str(L"<root xml:id=\"duplicate\"><child xml:id=\"duplicate\"/></root>");
    hr = IXmlDocumentIO_LoadXml(io, value);
    ok(FAILED(hr), "duplicate xml:id document was accepted\n");
    WindowsDeleteString(value);
    after = document_xml(document);
    ok(!lstrcmpW(WindowsGetStringRawBuffer(before, NULL), WindowsGetStringRawBuffer(after, NULL)),
            "duplicate-ID failure replaced the document\n");
    WindowsDeleteString(before);
    WindowsDeleteString(after);

    if (appended) IXmlNode_Release(appended);
    if (imported) IXmlNode_Release(imported);
    if (source_node) IXmlNode_Release(source_node);
    if (source_element) IXmlElement_Release(source_element);
    if (source) IXmlDocument_Release(source);
    if (owner) IXmlDocument_Release(owner);
    if (namespace_nodes) IXmlNodeList_Release(namespace_nodes);
    if (parent_identity) IUnknown_Release(parent_identity);
    if (parent) IXmlNode_Release(parent);
    if (namespaced_node) IXmlNode_Release(namespaced_node);
    if (root_node) IXmlNode_Release(root_node);
    if (attribute) IXmlAttribute_Release(attribute);
    if (namespaced) IXmlElement_Release(namespaced);
    if (namespace_value) IInspectable_Release(namespace_value);
    if (fragment_node) IXmlNode_Release(fragment_node);
    if (fragment) IXmlDocumentFragment_Release(fragment);
    if (found_identity) IUnknown_Release(found_identity);
    if (root_identity) IUnknown_Release(root_identity);
    if (by_id) IXmlElement_Release(by_id);
    if (root) IXmlElement_Release(root);
    if (implementation2) IXmlDomImplementation_Release(implementation2);
    if (implementation) IXmlDomImplementation_Release(implementation);
    if (item_by_id) IXmlElement_Release(item_by_id);
    if (xml_namespace_value) IInspectable_Release(xml_namespace_value);
    if (io) IXmlDocumentIO_Release(io);
}

static void test_node_survives_document(void)
{
    IXmlDocument *document = new_document(), *owner = NULL;
    IXmlDocumentIO *io = NULL;
    IXmlNodeList *list = NULL;
    IXmlNode *held = NULL;
    IXmlNodeSerializer *serializer = NULL;
    HSTRING value, output = NULL;
    HRESULT hr;

    if (!document) return;
    IXmlDocument_QueryInterface(document, &IID_IXmlDocumentIO, (void **)&io);
    value = str(L"<held><child/></held>");
    hr = IXmlDocumentIO_LoadXml(io, value);
    ok(hr == S_OK, "lifetime LoadXml returned %#lx\n", hr);
    WindowsDeleteString(value);
    IXmlDocumentIO_Release(io);
    value = str(L"child");
    hr = IXmlDocument_GetElementsByTagName(document, value, &list);
    ok(hr == S_OK, "lifetime GetElementsByTagName returned %#lx\n", hr);
    WindowsDeleteString(value);
    if (list) IXmlNodeList_Item(list, 0, &held);
    if (list) IXmlNodeList_Release(list);
    IXmlDocument_Release(document);

    hr = IXmlNode_QueryInterface(held, &IID_IXmlNodeSerializer, (void **)&serializer);
    ok(hr == S_OK, "held node serializer QI returned %#lx\n", hr);
    if (serializer)
    {
        hr = IXmlNodeSerializer_GetXml(serializer, &output);
        ok(hr == S_OK && wcsstr(WindowsGetStringRawBuffer(output, NULL), L"<child") != NULL,
                "held node serialization returned %#lx, %s\n", hr, wine_dbgstr_hstring(output));
        WindowsDeleteString(output);
        IXmlNodeSerializer_Release(serializer);
    }
    hr = IXmlNode_get_OwnerDocument(held, &owner);
    ok(hr == S_OK && owner, "held OwnerDocument returned %#lx, %p\n", hr, owner);
    if (owner) IXmlDocument_Release(owner);
    if (held) IXmlNode_Release(held);
}

START_TEST(dom)
{
    IXmlDocument *document;
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    ok(hr == S_OK || hr == S_FALSE, "RoInitialize returned %#lx\n", hr);

    document = new_document();
    if (document)
    {
        test_secure_load(document);
        test_nodes_and_lists(document);
        test_attribute_and_import(document);
        test_exact_document_operations(document);
        IXmlDocument_Release(document);
    }
    test_node_survives_document();

    RoUninitialize();
}
