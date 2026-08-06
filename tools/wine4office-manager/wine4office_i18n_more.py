#!/usr/bin/env python3
"""Broader locale coverage for the primary Manager interface."""

from __future__ import annotations


LANGUAGES = (
    "sv", "da", "nb", "fi", "cs", "sk", "hu", "ro", "el", "id", "vi",
    "fa", "hi", "th", "ca",
)

# English source followed by translations in LANGUAGES order. Less common and
# diagnostic text intentionally falls back to English rather than disappearing.
ROWS = (
    ("Wine4Office Manager", "Wine4Office-hanterare", "Wine4Office-administration", "Wine4Office-behandling", "Wine4Office-hallinta", "Správce Wine4Office", "Správca Wine4Office", "Wine4Office-kezelő", "Manager Wine4Office", "Διαχείριση Wine4Office", "Pengelola Wine4Office", "Trình quản lý Wine4Office", "مدیر Wine4Office", "Wine4Office प्रबंधक", "ตัวจัดการ Wine4Office", "Gestor del Wine4Office"),
    ("Checking…", "Kontrollerar…", "Kontrollerer…", "Kontrollerer…", "Tarkistetaan…", "Kontroluje se…", "Kontroluje sa…", "Ellenőrzés…", "Se verifică…", "Έλεγχος…", "Memeriksa…", "Đang kiểm tra…", "در حال بررسی…", "जाँच हो रही है…", "กำลังตรวจสอบ…", "S'està comprovant…"),
    ("Ready", "Klar", "Klar", "Klar", "Valmis", "Připraveno", "Pripravené", "Kész", "Gata", "Έτοιμο", "Siap", "Sẵn sàng", "آماده", "तैयार", "พร้อม", "A punt"),
    ("Environment", "Miljö", "Miljø", "Miljø", "Ympäristö", "Prostředí", "Prostredie", "Környezet", "Mediu", "Περιβάλλον", "Lingkungan", "Môi trường", "محیط", "परिवेश", "สภาพแวดล้อม", "Entorn"),
    ("Install Office & Teams", "Installera Office & Teams", "Installer Office & Teams", "Installer Office & Teams", "Asenna Office & Teams", "Nainstalovat Office & Teams", "Nainštalovať Office & Teams", "Office & Teams telepítése", "Instalează Office & Teams", "Εγκατάσταση Office & Teams", "Instal Office & Teams", "Cài đặt Office & Teams", "نصب Office و Teams", "Office और Teams इंस्टॉल करें", "ติดตั้ง Office และ Teams", "Instal·la l'Office i Teams"),
    ("Applications", "Program", "Programmer", "Programmer", "Sovellukset", "Aplikace", "Aplikácie", "Alkalmazások", "Aplicații", "Εφαρμογές", "Aplikasi", "Ứng dụng", "برنامه‌ها", "अनुप्रयोग", "แอปพลิเคชัน", "Aplicacions"),
    ("Office settings", "Office-inställningar", "Office-indstillinger", "Office-innstillinger", "Office-asetukset", "Nastavení Office", "Nastavenia Office", "Office-beállítások", "Setări Office", "Ρυθμίσεις Office", "Pengaturan Office", "Cài đặt Office", "تنظیمات Office", "Office सेटिंग्स", "การตั้งค่า Office", "Configuració de l'Office"),
    ("Wine tools", "Wine-verktyg", "Wine-værktøjer", "Wine-verktøy", "Wine-työkalut", "Nástroje Wine", "Nástroje Wine", "Wine-eszközök", "Instrumente Wine", "Εργαλεία Wine", "Alat Wine", "Công cụ Wine", "ابزارهای Wine", "Wine उपकरण", "เครื่องมือ Wine", "Eines del Wine"),
    ("Maintenance", "Underhåll", "Vedligeholdelse", "Vedlikehold", "Ylläpito", "Údržba", "Údržba", "Karbantartás", "Întreținere", "Συντήρηση", "Pemeliharaan", "Bảo trì", "نگهداری", "रखरखाव", "การบำรุงรักษา", "Manteniment"),
    ("Browse…", "Bläddra…", "Gennemse…", "Bla gjennom…", "Selaa…", "Procházet…", "Prehľadávať…", "Tallózás…", "Răsfoiește…", "Περιήγηση…", "Telusuri…", "Duyệt…", "مرور…", "ब्राउज़ करें…", "เรียกดู…", "Navega…"),
    ("Choose a directory", "Välj en mapp", "Vælg en mappe", "Velg en mappe", "Valitse kansio", "Vybrat složku", "Vybrať priečinok", "Mappa kiválasztása", "Alege un dosar", "Επιλογή φακέλου", "Pilih folder", "Chọn thư mục", "انتخاب پوشه", "फ़ोल्डर चुनें", "เลือกโฟลเดอร์", "Tria una carpeta"),
    ("Choose a file", "Välj en fil", "Vælg en fil", "Velg en fil", "Valitse tiedosto", "Vybrat soubor", "Vybrať súbor", "Fájl kiválasztása", "Alege un fișier", "Επιλογή αρχείου", "Pilih berkas", "Chọn tệp", "انتخاب پرونده", "फ़ाइल चुनें", "เลือกไฟล์", "Tria un fitxer"),
    ("Wine environment", "Wine-miljö", "Wine-miljø", "Wine-miljø", "Wine-ympäristö", "Prostředí Wine", "Prostredie Wine", "Wine-környezet", "Mediu Wine", "Περιβάλλον Wine", "Lingkungan Wine", "Môi trường Wine", "محیط Wine", "Wine परिवेश", "สภาพแวดล้อม Wine", "Entorn del Wine"),
    ("Environment paths", "Miljösökvägar", "Miljøstier", "Miljøstier", "Ympäristön polut", "Cesty prostředí", "Cesty prostredia", "Környezeti útvonalak", "Căile mediului", "Διαδρομές περιβάλλοντος", "Jalur lingkungan", "Đường dẫn môi trường", "مسیرهای محیط", "परिवेश पथ", "พาธสภาพแวดล้อม", "Camins de l'entorn"),
    ("Environment:", "Miljö:", "Miljø:", "Miljø:", "Ympäristö:", "Prostředí:", "Prostredie:", "Környezet:", "Mediu:", "Περιβάλλον:", "Lingkungan:", "Môi trường:", "محیط:", "परिवेश:", "สภาพแวดล้อม:", "Entorn:"),
    ("Wine executable:", "Wine-program:", "Wine-program:", "Wine-program:", "Wine-ohjelma:", "Program Wine:", "Program Wine:", "Wine program:", "Executabil Wine:", "Εκτελέσιμο Wine:", "Program Wine:", "Tệp thực thi Wine:", "اجرای Wine:", "Wine निष्पादन योग्य:", "ไฟล์เรียกทำงาน Wine:", "Executable del Wine:"),
    ("Create", "Skapa", "Opret", "Opprett", "Luo", "Vytvořit", "Vytvoriť", "Létrehozás", "Creează", "Δημιουργία", "Buat", "Tạo", "ایجاد", "बनाएँ", "สร้าง", "Crea"),
    ("Recreate…", "Skapa om…", "Opret igen…", "Opprett på nytt…", "Luo uudelleen…", "Vytvořit znovu…", "Vytvoriť znova…", "Újralétrehozás…", "Recreează…", "Επαναδημιουργία…", "Buat ulang…", "Tạo lại…", "ایجاد دوباره…", "फिर से बनाएँ…", "สร้างใหม่…", "Torna a crear…"),
    ("Stop Wine", "Stoppa Wine", "Stop Wine", "Stopp Wine", "Pysäytä Wine", "Zastavit Wine", "Zastaviť Wine", "Wine leállítása", "Oprește Wine", "Διακοπή Wine", "Hentikan Wine", "Dừng Wine", "توقف Wine", "Wine रोकें", "หยุด Wine", "Atura el Wine"),
    ("Save paths", "Spara sökvägar", "Gem stier", "Lagre stier", "Tallenna polut", "Uložit cesty", "Uložiť cesty", "Útvonalak mentése", "Salvează căile", "Αποθήκευση διαδρομών", "Simpan jalur", "Lưu đường dẫn", "ذخیره مسیرها", "पथ सहेजें", "บันทึกพาธ", "Desa els camins"),
    ("Background services", "Bakgrundstjänster", "Baggrundstjenester", "Bakgrunnstjenester", "Taustapalvelut", "Služby na pozadí", "Služby na pozadí", "Háttérszolgáltatások", "Servicii de fundal", "Υπηρεσίες παρασκηνίου", "Layanan latar belakang", "Dịch vụ nền", "خدمات پس‌زمینه", "पृष्ठभूमि सेवाएँ", "บริการเบื้องหลัง", "Serveis en segon pla"),
    ("Selected environment:", "Vald miljö:", "Valgt miljø:", "Valgt miljø:", "Valittu ympäristö:", "Vybrané prostředí:", "Vybrané prostredie:", "Kiválasztott környezet:", "Mediu selectat:", "Επιλεγμένο περιβάλλον:", "Lingkungan terpilih:", "Môi trường đã chọn:", "محیط انتخاب‌شده:", "चयनित परिवेश:", "สภาพแวดล้อมที่เลือก:", "Entorn seleccionat:"),
    ("Bound environment:", "Bunden miljö:", "Tilknyttet miljø:", "Tilknyttet miljø:", "Sidottu ympäristö:", "Navázané prostředí:", "Prepojené prostredie:", "Kapcsolt környezet:", "Mediu asociat:", "Συνδεδεμένο περιβάλλον:", "Lingkungan terikat:", "Môi trường đã liên kết:", "محیط پیوندشده:", "बंधित परिवेश:", "สภาพแวดล้อมที่ผูกไว้:", "Entorn vinculat:"),
    ("Run a Windows executable", "Kör ett Windows-program", "Kør et Windows-program", "Kjør et Windows-program", "Suorita Windows-ohjelma", "Spustit program Windows", "Spustiť program Windows", "Windows-program futtatása", "Rulează un program Windows", "Εκτέλεση προγράμματος Windows", "Jalankan program Windows", "Chạy chương trình Windows", "اجرای برنامه Windows", "Windows प्रोग्राम चलाएँ", "เรียกใช้โปรแกรม Windows", "Executa un programa del Windows"),
    ("Executable:", "Program:", "Program:", "Program:", "Ohjelma:", "Program:", "Program:", "Program:", "Executabil:", "Εκτελέσιμο:", "Program:", "Tệp thực thi:", "برنامه:", "निष्पादन योग्य:", "ไฟล์เรียกทำงาน:", "Executable:"),
    ("Arguments:", "Argument:", "Argumenter:", "Argumenter:", "Argumentit:", "Argumenty:", "Argumenty:", "Argumentumok:", "Argumente:", "Ορίσματα:", "Argumen:", "Đối số:", "آرگومان‌ها:", "तर्क:", "อาร์กิวเมนต์:", "Arguments:"),
    ("Working folder:", "Arbetsmapp:", "Arbejdsmappe:", "Arbeidsmappe:", "Työkansio:", "Pracovní složka:", "Pracovný priečinok:", "Munkamappa:", "Dosar de lucru:", "Φάκελος εργασίας:", "Folder kerja:", "Thư mục làm việc:", "پوشه کاری:", "कार्य फ़ोल्डर:", "โฟลเดอร์ทำงาน:", "Carpeta de treball:"),
    ("Choose and run…", "Välj och kör…", "Vælg og kør…", "Velg og kjør…", "Valitse ja suorita…", "Vybrat a spustit…", "Vybrať a spustiť…", "Kiválasztás és futtatás…", "Alege și rulează…", "Επιλογή και εκτέλεση…", "Pilih dan jalankan…", "Chọn và chạy…", "انتخاب و اجرا…", "चुनें और चलाएँ…", "เลือกและเรียกใช้…", "Tria i executa…"),
    ("Run", "Kör", "Kør", "Kjør", "Suorita", "Spustit", "Spustiť", "Futtatás", "Rulează", "Εκτέλεση", "Jalankan", "Chạy", "اجرا", "चलाएँ", "เรียกใช้", "Executa"),
    ("Updates", "Uppdateringar", "Opdateringer", "Oppdateringer", "Päivitykset", "Aktualizace", "Aktualizácie", "Frissítések", "Actualizări", "Ενημερώσεις", "Pembaruan", "Bản cập nhật", "به‌روزرسانی‌ها", "अपडेट", "การอัปเดต", "Actualitzacions"),
    ("Check for updates…", "Sök efter uppdateringar…", "Søg efter opdateringer…", "Se etter oppdateringer…", "Tarkista päivitykset…", "Zkontrolovat aktualizace…", "Skontrolovať aktualizácie…", "Frissítések keresése…", "Caută actualizări…", "Έλεγχος για ενημερώσεις…", "Periksa pembaruan…", "Kiểm tra bản cập nhật…", "بررسی به‌روزرسانی‌ها…", "अपडेट जाँचें…", "ตรวจหาการอัปเดต…", "Comprova si hi ha actualitzacions…"),
    ("Operation log", "Åtgärdslogg", "Handlingslog", "Operasjonslogg", "Toimintoloki", "Protokol operací", "Denník operácií", "Műveleti napló", "Jurnal operații", "Αρχείο λειτουργιών", "Log operasi", "Nhật ký thao tác", "گزارش عملیات", "संचालन लॉग", "บันทึกการทำงาน", "Registre d'operacions"),
    ("No operation running.", "Ingen åtgärd körs.", "Ingen handling kører.", "Ingen operasjon kjører.", "Ei käynnissä olevia toimintoja.", "Neprobíhá žádná operace.", "Nepracuje žiadna operácia.", "Nincs futó művelet.", "Nu rulează nicio operație.", "Δεν εκτελείται λειτουργία.", "Tidak ada operasi berjalan.", "Không có thao tác nào đang chạy.", "هیچ عملیاتی در حال اجرا نیست.", "कोई संचालन नहीं चल रहा है।", "ไม่มีการทำงานที่กำลังดำเนินอยู่", "No hi ha cap operació en curs."),
    ("Cancel operation", "Avbryt åtgärd", "Annuller handling", "Avbryt operasjon", "Peruuta toiminto", "Zrušit operaci", "Zrušiť operáciu", "Művelet megszakítása", "Anulează operația", "Ακύρωση λειτουργίας", "Batalkan operasi", "Hủy thao tác", "لغو عملیات", "संचालन रद्द करें", "ยกเลิกการทำงาน", "Cancel·la l'operació"),
    ("Installed", "Installerad", "Installeret", "Installert", "Asennettu", "Nainstalováno", "Nainštalované", "Telepítve", "Instalat", "Εγκατεστημένο", "Terinstal", "Đã cài đặt", "نصب‌شده", "इंस्टॉल किया गया", "ติดตั้งแล้ว", "Instal·lat"),
    ("Environment ready", "Miljön är klar", "Miljøet er klar", "Miljøet er klart", "Ympäristö on valmis", "Prostředí je připraveno", "Prostredie je pripravené", "A környezet kész", "Mediul este gata", "Το περιβάλλον είναι έτοιμο", "Lingkungan siap", "Môi trường đã sẵn sàng", "محیط آماده است", "परिवेश तैयार है", "สภาพแวดล้อมพร้อมแล้ว", "L'entorn està a punt"),
    ("Wine runner missing", "Wine-köraren saknas", "Wine-programmet mangler", "Wine-programmet mangler", "Wine-ohjelma puuttuu", "Program Wine chybí", "Program Wine chýba", "A Wine program hiányzik", "Lipsește executabilul Wine", "Λείπει το Wine", "Program Wine tidak ada", "Thiếu trình chạy Wine", "اجرای Wine موجود نیست", "Wine रनर अनुपलब्ध है", "ไม่มีตัวเรียกใช้ Wine", "Falta l'executable del Wine"),
    ("Status unavailable", "Status är inte tillgänglig", "Status er ikke tilgængelig", "Status er utilgjengelig", "Tila ei ole saatavilla", "Stav není dostupný", "Stav nie je dostupný", "Az állapot nem érhető el", "Starea nu este disponibilă", "Η κατάσταση δεν είναι διαθέσιμη", "Status tidak tersedia", "Không có trạng thái", "وضعیت در دسترس نیست", "स्थिति उपलब्ध नहीं है", "ไม่มีสถานะ", "L'estat no està disponible"),
    ("Download and install", "Hämta och installera", "Download og installer", "Last ned og installer", "Lataa ja asenna", "Stáhnout a nainstalovat", "Stiahnuť a nainštalovať", "Letöltés és telepítés", "Descarcă și instalează", "Λήψη και εγκατάσταση", "Unduh dan instal", "Tải xuống và cài đặt", "بارگیری و نصب", "डाउनलोड और इंस्टॉल करें", "ดาวน์โหลดและติดตั้ง", "Baixa i instal·la"),
    ("Later", "Senare", "Senere", "Senere", "Myöhemmin", "Později", "Neskôr", "Később", "Mai târziu", "Αργότερα", "Nanti", "Để sau", "بعداً", "बाद में", "ภายหลัง", "Més tard"),
    ("Close", "Stäng", "Luk", "Lukk", "Sulje", "Zavřít", "Zavrieť", "Bezárás", "Închide", "Κλείσιμο", "Tutup", "Đóng", "بستن", "बंद करें", "ปิด", "Tanca"),
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
