#ifndef XML_XPATH_H_PRIVATE__
#define XML_XPATH_H_PRIVATE__

#include <libxml/xpath.h>

#ifdef IN_LIBXML
#define XML_XPATH_HIDDEN XML_HIDDEN
#else
#define XML_XPATH_HIDDEN
#endif

XML_XPATH_HIDDEN void
xmlInitXPathInternal(void);

XML_XPATH_HIDDEN xmlXPathObjectPtr
xmlXPathCacheObjectCopy(xmlXPathContextPtr ctxt, xmlXPathObjectPtr val);

XML_XPATH_HIDDEN void
xmlXPathReleaseObject(xmlXPathContextPtr ctxt, xmlXPathObjectPtr obj);

#undef XML_XPATH_HIDDEN

#endif /* XML_XPATH_H_PRIVATE__ */
