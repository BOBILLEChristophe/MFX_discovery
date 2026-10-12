# MFX Discovery — ESP32 et bus CAN Märklin

**Version 0.4** — Christophe BOBILLE

MFX Discovery est un programme pour **ESP32** permettant de rechercher des décodeurs de locomotives **MFX** sur un réseau Märklin, de relever leur **identifiant unique (UID) sur 32 bits** et de leur attribuer une **adresse courte (SID)** par des échanges sur le **bus CAN Märklin à 250 kbit/s**.

Il s'agit d'un outil particulièrement utile pour connaitre rapidement l'UID à 32 bits d'un décodeur MFX.

## Matériel nécessaire

- Un **ESP32** compatible avec la bibliothèque `ACAN_ESP32`.
- Un **transceiver CAN MCP2562** (ou une interface CAN compatible avec les niveaux électriques de l'ESP32).
- Un accès au **bus CAN Märklin** d'une installation compatible.
- Un ordinateur avec **PlatformIO** et un moniteur série.

### Raccordement ESP32 ↔ transceiver CAN

| Signal | ESP32 |
| --- | --- |
| CAN RX (sortie RXD du transceiver) | GPIO **22** |
| CAN TX (entrée TXD du transceiver) | GPIO **23** |
| Masse | GND |

Raccorder ensuite `CANH` et `CANL` au bus CAN. Prévoir une alimentation adaptée au transceiver, une masse de référence et la terminaison CAN appropriée à la topologie du bus. **Vérifier la compatibilité des niveaux logiques du module utilisé avec l'ESP32 (3,3 V)** ; les modules MCP2562 ne sont pas tous câblés de la même manière.

> **Attention :** ne jamais raccorder directement les GPIO de l'ESP32 aux lignes CANH et CANL. Un transceiver CAN est indispensable.

## Installation

1. Cloner le dépôt :

   ```bash
   git clone https://github.com/BOBILLEChristophe/MFX_discovery.git
   ```

2. Ouvrir le dossier dans **Visual Studio Code / PlatformIO**.
3. Vérifier l'environnement et la carte sélectionnés dans `platformio.ini`.
4. Compiler et téléverser `src/main.cpp` sur l'ESP32.
5. Ouvrir le moniteur série à **115200 bauds**.

Le programme utilise la bibliothèque [ACAN_ESP32](https://github.com/pierremolinaro/acan-esp32) pour la communication CAN.

## Fonctionnement

### 1. Initialisation du CAN

L'ESP32 initialise le contrôleur CAN avec les paramètres suivants :

| Paramètre | Valeur |
| --- | --- |
| Débit | **250 000 bit/s** |
| GPIO RX | **22** |
| GPIO TX | **23** |
| Hash CAN émetteur | `0x2F39` |

Si l'initialisation CAN échoue, le programme affiche un code d'erreur et s'arrête.

### 2. Séquence de démarrage

La version 0.4 émet successivement, avec un intervalle d'environ une seconde entre les étapes :

1. Une commande système **STOP**.
2. Une commande **Bootloader** (`0x1B`, avec sous-valeur `0x11`).
3. Une commande système de **compteur MFX** (`0x09`).
4. Une commande **Verify globale** (UID et SID à zéro).
5. Une commande système **GO**.
6. Une commande **Discovery MFX** (`0x01`).

Cette séquence est celle implémentée dans le programme v0.4. Certains paramètres historiques, notamment la valeur `0x20` envoyée lors de Discovery et certains détails du démarrage, restent signalés dans le code comme **à valider au regard de la spécification CAN Märklin**.

> **Précaution :** les commandes STOP et GO agissent sur l'alimentation / l'exploitation de la voie. Réaliser les essais dans des conditions sûres, sans circulation incontrôlée de locomotives.

### 3. Découverte des locomotives

Lorsqu'une réponse Discovery est reçue, le programme relève l'**UID de quatre octets** du décodeur MFX.

Si cet UID n'est pas encore présent dans sa liste, le programme crée une entrée et lui attribue un **SID**. Dans la version actuelle, le premier SID attribué est **5**, puis le compteur augmente pour chaque nouvelle locomotive.

La liste peut contenir jusqu'à **40 locomotives**.

### 4. Attribution et vérification du SID

Pour chaque nouvelle locomotive, le programme suit une machine à états :

```text
Nouvelle locomotive
        |
        v
    Envoi BIND
        |
        | 20 ms
        v
   Envoi VERIFY
        |
        +---- Réponse conforme ----> CONFIRMÉE
        |
        +---- Pas de réponse après 1 s
                    |
                    +---- Nouvel essai (3 maximum)
                    |
                    +---- ÉCHEC
```

Le programme compare l'UID et le SID reçus avec ceux attendus. Une locomotive déjà connue qui se réannonce après confirmation ou échec peut être soumise à une nouvelle tentative avec **le même SID**.

L'émission des commandes Bind et Verify est non bloquante dans la boucle principale ; les temporisations sont gérées avec `millis()`.

## Commandes du moniteur série

| Commande | Action |
| --- | --- |
| `d` | Relancer une découverte MFX |
| `l` | Afficher la liste des locomotives détectées, leurs UID, SID, Loc-ID et états |

Exemple **illustratif** de ligne affichée (valeurs fictives) :

```text
[ 0] UID 12345678  SID 5  Loc-ID 0x4005  confirmee
```

## UID, SID et Loc-ID CAN : quelle différence ?

- **UID** : identifiant MFX sur **32 bits** du décodeur, utilisé pendant les échanges de découverte et d'association.
- **SID** : adresse courte attribuée au décodeur MFX dans le cadre de cette procédure.
- **Loc-ID CAN** : identifiant de locomotive utilisé par les commandes de pilotage CAN Märklin.

Pour une locomotive MFX, le programme calcule :

```cpp
LocID = 0x4000 | SID;
```

Exemple : pour `SID = 5`, le Loc-ID est `0x4005`.

Le code rappelle également les bases d'adressage suivantes :

| Protocole | Construction du Loc-ID CAN |
| --- | --- |
| MFX | `0x4000 | SID` |
| DCC | `0xC000 | adresse` |
| Motorola (MM) | `0x0000 | adresse` |

## Nouveautés de la version 0.4

Par rapport à la v0.3 :

- Correction de la commande **Discovery** : `0x01` au lieu de `0x00`.
- Extraction correcte de la **commande CAN** et du **bit réponse**, indépendamment des bits de priorité.
- Initialisation du tableau de locomotives et exclusion des emplacements libres lors des recherches.
- Compteur de SID sur **16 bits** avec encodage sur deux octets.
- Machine à états **Bind → Verify → confirmation**, avec au maximum trois tentatives.
- Possibilité de relancer Discovery depuis le moniteur série avec `d`.
- Arrêt du programme en cas d'échec d'initialisation CAN.
- Suppression des `delay()` dans le traitement des trames CAN et la machine à états des locomotives.
- Suppression de l'ancienne classe `CanMsg`, pour éviter un conflit avec un autre projet.
- Configuration des broches CAN : **GPIO 22 (RX)** et **GPIO 23 (TX)**.

## Exemple de recherche
Discovery MFX lance
Nouvelle loco : UID FFE9B194  SID 5  (Loc-ID CAN 0x4005)
Pas de reponse Verify, nouvel essai : UID FFE9B194
Pas de reponse Verify, nouvel essai : UID FFE9B194
[!] Pas de confirmation apres 3 essais : UID FFE9B194
Loco deja connue, nouvelle liaison : UID FFE9B194  SID 5
[!] Verify : SID different pour UID FFE9B194 (recu 7, attendu 5)
[!] Verify : SID different pour UID FFE9B194 (recu 7, attendu 5)
Pas de reponse Verify, nouvel essai : UID FFE9B194
Loco confirmee : UID FFE9B194  SID 5  (Loc-ID CAN 0x4005)

## Limites actuelles

- Les locomotives et leurs SID sont conservés **en mémoire vive uniquement** : ils ne sont pas enregistrés dans une mémoire persistante et sont perdus lors d'un redémarrage de l'ESP32.
- Le programme redémarre son compteur de SID à **4** à chaque lancement ; le premier SID attribué lors d'une nouvelle session est donc **5**.
- La liste est limitée à **40 locomotives**.
- Le programme est un **outil de découverte et d'attribution**, pas une centrale numérique complète ni une interface de conduite de locomotives.
- La réussite de l'association dépend de la centrale, des décodeurs et des échanges réellement observés sur le bus ; le logiciel signale explicitement les échecs de confirmation.

## Références

- [Dépôt MFX Discovery](https://github.com/BOBILLEChristophe/MFX_discovery)
- [Bibliothèque ACAN_ESP32](https://github.com/pierremolinaro/acan-esp32)
- [Documentation du protocole CAN Märklin (CS2)](https://streaming.maerklin.de/public-media/cs2/cs2CAN-Protokoll-2_0.pdf)

## Auteur

**Christophe BOBILLE** — [GitHub : BOBILLEChristophe](https://github.com/BOBILLEChristophe)

Projet expérimental consacré à la découverte MFX et aux échanges CAN Märklin avec ESP32.
