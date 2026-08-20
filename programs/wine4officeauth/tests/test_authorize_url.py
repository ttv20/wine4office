#!/usr/bin/env python3

import pathlib
import unittest


class AuthorizeUrlSourceTest(unittest.TestCase):
    def test_browser_navigates_to_converted_authorize_url(self):
        source = (pathlib.Path(__file__).parents[1] / "wine4officeauth.cpp").read_text()
        build = source.index('authorize_url = "https://login.microsoftonline.com/"')
        convert = source.index("authorize_url_w = utf8_to_wide(authorize_url);", build)
        validate = source.index("if (authorize_url_w.empty()) return false;", convert)
        create_browser = source.index("CoCreateInstance(CLSID_WebBrowser", validate)
        allocate = source.index("url = SysAllocString(authorize_url_w.c_str());", validate)
        navigate = source.index("hr = browser->Navigate(url,", allocate)

        self.assertLess(build, convert)
        self.assertLess(convert, validate)
        self.assertLess(validate, create_browser)
        self.assertLess(create_browser, allocate)
        self.assertLess(validate, allocate)
        self.assertLess(allocate, navigate)


if __name__ == "__main__":
    unittest.main()
