# Unwrapped strings that do not fit the panel

GENERATED -- do not hand-edit. Regenerate with:

    host-tests/i18nwidth/run.sh --report > docs/i18n-overflow.md

`renderer.drawCenteredText` draws ONE line and does not wrap, so a string
wider than the 480px panel runs off the edge. Widths below are real advance
widths read from the generated font headers, measured in the face and weight
each call site actually uses -- 26 of the 54 sites pass BOLD, which is about
5% wider, and measuring those in regular would under-report them.

**Every number here is a reading, not a property of the project.** Rewording
one string moves it. Re-measure rather than quoting these figures later.

Fixing them is a translator pass and needs judgement about meaning: see
`docs/translators.md`. The budget is 480px, not a character count.

## Summary

| language | overflowing | worst | within 15px of the edge | unmeasurable |
| --- | ---: | ---: | ---: | ---: |
| kazakh | 3 | 743.7px | 3 | - |
| slovak | 2 | 670.1px | 2 | - |
| german | 2 | 661.4px | 0 | - |
| czech | 2 | 651.6px | 0 | - |
| hungarian | 2 | 639.1px | 0 | - |
| hebrew | 1 | 638.9px | 0 | - |
| portuguese-PT | 3 | 619.0px | 0 | - |
| portuguese-BR | 3 | 618.0px | 0 | - |
| romanian | 1 | 608.8px | 0 | - |
| polish | 2 | 595.8px | 0 | - |
| ukrainian | 4 | 595.6px | 0 | - |
| french | 1 | 591.8px | 0 | - |
| bosnian | 1 | 591.4px | 0 | - |
| catalan | 5 | 581.9px | 0 | - |
| russian | 1 | 577.2px | 0 | - |
| valencian | 5 | 566.7px | 0 | - |
| finnish | 1 | 551.2px | 0 | - |
| spanish | 3 | 543.1px | 0 | - |
| belarusian | 3 | 540.2px | 1 | - |
| indonesia | 1 | 535.6px | 2 | - |
| lithuanian | 1 | 493.9px | 0 | - |
| turkish | 1 | 492.8px | 0 | - |
| vietnamese | 1 | 490.6px | 0 | - |
| dutch | 1 | 482.9px | 0 | - |
| slovenian | 1 | 480.5px | 1 | - |
| arabic | 0 | - | 0 | 52 |
| italian | 0 | - | 2 | - |
| norwegian | 0 | - | 1 | - |

25 of 32 languages have at least one overflowing unwrapped string.

## Not measurable, and worse than overflowing

These keys contain characters the face has no glyph for. Such characters
draw as nothing and cost zero width, so the string measures NARROW and
would otherwise be reported as fitting. It is not too long -- it does not
render. A width is not meaningful for these and none is given.

The first count below is the WHOLE translation, not the unwrapped subset
this report otherwise measures. "Does this language render at all" is not
a question about one screen, and the subset understates it badly.

- **arabic**: 420 of 429 keys in the whole translation; 52 of the 54 unwrapped
  keys measured here. Characters with no glyph include: `أؤإئابةتثجحخدذرزسشصضطعغفقكلمنهوىي`

This is not a property of non-Latin scripts. Hebrew is measured in the same
face and renders, which is what makes this a font problem with a name.

**It is reachable, not latent.** `scripts/gen_i18n.py` builds the language
table from a glob of `lib/I18n/translations/*.yaml` and requires every file
to declare `_language_name`, so any translation present is offered in the
picker. Selecting one whose script the face lacks gives a near-blank
interface -- including the menu needed to change it back.

A font carrying the script is the fix. No translator can shorten a string
out of this, and asking them to try wastes their time.

## Every overflowing string, worst first

### kazakh

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 743.7px | +263.7 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_CHECK_SERIAL_OUTPUT` | 499.6px | +19.6 | UI_10 | src/activities/settings/ClearCacheActivity.cpp:77 |
| `STR_KOREADER_SETUP_HINT` | 494.0px | +14.0 | UI_10 BOLD | src/activities/reader/KOReaderSyncActivity.cpp:620 |

`STR_RECOVERY_MODE_HINT` (743.7px):

> firmware.bin файлын SD картаның түбірлік қалтасына салып, оны таңдаңыз

`STR_CHECK_SERIAL_OUTPUT` (499.6px):

> Толық ақпарат үшін сериялық шығысты тексеріңіз

`STR_KOREADER_SETUP_HINT` (494.0px):

> Параметрлерде KOReader тіркелгісін баптаңыз

### slovak

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 670.1px | +190.1 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_CLEAR_CACHE_WARNING_1` | 554.1px | +74.1 | UI_10 | src/activities/settings/ClearCacheActivity.cpp:40 |

`STR_RECOVERY_MODE_HINT` (670.1px):

> Umiestnite firmware.bin do koreňového adresára SD karty a vyberte ho

`STR_CLEAR_CACHE_WARNING_1` (554.1px):

> Týmto vymažete všetky údaje kníh vo vyrovnávacej pamäti.

### german

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 661.4px | +181.4 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_CLOCK_SYNC_NO_WIFI_HINT` | 556.8px | +76.8 | UI_10 | src/activities/settings/ClockSyncActivity.cpp:132 |

`STR_RECOVERY_MODE_HINT` (661.4px):

> Lege firmware.bin im SD-Kartenwurzelverzeichnis ab und wähle es aus

`STR_CLOCK_SYNC_NO_WIFI_HINT` (556.8px):

> Verbinde zuerst mit dem WLAN, dann probiere es nochmal.

### czech

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 651.6px | +171.6 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_POWER_ON_HINT` | 553.1px | +73.1 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |

`STR_RECOVERY_MODE_HINT` (651.6px):

> Umístěte firmware.bin do kořenového adresáře SD karty a vyberte jej

`STR_POWER_ON_HINT` (553.1px):

> Stiskněte a podržte tlačítko napájení pro opětovné zapnutí

### hungarian

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 639.1px | +159.1 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_POWER_ON_HINT` | 511.8px | +31.8 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |

`STR_RECOVERY_MODE_HINT` (639.1px):

> Helyezd a firmware.bin fájlt az SD-kártya gyökerébe, majd válaszd ki

`STR_POWER_ON_HINT` (511.8px):

> Tartsd nyomva a bekapcsológombot a bekapcsoláshoz

### hebrew

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 638.9px | +158.9 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |

`STR_RECOVERY_MODE_HINT` (638.9px):

> שים את הקובץ firmware.bin בתיקייה הראשית של כרטיס ה-SD ובחר אותו

### portuguese-PT

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 619.0px | +139.0 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_CLOCK_SYNC_NO_WIFI_HINT` | 557.5px | +77.5 | UI_10 | src/activities/settings/ClockSyncActivity.cpp:132 |
| `STR_POWER_ON_HINT` | 510.8px | +30.8 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |

`STR_RECOVERY_MODE_HINT` (619.0px):

> Coloque o ficheiro firmware.bin na raiz do cartão SD e selecione-o

`STR_CLOCK_SYNC_NO_WIFI_HINT` (557.5px):

> Ligue-se primeiro ao Wi-Fi e, em seguida, tente novamente.

`STR_POWER_ON_HINT` (510.8px):

> Prima sem soltar o botão de energia para voltar a ligar

### portuguese-BR

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 618.0px | +138.0 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_POWER_ON_HINT` | 566.5px | +86.5 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |
| `STR_CLOCK_SYNC_NO_WIFI_HINT` | 490.9px | +10.9 | UI_10 | src/activities/settings/ClockSyncActivity.cpp:132 |

`STR_RECOVERY_MODE_HINT` (618.0px):

> Coloque o arquivo firmware.bin na raiz do cartão SD e selecione-o

`STR_POWER_ON_HINT` (566.5px):

> Pressione e segure o botão de energia para ligar novamente

`STR_CLOCK_SYNC_NO_WIFI_HINT` (490.9px):

> Conecte ao Wi-Fi primeiro, depois tente novamente.

### romanian

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_POWER_ON_HINT` | 608.8px | +128.8 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |

`STR_POWER_ON_HINT` (608.8px):

> Apăsaţi şi menţineţi apăsat întrerupătorul pentru a porni din nou

### polish

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 595.8px | +115.8 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_POWER_ON_HINT` | 593.2px | +113.2 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |

`STR_RECOVERY_MODE_HINT` (595.8px):

> Umieść firmware.bin w głównym katalogu karty SD i wybierz go

`STR_POWER_ON_HINT` (593.2px):

> Przyciśnij i przytrzymaj przycisk zasilania aby włączyć ponownie

### ukrainian

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_KOREADER_SETUP_HINT` | 595.6px | +115.6 | UI_10 BOLD | src/activities/reader/KOReaderSyncActivity.cpp:620 |
| `STR_RECOVERY_MODE_HINT` | 562.2px | +82.2 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_POWER_ON_HINT` | 547.8px | +67.8 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |
| `STR_CLOCK_SYNC_NO_WIFI_HINT` | 521.2px | +41.2 | UI_10 | src/activities/settings/ClockSyncActivity.cpp:132 |

`STR_KOREADER_SETUP_HINT` (595.6px):

> Налаштуйте обліковий запис KOReader в Налаштуваннях

`STR_RECOVERY_MODE_HINT` (562.2px):

> Помістіть firmware.bin у корінь SD-карти та виберіть його

`STR_POWER_ON_HINT` (547.8px):

> Натисніть і утримуйте кнопку живлення, щоб увімкнути

`STR_CLOCK_SYNC_NO_WIFI_HINT` (521.2px):

> Під'єднайтесь спершу до Wi-Fi, тоді спробуйте знову.

### french

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 591.8px | +111.8 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |

`STR_RECOVERY_MODE_HINT` (591.8px):

> Placez firmware.bin à la racine de la carte SD et sélectionnez-le

### bosnian

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 591.4px | +111.4 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |

`STR_RECOVERY_MODE_HINT` (591.4px):

> Stavi firmware.bin u korijenski direktorij SD kartice i odaberi ga

### catalan

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_SYNC_READY` | 581.9px | +101.9 | UI_10 | src/activities/settings/KOReaderAuthActivity.cpp:99 |
| `STR_POWER_ON_HINT` | 572.1px | +92.1 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |
| `STR_CLEAR_CACHE_WARNING_1` | 566.7px | +86.7 | UI_10 | src/activities/settings/ClearCacheActivity.cpp:40 |
| `STR_RECOVERY_MODE_HINT` | 525.6px | +45.6 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_KOREADER_SETUP_HINT` | 507.1px | +27.1 | UI_10 BOLD | src/activities/reader/KOReaderSyncActivity.cpp:620 |

`STR_SYNC_READY` (581.9px):

> La sincronització del KOReader està preparada per utilitzar-se

`STR_POWER_ON_HINT` (572.1px):

> Prem i mantén premut el botó d'encesa per tornar a engegar

`STR_CLEAR_CACHE_WARNING_1` (566.7px):

> Això esborrarà totes les dades de lectura de la memòria cau.

`STR_RECOVERY_MODE_HINT` (525.6px):

> Posa firmware.bin a l'arrel de la targeta SD i selecciona'l

`STR_KOREADER_SETUP_HINT` (507.1px):

> Configura el compte de KOReader a la configuració

### russian

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 577.2px | +97.2 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |

`STR_RECOVERY_MODE_HINT` (577.2px):

> Поместите firmware.bin в корень SD-карты и выберите его

### valencian

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_CLEAR_CACHE_WARNING_1` | 566.7px | +86.7 | UI_10 | src/activities/settings/ClearCacheActivity.cpp:40 |
| `STR_POWER_ON_HINT` | 565.8px | +85.8 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |
| `STR_SYNC_READY` | 564.2px | +84.2 | UI_10 | src/activities/settings/KOReaderAuthActivity.cpp:99 |
| `STR_RECOVERY_MODE_HINT` | 525.6px | +45.6 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_KOREADER_SETUP_HINT` | 500.1px | +20.1 | UI_10 BOLD | src/activities/reader/KOReaderSyncActivity.cpp:620 |

`STR_CLEAR_CACHE_WARNING_1` (566.7px):

> Això esborrarà totes les dades de lectura de la memòria cau.

`STR_POWER_ON_HINT` (565.8px):

> Prem i mantín premut el botó d'encesa per tornar a engegar

`STR_SYNC_READY` (564.2px):

> La sincronització del KOReader està preparada per a usar-se

`STR_RECOVERY_MODE_HINT` (525.6px):

> Posa firmware.bin a l'arrel de la targeta SD i selecciona'l

`STR_KOREADER_SETUP_HINT` (500.1px):

> Configura el compte de KOReader en Configuració

### finnish

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_CLEAR_CACHE_WARNING_1` | 551.2px | +71.2 | UI_10 | src/activities/settings/ClearCacheActivity.cpp:40 |

`STR_CLEAR_CACHE_WARNING_1` (551.2px):

> Tämä tyhjentää kaiken välimuistiin tallennetun kirjatiedon.

### spanish

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_KOREADER_SETUP_HINT` | 543.1px | +63.1 | UI_10 BOLD | src/activities/reader/KOReaderSyncActivity.cpp:620 |
| `STR_RECOVERY_MODE_HINT` | 533.9px | +53.9 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_SYNC_READY` | 492.6px | +12.6 | UI_10 | src/activities/settings/KOReaderAuthActivity.cpp:99 |

`STR_KOREADER_SETUP_HINT` (543.1px):

> Configure una cuenta de KOReader en la configuración

`STR_RECOVERY_MODE_HINT` (533.9px):

> Ponga firmware.bin en la raíz de la tarj. SD y selecciónelo

`STR_SYNC_READY` (492.6px):

> La sincronización de KOReader está lista para usarse

### belarusian

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 540.2px | +60.2 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |
| `STR_CLOCK_SYNC_NO_WIFI_HINT` | 512.8px | +32.8 | UI_10 | src/activities/settings/ClockSyncActivity.cpp:132 |
| `STR_POWER_ON_HINT` | 482.2px | +2.2 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |

`STR_RECOVERY_MODE_HINT` (540.2px):

> Змясціце firmware.bin у корань SD-карты і абярыце яго

`STR_CLOCK_SYNC_NO_WIFI_HINT` (512.8px):

> Спачатку падключыцеся да Wi-Fi, потым паўтарыце.

`STR_POWER_ON_HINT` (482.2px):

> Утрымлівайце кнопку сілкавання для ўключэння

### indonesia

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_POWER_ON_HINT` | 535.6px | +55.6 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |

`STR_POWER_ON_HINT` (535.6px):

> Tekan dan tahan tombol daya untuk menyalakan kembali

### lithuanian

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 493.9px | +13.9 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |

`STR_RECOVERY_MODE_HINT` (493.9px):

> Įkelkite firmware.bin į SD kortelės šaknį ir pasirinkite

### turkish

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_RECOVERY_MODE_HINT` | 492.8px | +12.8 | UI_10 | src/activities/settings/SdFirmwareUpdateActivity.cpp:263 |

`STR_RECOVERY_MODE_HINT` (492.8px):

> firmware.bin dosyasını SD kartın köküne koyup seçin

### vietnamese

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_CLEAR_CACHE_WARNING_1` | 490.6px | +10.6 | UI_10 | src/activities/settings/ClearCacheActivity.cpp:40 |

`STR_CLEAR_CACHE_WARNING_1` (490.6px):

> Thao tác này sẽ xóa toàn bộ dữ liệu sách đã lưu đệm.

### dutch

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_POWER_ON_HINT` | 482.9px | +2.9 | UI_10 | src/activities/settings/OtaUpdateActivity.cpp:169 |

`STR_POWER_ON_HINT` (482.9px):

> Houd de aan/uit-knop ingedrukt om in te schakelen

### slovenian

| key | width | over by | face | call site |
| --- | ---: | ---: | --- | --- |
| `STR_SYNC_READY` | 480.5px | +0.5 | UI_10 | src/activities/settings/KOReaderAuthActivity.cpp:99 |

`STR_SYNC_READY` (480.5px):

> Sinhronizacija KOReader je pripravljena za uporabo

## Within 15px of the edge

Not broken today. Listed because the set above is one reword away from
including them, which is the reason not to treat any count here as settled.

| language | key | width | margin |
| --- | --- | ---: | ---: |
| kazakh | `STR_SYNC_READY` | 476.4px | 3.6px |
| kazakh | `STR_CLOCK_SYNC_NO_WIFI_HINT` | 472.8px | 7.2px |
| kazakh | `STR_CLEAR_CACHE_WARNING_1` | 472.1px | 7.9px |
| slovak | `STR_SYNC_READY` | 478.1px | 1.9px |
| slovak | `STR_OPEN_URL_HINT` | 466.4px | 13.6px |
| belarusian | `STR_KOREADER_SETUP_HINT` | 472.2px | 7.8px |
| indonesia | `STR_CLOCK_SYNC_NO_WIFI_HINT` | 474.8px | 5.2px |
| indonesia | `STR_RECOVERY_MODE_HINT` | 469.6px | 10.4px |
| slovenian | `STR_RECOVERY_MODE_HINT` | 476.7px | 3.3px |
| italian | `STR_POWER_ON_HINT` | 477.6px | 2.4px |
| italian | `STR_KOREADER_SETUP_HINT` | 465.8px | 14.2px |
| norwegian | `STR_RECOVERY_MODE_HINT` | 476.4px | 3.6px |
