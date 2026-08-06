#!/usr/bin/env python3

import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

MANAGER_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(MANAGER_DIR))

import wine4office_desktop as desktop
import wine4office_i18n as i18n


class DesktopIntegrationTests(unittest.TestCase):
    def setUp(self):
        desktop._plugin_bridge = None
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name) / "qt-plugins"
        (self.root / "platformthemes").mkdir(parents=True)
        (self.root / "styles").mkdir()
        (self.root / "platformthemes/KDEPlasmaPlatformTheme6.so").touch()
        (self.root / "styles/breeze6.so").touch()

    def tearDown(self):
        if desktop._plugin_bridge is not None:
            shutil.rmtree(desktop._plugin_bridge, ignore_errors=True)
            desktop._plugin_bridge = None
        self.temp.cleanup()

    def test_non_kde_desktops_never_expose_kde_plugins(self):
        for current in ("GNOME", "XFCE", "X-Cinnamon", "LXQt"):
            environment = {"XDG_CURRENT_DESKTOP": current}
            bridge = desktop.prepare_kde_plugin_bridge(
                environment, (self.root,), frozen=True
            )
            self.assertIsNone(bridge, current)
            self.assertNotIn("QT_QPA_PLATFORMTHEME", environment)

    def test_kde_frozen_build_exposes_only_theme_and_style_plugins(self):
        runtime = Path(self.temp.name) / "runtime"
        runtime.mkdir()
        environment = {
            "XDG_CURRENT_DESKTOP": "KDE",
            "XDG_RUNTIME_DIR": str(runtime),
        }
        bridge = desktop.prepare_kde_plugin_bridge(
            environment, (self.root,), frozen=True
        )
        self.assertIsNotNone(bridge)
        self.assertEqual(environment["QT_QPA_PLATFORMTHEME"], "kde")
        self.assertTrue((bridge / "platformthemes/KDEPlasmaPlatformTheme6.so").is_symlink())
        self.assertTrue((bridge / "styles/breeze6.so").is_symlink())
        self.assertFalse((bridge / "platforms").exists())

    def test_kde_without_host_plugins_keeps_bundled_fallback(self):
        empty = Path(self.temp.name) / "empty"
        empty.mkdir()
        environment = {"XDG_CURRENT_DESKTOP": "KDE"}
        self.assertIsNone(desktop.prepare_kde_plugin_bridge(
            environment, (empty,), frozen=True
        ))
        self.assertNotIn("QT_QPA_PLATFORMTHEME", environment)

    def test_explicit_qt_theme_override_is_respected(self):
        environment = {
            "XDG_CURRENT_DESKTOP": "KDE",
            "QT_QPA_PLATFORMTHEME": "qt6ct",
        }
        self.assertIsNone(desktop.prepare_kde_plugin_bridge(
            environment, (self.root,), frozen=True
        ))
        self.assertEqual(environment["QT_QPA_PLATFORMTHEME"], "qt6ct")


class TranslationTests(unittest.TestCase):
    def test_supported_system_locales_select_translation_and_direction(self):
        self.assertEqual(i18n.system_language({"LANG": "he_IL.UTF-8"}), "he")
        self.assertEqual(i18n.system_language({"LANG": "ar_EG.UTF-8"}), "ar")
        self.assertIn("he", i18n.RTL_LANGUAGES)
        self.assertIn("ar", i18n.RTL_LANGUAGES)

    def test_english_and_unknown_locales_use_ltr_english(self):
        for locale_name in ("en_US.UTF-8", "sw_KE.UTF-8", "C"):
            language = i18n.system_language({"LANG": locale_name})
            self.assertEqual(language, "en")
            self.assertNotIn(language, i18n.RTL_LANGUAGES)
            self.assertEqual(i18n.translate("Environment", language), "Environment")

    def test_language_override_controls_text_independently_of_desktop(self):
        environment = {
            "LANG": "en_US.UTF-8",
            "XDG_CURRENT_DESKTOP": "KDE",
            "WINE4OFFICE_LANGUAGE": "he",
        }
        language = i18n.system_language(environment)
        self.assertEqual(i18n.translate("Environment", language), "סביבה")

    def test_hebrew_and_arabic_have_core_navigation_catalogs(self):
        keys = ("Environment", "Install Office", "Applications", "Office settings",
                "Wine tools", "Maintenance")
        for language in ("he", "ar"):
            for key in keys:
                self.assertNotEqual(i18n.translate(key, language), key)

    def test_additional_language_catalogs_are_complete_and_ltr(self):
        expected = {
            "de", "es", "fr", "it", "pt", "nl", "ru", "uk", "pl", "tr",
            "zh", "zh_Hant", "ja", "ko",
        }
        self.assertTrue(expected.issubset(i18n.SUPPORTED_LANGUAGES))
        reference_keys = set(i18n.CATALOGS["de"])
        self.assertGreaterEqual(len(reference_keys), 60)
        for language in expected:
            self.assertEqual(set(i18n.CATALOGS[language]), reference_keys)
            self.assertTrue(all(i18n.CATALOGS[language].values()))
            self.assertNotIn(language, i18n.RTL_LANGUAGES)

    def test_locale_variants_select_expected_catalogs(self):
        cases = {
            "de_DE.UTF-8": "de",
            "es_MX.UTF-8": "es",
            "pt_BR.UTF-8": "pt",
            "ru_RU.UTF-8": "ru",
            "zh_CN.UTF-8": "zh",
            "zh_Hans_CN.UTF-8": "zh",
            "zh_TW.UTF-8": "zh_Hant",
            "zh-Hant-HK": "zh_Hant",
            "ja_JP.UTF-8": "ja",
            "ko_KR.UTF-8": "ko",
        }
        for locale_name, expected in cases.items():
            self.assertEqual(
                i18n.system_language({"LANG": locale_name}), expected, locale_name
            )

    def test_more_language_catalogs_are_complete_with_persian_rtl(self):
        expected = {
            "sv", "da", "nb", "fi", "cs", "sk", "hu", "ro", "el", "id",
            "vi", "fa", "hi", "th", "ca",
        }
        reference_keys = set(i18n.CATALOGS["sv"])
        self.assertGreaterEqual(len(reference_keys), 40)
        for language in expected:
            self.assertEqual(set(i18n.CATALOGS[language]), reference_keys)
            self.assertTrue(all(i18n.CATALOGS[language].values()))
        self.assertIn("fa", i18n.RTL_LANGUAGES)
        self.assertTrue((expected - {"fa"}).isdisjoint(i18n.RTL_LANGUAGES))

    def test_more_locale_variants_are_detected(self):
        cases = {
            "sv_SE.UTF-8": "sv",
            "nb_NO.UTF-8": "nb",
            "cs_CZ.UTF-8": "cs",
            "el_GR.UTF-8": "el",
            "fa_IR.UTF-8": "fa",
            "hi_IN.UTF-8": "hi",
            "th_TH.UTF-8": "th",
            "ca_ES.UTF-8": "ca",
        }
        for locale_name, expected in cases.items():
            self.assertEqual(i18n.system_language({"LANG": locale_name}), expected)

    def test_even_more_language_catalogs_are_complete_with_urdu_rtl(self):
        expected = {
            "bg", "hr", "sr_Latn", "sl", "lt", "lv", "et", "is", "ms",
            "fil", "bn", "ta", "te", "mr", "ur",
        }
        reference_keys = set(i18n.CATALOGS["bg"])
        self.assertGreaterEqual(len(reference_keys), 30)
        for language in expected:
            self.assertEqual(set(i18n.CATALOGS[language]), reference_keys)
            self.assertTrue(all(i18n.CATALOGS[language].values()))
        self.assertIn("ur", i18n.RTL_LANGUAGES)
        self.assertTrue((expected - {"ur"}).isdisjoint(i18n.RTL_LANGUAGES))

    def test_even_more_locale_variants_are_detected(self):
        cases = {
            "bg_BG.UTF-8": "bg",
            "hr_HR.UTF-8": "hr",
            "sr_Latn_RS.UTF-8": "sr_Latn",
            "sr_RS.UTF-8": "sr_Latn",
            "sh_BA.UTF-8": "sr_Latn",
            "sl_SI.UTF-8": "sl",
            "lt_LT.UTF-8": "lt",
            "lv_LV.UTF-8": "lv",
            "et_EE.UTF-8": "et",
            "is_IS.UTF-8": "is",
            "ms_MY.UTF-8": "ms",
            "fil_PH.UTF-8": "fil",
            "bn_BD.UTF-8": "bn",
            "ta_IN.UTF-8": "ta",
            "te_IN.UTF-8": "te",
            "mr_IN.UTF-8": "mr",
            "ur_PK.UTF-8": "ur",
        }
        for locale_name, expected in cases.items():
            self.assertEqual(i18n.system_language({"LANG": locale_name}), expected)


if __name__ == "__main__":
    unittest.main()
