English translation: LLL_COMMANDS_MANUAL.en.md

# LLL Plus — Manuel des commandes (firmware canonique v1)

> **Le guide complet de toutes les commandes.** Chaque commande : ce qu'elle fait, ses paramètres, un exemple, et les pièges à connaître.
> Toutes les commandes se tapent dans la console Mainsail/Fluidd (ou s'utilisent dans des macros). Toutes exigent `LANE=` : le nom de la carte visée (`lll` ou `lll2`) — sauf `LLL_DETECT_LOADED`, commande globale.

---

## Avant de commencer — les 4 concepts

**🛤️ La lane** — chaque carte LLL Plus = une « voie » de filament. Chaque lane est indépendante : ses réglages, son état, ses erreurs. `LANE=lll` désigne la première, `LANE=lll2` la seconde.

**🎭 Le rôle** — ce que la lane a le droit de faire en ce moment :
| Rôle | Comportement |
|---|---|
| `active` | Buffer complet : pousse et rembobine tout seul selon la tension du bras (c'est la lane qui imprime) |
| `parked` | En attente : **ne pousse jamais** le filament (pour ne pas encombrer le Y), mais **accompagne un rembobinage** si on tire le filament (déchargement possible) |
| `standby` | Totalement figée : le moteur ne bouge sous aucun prétexte automatique |

**🎚️ Le mode** — le niveau d'automatisation du feed (pour le rôle `active`) :
| Mode | Comportement |
|---|---|
| `auto` | Comportement historique : la carte gère tout, en continu |
| `semi_auto` | La carte n'a le droit qu'à UN cycle de recharge à chaque `LLL_ARM` — vous validez chaque recharge |
| `semi_manual` | Aucun démarrage automatique ; un feed commandé (`LLL_RUN`) s'arrête tout seul à la position d'équilibre |
| `manual` | Rien d'automatique — uniquement vos commandes |

**🧩 Les fonctions** — le firmware est modulaire : 11 fonctions qu'on allume/éteint à chaud (`LLL_ENABLE`). Par défaut l'essentiel est allumé, les fonctions « bench » (LED, ADC, DIAG…) sont éteintes.

---

## 📊 Consulter l'état

### `LLL_QUERY LANE=<lane>`
**Interroge la carte et affiche son état complet.** Rafraîchit aussi l'état visible par les macros (`printer["lll_buffer <lane>"]`).
```
LLL_QUERY LANE=lll
→ LLL lll: role=active mode=auto error=0 ecode=none feeding=0 running=0 arm=0 present=1 diag=0 cfgerr=0
```
| Champ | Signification |
|---|---|
| `role` / `mode` | rôle et mode courants (voir concepts) |
| `error` | 1 = une erreur est verrouillée, le moteur est stoppé |
| `ecode` | la CAUSE : `none` / `feed_timeout` (poussée >60 s = bourrage probable) / `back_timeout` (rembobinage anormalement long) / `diag_fault` (défaut driver) / `host_alive` (perte de contact hôte) / `clear_pending` (déblocage demandé, en attente que le défaut driver retombe) |
| `feeding` | 1 = un feed avant est en cours (automatique ou `LLL_RUN` fwd commandé) |
| `running` | 1 = le moteur est réellement en mouvement (toute cause) |
| `arm` | position du bras, bit par capteur : 4=zone POUSSER, 2=zone ÉQUILIBRE, 1=zone REMBOBINER, 0=entre deux |
| `present` | 1 = filament détecté à l'entrée de la carte |
| `diag` | 1 = ligne DIAG du driver active (si fonction diag allumée) |
| `cfgerr` | 1 = une commande de config a été refusée depuis le dernier déblocage (regardez la console) |

---

## 🎭 Rôles

### `LLL_ROLE LANE=<lane> ROLE=active|parked|standby`
**Change le rôle de la lane.** Persistant (survit au redémarrage).
```
LLL_ROLE LANE=lll2 ROLE=parked
```
💡 En usage multicolor normal, ne vous en servez pas directement : `SET_ACTIVE_LANE` gère les rôles des deux lanes d'un coup (macro fournie : `config/lll_macros.cfg.example`).

### `LLL_SET_IDLE LANE=<lane> VALUE=0|1`
**Alias de compatibilité** (utilisé par les macros existantes) : `VALUE=1` → parked, `VALUE=0` → active. Même effet que `LLL_ROLE`.

---

## 🎚️ Modes et fonctions

### `LLL_MODE LANE=<lane> LEVEL=auto|semi_auto|semi_manual|manual`
**Règle le niveau d'automatisation du feed.** Persistant.
```
LLL_MODE LANE=lll LEVEL=semi_auto
```
⚠️ Changer de mode arrête immédiatement tout feed autonome en cours (un `LLL_JOG`/`LLL_RUN` host, lui, continue) ; le mode change ce que la carte a le droit de **démarrer**.

### `LLL_ARM LANE=<lane>`
**En mode `semi_auto` uniquement : autorise UN cycle de recharge.** La carte poussera à la prochaine demande du bras, puis se remettra en attente une fois l'équilibre atteint. Refusé (erreur explicite) dans les autres modes.

### `LLL_ENABLE LANE=<lane> FUNC=<fonction> EN=0|1`
**Allume/éteint une fonction du firmware, à chaud.** Persistant.
| FUNC | Rôle | Défaut |
|---|---|---|
| `feed` | le buffer automatique lui-même | ✅ ON |
| `buttons` | les 2 boutons physiques de la carte | ✅ ON |
| `entrance` | la garde « pas de filament à l'entrée → stop » | ✅ ON |
| `tmc` | le réglage runtime du driver moteur | ✅ ON |
| `move` | les commandes de mouvement (`LLL_JOG`/`LLL_RUN`) | ✅ ON |
| `telem` | les événements poussés vers Klipper | ✅ ON |
| `mbsig` | les entrées signal carte-mère (PB5/PB6, usage legacy filaire) | OFF |
| `diag` | la lecture du défaut driver (PB0) | OFF (à valider au bench) |
| `led` | les LED d'état/erreur de la carte | OFF (à valider au bench) |
| `adc` | la mesure tension d'alim + température carte | OFF (à valider au bench) |
| `ioext` | le registre d'E/S génériques (voir section IOEXT) | OFF |
```
LLL_ENABLE LANE=lll FUNC=led EN=1
```
⚠️ Éteindre une fonction stoppe proprement ce qu'elle pilotait (un maintien bouton en vol → moteur stoppé, etc.). Un refus (conflit de broches avec IOEXT) est signalé en erreur claire.

---

## ⚙️ Réglages

### `LLL_SET LANE=<lane> PARAM=<nom> VALUE=<entier>`
**Règle un paramètre, appliqué immédiatement et persistant.** La console confirme la valeur réellement appliquée (bornée si besoin).
| PARAM | Effet | Plage | Défaut |
|---|---|---|---|
| `feed_timeout` | poussée continue max avant erreur bourrage (ms) | 1000-60000 | 60000 |
| `back_timeout` | rembobinage continu max avant erreur (ms) | 1000-60000 | 60000 |
| `speed` | vitesse moteur (unités driver VACTUAL ; 77575 = vitesse d'origine) | 0-8388607 | 77575 |
| `irun` | courant moteur en marche (0-15 ; **15 = maxi autorisé**, valeur d'origine) | 0-15 | 15 |
| `ihold` | courant à l'arrêt | 0-15 | 7 |
| `iholddelay` | temporisation de passage au courant d'arrêt | 0-15 | 1 |
| `mres` | micro-pas (0=256 … 8=pas entier ; ⚠️ la vitesse ne se recalcule pas seule) | 0-8 | 2 (=64) |
| `stealth` | 1 = mode silencieux stealthChop, 0 = spreadCycle (défaut, plus de couple) | 0/1 | 0 |
| `standstill` | comportement moteur à l'arrêt : 0=normal, 1=roue libre, 2/3=freinage | 0-3 | 0 |
| `tpowerdown` | délai avant réduction du courant à l'arrêt | 0-255 | 20 |
| `run_max` | durée max d'un `LLL_RUN` (ms) | 1000-60000 | 30000 |
| `host_alive` | garde-fou perte d'hôte : 0=off, sinon délai ms (coupe les mouvements commandés si Klipper ne parle plus ; l'autonomie continue) | 0 ou 3000-60000 | 0 |
| `gate_by_role` | 1 = boutons inactifs quand la lane n'est pas active | 0/1 | 0 |
| `inform` | sorties « j'ai appuyé » vers PA2/PA3 (comportement d'origine Mellow) | 0/1 | 1 |
| `diag_action` | sur défaut driver : 0=signaler seulement, 1=stopper et verrouiller | 0/1 | 0 |
| `diag_polarity` | sens électrique de la ligne DIAG (à déterminer au bench) | 0/1 | 0 |
| `event_mask` | filtre des événements poussés (127 = tous) | 0-127 | 15 |
```
LLL_SET LANE=lll PARAM=speed VALUE=60000
→ LLL lll: speed=60000 (applied=60000)
```

---

## 🕹️ Mouvement manuel

### `LLL_JOG LANE=<lane> DIR=fwd|back|stop MS=<durée>`
**Petit mouvement ponctuel, borné à 10 s max.** Prioritaire sur tout sauf les boutons. `DIR=stop` arrête immédiatement.
```
LLL_JOG LANE=lll DIR=back MS=2000     ; rembobine 2 secondes
```

### `LLL_RUN LANE=<lane> DIR=fwd|back|stop MS=<durée>`
**Mouvement long borné** — l'outil des macros d'automatisation. `MS=0` = utilise `run_max` (30 s défaut). Renvoyer la commande prolonge la fenêtre. Sécurités intégrées : avancer exige du filament présent et zéro erreur (re-vérifié en continu) ; reculer est plafonné par `back_timeout`.
```
LLL_RUN LANE=lll2 DIR=fwd MS=0        ; pousse (max 30 s, ou jusqu'à LLL_STOP)
LLL_RUN LANE=lll2 DIR=stop            ; annule
```
💡 Pattern d'automatisation : lancer un RUN, laisser **un capteur** l'arrêter (`insert_gcode` → `LLL_STOP`). Si le capteur ne vient jamais, le RUN expire tout seul — jamais de moteur fou.

### `LLL_STOP LANE=<lane>`
**Arrêt immédiat de tout mouvement commandé** (jog + run + cycle armé). ⚠️ Ce n'est pas un interrupteur général : l'autonomie du rôle reprend au cycle suivant (une lane `active` en mode `auto` peut se remettre à buffer). Pour figer : `LLL_ROLE ROLE=standby` ou `LLL_MODE LEVEL=manual`.

---

## 📤 Déchargement/chargement automatique — non livré

Un pipeline complet de déchargement/chargement automatique (retract synchronisé extrudeur+buffer, détecteur post-splitter, queue calibrée, compensation anti-tangle en boucle fermée) a été prototypé et fonctionnait sur le setup de référence. Il n'est **pas livré** : sans cutter automatique ni ré-embobineurs passifs, ces procédures se sont révélées trop fragiles pour être généralisées. Voir `docs/MULTICOLOR_STATUS.fr.md` pour l'architecture, les leçons tirées, et ce que les setups équipés peuvent construire avec les commandes livrées.

---

## 🚨 Erreurs

### `LLL_CLEAR_ERROR LANE=<lane>`
**Déverrouille une erreur** (bourrage résolu, etc.) et remet `ecode`/`cfgerr` à zéro. Si le défaut driver (DIAG) est encore physiquement présent, le déblocage reste « en attente » (`ecode=clear_pending`) et s'achève tout seul quand le défaut retombe.
💡 Un clic court sur un bouton de la carte fait la même demande de déblocage (sans toucher `cfgerr`).

---

## 🗂️ Profils nommés (façon Beacon)

Un **profil** = une photo de vos réglages (mode + paramètres + fonctions — pas le rôle), sous un nom choisi par vous.

### `LLL_PROFILE_SAVE LANE=<lane> NAME=<nom>`
Fige les réglages actuels sous ce nom.
### `LLL_PROFILE_LOAD LANE=<lane> NAME=<nom>`
Applique le profil et le rend persistant. Ce que le profil ne définit pas reste inchangé. Refus partiels signalés.
### `LLL_PROFILE_LIST LANE=<lane>` / `LLL_PROFILE_DELETE LANE=<lane> NAME=<nom>`
Liste (avec contenu) / supprime.
```
LLL_SET LANE=lll PARAM=speed VALUE=50000
LLL_MODE LANE=lll LEVEL=semi_auto
LLL_PROFILE_SAVE LANE=lll NAME=nuit_silencieux
...
LLL_PROFILE_LOAD LANE=lll NAME=nuit_silencieux   ; switch en 1 commande
```

---

## 🔌 IOEXT — les E/S génériques (utilisateurs avancés)

Le LLL expose 12 canaux d'E/S sur ses broches d'extension : entrée, sortie, PWM lent, mesure analogique. **Sécurité par construction** : les broches vitales (USB, moteur, capteurs) ne sont pas dans la table — impossible de les toucher ; les broches non vérifiées au bench exigent un déverrouillage explicite.

Activer d'abord : `LLL_ENABLE LANE=<lane> FUNC=ioext EN=1`

| CH | Broche | Analogique | Verrou |
|---|---|---|---|
| 0 / 1 | PA4 / PA5 (connecteur IO) | ✅ | non |
| 2 / 3 | PB10 / PB11 (connecteur IO) | — | non |
| 4 / 5 | PA2 / PA3 (EXT) | ✅ | 🔒 + libérer `inform` |
| 6 / 7 | PB5 / PB6 | — | fonction `mbsig` éteinte |
| 8 / 9 | PA8 / PA15 (LED) | — | 🔒 + fonction `led` éteinte |
| 10 / 11 | PA0 / PA1 (VIN / temp carte) | ✅ | 🔒 + fonction `adc` éteinte |

### `LLL_UNLOCK LANE=<lane> KEY=0xDEB1`
Déverrouille les canaux 🔒 **pour la session** (volontairement non persistant : utiliser une broche non validée doit rester un acte conscient).

### `LLL_IO_CFG LANE=<lane> CH=<0-11> FUNC=din|dout|pwm|adc|release [options]`
Configure un canal : `din` + `PULL=none|up|down` (entrée) · `dout` + `INIT=` `SHUTDOWN=` `MAXON=<s>` (sortie, avec état de repli en cas d'arrêt d'urgence et minuterie de sécurité optionnelle) · `pwm` + `PERIOD=<ms>` (PWM logiciel — LED/relais, pas de servo) · `adc` (mesure) · `release` (libère).

### `LLL_IO_WRITE LANE=<lane> CH=<n> VALUE=<0/1 ou 0-255>`
Écrit une sortie (0/1) ou le rapport cyclique PWM (0-255).

### `LLL_IO_READ LANE=<lane> CH=<n>`
Lit le canal : niveau (entrée), valeur brute 0-4095 (analogique), consigne (sortie/PWM).
```
LLL_ENABLE LANE=lll FUNC=ioext EN=1
LLL_IO_CFG LANE=lll CH=0 FUNC=din PULL=up
LLL_IO_READ LANE=lll CH=0
→ LLL lll: ch0 = 1
```

---

## 🔧 Configuration printer.cfg

```ini
[lll_buffer lll]
mcu: LLL_PLUS               # obligatoire : le nom du [mcu] de la carte
role: parked                # RECOMMANDÉ multi-lanes : chaque carte démarre
                            # parquée (aucune poussée pendant le boot) ; la
                            # macro _LLL_INIT (fournie : config/lll_macros.cfg.example)
                            # active la bonne lane après ready
#feed_mode: auto            # mode au démarrage
#enable: led, diag          # fonctions allumées dès le démarrage
#disable: buttons           # fonctions éteintes dès le démarrage
#poll_interval: 1.5         # cadence du rafraîchissement d'état (0.3-30 s)
#event_gcode:               # gcode déclenché à CHAQUE événement de la carte
#  {% if event.name == 'ecode_set' and event.value == 1 %}
#    M118 Bourrage sur {event.lane} !
#    PAUSE
#  {% endif %}
```
`event_gcode` reçoit `event.name` (`ecode_set`, `error_cleared`, `present`, `diag`, `button`, `arm`, `role`), `event.code`, `event.value`, `event.lane` — réaction en ~20-50 ms. Les événements `button/arm/role` nécessitent `LLL_SET PARAM=event_mask VALUE=127`.

**Pour les macros** : tout l'état est dans `printer["lll_buffer <lane>"]` (mêmes champs que LLL_QUERY + `idle` compatible + `last_event`). Un `LLL_QUERY` force un rafraîchissement immédiat.

---

## 🔎 Détection de la lane chargée (FILAMENT CHECK)

### `LLL_FILAMENT LANE=<lane> TYPE=<txt> COLOR=<txt> [TEMP=<°C>]`
**Déclare quel filament est monté sur cette lane** (persistant). Affiché par la probe et dans `get_status.filament`.
```
LLL_FILAMENT LANE=lll2 TYPE=ASA COLOR=rouge TEMP=265
```

### `LLL_DETECT_LOADED [MM=10] [EXPECTED=<lane>] [ON_MISMATCH=warn|pause|error]`
**Détecte quelle lane est PHYSIQUEMENT engagée dans l'extrudeur** — sans aucun capteur supplémentaire : toutes les lanes passent brièvement en standby, petit retract extrudeur (`MM`, déf. 10 mm), **seul le bras de la lane engagée bouge** (ses capteurs de position le trahissent), re-push net zéro, rôles restaurés.
- Depuis la v1.0.3 le verdict vient des **événements de bras** (`EV_ARM`) que le firmware remonte **pendant** le retract — la sonde ajoute temporairement le bit « bras » au masque de télémétrie de chaque lane et compte les transitions par lane : un capteur traversé puis relâché pendant le mouvement compte quand même. La comparaison d'état avant/après reste en repli.
- ⚠️ **Exige un extrudeur chaud** (≥ `min_extrude_temp`, 170 °C) : à froid, Klipper bloque le mouvement E et le filament est figé dans la buse (slip garanti) → la commande refuse avec un message clair.
- `EXPECTED=` + `ON_MISMATCH=pause` : si la lane détectée n'est pas celle attendue → **PAUSE** + message d'instructions (charger la bonne lane, re-vérifier, `RESUME`).
- `ON_MISMATCH=error` : idem mais en **erreur G-code** — avorte la file et le reste de la macro appelante. Indispensable pour gater une purge depuis l'intérieur d'une macro start/resume : un `PAUSE` n'arrête pas l'expansion de la macro (la purge s'exécuterait quand même avec le mauvais filament).
- Verdict non concluant (aucun/plusieurs bras) → signalé honnêtement, jamais de faux positif.
- La lane détectée est mémorisée (`lll_loaded_lane` dans save_variables).
- `MM` est à calibrer sur votre géométrie : il faut que le bras traverse au moins un capteur pendant le retract (avec le verdict par événements, il n'a plus besoin de *finir* dans une autre zone).

### Vérification automatique au démarrage du print (opt-in)
```
SAVE_VARIABLE VARIABLE=lll_filament_check VALUE=1    ; activer (0 = désactiver)
```
Flux recommandé (macros fournies : `config/lll_macros.cfg.example`), en deux temps :
1. **Présélection au plus tôt** : passez `TOOL=[initial_tool]` à votre `PRINT_START` et faites appeler `_AFC_START TOOL=` en première action — la lane du print est activée et celle de l'ancien print **parquée avant même la chauffe**.
2. **Check AVANT la purge** : appelez `_AFC_CHECK` juste après votre dernier `M109`, juste avant la ligne de purge (`ON_MISMATCH=error`) :
- concordance → `LLL probe: lane 'lll2' chargée — ASA rouge (265C) - concordance OK` → purge, print
- discordance → **erreur « FILAMENT CHECK »** avant d'avoir extrudé quoi que ce soit ; le print reste en pause → chargez la bonne lane → `RESUME` rejoue chauffe → check → purge (retry gratuit, plateau propre)
Philosophie : **fonction disponible, jamais imposée** — désactivée par défaut, chacun l'active selon son workflow (indispensable en chargement manuel multi-lanes, inutile pour un mono-lane).

### Note boot (fix intégré)
Les sections `[lll_buffer]` doivent porter `role: parked` : chaque carte démarre parquée **dès la séquence de configuration** (zéro fenêtre où une lane pousserait par défaut), puis `_LLL_INIT` restaure la lane active **persistée** (`afc_active_lane`, écrite en save_variables par `SET_ACTIVE_LANE`) 2 s après ready — jamais un défaut en dur : après un restart, la lane que vous n'utilisez pas reste parquée. Sans ça, une carte redémarrée est `active/auto` par défaut compilé pendant ~15-40 s.
✅ **Reset MCU franc — corrigé par le firmware (fix #9, grace period)** : après un `FIRMWARE_RESTART`, un M112 ou une coupure d'alimentation, la carte ne démarre **aucun mouvement autonome** tant que Klipper ne lui a pas parlé (plafond 20 s) — la fenêtre « défauts compilés » qui provoquait une brève poussée fantôme est fermée (prouvé sur le setup de référence : reset avec bras tendu → zéro mouvement). Boutons, jog, run et rôles restent actifs pendant la grâce. Compromis assumé (pas une parité Mellow) : une carte jamais pilotée par un hôte attend 20 s au boot avant l'auto-feed, là où le firmware d'origine démarre ~1 s après. `role: parked` reste recommandé en couche complémentaire (couvre aussi un hôte Klipper qui reste indisponible > 20 s bras tendu).

---

## 🖥️ Interface visuelle — tout DANS Mainsail (zéro fork)

Quatre étages complémentaires — trois dans Mainsail, un sur l'écran physique :

### 1. La carte « LLL Lanes » (webcam iframe) — la vue permanente
Le dashboard LLL s'affiche **dans le dashboard Mainsail comme une webcam** (service natif `iframe`). Une carte par lane, **auto-découverte** (une nouvelle lane déclarée apparaît toute seule) :
- **Pastille couleur** du filament (la vraie couleur de la bobine Spoolman si `SPOOL_ID` est renseigné, sinon déduite du nom de couleur), matière, temp, n° bobine
- Rôle (badge vert ACTIVE / orange parked / gris standby), présence filament, position du bras (3 segments), moteur en mouvement, erreurs en rouge
- **Actions au clic** : activer la lane, jog ±, débloquer, **✎ éditer matière/couleur/bobine** (envoie `LLL_FILAMENT`)
- Live (websocket via le proxy nginx de Mainsail, même origin), reconnexion auto
- Fichier : `~/lll_dashboard/index.html` servi sur `/lll/` (nginx) ; webcam Moonraker « LLL Lanes » service `iframe`, URL `/lll/`. Dans Mainsail : panneau Webcam → sélectionner « LLL Lanes » (ou vue grille des cams).

### 2. `LLL_PANEL` — le dialog d'actions rapides
Macro (fournie : `config/lll_macros.cfg.example` ; bouton dans le panneau Macros) qui ouvre un **dialog natif Mainsail** : état de chaque lane + boutons Activer/Débloquer/Sonde/Rafraîchir. Auto-découvert depuis `_AFC_VARS.lanes`. Chaque action re-questionne les lanes et ré-affiche (état frais).
⚠️ Piège appris : dans un template de macro Klipper, un `#` N'IMPORTE OÙ dans la ligne démarre un commentaire — même dans une string jinja. Jamais de `#` dans les textes de dialog.

### 3. Spoolman — la base de données bobines (panneau natif Mainsail)
Service installé sur le Pi (`http://<ip>:7912`, systemd `Spoolman`), branché Moonraker (`[spoolman]`). Gère les bobines : couleur (pastille), matière, **poids restant**, fournisseur. **La bobine active suit la lane active automatiquement** (le pont `SPOOL_ID` : `SET_ACTIVE_LANE` → la consommation se décompte sur la bonne bobine, par lane). Déclarer une bobine dans Spoolman → noter son n° → `LLL_FILAMENT LANE=x SPOOL_ID=n`.

### 4. Menu LCD « LLL Buffer » (écran physique BTT MINI12864)
Section ajoutée dans `~/printer_data/config/lcd_menu.cfg` (sous le menu principal). Pour piloter les lanes **sans écran déporté**, directement à la machine :
- **Nom de chaque sous-menu = état LIVE de la lane** : `L1 ACTIVE` / `L1 PARKED` (+ ` !ERR` si erreur) — lecture instantanée de qui est active.
- Par lane : **Activer** (`LLL_ROLE active`), **Parker/Hold** (`LLL_ROLE parked`, grisé pendant une impression — guard sur `print_stats.state` ; ⚠️ reste cliquable pendant une PAUSE : ne parquez pas la lane active en cours d'impression, un parked est persistant et la lane « ne suit plus » jusqu'à ré-activation), **Effacer erreur** (`LLL_CLEAR_ERROR`), + une ligne d'état détaillé (`LLL_QUERY` : bras, présence, ecode).
- **Volontairement PAS de jog/rewind au LCD** : les boutons physiques des cartes LLL sont juste à côté. Le menu sert à **confirmer/basculer l'état** (parked ↔ active), pas à faire avancer le filament.
- **C'est l'interface du workflow rewind sûr** : Parker la lane sur le LCD → reculer avec les boutons physiques (une lane parked ne re-forwarde jamais) → la ré-Activer.

---

## 🧬 Intégration multi-lanes : de 2 à N cartes LLL

### Les profils sont PAR LANE
Chaque lane a son **catalogue de profils indépendant** (stocké `lll_<lane>_profiles`). La lane 1 peut être paramétrée d'une manière et la 10ᵉ d'une toute autre — c'est natif. Un même nom de profil peut exister sur plusieurs lanes avec des contenus différents. Pour dupliquer un profil d'une lane vers une autre : appliquer les mêmes réglages sur l'autre lane puis `LLL_PROFILE_SAVE` dessus.

### Ajouter une lane N — recette complète (exemple : 3ᵉ carte → lane `lll3`)

**Étape 0 — Flash.** Flasher la carte avec le firmware canonique (Katapult no-DFU, cf. guide). Elle apparaît :
```
/dev/serial/by-id/usb-Klipper_stm32f072xb_<SERIAL-UNIQUE>-if00
```
Chaque carte a un serial gravé unique — aucun risque de confusion, même avec 10 cartes sur un hub.

**Étape 1 — printer.cfg** (3 blocs par lane, calqués sur les lanes existantes) :
```ini
[mcu LLL_PLUS_3]
serial: /dev/serial/by-id/usb-Klipper_stm32f072xb_<SERIAL-UNIQUE>-if00
restart_method: command

[lll_buffer lll3]
mcu: LLL_PLUS_3
#role: parked              # optionnel : réglages de démarrage propres à CETTE lane
#feed_mode: auto
#poll_interval: 1.5
#event_gcode: ...

[filament_switch_sensor lll3_entrance]
switch_pin: ^!LLL_PLUS_3:PB7
pause_on_runout: True
runout_gcode:
    M118 LLL Plus 3: filament runout detected
insert_gcode:
    M118 LLL Plus 3: filament reinserted
```
⚠️ Le capteur d'entrée est TOUJOURS `^!<nom_mcu>:PB7` (pull-up + inversé). Le firmware ne touche pas ce pin — c'est Klipper natif.

**Étape 2 — macros host** (2 ajouts, à faire dans votre copie de `config/lll_macros.cfg.example`) :
```ini
# 1) déclarer la lane dans le registre (l'ordre du dict = l'ordre des outils T0,T1,T2…)
[gcode_macro _AFC_VARS]
variable_lanes: {'lll': 'lll_entrance', 'lll2': 'lll2_entrance', 'lll3': 'lll3_entrance'}

# 2) créer l'outil correspondant (Tn = position n dans le dict, base 0)
[gcode_macro T2]
gcode: _COLOR_CHANGE LANE=lll3
```
C'est tout : `SET_ACTIVE_LANE`, le boot (`_LLL_INIT`), le swap, la persistance et les profils **bouclent automatiquement sur le registre** — rien d'autre à modifier, quelle que soit la quantité de lanes.

**Étape 3 — redémarrer** : `RESTART` (config) — et si le firmware/extra a changé, `sudo systemctl restart klipper`.

### Côté OrcaSlicer (N couleurs)
Le pont Orca→Klipper tient en 2 lignes, à ajouter dans votre G-code machine (**Paramètres imprimante → onglet « Machine G-code » → champ « Change filament G-code »**) ; les macros `SET_NEXT_TEMP`/`Tn` sont fournies dans `config/lll_macros.cfg.example` :
```
SET_NEXT_TEMP TEMP=[new_filament_temp]
T[next_extruder]
```
- Orca émet `T0`, `T1`, `T2`… à chaque changement de couleur → la macro `Tn` fait le swap de la bonne lane. **Le mapping = l'ordre du registre** : 1ᵉʳ filament Orca = 1ʳᵉ lane du dict, etc.
- Pour ajouter des couleurs : la machine est en « single extruder multimaterial » → bouton **« + »** dans la barre des filaments pour ajouter des slots (autant que de lanes déclarées côté Klipper).
- `SET_NEXT_TEMP` transmet la température du NOUVEAU filament pour la chauffe pendant le swap (filaments différents = températures différentes, géré).
- Tour de purge désactivée (dans votre profil process multicouleur ; la purge est gérée par les macros au swap).

### Limites pratiques (combien de LLL max ?)
- **Klipper** : pas de limite dure au nombre de MCU — des configs à 6-8 MCU existent couramment. Chaque LLL ajoute ~1 poll/1,5 s + clock sync : négligeable même ×10.
- **Le vrai facteur = l'USB et l'alim** : hub USB **alimenté** obligatoire au-delà de 2-3 cartes, et chaque LLL veut son 24 V.
- **Firmware identique sur toutes les cartes** (one-flash, tout se différencie par la config) — c'est le principe plug-and-play du projet.

---

## 🆘 Aide-mémoire dépannage

| Symptôme | Réflexe |
|---|---|
| « JAM » en console | Le moteur est déjà stoppé. Résoudre physiquement, puis `LLL_CLEAR_ERROR` (ou clic bouton). `ecode` dit la cause. |
| La lane ne pousse plus | `LLL_QUERY` : vérifier `role=active`, `mode`, `error=0`, `present=1` |
| Grind / l'Orbiter patine, bras LLL tendu à fond | 1) `LLL_QUERY` : si `error=0`, c'est la **bande morte by-design de la FSM** (le feed ne démarre qu'au bras en butée hall3/arm=4 et s'arrête à hall2 → ~13 mm drainés sans re-feed ; identique sur toutes les versions du firmware) — le grind vient alors de la **friction en aval du Y** (un SFS sur le setup de référence — adaptez à votre chemin filament) que l'Orbiter encaisse seul pendant la bande morte. 2) Vérifier la mécanique aval Y (angle SFS : un coude ajoute une résistance que le buffer ne peut pas soulager). 3) Ne PAS monter le run_current de l'Orbiter en 1er réflexe (ça endommage le filament, surtout chargé fibres) ; la réduction de la bande morte (ex. démarrage feed sur perte de hall2) est une piste d'évolution future. |
| Une commande est refusée | Message d'erreur explicite en console + `cfgerr=1` au query. Souvent : conflit de broches (fonction vs IOEXT) ou verrou 🔒 |
| Réglage perdu après reboot ? | Non — tout `LLL_SET/MODE/ENABLE/ROLE/PROFILE_LOAD` est persistant : la valeur est stockée dans `[save_variables]` **et réémise au MCU au démarrage** par le module (`lll_buffer.py` : handler `klippy:connect` → `_replay_saved()` → renvoie chaque tweak au firmware). Ce n'est donc pas le `save_variables` standard seul (qui ne fait que stocker) : c'est ce replay maison qui rend le réglage effectif à chaque boot. Seuls `LLL_UNLOCK` et les configs IOEXT sont volontairement par-session. |
| Tout figer d'urgence | `LLL_ROLE LANE=<lane> ROLE=standby` (et `M112` reste le vrai bouton rouge : le firmware coupe le moteur sur tout shutdown) |
