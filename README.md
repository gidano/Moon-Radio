# Screenshot / Képernyőképek

<div align="center">

[![YouTube](https://img.shields.io/badge/YouTube-Moon%20Radio%20bemutat%C3%B3-red?logo=youtube&logoColor=white)](https://youtu.be/3OMWfQtBlBg?si=3hZpE63FgSKS6RIv)

[![Moon Radio – bemutató videó](https://img.youtube.com/vi/3OMWfQtBlBg/hqdefault.jpg)](https://youtu.be/3OMWfQtBlBg?si=3hZpE63FgSKS6RIv)

</div>

https://img.youtube.com/vi/3OMWfQtBlBg/hqdefault.jpg

<p align="center">
  <img src="https://github.com/gidano/Moon-Radio/blob/main/Photos/Moon_Radio.jpg" width="480"><img src="https://github.com/gidano/Moon-Radio/blob/main/Photos/Moon_Radio.2.jpg" width="480">
</p>

# Moon Radio

A Moon Radio ESP32-S3 alapú internetes rádió, érintőkijelzős és böngészőből
elérhető kezeléssel.

## Főbb funkciók

- Internetes rádióadók és helyi hálózati M3U lejátszási listák lejátszása.
- Mentett állomáslista, állomáskeresés, szerkesztés és presetek.
- Állomáslogó, valamint elérhető daladatok alapján megjelenő albumborító.
- Sztereó VU, spektrum vagy kikapcsolt alsó kijelzősáv.
- Időjárás, dátum, névnap és holdfázis megjelenítése.
- Választható diagnosztikai nézet a puffer, processzor, memória és
  hőmérséklet adataival.
- Hangerő- és fényerőszabályzás.
- Wi-Fi beállítás és időzóna-kezelés.
- Magyar, angol, német és lengyel nyelvű webes kezelőfelület.
- Állomások, logók, háttérképek és egyéb rádiós tartalmak vezeték nélküli
  kezelése.

## Támogatott kijelzők és vezérlés

- ILI9488 kijelző XPT2046 érintéssel.
- ILI9488 kijelző FT6X36 kapacitív érintéssel.
- ST7796 kijelző XPT2046 vagy FT6X36 érintéssel.
- Guition JC3248W535C modul beépített AXS15231B kijelzővel és érintéssel.

Az érintőképernyőről elérhető a lejátszás, a Wi-Fi-nézet, a hangerő, az
óra/IP-cím, az időjárási nézet és a vizualizáció módja. Forgóencoderekkel az
állomásválasztás, a hangerő és a lejátszás is kezelhető.

## Mit mutat a rádió?

A főképernyő az aktuális állomást, előadót és címet, hangformátumot,
Wi-Fi-állapotot, hangerőt, órát vagy IP-címet, időjárást, dátumot, névnapot,
holdfázist és hangvizualizációt mutathat. Ha nincs elérhető albumborító, az
állomáslogó marad látható.

## Első használat

Első indításkor a Moon Radio Wi-Fi beállítást kínál. Csatlakozás után a
böngészős kezelőfelület a kijelzőn látható IP-címen érhető el.

## Kapcsolódó eszközök

A Moon Radio használható állomáslista-szerkesztővel, vezeték nélküli
fájlkezelővel és számítógépes zenemappák streamelésére szolgáló kiegészítővel.

## LittleFS File Manager Wi-Fi v0.6.0-kapcsolat

A firmware kompatibilis a
`WiFi_manager_v0.6.0` csomagban lévő Wi‑Fi Partition Managerrel.
A programban válaszd a **WiFi / IP** kapcsolatot, majd add meg a kijelzőn
látható rádió-IP-címet (később az LVGL Radio feliratot érintve hívható elő).
Portot vagy külön útvonalat nem kell megadni.

Ajánlott kiegészítő partíciófájlok Wi-Fi-s kezeléséhez:
[LittleFS-SPIFFS_File_Manager_WiFi_v0.6.0](https://github.com/gidano/myRadio-SPIFFS-Manager/tree/main/LittleFS-SPIFFS%20Partition%20Manager)

Wi‑Fi-n elérhető műveletek:

- teljes LittleFS könyvtárfa listázása;
- fájlok feltöltése és letöltése;
- könyvtárak létrehozása és törlése;
- fájlok törlése;
- a rádió újraindítása.

Ezzel az egyes fájlok módosításához nincs szükség külön soros
`uploadfs` feltöltésre.
Ha viszont a teljes helyi `data` mappát szeretnéd egyszerre újraírni,
az `uploadfs` továbbra is használható.
A Wi‑Fi fájlkezelő csatlakozásakor a rádió biztonsági karbantartási módba
lép: leáll a lejátszás és az LVGL-frissítés, így a használatban lévő
fontok és cache-fájlok is cserélhetők. A munka végén használd a Partition
Manager **Újraindítás** parancsát a normál rádiómód visszaállításához.
A fájlkezelő API a helyi hálózaton nincs jelszóval védve, ezért csak
megbízható hálózaton használd.

Ajánlott kiegészítő állomáslista Wi-Fi-s kezeléséhez:
[myRadio Stations Editor](https://github.com/gidano/myRadio-Stations-Editor)

PC-n lévő zenei mappák hálózati streameléséhez használható kiegészítő:
[myRadio Music Server](https://github.com/gidano/myRadio-Music-Server)

Android rendszerű eszközről történő webes irányításhoz, készülékmentéssel:
[YoRadio Controller](https://github.com/gidano/YoRadio-Controller)


---

## English summary

# Moon Radio

Moon Radio is an ESP32-S3-based internet radio with a touchscreen and
controls accessible via a web browser.

## Key features

- Plays internet radio stations and local network M3U playlists.
- Saved station list, station search, editing and presets.
- Station logos and album covers displayed based on available track data.
- Stereo VU, spectrum or disabled bottom display bar.
- Display of weather, date, name day and moon phase.
- Optional diagnostic view showing buffer, processor, memory and
  temperature data.
- Volume and brightness control.
- Wi-Fi settings and time zone management.
- Web-based user interface in Hungarian, English, German and Polish.
- Wireless
  management of stations, logos, wallpapers and other radio content.

## Supported displays and controls

- ILI9488 display with XPT2046 touch.
- ILI9488 display with FT6X36 capacitive touch.
- ST7796 display with XPT2046 or FT6X36 touch.
- Guition JC3248W535C module with built-in AXS15231B display and touch.

The touchscreen provides access to playback, the Wi-Fi view, volume, the
clock/IP address, the weather view and visualisation mode. Using rotary encoders,
station selection, volume and playback can also be controlled.

## What does the radio display?

The main screen can display the current station, artist and track title, audio format,
Wi-Fi status, volume, clock or IP address, weather, date, name day,
moon phase and audio visualisation. If no album cover is available, the
station logo remains visible.

## First-time use

On first launch, Moon Radio prompts you to set up Wi-Fi. Once connected, the
browser interface can be accessed via the IP address shown on the display.

## Related devices

Moon Radio can be used with a station list editor, a wireless
file manager and a plug-in for streaming music folders from a computer.


## LittleFS File Manager Wi-Fi v0.6.0 connection

The firmware is compatible with the
Wi-Fi Partition Manager included in the `WiFi_manager_v0.6.0` package.
In the programme, select the **WiFi / IP** connection, then enter the radio IP address
shown on the display (this can be accessed later by tapping the ‘LVGL Radio’ label).
No port or separate route needs to be specified.

Recommended additional partition files for Wi-Fi management:
[LittleFS-SPIFFS_File_Manager_WiFi_v0.6.0](https://github.com/gidano/myRadio-SPIFFS-Manager/tree/main/LittleFS-SPIFFS%20Partition%20Manager).

Operations available via Wi-Fi:

- listing the entire LittleFS directory tree;
- uploading and downloading files;
- creating and deleting directories;
- deleting files;
- restarting the radio.

This means that modifying individual files does not require a separate serial
`uploadfs` command.
However, if you wish to overwrite the entire local `data` folder in one go,
`uploadfs` can still be used.
When the Wi-Fi file manager connects, the radio enters safety maintenance mode:
playback and LVGL updates are paused, so that the fonts and cache files
currently in use can also be replaced. Once you have finished, use the Partition
Manager’s **Restart** command to return to normal radio mode.
The file manager API is not password-protected on the local network, so only
use it on a trusted network.

Recommended add-on for managing station lists via Wi-Fi:
[myRadio Stations Editor](https://github.com/gidano/myRadio-Stations-Editor).

Add-on for streaming music folders from a PC over a network:
[myRadio Music Server](https://github.com/gidano/myRadio-Music-Server).

For web-based control from an Android device, with device backup:
[YoRadio Controller](https://github.com/gidano/YoRadio-Controller)

