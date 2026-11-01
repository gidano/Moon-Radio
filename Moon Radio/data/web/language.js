// The radio stores the shared language; browser storage never overrides it.
(() => {
    // API error codes stay stable for other clients; translate them only for display.
    const errors = {
        'invalid path': ['Hibás útvonal.', 'Invalid path.', 'Ungültiger Pfad.', 'Nieprawidłowa ścieżka.'],
        'background path only': ['Csak háttérfájl törölhető.', 'Only background files can be deleted.', 'Nur Hintergrunddateien können gelöscht werden.', 'Można usuwać tylko pliki tła.'],
        'active background': ['Előbb válassz másik hátteret.', 'Select another background first.', 'Zuerst anderen Hintergrund wählen.', 'Najpierw wybierz inne tło.'],
        'not found': ['A fájl nem található.', 'File not found.', 'Datei nicht gefunden.', 'Nie znaleziono pliku.'],
        'delete failed': ['A törlés nem sikerült.', 'Deletion failed.', 'Löschen fehlgeschlagen.', 'Usuwanie nie powiodło się.'],
        'filesystem busy': ['A fájlrendszer foglalt.', 'Filesystem is busy.', 'Dateisystem ist beschäftigt.', 'System plików jest zajęty.'],
        'upload failed': ['A feltöltés nem sikerült.', 'Upload failed.', 'Hochladen fehlgeschlagen.', 'Przesyłanie nie powiodło się.'],
        'background upload failed': ['A háttér feltöltése nem sikerült.', 'Background upload failed.', 'Hintergrund konnte nicht hochgeladen werden.', 'Nie udało się przesłać tła.'],
        'too large': ['A fájl túl nagy.', 'File is too large.', 'Datei ist zu groß.', 'Plik jest za duży.']
    };
    const unknownError = ['A művelet nem sikerült. Próbáld újra.', 'Operation failed. Please try again.', 'Aktion fehlgeschlagen. Bitte erneut versuchen.', 'Operacja nie powiodła się. Spróbuj ponownie.'];
    const uploadErrors = [
        ['Nem sikerült megnyitni a célfájlt.', 'Cannot open destination file.', 'Zieldatei kann nicht geöffnet werden.', 'Nie można otworzyć pliku docelowego.'],
        ['A háttérfájl túl nagy.', 'Background file is too large.', 'Hintergrunddatei ist zu groß.', 'Plik tła jest za duży.'],
        ['A célfájl nem elérhető íráshoz.', 'Destination file is not writable.', 'Zieldatei ist nicht beschreibbar.', 'Nie można zapisać pliku docelowego.'],
        ['Írási hiba a háttérfájl mentése közben.', 'Error saving background file.', 'Fehler beim Speichern des Hintergrunds.', 'Błąd zapisywania pliku tła.'],
        ['A feltöltési kapcsolat megszakadt.', 'Upload connection lost.', 'Upload-Verbindung unterbrochen.', 'Przerwano połączenie przesyłania.']
    ];
    // Also recognize already localized responses, including requests that finish
    // just after another client changes the radio language.
    for (const row of [...Object.values(errors), ...uploadErrors]) {
        for (const text of row) errors[text.toLowerCase()] = row;
    }
    window.radioErrorText = message => {
        const index = ['hu', 'en', 'de', 'pl'].indexOf(document.documentElement.lang);
        return (errors[String(message).trim().toLowerCase()] || unknownError)[index < 0 ? 1 : index];
    };
    async function syncLanguage() {
        if (document.hidden || window.radioLanguageChanging) return;
        try {
            const response = await fetch('/api/language', { cache: 'no-store' });
            if (!response.ok) return;
            const { language } = await response.json();
            if (!['hu', 'en', 'de', 'pl'].includes(language)) return;
            const pageLanguage = document.documentElement.lang;
            if (language === pageLanguage) return;
            const match = location.pathname.match(/(?:^|\/)(index|settings|search)(?:_[a-z]{2}\.html)?$/);
            const page = match ? match[1] : 'index';
            location.replace('/web/' + page + '_' + language + '.html' + location.search + location.hash);
        } catch (_) { /* Retry when the radio is reachable again. */ }
    }
    syncLanguage();
    setInterval(syncLanguage, 5000);
    document.addEventListener('visibilitychange', syncLanguage);
})();
