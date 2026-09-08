Az options.h fájl környezetfüggő beállítását szeretném megkönnyíteni a rendelkezésemre 
álló kijelzőkhöz igazodó előre elkészített konfigurációs fájlokkal.

AZ AXS15231B kijelzőhöz fix képernyő pin-ek tartoznak, ez adott, de a többi kijelzőhöz is
- a használt hardverelemek függvényében - a saját bekötésed alapján írd át a használt pin hozzárendeléseket.

Teljesen szabadon használhatod az érintőképernyős vagy 1/2 encoderes beállításokat,
a használt DAC függvényében viszont kommentáld ki/be az MCLK-pin használatát.
Ezeket én a 15, 17 pin valamelyikén szoktam használni.

- options.h_AXS15231B a JC3248W535C modulhoz
- options.h_ILI9488_TS azaz (T)ouch (S)creen
- options.h_ILI9488_noTS azaz érintőképernyő nélkül
- options.h_ST7796_noTS azaz érintőképernyő nélkül
több kijelzőt nem tudtam konfigurálni mert nálam nincs egyéb más 480x320-as kijelző

Egyéb haználati infókat a Readme-, és Functionality.md fájlok tartalmaznak!

Kellemes időtöltést kíván a Moon Radio! :)

---

I would like to simplify the environment-dependent configuration of the options.h file by using 
pre-built configuration files tailored to the displays I have available.

The AXS15231B display has fixed screen pins—that’s a given—but for the other displays as well,
—depending on the hardware components used—you’ll need to rewrite the pin assignments based on your own wiring.

You’re free to use either the touchscreen or 1/2 encoder settings,
but depending on the DAC used, you’ll need to comment out or uncomment the use of the MCLK pin.
I usually use one of pins 15 or 17 for these.

- options.h_AXS15231B for the JC3248W535C module
- options.h_ILI9488_TS, i.e., (T)ouch (S)creen
- options.h_ILI9488_noTS, i.e., without a touchscreen
- options.h_ST7796_noTS, i.e., without a touchscreen
I wasn’t able to configure more displays because I don’t have any other 480x320 displays

Additional usage information is included in the Readme and Functionality.md files!

Moon Radio wishes you a pleasant listening experience! :)
