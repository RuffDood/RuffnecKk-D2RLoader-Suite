# Laboratoire local Cast Triggers 1.0.0

Ce laboratoire teste Cast Triggers dans la pile active complète BKVince avec
QtyTester. Ce n'est pas un profil isolé. La qualification gameplay courante
s'exécute une seule fois sous D2R 3.3.93847; le build 3.2.92777 hérite seulement
de la couverture native byte-exact gouvernée tant que toutes les surfaces
utilisées demeurent identiques.

## Préparer le personnage

1. Déployez le fixture dans BKVince avec la pile complète active.
2. Utilisez QtyTester sur le personnage de test actuel.
3. Faites apparaître l'ingrédient indiqué, placez-le seul dans le Cube et
   transmutez.
4. Équipez une seule bague de gate à la fois.

## Créer les bagues de test

Placez un seul ingrédient dans le Cube et transmutez :

| Ingrédient | Commande QtyTester | Bague produite |
|---|---|---|
| Identify Scroll | `spawn isc,inv=1` | 100% chance to cast level 12 Fire Ball |
| Stamina Potion | `spawn vps,inv=1` | 100% chance to cast Nova at the source skill level |
| Antidote Potion | `spawn yps,inv=1` | Generic Nova plus channel-only Fire Ball, with level 10 Inferno and Chain Lightning oskills |
| Rejuvenation Potion | `spawn rvs,inv=1` | Frost Nova-only family: fixed Fire Ball plus source-level Nova, with level 10 Frost Nova oskill |
| Minor Healing Potion | `spawn hp1,inv=1` | Custom 100% Cast level 12 Fire Ball on Attack Attempt |
| Minor Mana Potion | `spawn mp1,inv=1` | 100% passive Critical plus 100% Cast Fire Ball on Critical Strike |
| Light Healing Potion | `spawn hp2,inv=1` | 100% Deadly Strike plus Cast Fire Ball on Critical Strike; negative gate |
| Light Mana Potion | `spawn mp2,inv=1` | 100% Crushing Blow plus 100% Cast Nova on Crushing Blow |
| Healing Potion | `spawn hp3,inv=1` | 100% Open Wounds plus 100% Cast Frost Nova on Open Wounds |
| Mana Potion | `spawn mp3,inv=1` | 100% Critical, Cast Fire Ball on Critical and an unmatched Cast Nova on Crushing Blow |

Le Town Portal Scroll n'est l'entrée d'aucune recette du laboratoire. La bague
same-level utilise bien une **Stamina Potion** afin de ne pas intercepter la
recette de clue scroll d'un autre mod.

Identifiez la bague si son affixe n'est pas immédiatement visible. Équipez une
seule bague de test à la fois afin de garder les observations non ambiguës.

## Matrice de gameplay

| Cas | Action suggérée | Résultat attendu |
|---|---|---|
| Tooltip fixe | Inspecter Fire Ball ou Blizzard | Le niveau 12 est visible |
| Tooltip same-level | Inspecter Nova ou Frost Nova | La ligne complète est visible et mentionne le niveau du sort source |
| Régression cast directionnel | Lancer Chain Lightning vers plusieurs unités/points avec la bague Fire Ball | Une Fire Ball suit la cible native de chaque cast |
| Cast ordinaire séparé | Lancer Chain Lightning avec la bague Antidote | Nova proc une fois; la Fire Ball channel-only ne proc pas |
| Exclusion attaque du cast-on-cast | Attaquer normalement avec la bague Antidote | Aucun Nova ni Fire Ball |
| Cast on Attack Attempt | Avec la bague Minor Healing, attaquer une cible, manquer puis Shift-attaquer le sol | Une Fire Ball est tentée dès que chaque attaque est acceptée, sans attendre un hit |
| Channeling séparé | Maintenir Inferno au moins 6 secondes avec la bague Antidote | Fire Ball proc immédiatement puis toutes les 2 secondes; la Nova générique ne proc pas |
| Arrêt du channeling | Relâcher Inferno après un proc | Aucun autre proc après l'arrêt |
| Source exacte positive | Avec la bague Rejuvenation, lancer Frost Nova | Une Fire Ball fixe et une Nova au niveau effectif de Frost Nova proc |
| Source exacte négative | Avec la même bague, lancer War Cry ou Taunt | Aucun proc de la famille Frost Nova |
| Séquence | Lancer Lightning ou Chain Lightning | Un seul dispatch par cast réussi |
| Critical positif | Frapper avec la bague Minor Mana | Chaque Critical confirmé lance une Fire Ball sur la cible |
| Deadly exclu | Frapper à mains nues avec la bague Light Healing | Deadly double les dégâts, mais aucune Fire Ball de Critical n'est lancée |
| Crushing Blow | Frapper avec la bague Light Mana | Chaque Crushing Blow appliqué lance une Nova |
| Open Wounds | Frapper avec la bague Healing | Chaque Open Wounds appliqué lance une Frost Nova |
| Filtrage combat | Frapper avec la bague Mana | Fire Ball proc sur Critical; Nova ne proc pas sans Crushing Blow |
| Chaîne de procs | Porter simultanément les bagues Mana et Antidote, puis attaquer | La Fire Ball de Critical ne déclenche pas le cast-on-cast de la seconde bague |

La bague créée avec une **Antidote Potion** rend la séparation visible :
Chain Lightning lance seulement la Nova générique; Inferno lance seulement la
Fire Ball channel-only, immédiatement puis toutes les 50 frames serveur. La
bague **Rejuvenation Potion** vérifie les deux niveaux d'une même règle source :
Frost Nova lance Fire Ball et Nova, tandis qu'un autre cast ne lance rien.

Le laboratoire ne contient volontairement aucun cas gameplay 25% séparé. Les
gates sont déterministes à 100%; les chances intermédiaires utilisent toujours
l'encodage et le jet natifs.

## Diagnostics

La configuration du laboratoire active les diagnostics. Dans la console
D2RLoader, la commande suivante affiche les compteurs :

```text
cast-triggers
```

Le log du plugin se trouve dans :

```text
<D2R>/mods/BKVince/d2rloader/logs/ruffneckk-cast-triggers.log
```

Les lignes `input captured`, `eligible source`,
`descriptor-source=input|channel-input|handler`,
`target=unit|position|none`,
`requested-level`, `effective-level`, `native-position` et
`native-unit-target` permettent de confirmer le descripteur et le chemin
réellement consommés. Les compteurs `channel ticks`, `channel dispatches`,
`channel throttled`, `inputs captured`, `inputs consumed`, `inputs expired` et
`channel targets reused` permettent de vérifier que les jets sont séparés par
au moins 50 frames, suivent l'input natif et s'arrêtent avec Inferno. Les compteurs `critical`,
`crushing-blow`, `open-wounds`, `combat dispatches`, `chains suppressed` et
`trigger stats filtered`, ainsi que les lignes
`dispatched combat trigger=attack-attempt`, couvrent le gate de combat et de
récursion.

## Déploiement et rollback

Le gate se fait dans BKVince avec QtyTester, jamais dans un profil isolé. Avant
le déploiement, le workflow runtime sauvegarde les fichiers BKVince remplacés.
Le nettoyage final retire seulement les recettes déterministes et les objets de
départ temporaires, puis remet `charstats.txt` dans son état gouverné. Les stats
`394` à `403`, leurs propriétés, leurs clés de tooltip, la DLL et le TOML de
production demeurent installés dans BKVince. Un snapshot de personnage ne peut
être restauré comme état « propre » qu'après avoir prouvé qu'il ne contient
aucun objet portant ces IDs. Les preuves post-test sont conservées hors du
runtime dans `analysis-cache`.

Un cold start ou un test gameplay ne doit jamais être déclaré réussi sans une
observation de la session courante. À la création initiale du laboratoire, ces
cases restent donc `not run` tant que Vincent n'a pas autorisé le lancement.
