# Moon Radio – funkcióösszefoglaló / functionality summary

## Magyar

### Áttekintés

A Moon Radio ESP32-S3 alapú internetes rádió és hálózati zenelejátszó,
érintőkijelzős és böngészőből elérhető kezeléssel.

### Lejátszás és állomások

- Internetes rádióadók és M3U lejátszási listák lejátszása.
- Mentett állomáslista, keresés, szerkesztés és presetek.
- Lejátszás/szünet, állomásváltás és hangerőszabályzás érintéssel,
  forgóencoderrel vagy a webes felületről.
- Állomáslogó, valamint elérhető daladatok alapján megjelenő albumborító.
  Utóbbi használata során erőforrás- vagy pufferprobléma esetén az újrapróbálás 30, 60,
  majd legfeljebb 60 másodperces várakozással történik

### Kijelző és információk

- Sztereó VU, spektrum vagy kikapcsolt alsó kijelzősáv.
- Állomásnév, előadó és cím, hangformátum, Wi-Fi állapot, hangerő, óra vagy
  IP-cím kijelzése.
- Időjárás, dátum, névnap és holdfázis megjelenítése.
- Igény szerint a logó helyén diagnosztikai nézet jeleníthető meg a puffer,
  processzor, memória és hőmérséklet adataival.
- AXS-képernyőn induláskor Moon Radio nyitókép jelenik meg a fő rádióképernyő előtt.

### Kezelés és hálózat

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

### Támogatott kijelzők

- ILI9488 + XPT2046 érintés.
- ILI9488 + FT6X36 kapacitív érintés.
- ST7796 + XPT2046 vagy FT6X36 érintés.
- Guition JC3248W535C modul beépített AXS15231B kijelzővel és érintéssel.

---

## English

### Overview

Moon Radio is an ESP32-S3 based internet radio and network music player with
touchscreen and browser-based control.

### Playback and stations

- Playback of internet radio stations and M3U playlists.
- Saved station list, search, editing and presets.
- Play/pause, station selection and volume control from touch, rotary
  encoders or the browser interface.
- Station logo and album cover displayed based on available song data.
  If a resource or buffer issue occurs while displaying the album cover, the system will retry after waiting 30, 60,
  and then up to 60 seconds

### Display and information

- Stereo VU meter, spectrum display or a clear lower display band.
- Current station, artist and title, audio format, Wi-Fi state, volume, clock
  or IP address.
- Weather, date, nameday and moon-phase information.
- Optional diagnostic view in place of the logo, showing buffer, processor,
  memory and temperature data.
- When you launch the app, the Moon Radio splash screen appears on the AXS screen before the main radio screen.

### Control and network

- Wi-Fi setup and time-zone settings.
- Browser-based station, settings and file management.
- Web interface in Hungarian, English, German and Polish.
- Wireless management of stations, logos, backgrounds and other radio
  content.

### Supported displays

- ILI9488 with XPT2046 touch.
- ILI9488 with FT6X36 capacitive touch.
- ST7796 with XPT2046 or FT6X36 touch.
- Guition JC3248W535C module with built-in AXS15231B display and touch.
