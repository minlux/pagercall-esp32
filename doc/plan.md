# Plan: OTA-Update für PagerCall

Stand: 2026-11-04. Vorlage: Candela (`/home/manuel/Projekte/candela/doc/plan.md`, Abschnitte
„OTA-Update über REST-Endpunkt“ und „Update-Server pollen (Souper)“, Code
`esp8266/src/souper.cpp`, `update_signature.cpp`, `sign_firmware.py`, `git_version.py`) und
die SGW-Firmware (`pvmonitor/sgw/firmware_idf/main/ota.cpp`, Signaturprüfung mit mbedtls auf der
Partition). Übernommen wird der Ablauf gegen die Souper-API, nicht der Code 1:1: Candela läuft auf
einem ESP8266 ohne RTOS-Tasks, PagerCall auf einem ESP32-S3 mit FreeRTOS und `esp_ota_*`.

Ziele:

1. **Lokales Update per REST** (`PUT /firmware`), nur mit signiertem Image.
2. **Update-Server Souper pollen**: Ist ein Token hinterlegt, fragt PagerCall regelmäßig nach
   und spielt eine bereitliegende Firmware selbst ein. Ohne Token bleibt alles wie bisher.
3. **Kein Pager-Ruf geht durch das Update verloren** oder wird mitten im Telegramm abgebrochen.

**Als Vorlage für andere Firmware (ESP32 mit Arduino oder IDF):** Allgemein übertragbar sind
lokales Update (`PUT /firmware`) mit eigener Signatur (auf dem ESP32 bevorzugt ECDSA P-256,
sonst RSA 2048, siehe „Signatur“; `sign_firmware.py`, `ota_key.h`, Prüfung nach `esp_ota_end()`), `PUT`/`DELETE /credentials` mit dem gemeinsamen Verhalten (siehe
„Credentials“), Version fest im Code plus `git_version.py`, der Ablauf gegen die Souper-API
(Poll mit Systeminfo, `GET /update`, Download, Status `2`/`1`/`-2` mit `merker`, `dryrun`),
URL fest eingebaut, nur Target-Tokens, Backoff, gegenseitiger Ausschluss der beiden Updates,
`souper_mock.py`. PagerCall-spezifisch: „Pause“ = `pagercall_idle()`, Gerätename
`PagerCall_<MAC>`, Einrichtungsportal, einzelne NVS-Schlüssel statt eines JSON-Strings.
Für ESP8266 siehe Candela (`doc/plan.md`, „Update-Server pollen (Souper)“).

### Nutzer der Vorlage und Lehren daraus

**LRX und LTX** (pvmonitor, LoRa-Gateway, TTGO LoRa32 v1, ESP32 mit 4 MB) nutzen diese Vorlage
seit 2026-11-04 (LRX V1.002, LTX 1.2.0.0, Commit `67d012b`). Details in
`/home/manuel/Projekte/pvmonitor/lora/TODO.md`, Punkt 2.

Regeln, die für jede weitere Firmware gelten:

- **Ein Schlüsselpaar je Firmware bzw. Gerätetyp.** Bei Arduino lässt sich der Typ eines Images
  nicht am App-Descriptor erkennen: `esp_app_desc_t.project_name` ist bei allen Builds
  `arduino-lib-builder`. Getrennte Schlüssel verhindern, dass ein Gerät das Image eines anderen
  annimmt (`403`). LRX und LTX hatten zuerst einen gemeinsamen Schlüssel und eine eigene Kennung
  im Image. Das wurde zugunsten getrennter Schlüssel verworfen. PagerCall hat ohnehin sein eigenes
  Paar.
- **Plattform festlegen** (`platform = https://github.com/pioarduino/platform-espressif32.git#55.03.37`).
  Bei der LTX stand nur `platform = espressif32`. Das löste inzwischen auf Arduino-Core 3.3.7
  auf, und die Firmware baute nicht mehr.
- **Partitionstabelle prüfen, bevor OTA eingebaut wird.** Bei 4-MB-Boards hat die
  Arduino-Standardtabelle nur 1280 KB je App-Slot. LRX/LTX nutzen deshalb `min_spiffs.csv` (1920
  KB je Slot, `nvs` und `otadata` an denselben Adressen). Eine neue Tabelle lässt sich nicht per
  OTA einspielen, sie muss einmal per Kabel aufs Gerät, am besten mit der ersten OTA-fähigen
  Firmware. PagerCall (8 MB, eigene `partitions.csv` mit 3,2 MB je Slot) braucht das nicht.
- **`git_version.py` in einem Repo mit mehreren Projekten:** `git describe --tags` greift dort
  fremde Tags auf (in pvmonitor z. B. `sgw_v2005`). LRX/LTX nutzen deshalb
  `git describe --always --dirty --exclude '*'`, also nur den Commit-Hash.
- **Gemeinsamer Code für mehrere Geräte:** Die OTA-Module (`ota_signature`, `ota_flash`,
  `ota_update`) lassen sich als PlatformIO-Bibliothek einbinden (`lib_deps = symlink://../common/pvm_ota`,
  siehe `pvmonitor/lora/common`). `ota_key.h` bleibt im Projekt und kommt per `-I include` in den
  `build_flags` auf den Include-Pfad. Ebenso die Skripte (`extra_scripts = post:../common/scripts/sign_firmware.py`),
  die Schlüssel und Header aus dem jeweiligen Projekt nehmen.
- **Optional: Neustart spätestens nach einer festen Zeit.** LRX/LTX starten nach einem Update
  neu, sobald kein LoRa-Paket ansteht, spätestens aber nach 60 s (`RESTART_FORCE_MS` in
  `ota_flash.cpp`). PagerCall wartet bewusst unbegrenzt auf `pagercall_idle()`, damit kein Ruf
  verloren geht. Das bleibt so.

Abweichungen von LRX/LTX, als Beispiele für eine andere Lösung:

- **Kein Web-Server im Normalbetrieb (LRX):** also kein `GET /ota` und kein `POST /ota/poll`. Den
  Zustand zeigt die serielle Konsole (`[souper] ...`). Der Token wird einmal beim Start gelesen,
  weil er sich nur im Einrichtungsmodus ändern lässt, und danach kommt ohnehin ein Neustart.
  `PUT /firmware` gibt es bei der LRX nur im Einrichtungsmodus, bei der LTX immer (über ihren AP).
- **Formular der LRX:** Ein leeres SSID-Feld lässt das WLAN unverändert, ein leeres Token-Feld den
  jeweiligen Token. Sonst würde jemand, der nur den Token einträgt, das WLAN-Passwort löschen, weil
  das Passwortfeld nie vorausgefüllt ist. Bei PagerCall ist die SSID im Portal Pflicht.
- **Zwei Tokens (LRX):** `pvm_token` für das PV-Monitor-Backend und `ota_token` für souper, beide
  mit denselben Regeln (höchstens 400 druckbare Zeichen) in `PUT /credentials`.

## Stand der Umsetzung

| Schritt | Stand |
|---|---|
| 1: Version (`git_version.py`, `version.cpp`, `GET /version`) | umgesetzt (1.000) |
| 2: Signieren beim Bauen (`sign_firmware.py`, `ota_key.h`) | umgesetzt, getestet (Build mit, ohne und mit falschem Schlüssel; `openssl` und `esptool` gegen `firmware.signed.bin`) |
| 3: Lokales Update `PUT /firmware` mit Signaturprüfung | umgesetzt, nicht auf Hardware getestet |
| 4: `PUT`/`DELETE /credentials` (wie SGW), Token im NVS, Feld im Einrichtungsportal | umgesetzt, nicht auf Hardware getestet |
| 5: Souper-Task: Poll mit Systeminfo, Intervall, Backoff, `GET /ota`, `POST /ota/poll` | umgesetzt, nicht auf Hardware getestet |
| 6: Update holen, flashen, prüfen, Status melden, `dryrun`, Neustart in Pause | umgesetzt, nicht auf Hardware getestet |
| 7: `test/souper_mock.py`, README | umgesetzt; Mock per curl getestet |
| Host-Tests (`test/host/run.sh`: `creds_patch`, `ota_signature`) | geschrieben, **noch nicht ausgeführt** (siehe unten) |

Stand 2026-11-04: Firmware baut ohne Warnungen, Flash 1 076 960 Byte (32,2 %, vorher
1 016 012), statischer RAM 49 284 Byte (15,0 %). Kein Board angeschlossen, also nichts auf dem Gerät
getestet.

Abweichungen und Entscheidungen bei der Umsetzung:

- **`PUT /firmware` und `PUT /credentials` als Raw-Handler** (`server.on(uri, HTTP_PUT, fn, ufn)`,
  `server.raw()`). Der `WebServer` liest bei `PUT` ohne Upload-Handler den ganzen Body ins RAM,
  bevor er den Handler aufruft; das bisherige `ota_put_firmware()` (mit `client.readBytes()` im
  Handler) hätte also nie funktioniert. Raw kommt in Blöcken von 1436 Byte. Bei `/credentials`
  zusätzlich nötig, damit der `Content-Type` egal ist: Mit `x-www-form-urlencoded` (Standard von
  `curl -d`) würde der Server den JSON-Body sonst als Formular zerlegen.
- **`OTA_WITH_SEQUENTIAL_WRITES` statt `esp_ota_begin(part, size)`:** Mit der Größe löscht
  `esp_ota_begin()` den ganzen Bereich vorab (ca. 260 Sektoren, mehrere Sekunden am Stück).
  Sequentiell wird jeder Sektor erst beim Schreiben gelöscht, das verteilt die Cache-Sperren über
  den Download. Die Größe wird vorher selbst geprüft (`413`).
- **`400` für ein ungültiges Image** (`esp_ota_write`/`esp_ota_end` mit
  `ESP_ERR_OTA_VALIDATE_FAILED`), zusätzlich zu den geplanten Antworten.
- **`ota_key.h` in `include/` statt `src/`**, damit der Host-Test eigene Testschlüssel über den
  Include-Pfad unterschieben kann.
- **Eigene Module für die Logik ohne Hardware:** `ota_signature.cpp` (nur mbedtls, liest über
  einen Callback) und `creds_patch.cpp` (nur cJSON). `ota_flash.cpp`, `credentials.cpp` und
  `souper.cpp` hängen daran.
- **Download parallel zu Rufen, Neustart erst in der Pause:** Der Souper-Task lädt sofort, auch
  wenn Rufe laufen. `ota_restart_loop(pagercall_idle())` in `loop()` startet frühestens 1 s nach
  Erfolg neu, wenn kein Ruf mehr ansteht; bis dahin `503` auf `/pagercall`. Läuft gerade ein
  lokales Update, verschiebt Souper das Update (ohne Meldung, also ohne `merker`) um 5 min.
- **Einrichtungsportal:** Feld „Update token“ plus Checkbox „Delete stored token“, `PUT`/`DELETE
  /credentials` und `GET /reset` auch dort. `GET /ota` gibt es nur im Normalbetrieb, gepollt wird
  im Portal nicht.
- **Anzeige:** OLED zeigt beim Download den Fortschritt mit Balken, danach „Installed,
  restarting...“.
- **Texte in der Firmware auf Englisch** (wie der übrige Code und die README), z. B. `result` in
  `GET /ota`: `ok`, `no token`, `Poll: HTTP 401, token invalid`.
- **Token wird für jede Anfrage aus dem NVS gelesen**, nicht im RAM gehalten (wie Candela).
- **Stack des Souper-Tasks 12 KB** (HTTPClient, cJSON, ECDSA-Prüfung). Der freie Rest wird nach
  jedem Update auf der seriellen Konsole ausgegeben, damit man ihn auf dem Gerät nachmessen kann.

Host-Tests: `test/host/run.sh` lädt cJSON 1.7.19 und mbedtls 3.6.5 (die Versionen der Firmware)
von GitHub nach `test/host/.deps/` und baut sie. Das Ausführen wurde in dieser Sitzung von der
Sicherheitsprüfung des Werkzeugs blockiert (heruntergeladener Fremdcode), die Tests sind also
geschrieben, aber noch nicht gelaufen. Auf dem eigenen Rechner einfach `test/host/run.sh`
starten.

---

## Ausgangslage

- `partitions.csv` hat schon zwei App-Partitionen (`app0`/`app1`, je 3,2 MB) und `otadata`.
  Das Flash-Layout passt, **kein Umflashen per Kabel nötig**, solange die Partitionstabelle
  gleich bleibt.
- `src/ota_update.cpp` enthält `ota_put_firmware()` (`esp_ota_begin/write/end`,
  `esp_ota_set_boot_partition`), die Route in `main.cpp` ist aber auskommentiert. Mängel:
  keine Signatur, `OTA_SIZE_UNKNOWN` löscht die ganze 3,2-MB-Partition vorab (mehrere Sekunden),
  kein Schutz gegen zwei gleichzeitige Updates.
- Zugangsdaten liegen per `Preferences` im NVS (Namespace `pagercall`). NVS überlebt ein
  OTA-Update.
- `loop()` bedient `WebServer` und `pagercall_update()`. Ein laufender Ruf blockiert je
  Wiederholung ca. 26 ms (`Serial1.flush()` + `delay(4)`), 32 Wiederholungen, also ca. 850 ms
  je Ruf. Rufe stehen in einer FIFO mit 16 Plätzen.
- Es gibt noch keine Versionsnummer in der Firmware.

## Entscheidungen

- **Eigener FreeRTOS-Task für Souper statt Zustandsautomat in `loop()`.** Anders als beim
  ESP8266 dürfen die Anfragen hier blockieren (`HTTPClient`, Timeouts), ohne dass `WebServer`
  oder die Rufe stocken. Candelas Zerlegung in Zustände mit „höchstens eine Anfrage je Aufruf“
  und Download in 1-KB-Blöcken je `task()` entfällt, der Ablauf wird linear wie beim SGW
  (`ota_poll_firmare()`). Der Task läuft mit niedriger Priorität auf Core 0 (Arduino-`loop()`
  läuft auf Core 1).
- **Eigene Signatur statt Secure Boot.** Die vorkompilierten Arduino-Bibliotheken erlauben kein
  `CONFIG_SECURE_SIGNED_APPS_*`. Deshalb ein eigenes Signaturformat wie bei Candela/SGW, für den
  ESP32 aber mit **ECDSA P-256 statt RSA 2048**. Einzelheiten im Abschnitt „Signatur“ unten.
- **Eigenes Schlüsselpaar für PagerCall** (`signing/private.key`, nicht in git, `.gitignore`).
  Öffentlicher Schlüssel in `include/ota_key.h`. Keine MD5, die Signatur deckt Übertragungsfehler ab.
- **`esp_ota_begin(part, size)`** mit der bekannten Größe (`Content-Length` bzw. `file.size`),
  damit nur der benötigte Bereich gelöscht wird.
- **URL fest eingebaut** (`-D SOUPER_URL="http://minlux.de/souper"` in `platformio.ini`), auf
  dem Gerät nicht änderbar, sonst könnte jeder im WLAN PagerCall samt Token auf einen eigenen
  Server umleiten. Testvariante `[env:heltec_wifi_lora_32_V3_test]` mit URL im LAN.
- **HTTP, kein HTTPS** (wie Candela/SGW). Das Image schützt die Signatur, HTTPS schützte nur den
  Token. Deshalb **nur der Token des eigenen Targets, nie der Universaltoken.** HTTPS wäre auf dem
  ESP32-S3 billig (`WiFiClientSecure` ist ohnehin gelinkt), bräuchte für eine echte
  Server-Prüfung aber ein CA-Zertifikat im Image; als optionaler späterer Schritt.
- **JSON mit cJSON aus dem IDF** (`#include <cJSON.h>`, in den Arduino-Bibliotheken enthalten),
  keine neue Abhängigkeit; Candelas `json.c` wird nicht gebraucht. Systeminfo wird mit
  `snprintf`/`String` geschrieben.
- **Token setzen über `PUT /credentials`, wie beim SGW** (`pvmonitor/sgw/firmware_idf/main/app_task.cpp`,
  `_put_credentials()`; Candela macht dasselbe mit `POST`). Siehe Abschnitt „Credentials“ unten.
  Der Token wird nie ausgegeben, nur `"token": true/false` in `GET /ota`. Nur druckbares ASCII
  (`0x21`–`0x7e`), höchstens 400 Zeichen.
- **Versionsnummer fest im Code** (entschieden 2026-11-04): `PAGERCALL_VERSION_STR` in
  `include/version.h`, z. B. `"1.000"`, wird von Hand bei jedem Release hochgezählt. Sie steht in
  `GET /version`, im User-Agent (`PagerCall/1.000`) und als `version` in der Systeminfo für
  Souper. Git-Revision (`git describe --always`, mit `-dirty`) und Build-Zeit kommen nur als
  Zusatzinfo (`git`, `build`) dazu, wie bei Candela über `git_version.py`.
- **Gerätename** `PagerCall_xx-xx-xx-xx-xx-xx` (MAC, kleine Hex-Ziffern), Target wird vorher in
  Souper angelegt.

## Signatur: ESP32 bevorzugt ECDSA P-256, sonst RSA 2048

**Regel für alle Projekte** (entschieden 2026-11-04): **ESP32 bevorzugt ECDSA P-256 (SHA256),
sonst RSA 2048 (SHA256, PKCS#1 v1.5)**, insbesondere auf dem **ESP8266**, wo Flash knapp ist
(Candela, SGW). Format und Werkzeuge sind für beide gleich, nur der Schlüssel unterscheidet sich.

### Warum ECDSA P-256 auf dem ESP32

| | RSA 2048 | ECDSA P-256 |
|---|---|---|
| Sicherheitsniveau | ca. 112 Bit (NIST: bis 2030) | ca. 128 Bit (wie RSA 3072) |
| öffentlicher Schlüssel (DER) | 294 Byte | 91 Byte |
| Signatur | 256 Byte | 70–72 Byte (DER, Länge schwankt) |
| Prüfen auf dem Gerät | sehr schnell (e = 65537) | langsamer, in Software einige 10 bis 100 ms (geschätzt, auf dem Gerät messen); einmal je Update, also egal |
| Flash auf dem ESP32-S3 (gemessen) | +140 Byte | +70 Byte (`mbedtls_ecdsa_*`), +210 Byte mit `mbedtls_pk` |
| Flash auf dem ESP8266 | BearSSL RSA (`br_rsa_i15_pkcs1_vrfy`), bei Candela im Einsatz | ECDSA-Code aus BearSSL käme dazu; über die Klassen des Cores etwa 30 KB (laut `candela/esp8266/src/update_signature.h`), direkt aufgerufen weniger, nicht gemessen |

Einordnung: RSA 2048 ist nicht unsicher, ECDSA P-256 ist aber das modernere Verfahren mit mehr
Reserve und kleineren Schlüsseln und Signaturen, und auf dem ESP32 kostet es keinen Flash extra.
Der Flash-Vorteil liegt also nicht im Platz für den Code, sondern darin, dass mbedtls ECDSA ohnehin
mitbringt. Gegen Quantencomputer sind beide nicht sicher.

**Machbarkeit geprüft** (2026-11-04, Probe-Build in einer Kopie des Projekts, pioarduino 55.03.37,
Arduino-Core 3.3.7, IDF 5.5):

- Firmware ohne Probe: 1 016 012 Byte. `mbedtls_ecdsa_read_signature`, `mbedtls_ecp_mul`,
  `mbedtls_pk_parse_public_key`, `mbedtls_mpi_exp_mod` und `mbedtls_sha256` sind **schon
  gelinkt** (WLAN-Stack mit WPA3 und TLS). Die Probe mit `mbedtls_pk_verify` kostet 400 Byte
  inklusive SHA256-Aufruf.
- Host: `openssl dgst -sha256 -sign` erzeugt mit einem P-256-Schlüssel eine DER-Signatur (70–72
  Byte), `openssl dgst -sha256 -verify <pub.der> -keyform DER` prüft sie; derselbe Aufruf
  funktioniert unverändert mit RSA-Schlüsseln. Ein verändertes Image wird abgelehnt.
- Nicht geprüft: Lauf auf dem Gerät (kein Board angeschlossen), also auch die Dauer der Prüfung.

### Format und Werkzeuge (gleich für beide Verfahren)

- **Image-Format unverändert:** `image | Signatur | Länge der Signatur als uint32 LE`. Die Länge
  steht schon im Format, die schwankende Länge der ECDSA-Signatur ist also kein Problem. RSA:
  256 Byte (PKCS#1 v1.5), ECDSA: `ECDSA-Sig-Value` in DER, wie es `openssl dgst -sign` ausgibt.
- **Schlüssel erzeugen:**

  ```sh
  openssl ecparam -name prime256v1 -genkey -noout -out signing/private.key      # ECDSA P-256 (ESP32)
  openssl genrsa -out signing/private.key 2048                                   # RSA 2048 (ESP8266)
  openssl pkey -in signing/private.key -pubout -outform DER | xxd -i             # -> include/ota_key.h
  ```

- **`ota_key.h`:** öffentlicher Schlüssel als DER (`SubjectPublicKeyInfo`), z. B.
  `static const uint8_t ota_key_der[] = { ... };`. Das Verfahren ergibt sich aus dem Schlüssel.
- **`sign_firmware.py`:** `openssl dgst -sha256 -sign signing/private.key` (für beide Verfahren
  gleich), danach Gegenprobe mit dem öffentlichen Schlüssel aus `ota_key.h` über
  `openssl dgst -sha256 -verify ... -keyform DER`. Anders als bei Candela keine eigene
  RSA-Rechnung in Python mehr, das Skript muss das Verfahren nicht kennen.
- **Prüfen auf dem Gerät** (`ota_flash.cpp`): nach `esp_ota_end()` Trailer lesen, SHA256 über das
  Image durch Zurücklesen der Partition (wie SGW), dann `mbedtls_pk_parse_public_key(ota_key_der)`
  und `mbedtls_pk_verify(MBEDTLS_MD_SHA256, hash, signatur)`. Derselbe Code prüft auch RSA, falls
  ein Projekt das braucht. Ungültig: ersten Sektor der Partition löschen, Boot-Partition bleibt
  unverändert.
- `esp_ota_end()` prüft das Image selbst (Header, Prüfsumme, SHA des IDF) und soll die
  angehängten Bytes ignorieren; das wird auf dem Gerät bestätigt.
- **Schlüsselwechsel** (z. B. später von RSA auf ECDSA): Geräte nehmen nur Images an, die zum
  eingebauten Schlüssel passen. Den Wechsel also einmal mit dem alten Schlüssel signiert einspielen
  (Firmware enthält schon den neuen) oder per Kabel. Für PagerCall entfällt das, es startet direkt
  mit ECDSA.
- **Candela und SGW (ESP8266) bleiben bei RSA 2048.** Dort ist nichts zu ändern.

## Credentials: `PUT /credentials`, `DELETE /credentials`

**Gemeinsames Verhalten für PagerCall, Candela und SGW** (entschieden 2026-11-04): SGW-Verhalten,
nur das Passwort wie bei Candela. Candela wird vorab darauf umgestellt (bisher `POST`), das SGW
braucht nur die Passwort-Regel (siehe unten „Abgleich“).

In **beiden Betriebsarten** (Einrichtungsportal unter `http://192.168.4.1` und Normalbetrieb):

```sh
curl -X PUT -d '{ "ssid": "Gammablitz", "password": "geheim123", "ota_token": "eyJ..." }' http://<ip>/credentials
curl -X PUT -d '{ "ota_token": "eyJ..." }' http://<ip>/credentials    # nur Token, WLAN bleibt
curl -X PUT -d '{ "ota_token": null }' http://<ip>/credentials        # Token löschen ("" geht auch)
curl -X DELETE http://<ip>/credentials                                # alles löschen
```

Regeln:

1. **Methode `PUT`**, Body ein JSON-Objekt. `Content-Type` wird nicht geprüft (wie SGW).
2. **Leerer Body** (kein oder `Content-Length: 0`) ist erlaubt und ändert nichts. Ein nicht
   leerer Body, der kein JSON-Objekt ist, ergibt `400`.
3. **Jedes Feld ist optional, Semantik wie JSON Merge Patch** (RFC 7386, beim SGW
   `cJSONUtils_MergePatch`): Fehlt ein Feld, bleibt der gespeicherte Wert. Ein String setzt ihn,
   `null` löscht ihn. Andere Typen ergeben `400`.
4. **Ausnahme Passwort (wie Candela):** Wird `ssid` gesetzt oder gelöscht und fehlt `password`,
   wird das Passwort gelöscht (offenes WLAN). `ssid` und `password` gelten also als Paar. Nur
   `password` ohne `ssid` ändert nur das Passwort.
5. **`"ota_token": ""`** gilt wie `null`: kein Token. Ebenso `"ssid": ""`.
6. **Prüfung (wie Candela)**: `ssid` höchstens 32 Byte, `password` leer oder 8–64 Zeichen, Token
   höchstens 400 druckbare ASCII-Zeichen (`0x21`–`0x7e`). Sonst `400` mit dem Grund als Text, und
   **nichts** wird gespeichert.
   - **Token-Grenze überall gleich** (entschieden 2026-11-04, vorher 383 Zeichen): Token
     höchstens **400 Zeichen**, gilt für SGW, Candela, PagerCall, LRX und LTX. Der Header
     `Authorization: Bearer <token>\r\n` (bis 424 Byte) kommt nicht in eigene feste Puffer:
     PagerCall, Candela, LRX und LTX nutzen `String`, das SGW seinen 8-KB-Puffer (auch für
     souper). Bei Candela wird das EEPROM-Struct größer. Eine Übernahme des alten Formats ist
     nicht nötig, weil noch kein Gerät einen Token gespeichert hat.
7. Antwort `200` bei Erfolg. Der Token wird nie ausgegeben, nur `"token": true/false` in
   `GET /ota`.
8. **`DELETE /credentials`** löscht WLAN-Zugangsdaten und Token, Antwort `200`.
9. **Wirksam:** WLAN-Daten nach dem nächsten Neustart, der Token sofort.

PagerCall im Einzelnen:

- Bekannte Felder: `ssid`, `password`, `ota_token`. Unbekannte Felder werden ignoriert (das SGW
  speichert sie mit, z. B. `pvm_token`).
- **Speicher:** Anders als beim SGW (ein JSON-String `credentials` im NVS) bleiben es einzelne
  Schlüssel im Namespace `pagercall` (`ssid`, `password`, neu `ota_token`). So laufen schon
  eingerichtete Geräte ohne Umstellung weiter. Nach außen sieht man keinen Unterschied.
- Neustart mit `GET /reset`. Der Token gilt sofort (Poll nach 5 s). Im Portal startet `/save`
  wie bisher neu.
- Nach `DELETE /credentials` und Neustart öffnet PagerCall das Einrichtungsportal, weil keine
  SSID mehr gespeichert ist.
- **Portal-Formular** (`/save`): neues optionales Feld „Update-Token“. Das Formular nutzt intern
  dieselbe Funktion (`creds_apply()`) wie `PUT /credentials`. Ein leeres Token-Feld lässt den
  gespeicherten Token stehen.
- CORS/`OPTIONS` wie beim SGW nur, wenn eine Web-Seite von woanders darauf zugreifen soll
  (vorerst nicht).

### Abgleich der drei Firmwares

| | vorher Candela | vorher SGW | gemeinsam |
|---|---|---|---|
| Methode | `POST` | `PUT` | `PUT` |
| `Content-Type` | Pflicht (`415`) | egal | egal |
| leerer Body / ohne `ssid` und `ota_token` | `400` | `200`, nichts geändert | `200`, nichts geändert |
| fehlendes Feld | bleibt (außer Passwort) | bleibt | bleibt (außer Passwort, Regel 4) |
| `null` | nicht unterstützt | löscht | löscht |
| `ssid` ohne `password` | Passwort leer | altes Passwort bleibt | Passwort leer |
| Prüfung der Werte | ja (`400`) | keine | ja (`400`) |
| `DELETE /credentials` | gibt es nicht | löscht alles | löscht alles |

Nötige Änderungen:

- **Candela** (`esp8266/src/http_server.cpp`, `hqsp.c`, `www/setup.html`, `test/smoke.sh`,
  README): `PUT` statt `POST`, kein `Content-Type`-Zwang, leerer Body, `null`, `DELETE`.
  **Umgesetzt** (2026-11-04, nicht committet), getestet gegen `www/test/mock.py` (UI-Test und
  curl), nicht auf Hardware. Siehe Candela `doc/plan.md`, „Zugangsdaten: gleich wie SGW und
  PagerCall“.
- **SGW** (`main/app_task.cpp`, `_put_credentials()`): Regel 4 (Passwort löschen, wenn `ssid` ohne
  `password` kommt) und Regel 6 (Prüfung der Werte, auch für `pvm_token`). **Umgesetzt**
  (2026-11-04, nicht committet), gebaut, Logik auf dem PC mit dem cJSON des SDK getestet, nicht
  auf Hardware. SGW-spezifisch: Ein gelöschtes Passwort wird als `""` gespeichert (offenes WLAN),
  weil ein fehlendes Passwort beim SGW das Standard-WLAN-Passwort bedeutet. Ohne SSID nutzt das
  SGW wie bisher das Standard-WLAN. Der Header an souper kommt statt in `OTA_HEADER_SIZE`
  (320 Byte) jetzt in den 8-KB-Puffer von `connector()`, wie der Header an den PV-Monitor (siehe
  Regel 6). `DELETE /credentials` löscht jetzt auch die Kopie im RAM.

## Lokales Update: `PUT /firmware`

```sh
curl -X PUT -H "Content-Type: application/octet-stream" \
     --data-binary @.pio/build/heltec_wifi_lora_32_V3/firmware.signed.bin http://<ip>/firmware
curl http://<ip>/version
```

Antworten: `200` (danach Neustart nach ca. 1 s, sobald kein Ruf läuft), `403` (Signatur
ungültig), `409` (Update läuft schon, auch vom Souper-Task), `411` (`Content-Length` fehlt),
`413` (größer als die OTA-Partition), `415` (falscher `Content-Type`), `500` (sonstiger Fehler,
Text im Body). Bei jedem Fehler läuft die alte Firmware weiter.

- Statt eines Raw-Handlers mit `server.client().readBytes()` wie heute: weiter so, aber mit
  Timeout (10 s ohne Daten) und mit dem gemeinsamen Ablauf aus `ota_flash.cpp` (siehe unten).
- Das Lesen blockiert `loop()` für die Dauer des Uploads (ca. 1,2 MB). In der Zeit werden keine
  Rufe gesendet, eingehende `/pagercall`-Anfragen warten. Vertretbar, weil lokale Updates von
  Hand angestoßen werden. Rufe in der FIFO gehen nicht verloren.
- `GET /reset` bleibt.

## Update-Server Souper

Basis-URL `SOUPER_URL "/assets/api"`, alle Anfragen mit `Authorization: Bearer <token>`.

1. **Poll:** `PATCH /target/PagerCall_..` mit Systeminfo. Antwort
   `{ "poll": 3600, "updates": [ 17, 18 ] }`.
2. **Je offenem Update** (höchstens 4 je Poll): `GET /update/<id>` → `merker`, `file.src`,
   `file.dst`, `file.size`.
3. **Prüfen:** kein `file`, oder `dst` beginnt nicht mit `firmware` → Status `-2`.
4. **Herunterladen und flashen** mit `HTTPClient` (HTTP/1.0, kein Chunked), in Blöcken von 4 KB
   in `esp_ota_write()`. Timeout 10 s ohne Daten. Größe muss zu `file.size` passen.
5. **Prüfen** (`esp_ota_end()` + Signatur). `dst = firmware`: `esp_ota_set_boot_partition()`.
   `dst = firmware.dryrun`: nicht aktivieren (auf dem ESP32 genügt es, die Boot-Partition nicht
   umzustellen), danach ersten Sektor löschen.
6. **Status melden:** `PATCH /update/<id>` mit `{ "status", "merker", "log" }`, bis zu 3 Versuche.
   `2` installiert, `1` fehlgeschlagen (neuer Versuch beim nächsten Poll, `merker` zählt),
   `-2` endgültig (nach 3 Versuchen oder bei nicht behebbaren Fehlern).
7. **Neustart** nach Status `2` (auch wenn das Melden scheitert, sonst würde jeder Reset das
   Image aktivieren), sobald eine Pause ist.

Zeiten (wie Candela): erster Poll 30 s nach `WL_CONNECTED`, danach Intervall `poll` vom Server,
begrenzt auf 5 min bis 24 h, ohne Angabe 1 h. Fehler: 5 min, verdoppelt bis zum Intervall.
`401`/`403`: nur noch alle 24 h. Nach `PUT /credentials` mit `ota_token` Poll nach 5 s.

### „Pause“ für Download und Neustart

Statt Candelas Serie gilt bei PagerCall: **FIFO leer und kein Ruf in Arbeit**
(`pagercall_idle()`, neu in `pagercall.cpp`). Weil der Neustart nichts löscht außer der FIFO,
reicht das. Konkret:

- Download und Flashen dürfen parallel zu Rufen laufen (der Task blockiert `loop()` nicht).
  Offen/zu prüfen: Flash-Löschen sperrt kurz den Cache beider Cores. Das UART-Telegramm (23 Byte)
  liegt komplett im Hardware-FIFO des UART und wird davon nicht verzerrt, höchstens der Abstand
  zwischen zwei Wiederholungen wird länger. Wenn der Test mit dem Pager zeigt, dass das stört:
  Download nur starten, wenn `pagercall_idle()`, und `esp_ota_write()` pausieren, solange ein Ruf
  läuft.
- Neustart nur, wenn `pagercall_idle()`. Währenddessen neue `/pagercall`-Anfragen mit `503`
  ablehnen (Flag `ota_restart_pending()`), damit die FIFO wirklich leer wird.

### Gegenseitiger Ausschluss

Ein gemeinsames Modul `ota_flash.cpp` hält die einzige laufende OTA-Sitzung
(`ota_flash_begin(size) / write() / finish(activate) / abort()`), geschützt durch ein
Flag/Mutex. `PUT /firmware` bekommt `409`, solange Souper flasht oder auf den Neustart wartet,
und umgekehrt startet Souper keinen Download, solange ein lokales Update läuft.

### Systeminfo im PATCH

Feldnamen wie SGW/Candela, damit das Souper-UI alle gleich anzeigt:

```json
{ "version": "1.000", "git": "958c9ee-dirty", "build": "2026-11-04 12:00", "name": "PagerCall_..",
  "chip": "esp32s3 rev=0", "flash": "8MB",
  "heap": { "free": 210000, "max_block": 110000 }, "wifi": { "ssid": "..", "bssid": "..", "rssi": -61 },
  "uptime": 12345, "boot": "1a2b3c4d" }
```

### Anzeige

- `GET /ota`: `{ "token": true, "url": "http://minlux.de/souper", "name": "PagerCall_..",
  "last_poll": 120, "next_poll": 3480, "result": "ok", "pending": [ 17 ], "state": "idle",
  "progress": 45 }`; `state`: `idle`, `update`, `download`, `report`, `restart`. Zeiten in
  Sekunden relativ zu jetzt.
- `POST /ota/poll`: sofort pollen.
- OLED: während Download „Update 45 %“, vor dem Neustart „Restart“ (optional, `oled.cpp`).

## Dateien

| Datei | Inhalt |
|---|---|
| `git_version.py` | pre-Script: `PAGERCALL_GIT_REV`, `PAGERCALL_BUILD_TIME` nur für `version.cpp` (wie Candela) |
| `sign_firmware.py` | post-Script: `firmware.signed.bin` mit `openssl dgst -sha256 -sign`, Gegenprobe mit `openssl dgst -verify` und dem Schlüssel aus `include/ota_key.h` |
| `include/version.h`, `src/version.cpp` | `PAGERCALL_VERSION_STR` (fest, von Hand gepflegt), Git-Revision, Build-Zeit |
| `include/ota_key.h` | öffentlicher Schlüssel als DER (ECDSA P-256) |
| `include/ota_signature.h`, `src/ota_signature.cpp` | Signaturprüfung (`mbedtls_pk_verify`), ohne Flash-Zugriff |
| `include/creds_patch.h`, `src/creds_patch.cpp` | Regeln von `PUT /credentials`, ohne NVS und HTTP |
| `test/host/run.sh`, `*_test.cpp` | Host-Tests für `creds_patch` und `ota_signature` |
| `include/ota_flash.h`, `src/ota_flash.cpp` | gemeinsame OTA-Sitzung, Sperre, Neustart in der Pause |
| `include/ota_update.h`, `src/ota_update.cpp` | `PUT /firmware` (überarbeitet) |
| `include/souper.h`, `src/souper.cpp` | Task, Poll, Download, Report, `GET /ota`, `POST /ota/poll`, Token |
| `include/credentials.h`, `src/credentials.cpp` | Token im NVS (`ota_token`), `creds_apply()` (Merge-Semantik, Prüfung), `PUT`/`DELETE /credentials` |
| `src/wifi_provisioning.cpp` | Feld „Update-Token“, `PUT`/`DELETE /credentials` auch im Portal |
| `src/pagercall.cpp` | `pagercall_idle()`, `503` während Neustart ansteht |
| `src/main.cpp` | Routen, `souper_begin()` in `enter_normal()`, Neustart und Anzeige in `loop()` |
| `src/oled.cpp` | Fortschritt des Downloads, „restarting“ |
| `platformio.ini` | `extra_scripts`, `SOUPER_URL`, Testvariante |
| `test/souper_mock.py` | aus Candela übernommen und angepasst (Pfade, Name) |
| `.gitignore` | `signing/` |
| `README.md` | Abschnitte „Update per WLAN“ und „Update per Update-Server“ |

## Umsetzung in Schritten

1. Version: `PAGERCALL_VERSION_STR` in `version.h`, `git_version.py`, `version.cpp`,
   `GET /version` (`{ "version": "1.000", "git": "..", "build": ".." }`).
2. Schlüsselpaar erzeugen, `ota_key.h`, `sign_firmware.py`. Prüfen: `pio run` erzeugt
   `firmware.signed.bin`, ohne Schlüssel nur `firmware.bin`.
3. `ota_flash.cpp` mit Signaturprüfung, `PUT /firmware` darauf umbauen und Route aktivieren.
4. `PUT`/`DELETE /credentials` in beiden Betriebsarten, Token im NVS, Portal-Feld.
5. `souper.cpp`: Task, Poll mit Systeminfo, Intervall/Backoff, `GET /ota`, `POST /ota/poll`.
6. Update holen, flashen, prüfen, melden, `dryrun`, Neustart in Pause, `pagercall_idle()`.
7. `souper_mock.py`, README.

## Tests

Auf dem Gerät (ohne Hardware nur Build und `sign_firmware.py`-Gegenprobe):

- `PUT /firmware`: signiert → neue Version unter `/version`; unsigniert / falscher Schlüssel /
  RSA-signiert / Image nach dem Signieren verändert → `403`, alte läuft weiter; abgebrochener Upload → alte läuft weiter; zweiter Upload parallel
  → `409`.
- Gegen `souper_mock.py` und ein lokales Souper: gültiges Image → Status `2`, neue Version im
  nächsten Poll; falsche Signatur → `1`, nach 3 Versuchen `-2`; `firmware.dryrun` → `2`, kein
  Neustart, alte Firmware auch nach Reset; `dst = spiffs` → sofort `-2`; WLAN-Abbruch im Download
  → `1`; falscher Token → `401`, Anzeige in `GET /ota`.
- Rufe während des Downloads: Pager klingelt weiterhin zuverlässig; Ruf kurz vor Neustart wird
  noch vollständig gesendet.

Fertig, wenn ein in Souper geplantes Update ohne Eingriff am Gerät installiert wird, Souper Status
`2` und danach die neue Version anzeigt, und kein Ruf verloren geht.

## Offene Entscheidungen

- App-Rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, neues Image erst nach erfolgreichem
  Start als gültig markieren): hängt am vorkompilierten Bootloader von Arduino, zurückgestellt.
  Bis dahin gilt wie bei Candela: jede Version einmal per Kabel testen.
- `PUT /firmware` und `PUT`/`DELETE /credentials` sind ungeschützt (jeder im WLAN). Firmware nur signiert,
  der Token kann überschrieben, aber nicht gelesen werden. Reicht das?
- HTTPS später?
