# Moon Radio – funkcióösszefoglaló

## Áttekintés

A Moon Radio ESP32-S3 alapú internetes rádió és hálózati zenelejátszó,
érintőkijelzős és böngészőből elérhető kezeléssel.

## Lejátszás és állomások

- Internetes rádióadók és M3U lejátszási listák lejátszása.
- Helyi zenefájlok lejátszása SD-kártyáról a FREENOVE FNK0104S beépített,
  4 bites SD_MMC-olvasójával vagy az ILI9488/ST7796 profilok SPI-s olvasójával.
- Mentett állomáslista, keresés, szerkesztés és presetek.
- Lejátszás/szünet, állomásváltás és hangerőszabályzás érintéssel,
  forgóencoderrel vagy a webes felületről.
- Állomáslogó, valamint elérhető daladatok alapján megjelenő albumborító.
  Utóbbi használata során erőforrás- vagy pufferprobléma esetén az újrapróbálás 30, 60,
  majd legfeljebb 60 másodperces várakozással történik

## SD-kártyás zenelejátszás

- Az SD-forrás a készülék kijelzőjén és a magyar, angol, német vagy lengyel
  webes oldal SD-jelvényével választható ki. A Rádió jelvény visszavált az
  internetes rádióra.
- SD-módban az állomások helyén a kártya könyvtárai és zenei fájljai jelennek
  meg. A mappák megnyithatók, a fájlok közvetlenül kiválaszthatók; a listázás
  64 valódi bejegyzésenként lapozható és újraolvasható.
- A kijelző saját SD-választója érintéssel és forgóencoderrel is kezelhető.
  A szünet, hangerő, előző/következő szám és a szám végén történő automatikus
  továbblépés SD-forrásnál is működik.
- Támogatott fájlkiterjesztések: MP3, AAC/AACP, M4A, WAV, FLAC, OGG/OGA,
  OPUS és VOR/VORBIS. A tényleges kódolási paramétereket a beépített dekóderek
  és az ESP32 erőforrásai korlátozzák.
- Előadó- és címinformáció alapján, internetkapcsolat mellett a meglévő
  borítókeresés használható. Találat hiányában az SD-logó, metaadat nélkül
  a fájlnév jelenik meg. Beágyazott borító kiolvasása nincs megvalósítva.
- A firmware a kártyát nem formázza és a zenefájlokat nem módosítja. FAT32
  használata, valamint a kártya kikapcsolt állapotban történő cseréje ajánlott.
- Az FNK0104S natív SD_MMC bekötése: CLK=38, CMD=40, D0=39, D1=41, D2=48,
  D3=47. Az ILI9488_TS SPI-s olvasója SCK=12, MISO=13, MOSI=11 és CS=39
  bekötéssel működik; a kijelzővel és az XPT2046 érintővel közös HSPI/SPI3
  buszt, de külön chip-select jelet használ. Az ILI9488 érintés nélküli és az
  ST7796 profil ugyanezt az SD-konfigurációt tartalmazza.

## Kijelző és információk

- Sztereó VU, spektrum vagy kikapcsolt alsó kijelzősáv.
- Állomásnév, előadó és cím, hangformátum, Wi-Fi állapot, hangerő, óra vagy
  IP-cím kijelzése.
- Időjárás, dátum, névnap és holdfázis megjelenítése.
- Igény szerint a logó helyén diagnosztikai nézet jeleníthető meg a puffer,
  processzor, memória és hőmérséklet adataival.
- AXS-képernyőn induláskor Moon Radio nyitókép jelenik meg a fő rádióképernyő előtt.

## Kezelés és hálózat

- Wi-Fi beállítás és időzóna-kezelés.
- Böngészőből elérhető állomás-, beállítás- és fájlkezelés.
- Magyar, angol, német és lengyel nyelvű webes kezelőfelület.
- Állomások, logók, háttérképek és egyéb rádiós tartalmak vezeték nélküli
  kezelése.

 ### Nyelvkezelés és névnapok

- Közös HU / EN / DE / PL nyelvválasztás a webes Beállítások / Nyelv részen.
- A választás átállítja a rádió kezelőfeliratainak, napneveinek, dátumának,
  névnapjainak és időjárási szövegeinek nyelvét, valamint az időbemondás nyelvét.
- A beállítás újraindítás után is megmarad. Mentett választás hiányában az
  `options.h` fájl `LANGUAGE` értéke adja az alapnyelvet: `HU`, `EN`, `DE` vagy `PL`.
  Egy firmware-frissítés megtartja a korábban elmentett nyelvet.
- Az időbemondás külön nyelvválasztója továbbra is használható.
- Mind a négy főoldal, beállítási oldal és keresési oldal követi a közös
  nyelvet. Az aktív böngészőlapok 5 másodpercenként ellenőrzik a rádió beállítását.
- A névnapok automatikusan a választott nyelvet követik; a korábbi
  `NAMEDAYS_FILE` beállítás már nem választ külön névnaplistát.
- Mind a négy névnaplista 366 napos, február 29-i bejegyzéssel. Az új
  `src/time/nameday_EN.h` angol nevekkel készült, európai névnapi és szentnapi
  hagyományokhoz igazított projektlista, nem hivatalos brit vagy amerikai naptár.
- Frissítéskor a firmware mellett a webes HTML-fájlokat és az új
  `data/web/language.js` fájlt is fel kell tölteni a rádióra. 

## Támogatott kijelzők

- ILI9488 + XPT2046 érintés és támogatott SPI-s SD-olvasó.
- ILI9488 + FT6X36 kapacitív érintés.
- ST7796 + XPT2046 vagy FT6X36 érintés.
- Guition JC3248W535C modul beépített AXS15231B kijelzővel és érintéssel.
- FREENOVE FNK0104S modul FT6X36 kapacitív érintéssel és beépített,
  támogatott SD_MMC-olvasóval.

---

# Moon Radio – functionality summary

## Overview

Moon Radio is an ESP32-S3 based internet radio and network music player with
touchscreen and browser-based control.

## Playback and stations

- Playback of internet radio stations and M3U playlists.
- Local music playback from the FREENOVE FNK0104S integrated 4-bit SD_MMC
  reader or the SPI reader configured by the ILI9488/ST7796 profiles.
- Saved station list, search, editing and presets.
- Play/pause, station selection and volume control from touch, rotary
  encoders or the browser interface.
- Station logo and album cover displayed based on available song data.
  If a resource or buffer issue occurs while displaying the album cover, the system will retry after waiting 30, 60,
  and then up to 60 seconds

## SD card playback

- Select the SD source on the device or with the SD badge on the Hungarian,
  English, German or Polish web pages. The Radio badge returns to internet
  radio playback.
- In SD mode, card folders and music files replace the station list. Folders
  can be opened and files selected directly; directory listings are paged in
  groups of 64 real entries and can be refreshed.
- The on-device SD selector supports both touch and rotary-encoder control.
  Pause, volume, previous/next track and automatic advancement at end of track
  continue to work with the SD source.
- Supported filename extensions are MP3, AAC/AACP, M4A, WAV, FLAC, OGG/OGA,
  OPUS and VOR/VORBIS. Actual encoding parameters remain subject to the
  existing decoder and ESP32 resource limits.
- With artist/title metadata and an internet connection, the existing online
  cover lookup can be used. Otherwise the SD logo remains visible, or the
  filename is shown when metadata is unavailable. Embedded cover extraction
  is not implemented.
- The firmware does not format the card or modify music files. FAT32 is
  recommended, and cards should be inserted or changed while powered off.
- The FNK0104S native SD_MMC wiring is CLK=38, CMD=40, D0=39, D1=41, D2=48
  and D3=47. The ILI9488_TS SPI reader uses SCK=12, MISO=13, MOSI=11 and
  CS=39; it shares HSPI/SPI3 with the display and XPT2046 touch controller,
  using separate chip-select signals. The ILI9488 no-touch and ST7796 profiles
  contain the same SD configuration.

## Display and information

- Stereo VU meter, spectrum display or a clear lower display band.
- Current station, artist and title, audio format, Wi-Fi state, volume, clock
  or IP address.
- Weather, date, nameday and moon-phase information.
- Optional diagnostic view in place of the logo, showing buffer, processor,
  memory and temperature data.
- When you launch the app, the Moon Radio splash screen appears on the AXS screen before the main radio screen.

## Control and network

- Wi-Fi setup and time-zone settings.
- Browser-based station, settings and file management.
- Web interface in Hungarian, English, German and Polish.
- Wireless management of stations, logos, backgrounds and other radio
  content.

## Language and name days

- Shared HU / EN / DE / PL language selection is available under
  Settings / Language in the web interface.
- The selection changes the language of the radio controls, weekday names,
  date, name days, weather text and time announcements.
- The setting survives restarts and firmware updates. If no language has
  been saved, `LANGUAGE` in `options.h` provides the default: `HU`, `EN`,
  `DE` or `PL`.
- The separate time-announcement language selector remains available.
- All four home, settings and search pages follow the shared language.
  Active browser tabs check the radio setting every five seconds.
- Name days automatically follow the selected language; the legacy
  `NAMEDAYS_FILE` option no longer selects a separate calendar.
- All four name-day tables contain 366 entries, including February 29.
  `src/time/nameday_EN.h` is a project list of English names adapted from
  European name-day and saint-day traditions, not an official UK or US
  calendar.
- Firmware updates must include the web HTML files and
  `data/web/language.js`.

## Supported displays

- ILI9488 with XPT2046 touch and a supported SPI SD reader.
- ILI9488 with FT6X36 capacitive touch.
- ST7796 with XPT2046 or FT6X36 touch.
- Guition JC3248W535C module with built-in AXS15231B display and touch.
- FREENOVE FNK0104S module with FT6X36 capacitive touch and a supported
  integrated SD_MMC reader.
