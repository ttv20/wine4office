#!/usr/bin/env python3
"""Additional dependency-free translations for the primary Manager interface."""

from __future__ import annotations


LANGUAGES = (
    "bg", "hr", "sr_Latn", "sl", "lt", "lv", "et", "is", "ms", "fil",
    "bn", "ta", "te", "mr", "ur",
)

# English source followed by translations in LANGUAGES order. Text outside this
# compact primary-interface catalog deliberately falls back to English.
ROWS = (
    ("Wine4Office Manager", "Мениджър на Wine4Office", "Wine4Office upravitelj", "Wine4Office upravljač", "Upravljalnik Wine4Office", "Wine4Office tvarkytuvė", "Wine4Office pārvaldnieks", "Wine4Office haldur", "Wine4Office-stjóri", "Pengurus Wine4Office", "Tagapamahala ng Wine4Office", "Wine4Office ব্যবস্থাপক", "Wine4Office மேலாளர்", "Wine4Office నిర్వాహకం", "Wine4Office व्यवस्थापक", "Wine4Office منتظم"),
    ("Checking…", "Проверка…", "Provjera…", "Provera…", "Preverjanje…", "Tikrinama…", "Pārbauda…", "Kontrollimine…", "Athuga…", "Memeriksa…", "Sinusuri…", "পরীক্ষা করা হচ্ছে…", "சரிபார்க்கிறது…", "తనిఖీ చేస్తోంది…", "तपासत आहे…", "جانچ جاری ہے…"),
    ("Ready", "Готово", "Spremno", "Spremno", "Pripravljeno", "Paruošta", "Gatavs", "Valmis", "Tilbúið", "Sedia", "Handa na", "প্রস্তুত", "தயார்", "సిద్ధం", "तयार", "تیار"),
    ("Environment", "Среда", "Okruženje", "Okruženje", "Okolje", "Aplinka", "Vide", "Keskkond", "Umhverfi", "Persekitaran", "Kapaligiran", "পরিবেশ", "சூழல்", "పర్యావరణం", "वातावरण", "ماحول"),
    ("Install Office", "Инсталиране на Office", "Instaliraj Office", "Instaliraj Office", "Namesti Office", "Įdiegti Office", "Instalēt Office", "Paigalda Office", "Setja upp Office", "Pasang Office", "I-install ang Office", "Office ইনস্টল করুন", "Office-ஐ நிறுவு", "Officeను ఇన్‌స్టాల్ చేయండి", "Office स्थापित करा", "Office انسٹال کریں"),
    ("Applications", "Приложения", "Aplikacije", "Aplikacije", "Programi", "Programos", "Lietotnes", "Rakendused", "Forrit", "Aplikasi", "Mga application", "অ্যাপ্লিকেশন", "பயன்பாடுகள்", "అనువర్తనాలు", "अनुप्रयोग", "ایپلیکیشنز"),
    ("Office settings", "Настройки на Office", "Postavke Officea", "Office postavke", "Nastavitve Officea", "Office nuostatos", "Office iestatījumi", "Office'i sätted", "Office-stillingar", "Tetapan Office", "Mga setting ng Office", "Office সেটিংস", "Office அமைப்புகள்", "Office సెట్టింగ్‌లు", "Office सेटिंग्ज", "Office ترتیبات"),
    ("Wine tools", "Инструменти на Wine", "Wine alati", "Wine alati", "Orodja Wine", "Wine įrankiai", "Wine rīki", "Wine'i tööriistad", "Wine-verkfæri", "Alat Wine", "Mga tool ng Wine", "Wine সরঞ্জাম", "Wine கருவிகள்", "Wine సాధనాలు", "Wine साधने", "Wine ٹولز"),
    ("Maintenance", "Поддръжка", "Održavanje", "Održavanje", "Vzdrževanje", "Priežiūra", "Uzturēšana", "Hooldus", "Viðhald", "Penyelenggaraan", "Pagpapanatili", "রক্ষণাবেক্ষণ", "பராமரிப்பு", "నిర్వహణ", "देखभाल", "دیکھ بھال"),
    ("Browse…", "Преглед…", "Pregledaj…", "Pregledaj…", "Prebrskaj…", "Naršyti…", "Pārlūkot…", "Sirvi…", "Fletta…", "Semak imbas…", "Mag-browse…", "ব্রাউজ…", "உலாவு…", "బ్రౌజ్ చేయండి…", "ब्राउझ करा…", "براؤز کریں…"),
    ("Choose a directory", "Изберете папка", "Odaberite mapu", "Izaberite fasciklu", "Izberite mapo", "Pasirinkite aplanką", "Izvēlieties mapi", "Vali kaust", "Veldu möppu", "Pilih folder", "Pumili ng folder", "একটি ফোল্ডার বেছে নিন", "கோப்புறையைத் தேர்ந்தெடு", "ఫోల్డర్‌ను ఎంచుకోండి", "फोल्डर निवडा", "فولڈر منتخب کریں"),
    ("Choose a file", "Изберете файл", "Odaberite datoteku", "Izaberite datoteku", "Izberite datoteko", "Pasirinkite failą", "Izvēlieties failu", "Vali fail", "Veldu skrá", "Pilih fail", "Pumili ng file", "একটি ফাইল বেছে নিন", "கோப்பைத் தேர்ந்தெடு", "ఫైల్‌ను ఎంచుకోండి", "फाइल निवडा", "فائل منتخب کریں"),
    ("Wine environment", "Wine среда", "Wine okruženje", "Wine okruženje", "Okolje Wine", "Wine aplinka", "Wine vide", "Wine'i keskkond", "Wine-umhverfi", "Persekitaran Wine", "Kapaligiran ng Wine", "Wine পরিবেশ", "Wine சூழல்", "Wine పర్యావరణం", "Wine वातावरण", "Wine ماحول"),
    ("Environment paths", "Пътища на средата", "Putanje okruženja", "Putanje okruženja", "Poti okolja", "Aplinkos keliai", "Vides ceļi", "Keskkonna asukohad", "Slóðir umhverfis", "Laluan persekitaran", "Mga path ng kapaligiran", "পরিবেশের পাথ", "சூழல் பாதைகள்", "పర్యావరణ మార్గాలు", "वातावरण पथ", "ماحول کے راستے"),
    ("Environment:", "Среда:", "Okruženje:", "Okruženje:", "Okolje:", "Aplinka:", "Vide:", "Keskkond:", "Umhverfi:", "Persekitaran:", "Kapaligiran:", "পরিবেশ:", "சூழல்:", "పర్యావరణం:", "वातावरण:", "ماحول:"),
    ("Wine executable:", "Изпълним файл на Wine:", "Izvršna datoteka Winea:", "Wine izvršna datoteka:", "Izvršna datoteka Wine:", "Wine vykdomasis failas:", "Wine izpildfails:", "Wine'i käivitusfail:", "Wine-keyrsluskrá:", "Fail boleh laku Wine:", "Wine executable:", "Wine এক্সিকিউটেবল:", "Wine இயக்கக்கோப்பு:", "Wine ఎక్జిక్యూటబుల్:", "Wine एक्झिक्युटेबल:", "Wine قابل عمل فائل:"),
    ("Create", "Създаване", "Stvori", "Napravi", "Ustvari", "Sukurti", "Izveidot", "Loo", "Búa til", "Cipta", "Gumawa", "তৈরি করুন", "உருவாக்கு", "సృష్టించండి", "तयार करा", "بنائیں"),
    ("Recreate…", "Повторно създаване…", "Ponovno stvori…", "Napravi ponovo…", "Znova ustvari…", "Sukurti iš naujo…", "Izveidot no jauna…", "Loo uuesti…", "Endurskapa…", "Cipta semula…", "Gawin muli…", "আবার তৈরি করুন…", "மீண்டும் உருவாக்கு…", "మళ్లీ సృష్టించండి…", "पुन्हा तयार करा…", "دوبارہ بنائیں…"),
    ("Stop Wine", "Спиране на Wine", "Zaustavi Wine", "Zaustavi Wine", "Ustavi Wine", "Stabdyti Wine", "Apturēt Wine", "Peata Wine", "Stöðva Wine", "Hentikan Wine", "Ihinto ang Wine", "Wine বন্ধ করুন", "Wine-ஐ நிறுத்து", "Wineను ఆపండి", "Wine थांबवा", "Wine روکیں"),
    ("Save paths", "Запазване на пътищата", "Spremi putanje", "Sačuvaj putanje", "Shrani poti", "Išsaugoti kelius", "Saglabāt ceļus", "Salvesta asukohad", "Vista slóðir", "Simpan laluan", "I-save ang mga path", "পাথ সংরক্ষণ করুন", "பாதைகளைச் சேமி", "మార్గాలను సేవ్ చేయండి", "पथ जतन करा", "راستے محفوظ کریں"),
    ("Background services", "Фонови услуги", "Pozadinske usluge", "Pozadinske usluge", "Storitve v ozadju", "Foninės paslaugos", "Fona pakalpojumi", "Taustateenused", "Bakgrunnsþjónustur", "Perkhidmatan latar belakang", "Mga serbisyo sa background", "পটভূমির সেবা", "பின்னணி சேவைகள்", "నేపథ్య సేవలు", "पार्श्वभूमी सेवा", "پس منظر کی خدمات"),
    ("Selected environment:", "Избрана среда:", "Odabrano okruženje:", "Izabrano okruženje:", "Izbrano okolje:", "Pasirinkta aplinka:", "Atlasītā vide:", "Valitud keskkond:", "Valið umhverfi:", "Persekitaran dipilih:", "Napiling kapaligiran:", "নির্বাচিত পরিবেশ:", "தேர்ந்தெடுத்த சூழல்:", "ఎంచుకున్న పర్యావరణం:", "निवडलेले वातावरण:", "منتخب ماحول:"),
    ("Run a Windows executable", "Стартиране на Windows програма", "Pokreni Windows program", "Pokreni Windows program", "Zaženi program Windows", "Paleisti Windows programą", "Palaist Windows programmu", "Käivita Windowsi programm", "Keyra Windows-forrit", "Jalankan program Windows", "Magpatakbo ng Windows program", "Windows প্রোগ্রাম চালান", "Windows நிரலை இயக்கு", "Windows ప్రోగ్రామ్‌ను అమలు చేయండి", "Windows प्रोग्राम चालवा", "Windows پروگرام چلائیں"),
    ("Executable:", "Изпълним файл:", "Izvršna datoteka:", "Izvršna datoteka:", "Izvršna datoteka:", "Vykdomasis failas:", "Izpildfails:", "Käivitusfail:", "Keyrsluskrá:", "Fail boleh laku:", "Executable:", "এক্সিকিউটেবল:", "இயக்கக்கோப்பு:", "ఎక్జిక్యూటబుల్:", "एक्झिक्युटेबल:", "قابل عمل فائل:"),
    ("Arguments:", "Аргументи:", "Argumenti:", "Argumenti:", "Argumenti:", "Argumentai:", "Argumenti:", "Argumendid:", "Viðföng:", "Argumen:", "Mga argumento:", "আর্গুমেন্ট:", "அளவுருக்கள்:", "ఆర్గ్యుమెంట్లు:", "युक्तिवाद:", "آرگیومنٹس:"),
    ("Run", "Стартиране", "Pokreni", "Pokreni", "Zaženi", "Paleisti", "Palaist", "Käivita", "Keyra", "Jalankan", "Patakbuhin", "চালান", "இயக்கு", "అమలు చేయండి", "चालवा", "چلائیں"),
    ("Updates", "Актуализации", "Ažuriranja", "Ažuriranja", "Posodobitve", "Naujinimai", "Atjauninājumi", "Uuendused", "Uppfærslur", "Kemas kini", "Mga update", "আপডেট", "புதுப்பிப்புகள்", "నవీకరణలు", "अद्यतने", "اپ ڈیٹس"),
    ("Check for updates…", "Проверка за актуализации…", "Provjeri ažuriranja…", "Proveri ažuriranja…", "Preveri posodobitve…", "Tikrinti naujinimus…", "Pārbaudīt atjauninājumus…", "Kontrolli uuendusi…", "Leita að uppfærslum…", "Semak kemas kini…", "Tingnan ang mga update…", "আপডেট পরীক্ষা করুন…", "புதுப்பிப்புகளைச் சரிபார்…", "నవీకరణల కోసం తనిఖీ చేయండి…", "अद्यतने तपासा…", "اپ ڈیٹس چیک کریں…"),
    ("Operation log", "Дневник на операциите", "Dnevnik radnji", "Dnevnik operacija", "Dnevnik opravil", "Veiksmų žurnalas", "Darbību žurnāls", "Toimingulogi", "Aðgerðaskrá", "Log operasi", "Talaan ng operasyon", "অপারেশন লগ", "செயல்பாட்டுப் பதிவு", "కార్యాచరణ లాగ్", "कार्य नोंद", "عملی لاگ"),
    ("No operation running.", "Няма текуща операция.", "Nema aktivne radnje.", "Nema aktivne operacije.", "Nobeno opravilo se ne izvaja.", "Nevykdoma jokia operacija.", "Neviena darbība nenotiek.", "Ükski toiming ei tööta.", "Engin aðgerð í gangi.", "Tiada operasi sedang berjalan.", "Walang tumatakbong operasyon.", "কোনো অপারেশন চলছে না।", "எந்தச் செயல்பாடும் இயங்கவில்லை.", "ఏ కార్యాచరణ అమలులో లేదు.", "कोणतेही कार्य चालू नाही.", "کوئی عمل جاری نہیں۔"),
    ("Installed", "Инсталирано", "Instalirano", "Instalirano", "Nameščeno", "Įdiegta", "Instalēts", "Paigaldatud", "Uppsett", "Dipasang", "Naka-install", "ইনস্টল করা হয়েছে", "நிறுவப்பட்டது", "ఇన్‌స్టాల్ చేయబడింది", "स्थापित", "انسٹال شدہ"),
    ("Environment ready", "Средата е готова", "Okruženje je spremno", "Okruženje je spremno", "Okolje je pripravljeno", "Aplinka paruošta", "Vide ir gatava", "Keskkond on valmis", "Umhverfið er tilbúið", "Persekitaran sedia", "Handa na ang kapaligiran", "পরিবেশ প্রস্তুত", "சூழல் தயாராக உள்ளது", "పర్యావరణం సిద్ధంగా ఉంది", "वातावरण तयार आहे", "ماحول تیار ہے"),
    ("Wine runner missing", "Липсва Wine", "Nedostaje Wine", "Nedostaje Wine", "Wine manjka", "Trūksta Wine", "Trūkst Wine", "Wine puudub", "Wine vantar", "Wine tiada", "Nawawala ang Wine", "Wine অনুপস্থিত", "Wine இல்லை", "Wine లేదు", "Wine उपलब्ध नाही", "Wine موجود نہیں"),
    ("Status unavailable", "Състоянието не е налично", "Status nije dostupan", "Status nije dostupan", "Stanje ni na voljo", "Būsena nepasiekiama", "Statuss nav pieejams", "Olek pole saadaval", "Staða ekki tiltæk", "Status tidak tersedia", "Hindi available ang status", "অবস্থা অনুপলব্ধ", "நிலை கிடைக்கவில்லை", "స్థితి అందుబాటులో లేదు", "स्थिती उपलब्ध नाही", "حالت دستیاب نہیں"),
    ("Download and install", "Изтегляне и инсталиране", "Preuzmi i instaliraj", "Preuzmi i instaliraj", "Prenesi in namesti", "Atsisiųsti ir įdiegti", "Lejupielādēt un instalēt", "Laadi alla ja paigalda", "Sækja og setja upp", "Muat turun dan pasang", "I-download at i-install", "ডাউনলোড ও ইনস্টল করুন", "பதிவிறக்கி நிறுவு", "డౌన్‌లోడ్ చేసి ఇన్‌స్టాల్ చేయండి", "डाउनलोड आणि स्थापित करा", "ڈاؤن لوڈ اور انسٹال کریں"),
    ("Later", "По-късно", "Kasnije", "Kasnije", "Pozneje", "Vėliau", "Vēlāk", "Hiljem", "Síðar", "Kemudian", "Mamaya", "পরে", "பின்னர்", "తర్వాత", "नंतर", "بعد میں"),
    ("Close", "Затваряне", "Zatvori", "Zatvori", "Zapri", "Uždaryti", "Aizvērt", "Sulge", "Loka", "Tutup", "Isara", "বন্ধ করুন", "மூடு", "మూసివేయండి", "बंद करा", "بند کریں"),
)


def catalogs() -> dict[str, dict[str, str]]:
    result = {language: {} for language in LANGUAGES}
    for row in ROWS:
        source, *translations = row
        if len(translations) != len(LANGUAGES):
            raise ValueError(f"Translation row has the wrong length: {source}")
        for language, translated in zip(LANGUAGES, translations, strict=True):
            result[language][source] = translated
    return result
