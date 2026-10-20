# V29 PRO — hardware acceptatie (releaseblokkerend)

**Status: NOT TESTED.** Deze checklist is geen bewijs van uitgevoerde tests. Gebruik alleen eigen hardware. Flashen kan bestaande instellingen/data veranderen; maak eerst een back-up en gebruik nooit `erase-all` zonder expliciete toestemming.

## Testregistratie
- Firmware commit SHA: ______
- Buildnummer / artifact ID: ______
- `flash-layout.json` en `manifest.json` SHA-256 gecontroleerd: PASS / FAIL
- Datum / tester / apparaat serienummer (lokaal bewaren): ______
- T-Deck Plus A / B hardware-revisie: ______
- SenseCAP P1 Pro V8 firmwareversie: ______
- Batterij / antenne / regio EU868: ______

## 1. Installatie en opstarten
- [ ] Board/chip gecontroleerd: T-Deck Plus, ESP32-S3; niet op andere hardware flashen.
- [ ] Installatie via USB met expliciete bevestiging, zonder ongewenste factory erase.
- [ ] Na flash: koude start, scherm/aanraking/toetsenbord bruikbaar, tropische achtergrond zichtbaar.
- [ ] Herstart x3; configuratie, contacten en bestaande data blijven behouden.
- [ ] Installatie met verkeerde boardkeuze wordt geblokkeerd; SHA-256 mismatch blokkeert flash.
- [ ] Herstelprocedure getest met bekende werkende firmware (zonder dataverlies).

## 2. RF en offline communicatie
- [ ] T-Deck A ↔ T-Deck B zonder internet, bidirectionele berichten.
- [ ] T-Deck ↔ P1 Pro V8 RF-compatibiliteit, beide richtingen.
- [ ] RF-first werkt ook als hotspot wegvalt; geen stil berichtverlies.
- [ ] Test herhalingen, duplicaten, timeouts, queue overflow, TTL en herstel na herstart.
- [ ] EU868-regio, antenne en EIRP naleving lokaal gecontroleerd.

## 3. Connectiviteit en privacy
- [ ] Android 2.4 GHz hotspot verbinden, 10x verbreken/herverbinden.
- [ ] iPhone hotspot alleen testen als deze modus officieel wordt ondersteund.
- [ ] Geen GPS-fix: duidelijke status, geen verzonnen locatie; privacy-opties werken.
- [ ] Portal: verlopen/onjuiste/duplicaat tokens afgewezen; geen tokenlek via logs/URL.
- [ ] Portal: origin/CSRF, inputlengte, rate limiting en reconnect gecontroleerd.

## 4. Stabiliteit en stroom
- [ ] 1 uur RF + scherm + Wi-Fi: geen crash, heap/PSRAM/stack gemeten.
- [ ] Accumodus en slaap/herstel, herstart na lege batterij.
- [ ] Queue/snapshot corruptie en herstel zonder stil dataverlies.
- [ ] Lange offline periode / tijdsprong en 8-daagse TTL-gedrag.
- [ ] Bij een mislukte stap: log, repro, firmware SHA en resultaat vastleggen.

## Releasebeslissing
**BLOCKED** totdat alle verplichte tests met datum, resultaten en logs zijn vastgelegd. Een geslaagde GitHub Actions-build is alleen bewijs van compilatie/automatische tests, niet van veilig flashen of werkende RF-hardware.
