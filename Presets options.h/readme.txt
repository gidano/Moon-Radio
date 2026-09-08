Az options.h fájl környezetfüggő beállítását szeretném megkönnyíteni a rendelkezésemre álló kijelzőkhöz igazodó előre elkészített konfigurációs fájlokkal.

AZ AXS15231B kijelzőhöz fix képernyő pin-ek tartoznak, ez adott, de a többi kijelzőhöz is - a használt hardverelemek függvényében - a saját bekötésed alapján írd át a használt pin hozzárendeléseket.

Teljesen szabadon használhatod az érintőképernyős vagy 1/2 encoderes beállításokat, a használt DAC függvényében viszont kommentáld ki/be az MCLK-pin használatát. Ezeket én a 15, 17 pin valamelyikén szoktam használni.

- options.h_AXS15231B a JC3248W535C modulhoz
- options.h_ILI9488_TS azaz (T)ouch (S)creen
- options.h_ILI9488_noTS azaz érintőképernyő nélkül
- options.h_ST7796_noTS azaz érintőképernyő nélkül
több kijelzőt nem tudtam konfigurálni mert nálam nincs egyéb más 480x320-as kijelző

Egyéb haználati infókat a Readme-, és Functionality.md fájlok tartalmaznak!

Kellemes időtöltést kíván a Moon Radio! :)
