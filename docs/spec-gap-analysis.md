# Analyse d'écart — Cahier des charges « Edilkamin_BT Enhanced » v1.0

Ce document confronte le cahier des charges (CDC) du 24 octobre 2026 au code
réel du dépôt, puis indique ce que livre cette première itération (périmètre
P0 : v1.1 *Reliability* + v1.2 *Diagnostics*) et ce qui reste à faire.

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

### F03 — Debug et journalisation (P0) 🟡
Niveaux `OFF…TRACE` modifiables à chaud par MQTT (entité *select*), retour automatique après 30 min pour `DEBUG/TRACE`, journal circulaire (48 lignes) horodaté, export par MQTT (`log_dump`), flux `log`, compteurs d'erreurs, suppression des messages trompeurs, aucun secret journalisé, Telnet **désactivé par défaut**. *Reste* : l'interface Web locale (v1.3) ; l'export texte passe par MQTT en attendant.

### F04 — Supervision ESP32 (P0) ✅
Document `diagnostics/state` + 19 entités HA (RSSI, état BLE réel, tentatives, déconnexions, requêtes OK/échouées, dernière communication valide, dernière erreur, uptime, mémoire, nombre et cause des redémarrages, version, file de commandes). Compteurs réinitialisables ; seuls le compteur de boot et la cause de redémarrage sont persistés (une écriture NVS par démarrage, pas d'usure).

### F05 — Configuration et maintenance (P1) 🟡
Schéma de configuration v2 validé et borné, portail enrichi (TZ, MAC, durées BLE), redémarrage par MQTT, reset usine par bouton. *Reste* : interface Web d'administration authentifiée, export/import, édition des paramètres avancés sans reflash (v1.3).

### F06 — OTA (P1) ⬜
Non commencé. Mesures utiles : firmware actuel **1 860 861 octets** (76,7 % du slot de 2,4 Mo). Sur 4 Mo, une table `nvs 0x5000 / otadata 0x2000 / app0 0x1E0000 / app1 0x1E0000 / littlefs 0x30000` tient (2 × 1,97 Mo + 192 Ko), avec ~110 Ko de marge par slot. Pistes si la marge devient insuffisante : désactiver les rôles NimBLE *peripheral/broadcaster*, remplacer WiFiManager par le futur serveur Web, retirer TelnetStream.

### F07 — Fiabilisation système (P1) 🟡
Copies bornées (`copyStr`), schéma versionné + migration v1→v2, écriture atomique de `config.json` (fichier temporaire + rename), erreurs LittleFS journalisées, attente NTP (8 s) et indicateur `ntp_synced`, TZ POSIX avec heure d'été, plus aucun redémarrage sur perte MQTT, watchdog Wi-Fi long (15 min) et cause persistée. *Reste* : portail non bloquant pendant les coupures réseau prolongées.

### HA01 — Disponibilité (P0) ✅
Last Will sur `availability/state`, `ble/connected` distinct, `last_update/state`, détection de péremption (`data_max_age_s`, contrainte ≥ session + pause + 60 s), entités poêle en `availability_mode: all`, publications dédupliquées, `optimistic` retiré.

### HA02 — Discovery (P1) 🟡
Fabricant/modèle/version, catégories *diagnostic*/*config*, classes et unités, `optimistic` et `retain` retirés, identifiants existants conservés. *Reste* : identité stable (série poêle / MAC ESP) avec migration, nettoyage des documents obsolètes (v1.5).

### HA03 — Entités (P1) 🟡
Toutes les entités listées sont exposées sauf *Puissance* en *select* séparé (reste via les presets du climate) ; les fonctions dépendantes du modèle passent par `StoveProfile` (un seul profil pour l'instant).

### HA04 — Commandes prédictibles (P1) ✅
Séquence HA → validation → envoi → écho → relecture → publication confirmée ; en cas d'échec l'ancienne valeur reste la référence et `command/result` + `last_error` expliquent pourquoi.

### HA05 / F08 / F09 (P2) ⬜
Non commencés ; `StoveProfile` et `Registers.h` préparent F08/F09.

### §8 Sécurité ✅
Commandes retenues effacées avant abonnement ; aucune commande émise au démarrage ; pas de réémission d'ordre sensible ; états d'alarme non masqués (statut inconnu journalisé, dernier statut valide conservé) ; interfaces de debug fermées par défaut ; comportement explicite en cas d'indisponibilité MQTT/BLE.

### §9 Tests 🟡
51 tests unitaires natifs (trames, CRC, conversions, validation MQTT, file, topics, machine à états BLE, cycle de vie des commandes, configurations invalides, logger, JSON de diagnostic). Les scénarios d'intégration du §9.2 sont décrits dans `docs/validation.md` et **restent à exécuter sur un poêle réel**.

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
| v1.3 Management | serveur Web (ESPAsyncWebServer ou WebServer core), auth basique, édition config complète, visualisation du journal circulaire, export/import JSON | — |
| v1.4 OTA | nouvelle table de partitions (ci-dessus), `Update` + rollback (`esp_ota_mark_app_valid_cancel_rollback` après connexion MQTT réussie), page de récupération | v1.3 |
| v1.5 Home Assistant | identité stable + migration des `unique_id`, nettoyage discovery, `select` puissance, `number` consigne externe | v1.3 |
| v2.0 | thermostat externe (mode A), profils par modèle, AirKare / ventilateurs 2-3 / alarmes après validation | v1.5 |
