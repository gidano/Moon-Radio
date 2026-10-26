#include "Language.h"
#include <cstring>
#include "options.h"

#ifndef LANGUAGE
#define LANGUAGE HU
#endif
namespace Language {
namespace {
// LANGUAGE in options.h is a token: HU, EN, DE or PL.
constexpr Code kDefault = Code::LANGUAGE;
Code active = kDefault;
const char* const codes[] = {"hu", "en", "de", "pl"};
const char* const messages[][4] = {
  {"[..] Szülőkönyvtár", "[..] Parent folder", "[..] Übergeordneter Ordner", "[..] Folder nadrzędny"},
  {"<< Előző oldal", "<< Previous page", "<< Vorherige Seite", "<< Poprzednia strona"},
  {">> Következő oldal", ">> Next page", ">> Nächste Seite", ">> Następna strona"},
  {"SD olvasási hiba - újra", "SD read error - retry", "SD-Lesefehler - erneut", "Błąd odczytu SD - ponów"},
  {"SD újraolvasása", "Refresh SD", "SD aktualisieren", "Odśwież SD"},
  {"Nincs zenei fájl", "No music files", "Keine Musikdateien", "Brak plików muzycznych"},
  {"SD: pipa vagy 3 mp várakozás", "SD: confirm or wait 3 s", "SD: bestätigen oder 3 s warten", "SD: zatwierdź lub poczekaj 3 s"},
  {"Wi-Fi hálózat beállítása", "Wi-Fi network setup", "WLAN einrichten", "Konfiguracja sieci Wi-Fi"},
  {"Hálózat neve", "Network name", "Netzwerkname", "Nazwa sieci"},
  {"Jelszó", "Password", "Passwort", "Hasło"},
  {"Mentés és újraindítás", "Save and restart", "Speichern und neu starten", "Zapisz i uruchom ponownie"},
  {"Vissza a rádióhoz", "Back to radio", "Zurück zum Radio", "Powrót do radia"},
  {"A Wi-Fi beállítás nem menthető.", "Cannot save Wi-Fi settings.", "WLAN-Einstellungen nicht speicherbar.", "Nie można zapisać ustawień Wi-Fi."},
  {"Wi-Fi elmentve. Az eszköz újraindul.", "Wi-Fi saved. Restarting device.", "WLAN gespeichert. Gerät startet neu.", "Zapisano Wi-Fi. Urządzenie uruchomi się ponownie."},
  {"Nincs ilyen fájl.", "File not found.", "Datei nicht gefunden.", "Nie znaleziono pliku."},
  {"Nincs ilyen oldal.", "Page not found.", "Seite nicht gefunden.", "Nie znaleziono strony."},
  {"Nincs logó.", "Logo not found.", "Logo nicht gefunden.", "Nie znaleziono logo."},
  {"Hibás logó.", "Invalid logo.", "Ungültiges Logo.", "Nieprawidłowe logo."},
  {"Hibás útvonal.", "Invalid path.", "Ungültiger Pfad.", "Nieprawidłowa ścieżka."},
  {"Nem sikerült megnyitni a célfájlt.", "Cannot open destination file.", "Zieldatei kann nicht geöffnet werden.", "Nie można otworzyć pliku docelowego."},
  {"A háttérfájl túl nagy.", "Background file is too large.", "Hintergrunddatei ist zu groß.", "Plik tła jest za duży."},
  {"A célfájl nem elérhető íráshoz.", "Destination file is not writable.", "Zieldatei ist nicht beschreibbar.", "Nie można zapisać pliku docelowego."},
  {"Írási hiba a háttérfájl mentése közben.", "Error saving background file.", "Fehler beim Speichern des Hintergrunds.", "Błąd zapisywania pliku tła."},
  {"A feltöltési kapcsolat megszakadt.", "Upload connection lost.", "Upload-Verbindung unterbrochen.", "Przerwano połączenie przesyłania."},

  {"Állomás betöltése", "Loading station", "Sender wird geladen", "Ładowanie stacji"},
  {"Kapcsolódás...", "Connecting...", "Verbinden...", "Łączenie..."},
  {"Leállítva", "Stopped", "Gestoppt", "Zatrzymano"},
  {"Lejátszás", "Play", "Wiedergabe", "Odtwarzaj"},
  {"Szünet", "Pause", "Pause", "Pauza"},
  {"Nincs lejátszás", "No playback", "Keine Wiedergabe", "Brak odtwarzania"},
  {"Nincs állomás", "No station", "Kein Sender", "Brak stacji"},
  {"I2S indítási hiba", "I2S startup error", "I2S-Startfehler", "Błąd uruchamiania I2S"},
  {"Kép-eseménysor hiba", "Artwork queue error", "Bildwarteschlangenfehler", "Błąd kolejki obrazów"},
  {"Audio kesz", "Audio ready", "Audio bereit", "Audio gotowe"},
  {"HTTP kapcsolat létrejött", "HTTP connected", "HTTP verbunden", "Połączono HTTP"},
  {"Kapcsolódási hiba", "Connection error", "Verbindungsfehler", "Błąd połączenia"},
  {"TTS lejátszás", "Speaking", "Sprachausgabe", "Komunikat głosowy"},
  {"TTS hiba", "Speech error", "Sprachfehler", "Błąd mowy"},
  {"Az M3U lista nem érhető el", "M3U playlist unavailable", "M3U-Liste nicht verfügbar", "Lista M3U niedostępna"},
  {"Indítás 3 mp múlva", "Starting in 3 s", "Start in 3 s", "Start za 3 s"},
  {"Indítás %u mp múlva", "Starting in %u s", "Start in %u s", "Start za %u s"},
  {"KARBANTARTÁSI MÓD", "MAINTENANCE", "WARTUNG", "KONSERWACJA"},
  {"LittleFS fájlkezelő csatlakozott", "LittleFS file manager connected", "LittleFS-Dateimanager verbunden", "Połączono menedżer LittleFS"},
  {"Fájlművelet van folyamatban.\nHa végeztél, használd az ÚJRAINDÍTÁS gombot.", "File operation in progress.\nWhen finished, press RESTART.", "Dateivorgang läuft.\nZum Abschluss NEUSTART drücken.", "Trwa operacja na plikach.\nPo zakończeniu naciśnij RESTART."},
  {"WI-FI BEÁLLÍTÁS", "WI-FI SETUP", "WLAN-EINRICHTUNG", "KONFIGURACJA WI-FI"},
  {"Kapcsolódj ehhez: Moon-Radio-Setup\nNyisd meg: http://%s/", "Connect to: Moon-Radio-Setup\nOpen: http://%s/", "Verbinden: Moon-Radio-Setup\nÖffnen: http://%s/", "Połącz z: Moon-Radio-Setup\nOtwórz: http://%s/"},
  {"A wifi.txt hiányzik vagy hibás.\nNyisd meg a helyi beállító oldalt,\nmentsd el a Wi-Fi nevét és jelszavát,\nmajd a rádió újraindul.", "wifi.txt is missing or invalid.\nOpen the local setup page,\nsave the Wi-Fi name and password.\nThe radio will restart.", "wifi.txt fehlt oder ist ungültig.\nLokale Einrichtung öffnen,\nWLAN-Name und Passwort speichern.\nDas Radio startet neu.", "Brak lub błędny plik wifi.txt.\nOtwórz lokalną stronę konfiguracji,\nzapisz nazwę i hasło Wi-Fi.\nRadio uruchomi się ponownie."},
  {"ÁLLOMÁSOK", "STATIONS", "SENDER", "STACJE"},
  {"Görgess a listában", "Scroll the list", "Liste durchblättern", "Przewiń listę"},
  {"A kijelölt adó 3 másodperc múlva elindul", "Selected station starts in 3 seconds", "Gewählter Sender startet in 3 Sekunden", "Wybrana stacja ruszy za 3 sekundy"},
  {"LEJÁTSZÁS", "PLAY", "ABSPIELEN", "ODTWÓRZ"},
  {"MENTÉS", "SAVE", "SPEICHERN", "ZAPISZ"},
  {"TÖRLÉS", "DELETE", "LÖSCHEN", "USUŃ"},
  {"Válassz egy kedvenc állomást", "Select a favourite station", "Favoriten auswählen", "Wybierz ulubioną stację"},
  {"A törlés megszakítva", "Deletion cancelled", "Löschen abgebrochen", "Anulowano usuwanie"},
  {"Érints meg egy állomást a lejátszáshoz", "Tap a station to play", "Sender zum Abspielen antippen", "Dotknij stacji, aby odtworzyć"},
  {"Válassz helyet az aktuális állomásnak", "Choose a slot for this station", "Speicherplatz für Sender wählen", "Wybierz miejsce dla tej stacji"},
  {"A törléshez érintsd meg kétszer a helyet", "Tap a slot twice to delete", "Zum Löschen zweimal antippen", "Dotknij miejsca dwa razy, aby usunąć"},
  {"Üres hely", "Empty slot", "Leerer Platz", "Puste miejsce"},
  {"Nincs menthető állomás", "No station to save", "Kein Sender zum Speichern", "Brak stacji do zapisania"},
  {"Elmentve: ", "Saved: ", "Gespeichert: ", "Zapisano: "},
  {"A preset nem menthető", "Cannot save preset", "Favorit nicht speicherbar", "Nie można zapisać ulubionej"},
  {"Ez a presethely üres", "This slot is empty", "Dieser Platz ist leer", "To miejsce jest puste"},
  {"Érintsd meg újra a törléshez", "Tap again to delete", "Zum Löschen erneut antippen", "Dotknij ponownie, aby usunąć"},
  {"Törölve: ", "Deleted: ", "Gelöscht: ", "Usunięto: "},
  {"A preset nem törölhető", "Cannot delete preset", "Favorit nicht löschbar", "Nie można usunąć ulubionej"},
  {"Az állomás már nincs a listában", "Station no longer in list", "Sender nicht mehr in der Liste", "Stacji nie ma już na liście"},
  {"Érintsd meg a nyilakat\na kalibráláshoz", "Tap the arrows\nto calibrate", "Pfeile zum Kalibrieren\nantippen", "Dotknij strzałek,\naby skalibrować"},
};
}
Code current() { return active; }
const char* defaultCode() { return codes[static_cast<uint8_t>(kDefault)]; }
const char* code() { return codes[static_cast<uint8_t>(active)]; }
bool set(String value) {
  value.trim(); value.toLowerCase();
  for (uint8_t i = 0; i < 4; ++i) {
    if (value == codes[i]) { active = static_cast<Code>(i); return true; }
  }
  return false;
}
const char* text(const char* source) {
  if (!source) return "";
  for (const auto& row : messages)
    if (strcmp(source, row[0]) == 0) return row[static_cast<uint8_t>(active)];
  return source;
}
}
