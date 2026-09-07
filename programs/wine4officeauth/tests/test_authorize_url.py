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

    def test_legacy_consumer_client_id_accepts_the_aad_audience_alias(self):
        source = (pathlib.Path(__file__).parents[1] / "wine4officeauth.cpp").read_text()

        self.assertIn(
            'consumer_aad_client_id[] = "00000000-0000-0000-0000-0000480728C5"',
            source,
        )
        self.assertIn(
            "requested_client_id == consumer_client_id && audience == consumer_aad_client_id",
            source,
        )

    def test_consumer_url_scopes_use_the_aad_consumers_endpoint(self):
        source = (pathlib.Path(__file__).parents[1] / "wine4officeauth.cpp").read_text()
        refresh = source[source.index("static bool refresh_resource_and_save") :]

        live_route = refresh.index(
            "bool consumer_live = consumer && consumer_scope_supported(requested_scope);"
        )
        normalize = refresh.index(
            "std::string scope = consumer_live ? requested_scope : normalize_resource_scope(requested_scope);"
        )
        tenant = refresh.index('consumer && !consumer_live ? "consumers" : "organizations"')
        preserve_liveid = refresh.index("if (!consumer || consumer_live)")

        self.assertIn('if (oauth_consumer && tenant != "consumers")', source)
        self.assertIn("account.consumer_client_id = requested_client_id;", refresh)
        self.assertIn("account.consumer_scope = consumer_licensing_scope;", refresh)
        self.assertLess(live_route, normalize)
        self.assertLess(normalize, tenant)
        self.assertLess(tenant, preserve_liveid)

    def test_consumer_url_scope_interactive_fallback_bootstraps_live_id(self):
        source = (pathlib.Path(__file__).parents[1] / "wine4officeauth.cpp").read_text()
        main = source[source.index("int WINAPI wWinMain") :]

        classify = main.index(
            "consumer_resource_mode = oauth_consumer && !consumer_scope_supported(request_scope);"
        )
        bootstrap = main.index(
            "oauth_consumer_scope = consumer_resource_mode ? consumer_licensing_scope : request_scope;"
        )
        browser_scope = main.index("oauth_consumer ? oauth_consumer_scope : office_scope")
        resource_refresh = main.rindex(
            "success = refresh_resource_and_save(request_scope, request_client_id, login_hint);"
        )

        self.assertLess(classify, bootstrap)
        self.assertLess(bootstrap, browser_scope)
        self.assertLess(browser_scope, resource_refresh)

    def test_consumer_refresh_routing_is_scoped_to_the_active_account(self):
        source = (pathlib.Path(__file__).parents[1] / "wine4officeauth.cpp").read_text()
        refresh = source[source.index('if (argc >= 2 && !wcscmp(argv[1], L"--refresh"))') :]

        load_active = refresh.index("success = cache_record_load({}, active);")
        account_routing = refresh.index("!active.consumer_client_id.empty()")
        legacy_consumer_only = refresh.index("active.tid == consumer_tenant_id")
        legacy_projection = refresh.index("load_consumer_identity(cached_client, cached_scope);")

        self.assertIn('add_string("consumer_client_id", record.consumer_client_id);', source)
        self.assertIn('add_string("consumer_scope", record.consumer_scope);', source)
        self.assertLess(load_active, account_routing)
        self.assertLess(account_routing, legacy_consumer_only)
        self.assertLess(legacy_consumer_only, legacy_projection)


if __name__ == "__main__":
    unittest.main()
