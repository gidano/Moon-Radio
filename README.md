# Képernyőképek

<div align="center">

[![YouTube](https://img.shields.io/badge/YouTube-Moon%20Radio%20bemutat%C3%B3-red?logo=youtube&logoColor=white)](https://youtu.be/3OMWfQtBlBg?si=3hZpE63FgSKS6RIv)

[![Moon Radio – bemutató videó](https://img.youtube.com/vi/3OMWfQtBlBg/hqdefault.jpg)](https://youtu.be/3OMWfQtBlBg?si=3hZpE63FgSKS6RIv)

</div>

<p align="center">
  <img src="https://github.com/gidano/Moon-Radio/blob/main/Photos/Moon_Radio.jpg" width="480"><img src="https://github.com/gidano/Moon-Radio/blob/main/Photos/Moon_Radio.2.jpg" width="480">
</p>

# Moon Radio

A Moon Radio ESP32-S3 alapú internetes rádió, érintőkijelzős és böngészőből
elérhető kezeléssel.

## Főbb funkciók

- Internetes rádióadók és helyi hálózati M3U lejátszási listák lejátszása.
- DLNA hálózati zenelejátszás: a rádió megkeresi a helyi hálózat PC-n, NAS-on, szerveren vagy telefonon (én ezt használom, ingyenes: https://github.com/thawee/musicmate)elérhető DLNA-médiatárakat, amelyek mappái böngészhetők,
  zeneszámai pedig közvetlenül lejátszhatók a kijelzőről vagy a webes felületről.
- Helyi zenelejátszás SD-kártyáról a FREENOVE FNK0104S beépített
  SD_MMC-olvasójával vagy az ILI9488/ST7796 profilok SPI-s olvasójával,
  mappa- és fájlböngészéssel.
- Mentett állomáslista, állomáskeresés, szerkesztés és presetek.
- Állomáslogó, valamint elérhető daladatok alapján megjelenő albumborító.
- Sztereó VU, spektrum vagy kikapcsolt alsó kijelzősáv.
- Időjárás, dátum, névnap és holdfázis megjelenítése.
- Választható diagnosztikai nézet a puffer, processzor, memória és
  hőmérséklet adataival.
- Hangerő- és fényerőszabályzás.
- Wi-Fi beállítás és időzóna-kezelés.
- Magyar, angol, német és lengyel nyelvű rádió és webes kezelőfelület.
- Állomások, logók, háttérképek és egyéb rádiós tartalmak vezeték nélküli
  kezelése.

## Nyelvválasztás

Mentett nyelvválasztás hiányában az `options.h` fájl `LANGUAGE` beállítása adja az alapnyelvet:
`HU`, `EN`, `DE` vagy `PL`. A webes Beállítások / Nyelv választója együtt
váltja a rádió és a webfelület nyelvét, a dátumot, névnapokat, időjárási
feliratokat és az időbemondás nyelvét. A választás újraindítás után is megmarad,
és felülírja a fordításkori alapnyelvet. Egy későbbi firmware-frissítés is
megtartja a mentett választást; az `options.h` átírása önmagában nem írja felül.
Az időbemondás külön nyelvválasztója
továbbra is használható. A régi `NAMEDAYS_FILE` beállítás helyett a névnaplista
automatikusan a közös nyelvet követi.

Mind a négy `index_*.html`, `settings_*.html` és `search_*.html` oldal a rádió
mentett nyelvét követi. Az aktív böngészőlapok 5 másodpercenként ellenőrzik
a rádió beállítását; háttérből visszatérve szintén frissítik a nyelvet.
A frissítéshez a firmware mellett a webes
fájlokat is fel kell tölteni, az új `data/web/language.js` fájllal együtt.

Az angol `src/time/nameday_EN.h` 366 napos, európai névnapi és szentnapi
hagyományokhoz igazított projektlista, angol nevekkel; nem hivatalos brit
vagy amerikai névnaptár. Mind a négy táblázat tartalmaz február 29-i helyet,
így a márciusi és későbbi dátumok nem csúsznak el nem szökőévben sem.

## Támogatott kijelzők és vezérlés

- ILI9488 kijelző XPT2046 érintéssel és támogatott SPI-s SD-olvasóval.
- ILI9488 kijelző FT6X36 kapacitív érintéssel.
- ST7796 kijelző XPT2046 vagy FT6X36 érintéssel.
- Guition JC3248W535C modul beépített AXS15231B kijelzővel és érintéssel.
- FREENOVE FNK0104S modul FT6X36 kapacitív érintéssel és támogatott,
  beépített SD_MMC-olvasóval.

Az érintőképernyőről elérhető a lejátszás, a Wi-Fi-nézet, a hangerő, az
óra/IP-cím, az időjárási nézet és a vizualizáció módja. Forgóencoderekkel az
állomásválasztás, a hangerő és a lejátszás is kezelhető.

Az SD-forrás a készülék kijelzőjén és mind a négy nyelvű webes felületen
kiválasztható. SD-módban az állomáslista helyén a kártya könyvtárai és
lejátszható fájljai jelennek meg. A részletes használat és a támogatott
formátumok az **SD-zenelejátszás – támogatott olvasók** fejezetben találhatók.
Az ILI9488_TS modul SPI-s SD-foglalata, a kijelző és az XPT2046 érintő közös
HSPI/SPI3 buszon működik, külön chip-select vezetékekkel.

## Mit mutat a rádió?

A főképernyő az aktuális állomást, előadót és címet, hangformátumot,
Wi-Fi-állapotot, hangerőt, órát vagy IP-címet, időjárást, dátumot, névnapot,
holdfázist és hangvizualizációt mutathat. Ha nincs elérhető albumborító, az
állomáslogó marad látható.

## Első használat

Első indításkor a Moon Radio Wi-Fi beállítást kínál. Csatlakozás után a
böngészős kezelőfelület a kijelzőn látható IP-címen érhető el.

## Kapcsolódó eszközök

A Moon Radio használható vezeték nélküli fájlkezelővel és számítógépes
zenemappák streamelésére szolgáló kiegészítővel.

## LittleFS File Manager Wi-Fi v0.6.0-kapcsolat

A firmware fájllistázási, letöltési és törlési végpontjait a
`WiFi_manager_v0.6.0` csomagban lévő Wi‑Fi Partition Manager is használhatja.
A fájlkezelő Wi-Fi-s feltöltése is támogatott (`POST /upload`). A rádió
ideiglenes fájlba fogadja az adatokat, és csak a teljes feltöltés után
cseréli le a célfájlt; megszakításkor az eredeti fájl megmarad.
A programban válaszd a **WiFi / IP** kapcsolatot, majd add meg a kijelzőn
látható rádió-IP-címet (később az LVGL Radio feliratot érintve hívható elő).
Portot vagy külön útvonalat nem kell megadni.

Ajánlott kiegészítő partíciófájlok Wi-Fi-s kezeléséhez:
[LittleFS-SPIFFS_File_Manager_WiFi_v0.6.0](https://github.com/gidano/myRadio-SPIFFS-Manager/tree/main/LittleFS-SPIFFS%20Partition%20Manager)

Wi‑Fi-n elérhető műveletek:

- teljes LittleFS könyvtárfa listázása;
- fájlok letöltése;
- fájlok feltöltése és cseréje;
- könyvtárak létrehozása és törlése;
- fájlok törlése;
- a rádió újraindítása.

A helyi `data` mappa tartalmát soros `uploadfs` művelettel lehet feltölteni.
A webes háttérkép- és logókezelés saját feltöltési funkciót használ.
A Wi‑Fi fájlkezelő csatlakozásakor a rádió biztonsági karbantartási módba
lép: leáll a lejátszás és az LVGL-frissítés, így a használatban lévő
fontok és cache-fájlok is cserélhetők. A munka végén használd a Partition
Manager **Újraindítás** parancsát a normál rádiómód visszaállításához.
A fájlkezelő API a helyi hálózaton nincs jelszóval védve, ezért csak
megbízható hálózaton használd.

PC-n lévő zenei mappák hálózati streameléséhez használható kiegészítő:
[myRadio Music Server](https://github.com/gidano/myRadio-Music-Server)

Android rendszerű eszközről történő webes irányításhoz, készülékmentéssel:
[YoRadio Controller](https://github.com/gidano/YoRadio-Controller)

## SD-zenelejátszás – támogatott olvasók

Az FNK0104S beépített olvasója a Freenove gyári példái szerinti, 4 bites
SD_MMC buszt használja: CLK=38, CMD=40, D0=39, D1=41, D2=48, D3=47.
A kijelző SPI-buszától független; az `options.h_FNK0104S` tartalmazza ezt a
bekötést. Az I2S marad DIN=2, BCK=3, WS=14; a 38-as és 47-es láb SD mellett
nem használható hangkimenetre.

Az ILI9488_TS SPI-s olvasója SCK=12, MISO=13, MOSI=11 és CS=39 bekötéssel
működik. A kijelző, az XPT2046 érintő és az SD ugyanazt a HSPI/SPI3 vezérlőt
használja, saját chip-select jelekkel. Ezt az `options.h_ILI9488_TS`, az
ILI9488 érintés nélküli profilja és az ST7796 profil is tartalmazza.

- A webes fő-, keresési és beállítási oldalakon a **SD-kártya** jelvény választja
  ki az SD-forrást. A főoldalon az állomások helyén mappák és zenefájlok jelennek meg.
- Nyisd meg a kívánt mappát, majd kattints a zenére. A lista 64 bejegyzésenként
  tölthető tovább. A keresés az addig betöltött bejegyzésekben keres.
- A szünet és a hangerő változatlanul használható. Az előző/következő gombok
  SD-módban az aktuális mappa zenéi között lépnek, fájlnév szerinti sorrendben.
  A szám végén a következő indul, a mappa végén az elsőre ugrik vissza.
  A **Rádió** jelvény visszavált az utoljára kiválasztott rádióállomásra.
- A készülék saját kijelzőjén SD-módban az SD/állomásnévre koppintva,
  a szokásos listanyitó gesztussal vagy az encoder hosszú gombnyomásával
  az SD-választó nyílik meg. A `[+]` sorok mappák; a `[..]` sor egy szinttel
  visszalép. A fájlnév kiválasztása lejátszást indít. A pipa vagy az encoder
  rövid gombnyomása azonnal megerősít; görgetés után 3 másodperc várakozás
  is kiválasztja az elemet. A lista 64 valódi bejegyzésenként lapozható,
  újraolvasható, és tartalmaz visszaváltást rádiómódra. SD-módban a rádiós
  presetek helyett ez a fájlválasztó jelenik meg.
- A helyi dekóderek formátumai: MP3, AAC/AACP, M4A (AAC), WAV, FLAC,
  OGG/OGA (Vorbis/Opus), OPUS és VOR/VORBIS. A konkrét kódolás, mintavétel
  és bitráta továbbra is a meglévő hangkönyvtár és az ESP32 korlátaihoz igazodik.
- SD-logó jelenik meg a kijelzőn és a weben. Elérhető előadó/cím metaadat esetén
  a meglévő, engedélyezett borítókeresés is működhet, internetkapcsolattal.
  Találat hiányában az SD-logó marad. Metaadat nélkül a fájlnév látható.
  A fájlba ágyazott borítók SD-ről történő kiolvasása ebben a változatban nincs bekötve.
- A kártyát a készülék kikapcsolt állapotában helyezd be vagy cseréld; FAT32
  javasolt. A firmware nem formázza a kártyát és nem ír a zenefájlokba.
  Az SD-lejátszás nem igényel internetet; induláskor a készülék rádiómódban marad.

---

# Moon Radio – English

Moon Radio is an ESP32-S3-based internet radio with a touchscreen and
controls accessible via a web browser.

## Key features

- Plays internet radio stations and local network M3U playlists.
- DLNA network music playback: The radio searches the local network for PCs, NAS devices, servers, or phones (I use this one—it’s free: https://github.com/thawee/musicmate) and allows you to browse their folders and  
play songs directly from the display or the web interface.
- Plays local music from the FREENOVE FNK0104S integrated SD_MMC reader or
  the SPI card reader configured by the ILI9488/ST7796 profiles, including
  folder and file browsing.
- Saved station list, station search, editing and presets.
- Station logos and album covers displayed based on available track data.
- Stereo VU, spectrum or disabled bottom display bar.
- Display of weather, date, name day and moon phase.
- Optional diagnostic view showing buffer, processor, memory and
  temperature data.
- Volume and brightness control.
- Wi-Fi settings and time zone management.
- Radio display and web interface in Hungarian, English, German and Polish.
- Wireless
  management of stations, logos, wallpapers and other radio content.

## Language selection

When no language has been saved, `LANGUAGE` in `options.h` sets the default:
`HU`, `EN`, `DE` or `PL`. The web interface's Settings / Language selector
updates the radio and web interface together, including weekday names, dates,
name days, weather text and the time announcement language.

The selected language survives restarts and firmware updates and takes
priority over the build-time default. Changing `options.h` alone does not
override a saved selection. The separate time announcement language selector
remains available. Name days automatically follow the shared language;
the legacy `NAMEDAYS_FILE` setting no longer selects a separate calendar.

All four `index_*.html`, `settings_*.html` and `search_*.html` variants follow
the radio's saved language. Active browser tabs check the setting every
5 seconds and also refresh it when returning from the background.
Updating the radio requires both the firmware and the web files, including
the new `data/web/language.js` file.

The English `src/time/nameday_EN.h` contains 366 entries with English names,
adapted from European name-day and saint-day traditions for this project.
It is not an official UK or US calendar. All four tables include a February 29
slot, keeping March and later dates aligned in non-leap years as well.

## Supported displays and controls

- ILI9488 display with XPT2046 touch and a supported SPI SD reader.
- ILI9488 display with FT6X36 capacitive touch.
- ST7796 display with XPT2046 or FT6X36 touch.
- Guition JC3248W535C module with built-in AXS15231B display and touch.
- FREENOVE FNK0104S display with FT6X36 capacitive touch and supported
  integrated SD_MMC reader.

The touchscreen provides access to playback, the Wi-Fi view, volume, the
clock/IP address, the weather view and visualisation mode. Using rotary encoders,
station selection, volume and playback can also be controlled.

The SD source can be selected on the device and on every localized web page.
In SD mode, folders and playable files replace the station list. See
**SD playback – supported readers** below for controls and supported formats.
The ILI9488_TS SPI card slot, display and XPT2046 touch controller share the
HSPI/SPI3 bus and use separate chip-select signals.

## What does the radio display?

The main screen can display the current station, artist and track title, audio format,
Wi-Fi status, volume, clock or IP address, weather, date, name day,
moon phase and audio visualisation. If no album cover is available, the
station logo remains visible.

## First-time use

On first launch, Moon Radio prompts you to set up Wi-Fi. Once connected, the
browser interface can be accessed via the IP address shown on the display.

## Related devices

Moon Radio can be used with a wireless file manager and a plug-in for
streaming music folders from a computer.


## LittleFS File Manager Wi-Fi v0.6.0 connection

The file listing, download and deletion endpoints can be used by the
Wi-Fi Partition Manager included in the `WiFi_manager_v0.6.0` package.
Wi-Fi uploads are supported through `POST /upload`. The radio receives data
into a temporary file and replaces the destination only after the complete
upload; an interrupted transfer preserves the original file.
In the programme, select the **WiFi / IP** connection, then enter the radio IP address
shown on the display (this can be accessed later by tapping the ‘LVGL Radio’ label).
No port or separate route needs to be specified.

Recommended additional partition files for Wi-Fi management:
[LittleFS-SPIFFS_File_Manager_WiFi_v0.6.0](https://github.com/gidano/myRadio-SPIFFS-Manager/tree/main/LittleFS-SPIFFS%20Partition%20Manager).

Operations available via Wi-Fi:

- listing the entire LittleFS directory tree;
- downloading files;
- uploading and replacing files;
- creating and deleting directories;
- deleting files;
- restarting the radio.

Upload the local `data` folder with serial `uploadfs`.
Web background and logo management use their own dedicated upload endpoints.
When the Wi-Fi file manager connects, the radio enters safety maintenance mode:
playback and LVGL updates are paused, so that the fonts and cache files
currently in use can also be replaced. Once you have finished, use the Partition
Manager’s **Restart** command to return to normal radio mode.
The file manager API is not password-protected on the local network, so only
use it on a trusted network.

Add-on for streaming music folders from a PC over a network:
[myRadio Music Server](https://github.com/gidano/myRadio-Music-Server).

For web-based control from an Android device, with device backup:
[YoRadio Controller](https://github.com/gidano/YoRadio-Controller)


## SD playback – supported readers

The integrated slot uses Freenove's native 4-bit SD_MMC wiring:
CLK=38, CMD=40, D0=39, D1=41, D2=48, D3=47. It does not share the display SPI bus.
Keep I2S on DIN=2, BCK=3, WS=14.

The ILI9488_TS SPI reader uses SCK=12, MISO=13, MOSI=11 and CS=39. The display,
XPT2046 touch controller and SD reader share HSPI/SPI3 with separate chip-select
signals. The ILI9488 no-touch and ST7796 profiles use the same SD configuration.

Select **SD card** on any localized web control
page, browse a folder on the home page and select a music file. Previous/next
and automatic advance cycle through that folder by filename. **Radio** restores
the selected station. Directory pages contain 64 entries; search filters loaded
entries. MP3, AAC/AACP, M4A/AAC, WAV, FLAC, Vorbis and Opus use the existing
decoders and their format limits. With artist/title tags and internet access,
the existing album-cover lookup is used; otherwise the SD logo stays visible.
Embedded local cover extraction is not implemented. FAT32 is recommended;
insert/change cards with power off. SD playback works offline. Upload both the
firmware and the changed web assets, including `sd.js`, and retain `sd_card.png`.

---
