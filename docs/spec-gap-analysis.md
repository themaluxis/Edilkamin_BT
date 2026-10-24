# Analyse d'écart — Cahier des charges « Edilkamin_BT Enhanced » v1.0

Ce document confronte le cahier des charges (CDC) du 24 octobre 2026 au code
réel du dépôt, puis indique ce que livrent les itérations réalisées (périmètre
P0 : v1.1 *Reliability* + v1.2 *Diagnostics*, puis v1.3 *Management*,
v1.4 *OTA*, v1.5 *Home Assistant* et v2.0 *Advanced Control*) et ce qui
reste à faire.

## 1. Vérification de l'audit (CDC §2) sur le firmware d'origine (commit `714ce29`)

| Constat du CDC | Vérifié dans le code ? | Détail |
|----------------|------------------------|--------|
| Niveau de debug défini à la compilation | **Oui** | `#define DEBUG 1` en tête de `main.cpp`, sortie série ou Telnet non authentifié |
| Pas de mécanisme OTA | **Oui** | `partitions.csv` : un seul slot `app0` (0x250000), pas de code OTA |
| Publication excessive de `bluetooth/state` | **Oui, et pire** | dans `loop()`, le bloc `else if (!bleConnected && doBtConnect)` relançait un scan **et** publiait `ON` (retained) à **chaque itération de boucle** tant que le poêle n'était pas connecté — c'est la source du flot d'`ON` observé |
| File de 10 éléments sans signalement de saturation | **Oui** | `queueBtCommand()` sort silencieusement de la boucle quand la file est pleine |
| Identifiants et topics codés en dur | **Oui** | `322707E4` apparaît ~90 fois dans `main.cpp` / `Helper.h` |
| Données périmées non gérées | **Oui** | `availability/state` publié `ONLINE` retained à la première connexion BLE et **jamais** repassé `OFFLINE` (ligne commentée), pas de Last Will, pas d'horodatage |
| Message MQTT trompeur sur échec BLE | **Oui** | `"Failed to connect to the MQTT-Server; there is nothing more we will do."` affiché quand `connectToServer()` (BLE) échoue |
| Conversion de température fragile | **Oui** | `msg.replace(".", ""); msg.toInt()` : `17` → 1,7 °C, `17.50` → 175 °C |
| Watchdog BLE avec Bluetooth désactivé | **Oui** | le redémarrage après 5 min sans connexion BLE ne tenait pas compte de `doBtConnect = false` |
| Redémarrages réseau | **Oui** | redémarrage après 30 s sans broker MQTT, et `mqttReconnect()` (bloquant) appelé à chaque itération |

Constats supplémentaires relevés pendant l'audit :

* **Sécurité (CDC §8)** : les documents Discovery contenaient `"retain": true`, donc Home Assistant publiait `hvac_mode/set` en *retained*. Au redémarrage de l'ESP32, le broker rejouait le dernier `heat`/`off`, qui était exécuté → allumage possible du poêle sans commande explicite. **Corrigé** (voir 2.6).
* **Relance aveugle** : en cas de timeout, le dernier paquet (y compris un ordre d'allumage) était réémis une fois sans vérification.
* **Aucune vérification des trames** : ni CRC, ni correspondance entre la réponse et la commande en cours ; seul `payload[1]` (3 ou 6) était testé.
* **Température ambiante** : `(int16_t)(0 << 8) + payload[4]` ne décodait que l'octet bas → lectures > 25,5 °C fausses. Le nouveau décodeur lit 16 bits et ne retombe sur l'octet bas que si la valeur est invraisemblable (> 60 °C) — **à confirmer sur matériel**.
* **Copies non bornées** : `strcpy(mqtt_server, json["mqtt_server"])` plantait si la clé était absente (pointeur nul).
* **Deux lectures du même registre** : `READ_POWER` et `READ_FAN` interrogeaient tous deux `0x0529` ; fusionnées en une lecture.
* **NTP** : `configTime()` suivi immédiatement de `getLocalTime()` sans attente ; décalage fixe sans heure d'été.

## 2. Couverture des exigences par cette itération

Légende : ✅ livré · 🟡 partiel · ⬜ non commencé (prévu).

### F01 — Gestion Bluetooth (P0) ✅
Machine à états explicite `DISABLED / IDLE / SCANNING / CONNECTING / CONNECTED / COMMUNICATING / ERROR` (`ble/BleSessionPolicy`, 100 % testée nativement), publication des transitions uniquement, backoff exponentiel 5 s → 120 s configurable, cycle session/pause conservé et configurable (mode continu possible), priorité aux commandes utilisateur avec écart minimal de 3 s, adresse MAC cible configurable (portail ou `stove_mac`), aucun watchdog quand le Bluetooth est désactivé, watchdog BLE optionnel et désactivé par défaut. Distinction « Bluetooth activé » (`bluetooth/state`) / « liaison réelle » (`ble/connected`) / « données valides » (`data_valid/state`).

### F02 — Fiabilisation des commandes (P0) ✅
Cycle `RECEIVED → VALIDATED → QUEUED → SENT → ACKNOWLEDGED → VERIFIED` + `REJECTED / TIMEOUT / FAILED` (`commands/*`, `stove/StoveController`). Validation stricte des payloads et conversion numérique de la consigne (`17`, `17.0`, `17.5` acceptés ; comma, lettres, hors plage refusés). Vérification des trames : longueur, clé fixe, padding PKCS#7, CRC16, code fonction, correspondance registre/longueur avec la commande en cours, exceptions modbus. Priorités User > Verify > Poll, déduplication documentée, débordement compté et signalé (`FAILED command queue full`, `queue_overflows`). Relecture systématique après écriture ; **aucune réémission automatique d'une écriture** (les lectures sont réessayées une fois). Résultat observable de chaque commande sur `command/result`.

### F03 — Debug et journalisation (P0) ✅
Niveaux `OFF…TRACE` modifiables à chaud par MQTT (entité *select*) et depuis la page Web, retour automatique après 30 min pour `DEBUG/TRACE`, journal circulaire (48 lignes) horodaté, export par MQTT (`log_dump`) et téléchargement texte depuis la page Web (onglet *Log*), flux `log`, compteurs d'erreurs, suppression des messages trompeurs, aucun secret journalisé, Telnet **désactivé par défaut**.

### F04 — Supervision ESP32 (P0) ✅
Document `diagnostics/state` + 19 entités HA (RSSI, état BLE réel, tentatives, déconnexions, requêtes OK/échouées, dernière communication valide, dernière erreur, uptime, mémoire, nombre et cause des redémarrages, version, file de commandes). Compteurs réinitialisables ; seuls le compteur de boot et la cause de redémarrage sont persistés (une écriture NVS par démarrage, pas d'usure).

### F05 — Configuration et maintenance (P1) ✅ (v1.3)
Interface Web locale (`http://edilkaminble.local/`, mDNS) servie depuis la flash (page gzip 7,8 Ko, aucune ressource externe) : informations système, édition de **tous** les paramètres (Wi-Fi, MQTT, poêle/MAC BLE, cycle Bluetooth, intervalles, niveaux de log, administration), journal circulaire, export/import JSON **sans secrets**, redémarrage, reset usine avec confirmation textuelle (`ERASE`), version/partition/aptitude OTA. Le portail captif reste le mode secours (ouvert 3 min à chaque démarrage sans Wi-Fi) et permet de définir le mot de passe admin.
Sécurité : authentification **Digest** obligatoire (seul `MD5(user:realm:mdp)` est stocké), administration verrouillée tant qu'aucun mot de passe n'est défini, limitation anti-force-brute (5 échecs/min → 30 s), validation stricte et bornes de longueur (corps ≤ 6 Ko, champs bornés), mot de passe MQTT jamais renvoyé ni exporté, service désactivable (`web_enabled`, interrupteur HA *Web administration*), HTTP en clair réservé au LAN (documenté). Voir `docs/web-ui.md`.

### F06 — OTA (P1) ✅ (v1.4)
Table de partitions à deux slots (`app0`/`app1` 0x1E0000 chacun, `otadata`, `littlefs` 192 Ko, `nvs` à son offset d'origine) ; firmware **1 316 161 octets** (66,9 % d'un slot) sur Arduino 2.0.17. Téléversement depuis l'onglet *Maintenance* (authentifié, Digest) ; vérifications avant validation : en-tête ESP32 (magic, identifiant de puce, descripteur d'application), marqueur de projet `EDILKAMIN_BT_FW_V1:<version>` embarqué dans chaque build (refuse les images étrangères et les builds < v1.4), vérification ESP-IDF de l'image (checksum + SHA-256), taille du slot. Configuration conservée (LittleFS non touché par l'OTA). **Rollback automatique** : la nouvelle image démarre en *pending verify* et se valide à la connexion MQTT, après 10 min, ou lors d'un redémarrage volontaire ; tout crash/reset avant validation ramène le bootloader sur l'ancien slot (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` du core 2.0.17, `verifyRollbackLater()` surchargé). Rollback manuel depuis la page. Version publiée dans MQTT (`fw`, `ota_slot`, `ota_pending`). Procédure de récupération documentée (`docs/ota.md`), y compris le flash USB unique qui installe la nouvelle table (export/import de la configuration, Wi-Fi conservé) et la correction du manifeste WebFlash (`boot_app0.bin`).

### F07 — Fiabilisation système (P1) 🟡
Copies bornées (`copyStr`), schéma versionné + migration v1→v2→v3 (codec JSON unique `ConfigJson` testé nativement), écriture atomique de `config.json` (fichier temporaire + rename), erreurs LittleFS journalisées, attente NTP (8 s) et indicateur `ntp_synced`, TZ POSIX avec heure d'été, plus aucun redémarrage sur perte MQTT, watchdog Wi-Fi long (15 min) et cause persistée, changement de Wi-Fi depuis la page Web avec repli automatique sur le portail. *Reste* : portail non bloquant pendant les coupures réseau prolongées (le portail WiFiManager bloque 3 min au démarrage).

### HA01 — Disponibilité (P0) ✅
Last Will sur `availability/state`, `ble/connected` distinct, `last_update/state`, détection de péremption (`data_max_age_s`, contrainte ≥ session + pause + 60 s), entités poêle en `availability_mode: all`, publications dédupliquées, `optimistic` retiré.

### HA02 — Discovery (P1) ✅ (v1.5)
Fabricant/modèle/version/`hw_version`, `configuration_url` vers la page Web, `connections` (MAC ESP32 + adresse Bluetooth du poêle), catégories *diagnostic*/*config*, classes et unités, `optimistic` et `retain` retirés. Identité : les modules mis à jour conservent `322707E4` (continuité des `unique_id`), les modules neufs reçoivent un identifiant dérivé de la MAC ESP32 ; un changement de `device_id`/`topic_prefix` déclenche à la connexion MQTT suivante la suppression de tous les documents Discovery et topics retenus de l'ancienne identité (`ha_published_*`). Nettoyage des documents obsolètes : interrupteur *AirKare* v0.x supprimé, interrupteurs désactivés par profil supprimés, bouton *Republish discovery*.

### HA03 — Entités (P1) ✅ (v1.5)
Toutes les entités du tableau sont exposées, y compris *Puissance* en `select` séparé (`power/set` P1–P5, `power/state`) en plus des presets du climate. Les fonctions dépendantes du modèle sont **configurables** (`feat_relax`, `feat_standby`, `feat_chrono`, page Web → *Stove features*) : entité retirée de HA et commande refusée quand la fonction est désactivée. La détection automatique par modèle reste du ressort de F08 (v2.0).

### HA04 — Commandes prédictibles (P1) ✅
Séquence HA → validation → envoi → écho → relecture → publication confirmée ; en cas d'échec l'ancienne valeur reste la référence et `command/result` + `last_error` expliquent pourquoi.

### HA05 — Thermostat sur sonde externe (P2) ✅ mode A (v2.0)
Module pur `ExternalThermostat` (7 tests) : consigne externe + hystérésis configurable, le poêle garde sa propre régulation et ses sécurités (seule la consigne est déplacée, jamais `heat`/`off`), bornes profil et ±4 °C autour de la cible, cadence minimale entre ajustements, validité de la mesure (par défaut 10 min) avec **mode de secours** (restauration unique de la consigne d'origine puis attente), aucune action après redémarrage tant qu'aucune mesure fraîche n'est reçue. Entités HA : interrupteur, `number` consigne, `number` mesure (alimenté par une automatisation), capteur d'état. Mode B évalué et non retenu (justification dans `docs/external-thermostat.md`).

### F08 — Profils de poêle (P2) ✅ configurable (v2.0)
Registre de profils (`generic` validé, `custom` entièrement paramétrable : plage de consigne, niveaux de puissance et de ventilation, mode auto, nombre de ventilateurs 1–3) appliqué au climate, au `select` puissance, au thermostat externe et à la validation des commandes ; application à chaud. Pas de preset par modèle faute de données validées : procédure de contribution documentée (`docs/compatibility.md`).

### F09 — Fonctions The Mind supplémentaires (P2) 🟡 étude + expérimental (v2.0)
Étude dans `docs/the-mind-registers.md` : registres validés / expérimentaux / non supportés et pourquoi (AirKare : l'expérience antérieure écrivait chrono/automatique/format de température, aucun registre AirKare identifié ; tables chrono trop grandes pour les trames de 32 octets ; commandes de service volontairement non exposées). Lecture **expérimentale** (option `exp_registers`, désactivée par défaut) d'alarme `0x02E6`, granulés restants `0x02EC`, version contrôleur `0x0032`, ventilateurs 2/3 `0x052A`, publiée en valeurs brutes (catégorie diagnostic). Mot d'état brut `status_raw` toujours publié (§8 : aucune alarme masquée). **Outil de lecture de registre** (Web + MQTT, fonction 3 seulement, 1–3 registres, 1 lecture / 2 s) pour valider sur matériel sans reflasher. Aucune écriture nouvelle.

### §8 Sécurité ✅
Commandes retenues effacées avant abonnement ; aucune commande émise au démarrage ; pas de réémission d'ordre sensible ; états d'alarme non masqués (statut inconnu journalisé, dernier statut valide conservé) ; interfaces de debug fermées par défaut ; comportement explicite en cas d'indisponibilité MQTT/BLE ; interface d'administration authentifiée (Digest, hash seul stocké, verrouillée sans mot de passe, anti-force-brute, désactivable) et dépourvue de toute commande vers le poêle (la page ne pilote pas le poêle, uniquement l'ESP32).

### §9 Tests 🟡
76 tests unitaires natifs (trames, CRC, conversions, validation MQTT, file, topics, machine à états BLE, cycle de vie des commandes, configurations invalides, codec JSON de configuration et politique des secrets, logger, JSON de diagnostic, MD5 et Digest RFC 2617 (vecteur de référence), nonces, limitation de connexion, en-tête d'image OTA et scanner de marqueur, thermostat externe, profils, outil de lecture de registre et sondage expérimental). Les scénarios d'intégration du §9.2 sont décrits dans `docs/validation.md` et **restent à exécuter sur un poêle réel**.

## 3. Choix et écarts à valider avec le propriétaire du projet

1. **`optimistic` retiré** : les interrupteurs HA « rebondissent » jusqu'à confirmation par le poêle (quelques secondes). C'est le comportement demandé par HA01/HA04 mais il change le ressenti.
2. **Watchdog BLE désactivé par défaut** (`ble_reboot_after_min = 0`) alors que l'auteur d'origine redémarrait après 5 min. Le CDC interdit les redémarrages répétés ; un redémarrage après 60 min reste activable.
3. **Mode HVAC pendant `Shutdown`/`Extinction`** : conservé tel quel (ambigu entre arrêt demandé et pause standby) ; `hvac_action` passe à `idle`.
4. **Température ambiante** : décodage 16 bits avec repli octet bas si > 60 °C — à confirmer sur un relevé > 25,5 °C.
5. **Validité des données = 360 s** par défaut ; à ajuster si les sessions sont espacées davantage.
6. **Niveau de log MQTT** par défaut `WARN` ; `INFO` est plus bavard mais utile pendant la phase de validation.
7. **Horodatage BLE sans NTP** : les trames sont envoyées même si l'heure n'est pas synchronisée (lecture seule, aucune action sensible) ; à surveiller si le poêle refuse ces trames.

## 4. Points à vérifier sur matériel avant publication

* Comportement réel de la fenêtre « pairing » et du reconnect après coupure du poêle.
* Délai écho → relecture : si le poêle met du temps à appliquer `0x0525`, un échec de vérification transitoire apparaîtrait (`FAILED read back …`) ; le cas échéant ajouter un court délai avant la relecture de vérification.
* Lecture de `0x02E9` au-dessus de 25,5 °C.
* Charge mémoire : `min_free_heap` sur 48 h (attendu > 60 Ko).
* Stabilité NimBLE avec scan actif pendant 10 s toutes les 2,5 min (RSSI, interférences Wi-Fi).

## 5. Plan proposé pour la suite

| Version | Contenu | Dépend de |
|---------|---------|-----------|
| v1.3 Management | **livré** : `WebServer` du core (synchrone, pas de dépendance supplémentaire), authentification Digest, édition complète, journal, export/import, Wi-Fi, redémarrage, reset usine. Firmware : 1 307 677 octets (53,9 % du slot actuel), +63 Ko | — |
| v1.4 OTA | **livré** : voir F06. Reste à faire sur matériel : scénarios O1–O8 de `docs/validation.md` (dont le rollback réel sur une image qui plante) | v1.3 |
| v1.5 Home Assistant | **livré** : voir HA02/HA03. Non retenu : entité HA `update` (pas de serveur de versions en local) ; `number` consigne externe reporté en v2.0 avec HA05. À valider sur matériel : scénarios H1–H7 | v1.4 |
| v2.0 | **livré** : voir HA05 / F08 / F09. À valider sur matériel : T1–T7 et R1–R5 de `docs/validation.md` | v1.5 |
| Suite | portail non bloquant (F07), presets de profils par modèle et sémantique des registres expérimentaux dès retours matériels, campagne de 48 h (§9.3) | retours terrain |
