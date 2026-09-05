# Findings persistants — D2R 3.2.92777

Ce document conserve uniquement les conclusions utiles aux prochaines sessions.
Les sorties volumineuses demeurent sous `analysis-cache/corpus/`.

## ISC12 — disposition runtime du conteneur persistant

- Le premier write réel du candidat à enveloppe a produit exactement 499
  octets : 96 octets d'enveloppe ISC12 suivis d'un D2S valide de 403 octets.
  Le writer et la primitive atomique ont donc rempli leur contrat local.
- D2RLoader 1.2 a néanmoins annulé le lancement au frontend avec
  `route=prepare result=invalid-character`. Une enveloppe externe opaque est
  incompatible avec ce trajet même lorsque son payload interne est valide.
- La DLL runtime conserve désormais les conteneurs D2S/D2I standards. Le hook
  reader valide le buffer inchangé; le hook writer valide puis retourne à la
  continuation native avant `CREATE_ALWAYS`, afin que le couple D2RCore
  writer/closer compose normalement `.d2rl`, backups et environnement.
- Vincent accepte la persistance native non atomique et fixe le clean-sheet
  comme contrat de support : nouveau personnage ou migration externe future,
  backups obligatoires, aucun chargement direct d'une save vanilla/non-ISC12.
  L'enveloppe et l'atomic writer restent des composants unit-testés réservés à
  l'outillage de migration.

## ProgressiveAffixesPlugin — sélection du nombre d’affixes

- `0x442C60` est le générateur Magic appelé avec `(itemWrapper, generation)` ;
  l’item est à `itemWrapper+0x00` et l’ilvl gouverné à `generation+0x18`.
  `0x442C78` charge la décision prefix à `generation+0xA8` et `0x442CDC`
  charge la décision suffix à `generation+0xB4`. Les valeurs positives exigent
  les tentatives qui permettent de garantir un prefix et un suffix.
- Le site prefix `0x442C78` précède `mov rsi, rdx` et `mov rdi, rcx` dans le
  prologue Magic. Un helper appelé depuis ce site doit donc préserver les deux
  arguments volatils malgré l’ABI Windows x64. La v0.2.0 ne le faisait pas et
  pouvait transmettre un faux pointeur à `UNITS_GetUnitType` pendant
  `Game::AddPlayerToGame`; la v0.2.1 appelle le helper avec shadow space et
  restaure `RDX/RCX` avant le retour au prologue natif.
- `0x58A120` est le générateur Crafted `(item, generation)`. Le bloc
  `0x58A1E7..0x58A225` prouve les minima ilvl 1/31/51/71 et l’appel au RNG
  général `0x153B00` avec borne 5. Les distributions vanilla correspondent
  exactement aux poids PD2 `2/1/1/1`, `0/3/1/1`, `0/0/4/1`, `0/0/0/1`.
- `0x58BBA0` est le générateur Rare `(item, generation)`. La branche jewel
  `0x58BC65..0x58BC8E` avance directement le seed et produit 3 ou 4 ; la branche
  ordinaire `0x58BC90..0x58BCAD` roule huit entrées avant la boucle native qui
  respecte les limites de trois prefixes et trois suffixes.
- `ProgressiveAffixesPlugin` possède uniquement les décisions de compte aux
  plages `0x442C78`, `0x442CDC`, `0x58A21B`, `0x58A220`, `0x58BC65` et
  `0x58BC90..0x58BCAD`. Le moteur conserve sélection, application, sérialisation
  et synchronisation des affixes.
- Le PluginPack épinglé `dc75b49ffbb67b887d7757ee00ee9a03bcde5d8a` ne
  contient aucun affix hook ni ces RVA. Les trois anciens patchsets BKVince sont
  les seuls propriétaires locaux antérieurs et doivent être remplacés
  atomiquement.

Commandes de reprise :

```powershell
npm.cmd run re:d2r32 -- status
npm.cmd run re:d2r32 -- xrefs 0x442C60
npm.cmd run re:d2r32 -- xrefs 0x153B00
npm.cmd run ref:d2rlplugins -- status
```

## Base d'analyse

- Image canonique gouvernee : SHA-256
  `CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715`.
- Image d'analyse deterministe : SHA-256
  `673E8C0B2E89563E75525B24D137098EFD07B2DB4ED42ADEC56AA1ADDF0E63AB`.
- Le `.text` de l'image canonique est preserve. Les metadonnees PE non-code
  rehydratees rendent 105 850 fonctions x64 accessibles par `.pdata`.
- L'index compact connait actuellement plus d'un million de references code ou
  RIP, les chaines des sections de donnees et les patches BKVince actifs.

## Treasure Class drop entry et Player Condition Calc

- `0x441300` est l'entrée serveur de génération des drops Treasure Class. Ses
  trois appels directs sont `0x40BB3E`, `0x415C21` et `0x447E17`.
- L'ABI Windows x64 observée comporte dix arguments. `R8` transporte le joueur
  ou le monstre tueur et `R9W` un identifiant Treasure Class scalaire; les six
  arguments restants occupent les slots de pile du caller.
- Le crash Community Pack du 14 août 2026 provenait d'un ancien wrapper à quatre
  arguments qui déréférençait `R9` comme un pointeur. Le témoin fautif avait
  `R9=0x261`, exactement conforme à l'usage natif `movzx ebp,r9w`.
- Lorsqu'un tueur de type Monster est reçu, `D2GAME_GetMinionOwner 0x4A53C0`
  fournit le propriétaire joueur éventuel. Un wrapper doit transmettre les dix
  arguments inchangés, conserver l'état par thread de manière réentrante et ne
  jamais déréférencer `R9`.

## Routine de quantite des Books

- `0x5817BD` charge le delta `-1` apres le controle du type `Book` (`0x12`).
- L'appel a `0x46F090` part de `0x5817CC`; l'index retrouve six callers directs
  de cette routine centrale.
- `0x46F090` synchronise `STAT_QUANTITY` et la quantite du skill lie au
  scroll/tome.
- `0x47145D` est un chemin Javelin (`0x29`) rejete pour les tomes;
  `0x4F5849` est egalement un faux candidat conserve vanilla.

Commandes de reprise :

```powershell
npm run re:d2r32 -- function 0x5817BD
npm run re:d2r32 -- xrefs 0x46F090
```

## pSpell et consommables a skill

- La structure officielle partagee de D2RL-Plugins place `pSpell` a l'offset
  `+0x94` de l'enregistrement item.
- La documentation D2R 3.2 expose des handlers `pSpell` fixes; elle n'expose pas
  un handler generique prenant directement un ID arbitraire de skill.
- `books.txt` associe `ScrollSkill`/`BookSkill`; le `srvdofunc 113`
  `ItemDoBookSkill` utilise le skill du Book/Scroll et met sa quantite a jour.
  Cette voie constitue le prototype data-only prioritaire.
- Le pack officiel `plugin-skills` intercepte deja la consommation native des
  charges a `0x436830` (`D2GAME_SKILLMANA_Consume`). C'est la fondation prouvee
  pour un plugin exact si le clic droit inventaire arbitraire reste requis.
- Les fonctions autour de `0x1AC881`, `0x1AC8F0` et `0x1AC932` lisent bien le
  champ `+0x94`, mais leur role de dispatcher d'utilisation n'est pas prouve.
- Les routines autour de `0x1A7660`/`0x1A77D0` testent une borne `< 16` et sont
  des candidates possibles pour une table de handlers; confiance faible tant
  que callers, arguments et effets ne sont pas etablis.

Sortie brute conservee :
`analysis-cache/corpus/pspell-2026-07-19/pspell-analysis.txt`.

Prochaine etape efficace : partir des xrefs de l'acces `D2ItemsTxt+0x94`, puis
remonter depuis l'evenement serveur d'utilisation d'objet. Ne pas relancer un
scan global du `.text` avant d'avoir epuise l'index et le projet Ghidra.

## Rafraichissement du stock des marchands normaux

- `0x53C9F0` est `D2GAME_NPC_FillStoreInventory` avec l'ABI
  `(game, player, npc) -> void`; son unique caller est `0x540960`.
- La routine resout le `VendorChainEntry`, ecrit `GetTickCount64()` a `+0x38`,
  puis genere le stock aleatoire et permanent par `0x540EA0`.
- Le dispatch normal teste l'etat rempli `+0x34` et le drapeau de refresh
  `+0x35`. Lorsque ce dernier est arme, l'appel unique a `0x502F00` depuis
  `0x540952` nettoie et synchronise l'inventaire vendeur, remet `+0x34` et
  `+0x38` a zero, puis le remplissage est rejoue.
- `0x503290` parcourt la chaine des vendeurs. Son chemin temporise compare le
  timestamp a `GetTickCount64() - 0x3A980`, soit quatre minutes, arme `+0x35`
  et actualise `+0x38`. Ses deux callers directs sont `0x502DEB` et `0x502EE2`.
- Le gamble est distinct : le dispatch appelle `0x541880` depuis `0x540913`
  pour le type d'interface 3; sa limite de generation reste gouvernee a
  `0x541A7E`.
- Le commit epingle `D2RL-Plugins@dc75b49` confirme les structures
  `NpcItemCacheEntry`/`VendorChainEntry` et possede deja le hook canonique de
  `FillStoreInventory` dans `plugin-items`.
- Le layout natif du panel declare deja `button_refresh` avec
  `VendorPanelMessage:RefreshAll`, le sprite gamble, le son natif et `@refresh`.
  La configuration `0x2411E0` utilise `panel+0x168` : gamble `2`, tandis que les
  vendeurs normaux couvrent les modes `0` et `1`. Charsi a `hcIdx=154`; l'index
  `154 - 0x93` lit le selecteur `1` dans la table et saute a `0x2412DE`, qui
  ecrit explicitement le mode `1`. Les gates uniques `0x24137D`/`0x241391`
  identifient le widget exclusif au gamble; `0x240E0D` garde le chemin d'entree
  direct sur le meme mode. La politique 0.1.1/0.1.2 `mode != 1` masquait donc
  Charsi; 0.1.3 a prouve le trajet sur tous les modes en forcant ces gates, mais
  deplacait aussi le bouton du gamble.
- `0x856220` parcourt les enfants directs entre `panel+0x58` et `panel+0x60`,
  compare leur nom et renvoie le widget correspondant. Les noms bruts du panel
  sont confirmes en memoire runtime. Le hook 0.1.4 a `0x2411E0` appelle d'abord
  la configuration originale, resout `button_refresh_normal`, puis utilise les
  methodes vtable `+0x48`/`+0x50` pour le montrer uniquement hors gamble. Les
  trois gates vanilla du `button_refresh` original ne sont plus patches.
- Le handler de message `0x241B20` compare le hash RefreshAll
  `0xB7AA1748D66EFCAF` et rejoint `0x10F520`. Ce sender n'a que deux references
  dans le panel vendeur et emet l'action 2 via `0xEC730`.
- `0xEC730` construit exactement neuf octets `{opcode, action, npcGuid}`. Les
  callers 92777 prouvent opcode `0x38`, action 1 normal, 2 gamble et 3 hire. Le
  callback serveur `0x4B0470` exige neuf octets, valide le NPC puis route normal
  et gamble vers `0x540850`; ne pas transposer les 13 octets D2MOO 1.10f.
- `0x10CAC0` retourne l'etat gamble utilise par le panel. Le hook client conserve
  donc l'action 2 en gamble; le correctif 0.1.1 utilise le marqueur prive `VSRF`
  en normal dans le meme paquet opcode `0x38` de neuf octets.
- `0x502F60` recoit `(game, npc, player, mode)` juste avant `0x540850`. Il lit et
  remplace `PlayerData+0x100`; mode 2 normal, 3 gamble, 4 hire. Le premier temoin
  gameplay a invalide le discriminateur 0.1.0 fonde sur l'ancien mode et la
  classe : bouton visible, mais aucun changement dans la grille Charsi.
- Le correctif 0.1.1 hooke aussi le callback autoritaire `0x4B0470`. Il traduit
  uniquement `VSRF` en action vanilla 1, laisse le callback original valider NPC,
  acte et distance, puis arme `VendorChainEntry+0x35` dans la portee thread-local
  exacte du `0x502F60` ainsi rejoint. Une ouverture normale reste hors de cette
  portee et ne peut pas armer le refresh.
- Le temoin 0.1.3 a produit neuf `sent`, neuf `armed` et zero rejet, prouvant le
  trajet client/serveur. Sa capture a revele un centrage trop a droite et la
  perte de la position vanilla en gamble.
- Cold start mod-local 0.1.4 du 26 juillet 2026 : DLL source/runtime identique
  (`12B6F035...D60CD`), layout identique (`2E59C9BE...A8FBEE`), JSON identique
  (`B7510070...65C41`), hooks `0x2411E0`, `0x502F60`, `0x4B0470` et `0x10F520`
  installes, `20/20` patchsets, 24 plugins actifs, zero rejet, zero echec et
  demarrage `24/24`. La separation visuelle normal/gamble reste a confirmer.
- `0x8562A0` lit le rectangle local d'un widget avec l'ABI
  `(widget, rectOut) -> rectOut`. Pour un widget ordinaire, il copie les quatre
  entiers signes `{x,y,width,height}` situes a `widget+0x70`; le drapeau
  fill-parent `+0x52` suit le parent `+0x30`. Les chemins de hit-test et de rendu
  relisent cette meme geometrie.
- Le correctif 0.1.5 supprime le clone et toute dependance a un layout livre. Le
  hook post-configuration retrouve le `button_refresh` natif et `StashWidget`,
  calcule `x = anchor.x + (anchor.width - button.width) / 2`, puis place le
  bouton sous l'ancre avec un espacement proportionnel a sa hauteur. Le repli
  utilise l'union de `gold_icon` et `gold_amount`; sans geometrie exploitable,
  le bouton normal reste masque. En gamble, la position exacte capturee depuis
  le layout actif est restauree et les gates vanilla restent autoritaires.
- Cold start mod-local 0.1.5 du 27 juillet 2026 sans override de layout : DLL
  source/runtime identique (`21E75601...C09FE`), JSON inchange
  (`B7510070...65C41`), quatre hooks acceptes, `20/20` patchsets, 24 plugins
  actifs, zero rejet, zero echec et demarrage `24/24`. Chez Charsi, le placement
  dynamique est logge a `519,1383` depuis l'ancre runtime
  `421,1305,313,58`; le rendu est centre sous l'or sans chevauchement et un clic
  renouvelle la grille avec `sent=1`, `armed=1`, `rejected=0`.

Conclusion : le serveur fournit deja la primitive atomique nettoyage puis
reconstruction et tout le trajet UI/reseau vanilla est maintenant prouve. Le
correctif autonome 0.1.5 compile, son test statique passe et son placement normal
est confirme sans override de layout. Le prochain travail efficace est la
confirmation du rectangle original en gamble, puis un vendeur de mode 0 et un
layout reellement modde avant la matrice multi-vendeur/manette.

## Cube Quick Move Bottom-Right

- La référence sémantique épinglée
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`,
  `source/D2Common/src/D2Inventory.cpp:658-690`, montre que
  `INVENTORY_GetFreePosition` choisit le parcours pondéré bas-droite uniquement
  lorsque la hauteur de l'objet vaut `1`; D2MOO 1.10f ne fournit aucune adresse
  transposable au build 92777.
- L'équivalent 92777 est `INVENTORY_FindFreePosition` à `0x3865B0`. Son ABI x64
  est `(inventory, item, inventoryRecordId, freeXOut, freeYOut, pageByte) ->
  int32`. `ITEMS_GetDimensions` à `0x371850` écrit largeur puis hauteur.
- Le gate `0x386735` porte les octets `40 80 FE 01`. L'égalité appelle l'unique
  xref vanilla de `INVENTORY_SearchBottomRightWeighted` à `0x38D8F0`; les objets
  plus hauts suivent les branches joueur qui aboutissent au parcours haut-gauche
  à `0x38DCC0`.
- Le chemin indirect du Cube autour de `0x4BB8C0` vérifie le code de base `box `,
  sélectionne la page native `3`, résout l'Inventory.txt actif et appelle
  `INVENTORY_FindFreePosition` à `0x4BBA73`. Les cinq octets
  `E8 38 AB EC FF` sont uniques dans `.text` et le retour est `0x4BBA78`, mais
  cette unicité de signature ne prouve pas l'unicité sémantique du chemin Cube.
- Parmi les `36` xrefs de `INVENTORY_FindFreePosition`, huit call-sites écrivent
  explicitement la page `3` via `C6 44 24 28 03` avant leur `CALL rel32` :
  `0x0FA33D`, `0x2C7306`, `0x471D62`, `0x4BBA73`, `0x4C21D6`, `0x4F2C8B`,
  `0x527DC2` et `0x528053`. `0x4C21D6` appartient au parseur d'un paquet de
  `0x15` octets et constitue le candidat principal du Ctrl-clic équipé.
- La construction native de la grille d'occupation est reproduite sans structure
  inventée : `GetItemDataContext` à `0x34A0E0`, contexte temporaire à
  `0x3C6D80` et résolution de grille à `0x38B070` avec `page + 2`. La grille
  expose sa largeur à `+0x10`, sa hauteur à `+0x11` et le tableau de cellules à
  `+0x18`, comme le prouvent les deux branches de recherche 92777.
- `CubeQuickMove 0.1.2` conservait la fonction et le gate partagés intacts et
  remplaçait les huit appels explicites page `3`. Après l'échec gameplay de
  l'épée, une lecture directe du processus a prouvé que les huit `CALL rel32`
  visaient bien le relais `0x3E80000`, puis que `CubeCalls`, `redirected`,
  `vanilla` et `safeFallbacks` valaient tous `0`. Le Ctrl-clic testé ne traversait
  donc aucun de ces huit sites.
- Les `36` xrefs directs se divisent en neuf appels dont le sixième argument est
  constamment `0`, `2` ou `4`, et `27` appels capables de transporter la page
  Cube `3`. À `0x15A25C`, le contrôle client reprend la page dynamique `r14b`;
  son appelant `0x15A2BD` lui passe explicitement `cl=3`. À `0x4FBC0E`, la
  branche serveur choisit dynamiquement la page `0` ou `3`, consomme les
  coordonnées trouvées puis appelle `ITEMS_PlaceItemForPlayer 0x471500`.
- `CubeQuickMove 0.1.3` redirige ces 27 sites vers le même relais, mais ne
  modifie les coordonnées que si la page runtime vaut exactement `3`. Son cold
  start a observé le premier calcul `1x3` à `3,3` depuis `0x15A25C`, contrôle
  préalable de l'espace client.
- Le témoin gameplay suivant identifie le producteur réel au call-site
  `0x15F94F`, dans la routine client `0x15F8B0`. Cette routine copie la paire x/y
  retournée dans sa structure de placement, puis le paquet `0x54` observé porte
  les coordonnées `4,3`. Le serveur accepte le transfert (`result=1`) et Vincent
  confirme visuellement l'épée `1x3` en bas à droite. `0x4FBC0E` reste une
  branche serveur page 0/3 gouvernée, mais n'est pas le témoin de cette action.
- L'audit du PluginPack épinglé
  `eezstreet/D2RL-Plugins@dc75b49ffbb67b887d7757ee00ee9a03bcde5d8a`
  ne trouve aucune clé Cube ni hook d'inventaire dans `plugin-misc`; les sites
  existants `0x18885B`, `0x18887F` et `0x542F40` sont distincts.

## Book of Lore

- `objects.txt` 3.2 associe `TowerTome` (`*ID=8`) a `OperateFn=6`. Cette
  donnee compilee est confirmee dans BKVince et dans l'extraction vanilla 3.2;
  elle ne fournit toutefois aucun pointeur natif a elle seule.
- `OBJECTS_OperateFunction06_TowerTome` est identifie a `0x5DC570`. Son ABI
  observee est `(operation, operateMode) -> int32`, avec le second argument
  inutilise et `operation+0x00/+0x08/+0x10/+0x20` correspondant au jeu, a
  l'objet, au joueur et a l'identifiant de classe objet.
- Le handler refuse les modes d'animation superieurs a 2, resout la quete 5 par
  `QUESTS_GetQuestData 0x518220`, exige son gate d'introduction, demarre le mode
  operating et l'evenement ENDANIM, puis envoie l'identifiant de chaine 127 et
  avance l'etat A1Q5. Cette structure de controle est l'equivalent exact du
  Tower Tome D2MOO 1.10f, sans transposer son adresse ni ses structures 32 bits.
- La signature etendue de `0x5DC570`, depuis le prologue jusqu'aux appels mode
  d'animation et quete, est unique. Le prologue court seul ne l'est pas : il a
  egalement un temoin a `0x58EFA0` et ne doit jamais etre utilise comme gate.
- `QUESTS_SendScrollMessage 0x517E90` recoit `(game, player, unit, uint16
  stringId)`, resout le client et construit le paquet serveur `0x27` de 0x28
  octets. Le layout observe est header `+0x00`, unit type `+0x01`, GUID `+0x02`,
  compteur de messages `+0x06` et premier string id `+0x0A`. Ses trois callers
  directs incluent Tower Tome a `0x5DC61E`.
- `CLIENT_HandleScrollMessagePacket27 0x19A630` ferme le trajet reseau. Pour un
  objet (`unitType=2`) et le menu ordinaire du Tower Tome, il conserve le GUID
  et transmet le string id de `packet+0x0A` a
  `CLIENT_ShowScrollMessageById 0x197BF0`.
- `CLIENT_ShowScrollMessageById` appelle `LANG_GetStringById 0x5F4A50`, cree le
  panneau natif de texte defilant et reconnait explicitement l'id 127. Son seul
  caller direct est le handler `0x27`; leurs signatures strictes sont uniques.
- Le callsite `0x197C5F` est l'appel direct de
  `CLIENT_ShowScrollMessageById` vers `LANG_GetStringById`. Ses cinq octets
  `E8 EC CD 45 00` sont verifies et Book of Lore `0.2.0` redirige uniquement ce
  site vers un relais proche; la fonction globale de langue reste intacte pour
  AdvancedItemTooltips, Transmogrify et les autres consommateurs.
- Le temoin client `0.2.0` hooke le handler `0x27`, arme une portee thread-local
  seulement pour `unitType=2`, menu `0` et id 127, puis substitue le premier
  texte configure au callsite `0x197C5F`. Il est compile mais non deploye et la
  configuration livree reste desactivee. La selection autoritaire Tower Tome,
  les identifiants prives, les filtres et l'historique ne sont pas implantes au
  runtime.
- La table runtime `OperateFn` n'expose aucun xref ou pointeur brut vers
  `0x5DC570` dans l'image hydratee. L'identite du handler est haute confiance,
  mais la propriete exacte de cette table et son dispatch indirect restent un
  gate distinct avant installation d'un hook de production.

## Magic Find Formula — linéarisation positive

- `D2GAME_ITEMS_RollItemQuality 0x4421B0` est la fonction serveur qui choisit
  la qualité d'un drop; son prologue strict de 38 octets est unique et son seul
  caller direct est `0x440FD9`. L'ABI observée est `(game, seedUnit,
  playerOrKiller, itemLevel, itemId, tcQualityMods) -> itemQuality`.
- La fonction lit la stat 80 sur le joueur ou monstre à `0x44236C`, ajoute celle
  du propriétaire d'un minion à `0x44238F`, traite `MF == 0` séparément puis
  rejette les qualités MF pour `MF <= -100` à `0x4423AA..0x4423AD`. Le registre
  `EBP` reçoit ensuite `MF + 100`.
- Les trois gates de diminishing returns comparent `MF + 100` à `110` et portent
  chacun `7F 04` : unique `0x4423CD` avec constante 250, set `0x44246A` avec
  500 et rare `0x4424F7` avec 600. Leurs témoins étendus de 15 octets sont
  individuellement uniques sous 92777.
- Le bloc magic à `0x442576` divise déjà directement par `MF + 100`; il est donc
  linéaire et ne doit pas être patché.
- Le mode `linear` peut remplacer seulement les trois `7F 04` par `90 90`.
  L'exécution tombe alors sur le chemin natif `ECX = MF + 100`, en conservant
  le gate négatif, les bases et minima ItemRatio, les modificateurs Treasure
  Class, le seuil 128, le RNG et la cascade des qualités. `vanilla` n'écrit rien.
- Aucun patch gouverné BKVince, addon ou PluginPack n'occupe ces six octets. Le
  manifeste du clone PluginPack `db420481` ne chevauche ni la fonction ni les
  sites; ses voisins les plus proches restent `0x441B10`, `0x442D2A` et
  `0x4432F4`. Le propriétaire canonique est `plugin-items.dll` sous
  `items.magicFindFormula`.
- Le module RuffnecKk a d'abord passé son incubation hybride, puis a été fusionné
  dans `plugin-items.dll`. Le manifeste/écritures passe à `139/139`, les tests
  Release à `26/26`, et le témoin autonome a été retiré après validation.
- La matrice runtime intégrée prouve `linear` (`90 90` aux trois sites),
  `vanilla` explicite ou clé absente (`7F 04`) et le refus fail-closed d'une
  valeur inconnue. Le cold start
  final BKVince charge `9/9` plugins, applique `19/19` patches et atteint `24/24`.
  Restent les témoins de drops `MF=-199/-100/-99/0/10/11`, MF positif élevé,
  minion/propriétaire et hôte/joiner.

## ExtendedMerc

- Une trace runtime 92777 sur `QtyTester` a capturé l'équipement et le retrait
  vanilla via le paquet client `0x51` de 17 octets. Le layout observé est
  `{opcode, cursorItemId, mercId, equippedItemId, bodyLoc}`; les callsites
  clients sont `0x15C599` et `0x161875` vers le constructeur `0xEC7D0`.
- Le callback serveur est `0x4C0E20`, ABI
  `(game, player, packet, packetSize) -> int32`. Il exige 17 octets, valide le
  mercenaire par son identifiant, lit les deux identifiants d'objet et accepte
  au gate initial un `bodyLoc < 11`. Le succès d'équipement atteint
  `SUNIT_AttachSound` depuis `0x4C1364` avec le son `0x5E`.
- `0x34A330` retourne en réalité le `UnitId` à `unit+0x08`; le nom gouverné a
  été corrigé en `UNITS_GetUnitId`.
- Une instance ouverte de `HirelingInventoryPanel` contient 42 enfants à
  `panel+0x58/+0x60`; les slots partagent le vtable `0x1CF3F20`, leur BodyLoc
  runtime est à `+0x5D8`, leur rectangle à `+0x70` et `isHireable` à `+0x638`.
  Les rings et l'amulet existent avec le drapeau à zéro; gloves et boots sont
  absents. Le passage à un de `slot_right_hand+0x638` a permis à Vincent
  d'équiper un ring qui est resté équipé.
- `0x2C9850` est la factory `InventorySlotWidget`, ABI effective
  `(descriptor ignoré, name, parent) -> widget*`. Elle alloue `0x640` octets,
  appelle le constructeur de base, pose le vtable `0x1CF3F20` et initialise les
  champs directs jusqu'à `isHireable+0x638`. La signature de 37 octets incluant
  la taille d'allocation est unique dans le `.text` 92777.
- Le vtable runtime place le finalizer `0x2CA970` à `+0x08`. Il charge le fond
  configuré par `0x858990` puis délègue au finalizer de base `0x2A8E60`. Les
  constructeurs natifs appellent ce vtable `+0x08` avant
  `UI_Widget_AddChild 0x854DE0`.
- `0x854DE0` a l'ABI `(parent, child) -> void`, ajoute le pointeur au tableau
  d'enfants à `parent+0x58/+0x60/+0x68` et en gère la croissance/ownership. Son
  prologue strict de 18 octets n'apparaît qu'une fois dans le `.text` 92777.
- Le constructeur `InventoryItemWidget 0x2A6FE0` remet le couple `cellSize` à
  zéro à `+0x5B8/+0x5BC`. Les chemins de rendu `0x2A7574/0x2A75A3` et
  `0x2A7969/0x2A7971` consomment respectivement sa largeur et sa hauteur; les
  layouts 92777 assignent ce champ à chaque `InventorySlotWidget`. Le probe
  reprend donc le couple d'un slot existant sain et refuse la création s'il ne
  peut pas l'obtenir.
- Le toggle d'interface appelle `UI_DispatchMessage 0x843D90` à `0xCE3B1` et
  reprend à `0xCE3B6`. La séquence de 23 octets à partir de `0xCE3A9` est
  unique; ce callsite étroit permet d'agir après la création du panneau sans
  prendre un second hook sur l'entrée globale du dispatcher.
- Le probe jetable Release 0.0.1 construit sous `analysis-cache` vérifie toutes
  ces signatures et le vtable avant de tenter la création de `slot_gloves`.
  Une première copie sans ressource-manifeste a été rejetée par D2RLoader avant
  son point de chargement et n'a installé aucun hook. Le build corrigé avec
  manifeste v2, `cellSize` hérité du layout hôte et sprite de socket natif est
  compilé mais non exécuté, à la demande de Vincent. Il remplace désormais le
  seul call `0xCE3B1` par un relais proche géré via `PatchCallRel32`, appelle le
  dispatcher vivant puis agit après son retour; son SHA-256 est
  `ECB258B6129816B09380272187F67228A0F2406693FFADA1DF526DBB97ADF668`.
- Le manifeste PluginPack exact des checkpoints `4f8b276` et `5b56690`
  contient 132 écritures. Aucune ne chevauche `0xCE3B1..0xCE3B5`, l'entrée
  jetable `0xCDE00`, ni les plages témoins des helpers `0x2C9850`, `0x2CA970`
  et `0x854DE0`. Le clone de travail a depuis évolué; il n'est pas utilisé pour
  reformuler rétroactivement cette preuve de coexistence.
- Le réseau, l'autorité serveur et l'adoption d'un widget existant sont donc
  prouvés. Restent ouverts le premier slot absent créé et attaché au runtime,
  son rendu/hit-test, la navigation controller et un placement réel dans les
  BodyLocs gloves/boots.

## MassID — clic de tome et identification autoritaire

- Le témoin 0.1.6 confirme que ni `0x228AB0` ni `0x2C7540` ne reçoit l'usage du
  tome dans le panneau actif : les hooks sont installés, le curseur Identify
  apparaît, mais aucun compteur client ne bouge. Ces deux surfaces sont retirées
  de MassID 0.1.7.
- `0x1AC830` reçoit l'objet utilisé en `RCX`, résout son record ItemsTxt, lit le
  champ `pSpell` à `+0x94`, collecte les paramètres de l'objet et appelle
  `0x1B9720` avant de retourner `1`. Cette chaîne est la construction cliente de
  l'usage ciblé qui arme le curseur Identify. La signature stricte de 32 octets
  est unique. MassID 0.1.7 l'intercepte avant son effet uniquement pour Shift +
  `ibk `; la confirmation gameplay reste ouverte.

- D2R 92777 possède deux pipelines parallèles pour les slots d’inventaire. Le
  premier témoin étudié commence à `0x2C7540`; il lit l’objet par le vtable
  `+0xC8` à `0x2C7801`, teste Ctrl à `0x2C78CD` puis Shift à `0x2C7CEC`. Le test
  physique de MassID 0.1.4 prouve cependant que le panneau clavier/souris actif
  de Vincent ne passe pas par ses actions `0x2AA9F0`/`0x15F660`.
- Le pipeline réellement utilisé commence à `0x228AB0`, avec la même ABI
  `(widget, eventState) -> void`. Il rejette l’état souris `5` à `0x228AF0`,
  résout l’objet exact par le vtable `+0xC8` à `0x228B2D`, teste Ctrl à
  `0x228BA3`, Shift à `0x228CA4`, puis appelle le comportement vanilla par le
  vtable `+0xD0` à `0x228CED`. MassID 0.1.5 hooke cette entrée avant toute
  délégation : un `ibk ` avec Shift, branche droite et curseur vide envoie la
  requête privée puis retourne, donc le curseur Identify n’est pas armé.
- `0x2AA9F0` accepte `(widget, item) -> void`; son unique caller direct est le
  chemin Shift ci-dessus. `0x15F660` accepte
  `(item, owner, page, flag, state) -> void` et possède trois callers, dont le
  même chemin Shift. Leurs signatures strictes de 32 octets sont uniques.
  MassID 0.1.4 les hookait aux retours `0x2C7D1F/0x2C7D59`; le témoin joueur a
  invalidé ce choix pour le panneau actif et 0.1.5 laisse désormais ces deux
  fonctions intactes.
- Le client Cain à `0x1141AB` appelle `0xEC820` avec l’opcode `0x34`.
  `0xEC820` sérialise exactement un opcode et cinq `uint32`, soit 21 octets,
  avant la queue sortante. Le paquet classique D2MOO de cinq octets n’est donc
  pas transposé au build 92777.
- Le callback serveur 92777 de l’opcode `0x34` commence à `0x4C6C90`. Il
  possède l’ABI
  `(game, player, packet, packetSize) -> int32`, exige `packetSize == 0x15`,
  désérialise les cinq champs et rejoint le traitement serveur Cain. Le chemin
  privé MassID peut ainsi être multiplexé avant le flux vanilla sans accrocher
  `D2GAME_PACKETCALLBACK_EntityAction 0x4B0470`, déjà possédé par Vendor Stock
  Refresh dans `plugin-items.dll`.
- Le témoin MassID 0.2.3 a prouvé que la queue cliente acceptait byte-exactement
  la requête privée `0x34`, tandis que le hook serveur à `0x4AE280` ne recevait
  rien. La table autoritaire commence à `0x1D2A790`, ancrée par le transport
  RemoteStash fonctionnel (`0x18 -> 0x4BFF30` à `0x1D2A850`). Elle place
  `0x4AE280` au slot `0x2E` et le vrai callback `0x34` à `0x4C6C90`, stocké à
  `0x1D2A930`. MassID 0.2.4 corrige ce mauvais RVA.
- `D2GAME_ITEMS_Identify 0x46E8C0` accepte `(game, player, item, flag)`. Le
  caller Cain à `0x53C8F5` passe `1`; la routine pose `IFLAG_IDENTIFIED`, met à
  jour les statlists si nécessaire, envoie `ITEMS_SendItemUpdate`, rafraîchit
  l’inventaire puis appelle `SUNIT_AttachSound(player, 6, player)`. Sa signature
  de 32 octets est unique dans `.text`.
- `0x46EA70` accepte `(game, item, player)`, pose le flag et exécute seulement le
  sous-chemin de statlists. Son unique caller à `0x4FD79D` l’utilise pendant une
  création d’objet avant d’autres étapes de placement. Il ne fournit pas seul
  l’update client et le son nécessaires à MassID; son emploi dans 0.1.0/0.1.1
  expliquait le retour joueur « rien ne se passe ».
- `SynchronizeItemAndBoundSkillQuantity 0x46F090` reçoit
  `(game, player, book, delta)`. Le caller tome à `0x5817BD` passe `-1` en `r9d`
  puis le tome en `r8`; la routine lit `STAT_QUANTITY`, calcule la nouvelle
  valeur et synchronise le skill lié. MassID peut donc consommer le nombre exact
  d’identifications réussies sans écrire directement la statistique.
- L’architecture retenue n’accroche ni le callback EntityAction partagé, ni
  `D2GAME_HandleUseItemPacket 0x4F40C0` possédé par Transmogrify, ni
  `CLIENT_QueueOutgoingPacket 0xEE2A0` déjà utilisé par EquippedItemToCube.
- Le client 0.1.5 n’appelle pas `UI_TOOLTIP_ResolveHoveredUnit 0x2A7810`, déjà
  partageable avec `plugin-items.dll`; il réutilise seulement la résolution
  virtuelle `+0xC8` déjà effectuée par `0x228AB0`. Propriété et page restent
  validées par le callback serveur autoritaire.
- Le témoin 0.1.5 invalide l'ABI précédemment attribuée au second argument des
  handlers `0x228AB0` et `0x2C7540`. Il s'agit d'un état d'événement transmis
  par valeur : chacun le sauvegarde sur sa pile et passe l'adresse de cette
  copie à la méthode virtuelle `widget+0xC8`. Le passage direct de cette valeur
  comme pointeur empêchait la résolution du tome et laissait vanilla armer le
  curseur Identify. MassID 0.1.6 reproduit la copie locale et couvre les deux
  handlers à leur entrée; la validation gameplay demeure ouverte.
- `Ctrl + Left Click to Drop/Move` ne fait pas partie du buffer produit par
  `ITEMS_BuildItemTooltip 0x2BD480`. Drop vient de
  `InventoryItemTooltipAppenderDrop`, aux appels `0x2279BD` et `0x2C552D`.
  Lorsque le Cube est ouvert, vanilla sélectionne
  `InventoryItemTooltipAppenderMove`, résolu à `0x2278DC`, `0x227936`,
  `0x2C5241`, `0x2C528D`, `0x2C53AB` et `0x2CA2E0`. Les pipelines legacy et
  alternatif conservent l’item en `r13`; les appels modernes Move le conservent
  en `r12`, tandis que Drop moderne le garde dans `[rbp-0x78]`. MassID 0.2.5
  redirige seulement ces huit appels de cinq octets vers trois relais proches.
  Le wrapper retourne le texte natif suivi du hint Mass ID sans balise de
  couleur : les deux lignes partagent donc exactement le style gris de
  l’appender actif. La localisation globale et `0x2BD480` restent libres pour
  le PluginPack.
- `LOCALIZATION_GetStringByKey 0x5F4B90` résout aussi `ItemStats1h`; son
  fingerprint sélectionne la même famille de treize locales intégrées
  qu’AdvancedItemTooltips.
- La page item `4` ne suffit pas à découvrir le shared stash depuis
  l’inventaire du joueur principal. Le témoin 0.2.5 identifie le coffre
  personnel mais retourne zéro devant les onglets partagés. Les handlers
  shared-stash `0x4C5570` et `0x4C6480` montrent que ces items appartiennent à
  des `UNIT_PLAYER` auxiliaires : ils résolvent le proxy par GUID, exigent
  l’état `0xBA` avec `STATES_CheckState 0x3351B0`, lisent son inventaire et comparent
  `INVENTORY_GetOwnerId 0x388BA0` au GUID du joueur principal.
- La liste auxiliaire est accessible sans nouveau hook par
  `INVENTORY_GetFirstCorpse 0x388E00` (lecture `inventory+0x68`),
  `INVENTORY_GetNextCorpse 0x38CD70` (lecture `record+0x10`) et
  `INVENTORY_GetUnitGUIDFromCorpse 0x2EF880` (premier dword du record). Le
  caller natif `0x425010` prouve la chaîne record GUID vers
  `SUNIT_GetServerUnit(game, UNIT_PLAYER, guid)`. Le témoin 0.2.6 a retourné
  `sharedContainers=0` parce que le marqueur avait été interprété à tort comme
  une statistique. MassID 0.2.7 reproduit l’appel natif à deux arguments
  `STATES_CheckState(proxy, 0xBA)`, puis impose le propriétaire natif avant toute
  identification; les hooks RemoteStash sur les deux handlers restent intacts.
- Le témoin 0.2.7 a validé cette découverte avec `sharedContainers=1001` et
  `sharedStash=3`, mais a révélé un second contrat : l’acteur passé à
  `ITEMS_SendItemUpdate 0x535F60` détermine le conteneur client. En passant le
  joueur principal pour un item appartenant à un proxy, l’identification était
  persistée dans le `.d2i`, tandis que le client créait un fantôme gelé dans le
  coffre personnel. Les GUID partagés sont demeurés absents du `.d2s`.
- `ITEMS_SendItemUpdate` reconnaît précisément un acteur `UNIT_PLAYER` portant
  l’état `0xBA` à `0x53623C`, vérifie son client propriétaire et appelle le
  sérialiseur `0x536410`. Le retrait shared natif passe le proxy à l’update de
  suppression `0x4C690C`, puis le joueur principal à l’update d’ajout
  `0x4C694B`. MassID 0.2.8 passe donc chaque proxy validé comme acteur de
  `D2GAME_ITEMS_Identify` pour ses propres items; ce routage attend encore son
  témoin gameplay avant promotion comme preuve fonctionnelle.

## CharmZone — zone de charmes autoritaire et rendu coopératif

- `ITEMS_CapturePacketState 0x382D20` expose l'ABI
  `(stateOut, item) -> stateOut`. La sortie de 16 octets contient le mode à
  `+0`, la page d'inventaire à `+4`, `x/y` en deux words à `+8/+10` et la page
  de noeud à `+12`. L'ordre des arguments est prouvé par les mouvements d'entrée
  `RCX -> RSI` pour la sortie et `RDX -> RBX` pour l'item. L'ancien ordre inversé
  écrivait 16 octets dans l'unité item; il a été corrigé avant la version 0.3.1.
- `ITEMS_GetDimensions 0x371850` complète cette position par la largeur et la
  hauteur sur un octet. La règle BKVince peut donc exiger le containment complet
  dans `x=0..10, y=4..7` sans lire de champ privé d'`ItemData`.
- `ITEMS_IsCharmUsable 0x36AE00` est l'équivalent 92777 exact du prédicat que
  BaseMod remplaçait dans D2Common 1.13d, ordinal 10415/RVA `0x47090`. Son ABI
  est `(item, player) -> int32` et sa signature stricte de 32 octets est unique.
  CharmZone appelle d'abord l'original puis refuse seulement les charms qui ne
  tiennent pas entièrement dans la zone. Il ne modifie ni owner ni statlist.
- `charstats.txt` BKVince reproduit byte-exactement BK pour les objets de départ :
  chaque classe reçoit six `mfd`, un `mfc` et un `mff`. `ITEMS_GetItemCode
  0x36EF50` retourne ce code compacté avec l'ABI `(item) -> uint32`; sa signature
  stricte de 32 octets est unique, 94 callers directs sont indexés et le caller
  serveur `0x4BB963` compare son retour à `box `. CharmZone 0.3.2 laisse donc ces
  trois codes nativement éligibles actifs partout dans l'inventaire joueur avant
  d'appliquer le containment aux autres charms. Les trois lignes `misc.txt` sont
  de type `chms`, mesurent 1×1 et portent `spawnable=0`; l'exception ne couvre
  donc pas un charm ordinaire droppable partageant accidentellement leur base.
- `STATLIST_MergeStatLists 0x2F81A0`, `STATLIST_GetOwner 0x2F8120` et
  `STATLIST_ExpireUnitStatlist 0x2F8290` restent gouvernés, mais ne sont plus
  possédés par CharmZone. Le prédicat natif offre une surface plus étroite et
  reproduit directement l'architecture BaseMod sans toucher au cycle de vie
  partagé des statlists.
- `UI_RenderItemIcon 0x15BB80` possède l'ABI
  `(item, packedScreenXY, scale, renderParams) -> void`, avec `x` dans le dword
  bas et `y` dans le dword haut; ces valeurs sont le coin supérieur gauche en
  coordonnées écran. Ses quatre callers directs sont `0x2267E9`, `0x2A739B`,
  `0x2A776C` et `0x2CE636`. Le trampoline inline D2RLoader masque le callsite à
  `_ReturnAddress()`: CharmZone filtre donc par un cache lock-free borné des
  pointeurs refusés par `ITEMS_IsCharmUsable`, puis déduplique chaque objet dans
  la file visuelle de la frame.
- Le constructeur de tooltip item `0x2BD480` reste volontairement intact : il
  appartient déjà à la chaîne Transmogrify, AdvancedItemTooltips et
  ExtendedItemStats. FloatingDamage demeure aussi l'unique propriétaire du
  hook D3D12/ImGui. Son callback historique reste réservé à ExtendedItemStats.
  CharmZone 0.3.2 conserve seulement les primitives rectangle de son registre
  multi-overlay : l'appel et la chaîne `Inactive outside Charm Zone` ne sont plus
  présents dans la DLL; l'ancienne clé TOML reste acceptée mais ignorée pour la
  compatibilité de mise à niveau.
- Le manifeste PluginPack épinglé au commit
  `dc75b49ffbb67b887d7757ee00ee9a03bcde5d8a` ne hooke ni `0x36AE00`, ni
  `0x382D20`, ni `0x15BB80`. Le cold start mod-local 92777 charge huit plugins,
  zéro échec; un charm synthétique de résistance feu +20 produit exactement
  `0 -> 20 -> 0` hors zone, dans la zone, puis hors zone. La version 0.3.1
  affiche le masque rouge aligné, le message au survol, et finit avec
  `classification failures=0`, `placement failures=0`, `drops=0`.
- La version 0.3.2 compile en Release et passe `1/1` test. La DLL source,
  package et runtime porte le SHA-256
  `E07F37F102CC05DC1E7E468E4348EF03375D173EB1D50C28A917D61E9892DBD7`;
  la configuration source/runtime porte
  `C150DF2FA1614FF6A2FE2306066A7C23DD2163EC341EA71B2B8C4826C3FF3ABF`.
  Le cold start du 2 août 2026 charge `11/11` plugins, zéro rejet, zéro échec,
  atteint `24/24`, installe les deux hooks CharmZone et enregistre l'overlay.
  L'observation gameplay des huit starters hors zone et de l'absence de tooltip
  reste ouverte parce que Vincent a repris la fenêtre D2R pendant l'automatisation.

## Services de quête répétables — paiement et charges natives

- `D2GAME_NPC_TryDeductGold 0x5416D0` reçoit `(game, player, amount)` et
  retourne un booléen. Il additionne l'or porté (`STAT_GOLD=14`) et le coffre
  personnel (`STAT_GOLDBANK=15`), refuse sans aucune mutation si le total est
  insuffisant, puis débite l'or porté avant le reliquat du coffre. Le coffre
  partagé n'est jamais lu. Son prologue strict de 32 octets est unique et le
  handler Repair All l'appelle à `0x53FF7B`.
- Les consommations gratuites sont des routines serveur distinctes par service
  et difficulté : Charsi `0x5DA1C0` utilise la quête 3, Anya `0x547C60` la
  quête `0x26`, et Larzuk `0x548B60` la quête `0x23`. Leur ABI observée est
  `(game, player) -> void`; chacune emploie les mêmes helpers gouvernés de
  lecture, pose et effacement des quest flags. Leurs signatures strictes de 32
  octets sont toutes uniques dans le `.text` 92777.
- Le patch BKVince `infinite-quest-rewards.json` neutralise actuellement deux
  appels pour chacun de ces trois services et ne couvre pas Akara. Il remplace
  donc la consommation des charges gratuites; il ne constitue ni un paiement,
  ni une nouvelle offre NPC après la quête.
- Le client possède déjà les actions et panneaux natifs nécessaires : imbue à
  `0x109100`, respec combiné à `0x109200` avec le texte 11168, socketing à
  `0x109300` et personalization à `0x109400`. Les labels localisés D2R existent
  déjà; BaseMod 1.13 proposait séparément Reset Stats et Reset Skills, mais
  cette séparation ne correspond pas au service natif D2R 3.2. La précédente
  attribution de l'imbue à `0x109500` était erronée : ce bloc enregistre Hire.
- `CLIENT_BuildNpcInteractionMenu 0x1147A0` construit le menu à partir du
  registre natif de 72 octets par NPC. Pour l'entrée texte 11168, le bloc
  `0x114C14..0x114C79` lit les flags `RewardGranted=0` et `RewardPending=1` de
  la quête `0x29` dans la difficulté courante : une charge consommée saute
  l'ajout, une charge pending conserve l'entrée et son callback. L'« émission
  serveur du menu » supposée précédemment est donc corrigée : la visibilité est
  filtrée côté client à partir des quest flags synchronisés; le clic reste
  validé côté serveur.
- Le callback affirmatif `0x1130D0` envoie exactement cinq octets, opcode
  `0x39` suivi du GUID NPC. Une lecture runtime contrôlée du build 92777 prouve
  que la case `0x39` du tableau serveur à `D2R+0x1D2A790` pointe vers
  `D2GAME_PACKETCALLBACK_Rcv0x39_ResetStatsAndSkillsWithNpc 0x4B2530`; les
  cases voisines `0x38`, `0x41` et `0x51` concordent avec leurs callbacks déjà
  gouvernés. Le handler exige la taille 5, valide l'interaction/NPC et exige
  `RewardPending` pour la quête désignée par le service Akara.
- La dernière validation Akara précède immédiatement l'appel à
  `D2GAME_PLAYER_ResetStatsAndSkills 0x580F20` à `0x4B2A23`. Cette transaction
  combinée appelle `D2GAME_PLAYER_ResetSkills 0x4360F0`, qui rembourse les
  ranks dans le stat 5, puis `D2GAME_PLAYER_ResetBaseStats 0x52DDF0`, qui remet
  Strength/Energy/Dexterity/Vitality aux bases de classe et rembourse le stat 4.
  Aucune mutation de stats ou skills ne précède cette couture.
- Après le respec gratuit, `QUESTS_ConsumeAkaraRespecReward 0x5D9AE0` pose
  `RewardGranted` et efface `RewardPending` pour la quête `0x29`. Un repeat paid
  doit donc débiter l'or après la validation finale et avant `0x580F20`, puis
  sauter `0x5D9AE0`; la première récompense native doit garder ce bookkeeping
  strictement inchangé.
- Le moteur serveur partagé `D2GAME_NPC_HandleItemServiceTransaction 0x4FC230`
  est rejoint par le callback opcode `0x34` à `0x4AE317` et par une continuation
  interne à `0x4B6087`. Son prologue strict de 23 octets est unique. Le record
  résolu dans `[rbp-0x78]` sélectionne Charsi, Larzuk ou Anya par son byte
  `+0x0B` égal à `1`, `2` ou `4`.
- Le byte `record+0x10` non nul déclenche le gate de disponibilité de quête à
  `0x4FC5EC`; le même byte conditionne plus bas les appels de consommation
  Charsi `0x5DA1C0`, Larzuk `0x548B60` et Anya `0x547C60`. Chacune de ces
  routines pose le flag 0 et efface le flag 1 de sa quête courante : elles ne
  sont donc pas idempotentes après une récompense consommée et doivent être
  supprimées sur un repeat.
- Les prédicats propres à chaque service sont suivis des validateurs serveur
  d'état de paquet/inventaire `0x471E90` et `0x472590`. Tous ces contrôles ont
  réussi lorsque le flux atteint `0x4FD442`; `0x4FD446` distribue ensuite vers
  Anya `0x4FD461`, Larzuk `0x4FD4C6` ou Charsi `0x4FD5A2`. Le premier chemin
  personnalise l'objet, le deuxième appelle `ITEMS_AddSockets 0x375560` à
  `0x4FD57B`, et le troisième retire puis recrée l'objet à partir de `0x4FD71A`.
  La signature stricte de 22 octets à `0x4FD442` est unique sous 92777 : c'est la
  couture commune post-validation/pré-mutation recherchée.
- Une insuffisance de fonds peut reprendre l'épilogue d'échec natif avec retour
  `1` avant le dispatch, sans emprunter la finalisation commune qui modifie
  encore flags, mode ou placement de l'objet. La conservation exacte de l'objet
  et le déverrouillage de l'UI restent toutefois à confirmer en runtime.
- Mettre `record+0x10=0` contourne à la fois le gate de quête et les trois appels
  de consommation. Une copie par invocation du record est donc la candidate la
  plus étroite; une mutation globale est interdite. Le cycle est désormais
  prouvé statiquement : le callback opcode `0x34` passe phase `0`; si le record
  natif porte une quête, le moteur consomme la récompense puis retourne `4` à
  `0x4FD36B`. Le callback copie alors le player id, le game et le paquet dans un
  job de `0x30` octets; `0x4B6050` résout de nouveau le player et rappelle
  `0x4FC230` à `0x4B6087` avec phase `1`. Un record `+0x10=0` saute entièrement
  le retour `4` et atteint la mutation dans l'appel initial : la couture de débit
  ne peut s'exécuter qu'une fois pour un repeat.
- Les onze rechargements du record local ne lisent ensuite que `+0x0B` et
  `+0x10`. Une copie call-local de 17 octets est donc suffisante et n'a aucune
  durée de vie à prolonger vers le worker; le chemin gratuit natif garde son
  record original, sa consommation et ses deux phases.
- La couture `0x4FD442` précède le callsite Larzuk déjà possédé par
  ForceLarzukSockets et peut filtrer strictement les sélecteurs `1/2/4`; MassID
  et les autres transactions opcode `0x34` restent délégués. L'affichage
  dynamique possède aussi une couture étroite : `CLIENT_BuildNpcInteractionMenu`
  traite Imbue `4017`, Add Sockets `22748` et Personalize `22749` par son chemin
  générique; Akara `11168` passe d'abord par le gate unique `0x114C20`. À
  `0x114CD7`, le builder charge le string id enregistré, appelle
  `LANG_GetStringById 0x5F4A50` à `0x114CDC`, puis copie immédiatement le texte
  dans un buffer local de `0x200` octets avant l'ajout UI `0x1E9030`. Un wrapper
  de callsite peut donc retourner un texte formaté par joueur sans modifier le
  string global. Le niveau est le stat 12 lu par les helpers client gouvernés;
  le serveur reste seul autoritaire pour le prix et le débit.

## BaseMod CPU Fix — GameUX legacy

- Le seul artefact CPU de BaseMod est `Other/GameUX Rundll32 CPU Fix.txt`. Il
  vide par registre la valeur système
  `HKCR\Local Settings\Software\Microsoft\Windows\GameUX\ServiceLocation\Games`
  et demande un redémarrage pour contourner le processus Game Explorer
  `rundll32.exe` bloqué sur un cœur sous Windows 7. Il ne modifie aucun binaire
  Diablo et ne règle aucune cadence de simulation.
- Le runtime actuel est Windows 11 build 26200 et ne possède pas cette clé.
  L'exécutable installé `D2R.exe` importe seulement `D2R_loader.dll`; les 39
  imports de ce loader ne contiennent ni GameUX, ni Game Explorer, ni Rundll32.
  Aucune friction ni chaîne d'appel ne justifie donc une modification globale
  de HKCR : ce correctif est obsolète pour D2R 3.2.
- `PotionAutoPickup` est indépendant. Son hook `0x4B9DF0` correspond par contrôle
  de flux au callback serveur opcode `0x16` de ramassage d'un objet au sol : il
  exige 17 octets, résout le GUID item puis rejoint le helper de pickup. La
  référence sémantique D2MOO 1.10f porte le même rôle, avec un paquet legacy de
  13 octets qui n'est pas transposé. Le compteur nommé
  `minimum_interval_frames` avance donc par paquet de ramassage, jamais par frame,
  et ne crée aucun polling au repos.
- Le chemin de pickup rejoint `INVENTORY_GetFreeBeltSlot 0x3862D0` à
  `0x471C31`. Son ABI prouvée est `(inventory, item, freeSlotOut,
  allowAnyBeltable) -> int32` et sa signature stricte de 32 octets est unique.
  Il résout la grille de ceinture par `INVENTORY_ResolveOccupancyGrid 0x38B070`
  avec l’index 1 et le descripteur statique `0x237B638`; la grille expose largeur
  `+0x10`, hauteur `+0x11` et tableau de pointeurs item `+0x18`.
- PotionAutoPickup 1.1.1 lit cette grille avant chaque choix et arme un override
  thread-local uniquement pour l’inventaire et l’objet exacts du pickup. Les
  listes TOML `tiers` et `overflow_tiers` couvrent indépendamment les 12 codes;
  aucun RVA, layout ou ABI 2.4 n’est transposé.
- Une capture runtime contrôlée du build 92777 confirme les 18 pointeurs des
  cases `0x01`–`0x12` de la table serveur `0x1D2A790`, de `0x4AC050` à
  `0x4AD4F0`. La version 1.1.1 détourne ces cases vers un dispatcher qui appelle
  d’abord chaque callback original puis scanne les potions; le callback pickup
  `0x16` à `0x4B9DF0` reste intact. `ITEMS_GetBeltType 0x349720` lit le byte
  ItemsTxt `+0x140` du belt équipé pour limiter la capacité à 4, 8, 12 ou 16
  cases.
- Cold start mod-local du 8 août 2026 : DLL build/source/runtime identique
  (`60295EB5…D94D6A8`), TOML source/runtime identique
  (`B32C9BB6…4ED3D`), hook belt accepté, les 18 cases d’action redirigées vers
  une cible unique interne à la DLL, `18/18` patchsets, `14/14` plugins actifs,
  zéro rejet/échec et startup `24/24`. Le log 1.1.1 confirme le preset exact;
  les témoins gameplay de destination restent ouverts.

## RogueScoutMovement — suivi walk/run et vélocité absolue

- `AITHINK_Fn061_Hireable` est confirmé autour de `0x5BEC20`. Ses appels à
  `D2GAME_PETAI_PetMove 0x5C1460` utilisent le motion type `0` pour le suivi
  proche et `1` pour le rattrapage. Les quatre xrefs directes de la fonction
  sont `0x5BEC72`, `0x5BECA1`, `0x5BEFCD` et `0x5BF00A`; la signature
  `48 89 5C 24 08 48 89 74 24 10 48 89 7C 24 18 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 48 8B F1 4D 63 F1 49 8B C8 49 8B F8 4C 8B EA E8 ?? ?? ?? ??`
  ne correspond qu'à l'entrée `0x5C1460` dans le build 92777.
- L'ABI x64 observée est `(game, owner, unit, motionType, run,
  velocityPercent, steps) -> int32`. La fonction appelle
  `AITACTICS_SetVelocity 0x4A7270`; D2MOO confirme que le paramètre de vitesse
  devient le stat temporaire `velocitypercent`, tandis que `Velocity=11` dans
  `monstats.txt` demeure la base de déplacement. Le chemin de rattrapage remplace
  un argument nul par une valeur aléatoire de 50 à 64 : transmettre simplement
  zéro ne suffit donc pas à garantir la base 11.
- `D2GAME_MONSTERMODE_SetVelocityParams 0x4473F0` reçoit
  `(aiParam, pathType, velocityPercent, distance)`. Il écrit les arguments
  non nuls aux offsets `+0x20`, `+0x24` et `+0x28`; zéro signifie « conserver ».
  Le prototype autonome retient par conséquent un second hook à cette entrée,
  armé seulement pendant un appel `PetMove` motion `0/1`, avec correspondance
  thread-local du pointeur `aiParam`. Une vitesse configurée à 11 efface alors
  explicitement `aiParam+0x24`, sans toucher un autre monstre, un autre chemin
  de mouvement ou un autre thread.
- `UNITS_GetUnitType 0x34B9D0` retourne `[unit+0x00]`,
  `UNITS_GetClassId 0x349860` retourne `[unit+0x04]`,
  `UNITS_GetRoom 0x34B440` et `DUNGEON_IsRoomInTown 0x2F0750` fournissent les
  filtres restants. Le plugin cible seulement le type monstre, la classe
  `roguehire=271`, et préserve tous les motion types autres que `0/1`; combat,
  recul, errance, warp et espacement demeurent natifs.
- L'audit du PluginPack eezstreet épinglé à
  `dc75b49ffbb67b887d7757ee00ee9a03bcde5d8a` ne trouve aucun owner de
  `0x5C1460` ou `0x4473F0`. `ReviveOverhaul` hooke `AITACTICS_SetVelocity`
  `0x4A7270`, une entrée distincte en amont; le nouveau hook aval reste inerte
  hors de son scope Rogue et conserve donc la chaîne existante.

## Static Field — package PD2 avec fonctions serveur D2R 3.2

- Le handler serveur natif de Static Field commence à `0x5546B0`. Son ABI x64
  observée est `(game, unit, skillId, skillLevel) -> int32`; il résout la ligne
  SkillsTxt, évalue `calc2`, `calc1` et `aurarangecalc`, puis délègue la
  sélection des cibles à `0x4327D0` avec le callback `0x5556A0`. La signature
  wildcardée de 53 octets ne correspond qu'à cette entrée dans le build 92777.
- Le `srvdofunc=20` D2R ne lit ni `auratargetstate`, ni `auralencalc`, ni les
  paires `aurastat/aurastatcalc`. Le portage exact ne peut donc pas être obtenu
  par la seule ligne `skills.txt`; le `srvdofunc=160` de PD2 est spécifique à
  PD2 et correspond à un autre comportement dans D2R 3.2.
- Le handler natif de malédiction/état commence à `0x55D6B0` avec la même ABI.
  Il valide le target state et le premier aura stat, évalue le rayon et la
  durée, évalue jusqu'à six stats d'aura et énumère les cibles selon
  `aurafilter`. Sa signature wildcardée de 64 octets est elle aussi unique.
- `StaticFieldRework` conserve le handler 20 comme autorité pour les 25 % de
  vie, puis appelle le handler 30 avec le même skill et niveau. La ligne
  BKVince fournit donc seulement les données natives : rayon
  `min(ln12 / 2, 14)`, état `staticfield_debuff`, durée liée à Lightning
  Mastery et `lightresist=-min(lvl,100)`. Les statlists, le filtrage et
  l'autorité multijoueur restent entièrement dans les fonctions D2R.
- L'audit du PluginPack épinglé à
  `dc75b49ffbb67b887d7757ee00ee9a03bcde5d8a` ne trouve aucun hook à ces deux
  entrées. Le patch Telekinesis de `plugin-skills` à `0x554936` est voisin mais
  hors du corps de Static Field; les quatre autres DLL ne possèdent aucun site
  chevauchant.

## Mechanics 2.0 MEC-00 — sous-graphe damage 92777

- Le workbench vérifié ferme statiquement `D2Damage` à `0x180` octets. Les
  constructeurs de copie/déplacement et le destructeur
  `0x4494B0/0x449760/0x4496E0` prouvent le conteneur SBO et le
  sous-objet possédé; les offsets gameplay utiles sont consignés dans
  `Mission/mechanics-native-proof-92777.md`.
- `D2Damage_MoveConstructor 0x449760` possède une signature stricte de 33
  octets unique et quatre xrefs. Le linker de combat `0x4507B0` copie d'abord
  le dommage vers un temporaire par `0x4494B0`, puis le déplace dans le nœud à
  `0x450908`; toute provenance externe attachée au pointeur doit donc suivre
  la copie **et** ce déplacement.
- `SUNITDMG_FillDamageValues 0x44C030` résout Critical/Deadly dans l'ordre
  court-circuité weapon mastery, passive Critical Strike, Deadly Strike. Le
  succès double uniquement `damage+0x18` et pose le même bit
  `resultFlags+0x04 & 0x2000`; l'origine Critical ou Deadly est donc perdue
  après résolution.
- `SUNITDMG_ExecuteEvents 0x44CE80` est une couture autoritaire de commit
  partagée, pas une couture exclusivement melee. Le callsite melee
  `0x44B3FA` passe `bMissile=0`; le missile `0x436F95` passe `1`. Toute
  conclusion « hit melee réussi » doit encore corréler ce flag, `SUCCESS`, le
  caller et un témoin runtime read-only.
- Le life/mana leech possède un consumer unique à `0x450C90`, appelé seulement
  par ExecuteEvents à `0x44D038`. Les pourcentages bruts vivent à
  `D2Damage+0x120/+0x124`, puis sont transformés en montants effectifs après
  Drain, diviseurs de difficulté, troncatures et caps.
- Les jets Critical/Deadly, monster critical et overlay leech prennent le seed
  de l'attacker par `UNITS_GetSeed 0x34A1E0`, mais appliquent le LCG et le
  modulo inline. Aucune primitive callable commune de roll n'est démontrée sur
  ce sous-graphe.
- Le dispatcher `0x5881E0` transporte neuf positions aux callbacks, et non les
  sept arguments exacts du préfixe legacy. Les handlers statiques sont Freeze
  `0x583580`, Open Wounds `0x584170`, Crushing Blow `0x583150` et
  SkillOnAttack/Hit/Kill `0x583B30`. Ils sont appelés indirectement; aucune
  table D2R associant les numéros 14/15/16/20 n'a été retrouvée, et les labels
  numériques reposent sur l'isomorphisme sémantique complet avec D2MOO épinglé.
- Open Wounds appelle le helper curse/statlist `0x433D20`; création, refresh,
  remplacement, expiration et callback `0x436240` sont fermés statiquement.
  La constante BSS duration/stat, les stacks multi-attacker, disconnect,
  fin de game et GUID reuse restent des témoins MEC-01.
- `0x44DF10` calcule résistances/réductions/absorb/total et délègue au resolver
  `0x4523E0`; `0x44A9B0` finalise modes, réactions et mort. La chaîne prouve un
  transport natif cohérent, mais pas encore une primitive sûre d'application
  secondaire ni une architecture `Pd2CombatCore`.
- `0x48E060` est confirmé comme prédicat serveur
  `(game, attacker, candidate)->int32`. `0x4398B0` est seulement `partial` :
  son entrée, ses huit callers, son descriptor room/x/y et son callback sont
  observés, mais ses bornes, rooms adjacentes, doublons et LOS restent ouverts.

## Crafted — niveau requis identique au Rare

- `ITEMS_GetRequiredLevel 0x376DE0` possède l'ABI observée
  `(item, unit) -> int32`. Ses trois xrefs indexées sont le wrapper exporté en
  tail jump `0x371AA0`, l'appel du prédicat partagé d'équipement à `0x36C06D`
  et l'appel récursif des objets socketés à `0x3771D3`.
- Le dispatch lit la qualité à `ItemData+0x00`, puis sépare Magic `4`, Set `5`,
  Rare `6`, Unique `7` et Crafted `8`. Le chemin Crafted initialise `r12d` à
  `10` à `0x376EBA`, parcourt les trois couples préfixe/suffixe et ajoute `3`
  à `0x377089` ou `0x377092` lorsque le record correspondant existe.
- Le témoin `44 8D 61 09 44 8D 79 4D` de l'initialisation Crafted est unique
  dans `.text`. Le témoin de boucle de 24 octets commençant à `0x377084` et
  contenant les deux `ADD r12d,3` est également unique sous 92777.
- Après la boucle, le moteur ajoute le bonus à l'exigence maximale des affixes,
  conserve le cap `maxLevel-1`, puis rejoint le chemin commun. Celui-ci conserve
  les exigences des socketables et skills, ajoute la stat `item_levelreq=92` à
  `0x3779E4`, borne le résultat à zéro et applique l'ajustement dépendant du
  personnage. Le patch ne modifie aucun de ces consommateurs.
- La référence sémantique épinglée
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`,
  `source/D2Common/src/Items/Items.cpp:1306-1334`, porte exactement le même
  triplet `10`, `+3 prefix`, `+3 suffix`; aucune adresse ni structure 32 bits
  n'est transposée.
- `crafted-rare-level-requirements.json` neutralise seulement les trois
  instructions gouvernées. Le nombre d'affixes reste entièrement propriétaire
  de `ProgressiveAffixesPlugin`, les recettes restent inchangées et les objets
  Rare ne deviennent pas des entrées de crafting.

## Environnements sonores — héritage des Terror Zones

- `SOUNDENVIRON_GetRecord 0x3B0BA0` reçoit l'index demandé en `ECX`, résout le
  contexte data `3`, lit le compteur compilé à `DataTables+0x508`, puis rejette
  les index négatifs ou supérieurs ou égaux au compteur. Le bloc d'échec à
  `0x3B0C46` rejoint l'assertion à `0x3B0C82` lorsque le compteur est non nul.
- Le prototype No Terror Zone Music supprimait la ligne stable 75 de
  `soundenviron.txt`. Une demande native de l'environnement 75 confrontée au
  compteur 75 satisfait donc exactement `index >= count`; le texte
  `!SoundGetNumSoundEnviron()` affiché par l'assertion ne signifie pas que le
  compteur devait être nul.
- Dans le résolveur d'environnement hérité autour de `0x20C370`, la ligne
  marquée `InheritEnvrionment=1` part de l'environnement normal courant, puis
  remplace onze champs. La paire commençant à `0x20C4EC` charge `Song` depuis
  `record+0x34`; le store de six octets `89 05 1F B5 88 02` à `0x20C4EF` est
  le seul write du morceau dans cette séquence.
- Cette séquence stricte possède une seule occurrence dans `.text` du build
  92777. Le patch BKVince restaure la ligne vanilla 75 et neutralise uniquement
  ce store, de sorte que le morceau de la zone de base reste actif tandis que
  les dix autres champs hérités continuent d'être copiés nativement.

## Floating Damage — commit HP visible

- La mutation principale des HP dans `SUNITDMG_ExecuteEvents` est inline à
  `0x44D06C..0x44D093`. Le callsite final `0x44D093` appelle
  `STATLIST_SetUnitStat 0x2F7D10` avec `(defender, stat 6, newHpFixed, layer 0)`.
  Son appel de cinq octets `E8 78 AC EA FF` et son contexte de 21 octets à
  `0x44D083` sont uniques sous le build 92777.
- Les non-volatiles `R14=attacker` et `RDI=D2Damage` restent vivants au callsite.
  FloatingDamage 1.2.1 les transporte par un relais proche, conserve les quatre
  arguments natifs du setter et calcule seulement la différence des deux bornes
  HP entières visibles. Il ne hooke pas l'entrée `0x44CE80`, qui reste possédée
  par MeleeSplash.
- Le témoin runtime `169472 -> 168480` correspond à `992/256 = 3.875`; le popup
  produit vaut `4`, là où l'ancien décalage séparé `finalDamage >> 8` donnait
  `3`. Le changement reste un observateur et ne modifie aucune valeur de combat.

## Aura Enchanted — pool et scaling PD2

- La table runtime de callbacks à la RVA `0x2395FE0` contient exactement 43
  pointeurs. Son masque nul/non nul correspond aux indices MonUMod hérités et
  l'index `30` pointe sur `MONSTERUNIQUE_UMod30_AuraEnchanted 0x495CD0`.
  L'entrée possède une signature stricte de 32 octets unique sous 92777.
- Le chemin ordinaire lit `STAT_LEVEL=12`, borne le niveau source à au moins 1,
  choisit un record pondéré, calcule
  `multiplier * (mlvl + offset) / divisor`, puis borne le résultat entre 1 et
  99 avant d'ajouter et d'assigner le skill. Lord de Seis impose l'index 5 dans
  Vanilla; Uber Mephisto conserve une branche séparée Conviction 123 niveau 20.
- La table Vanilla à `0x1D1DE70` comporte sept records de `0x18` octets, pas
  huit : `[98/6, 368/6, 108/5, 365/7, 123/8, 122/8, 369/8]`, avec un seuil 20
  sur Holy Shock. La table distincte qui commence à `0x1D1DF20` chevauche
  l'espace qu'occuperait un huitième record; elle ne peut donc pas être écrasée.
- Le patch BKVince place huit records dans la plage `0xA5840..0xA58FF`, à
  l'intérieur d'un intervalle `CC` `0xA583B..0xA5EBF` sans xref indexée. Les
  six lectures de table et les trois bornes de boucle sont redirigées vers
  `[451,452,369,365,368,453,454,455]`, tous pondérés 1 et divisés par 7. Le
  plafond natif devient 13 et l'index spécial de Lord de Seis devient 1, soit
  `MonFanaticism 452` dans le nouvel ordre.
- Les huit rows `skills.txt` sont monster-only et ont une portée doublée.
  Might, Concentration, Fanaticism, Conviction, Holy Fire, Holy Freeze et Holy
  Shock reprennent leurs paramètres PD2. `MonVigor 455` conserve expressément
  la courbe BKVince `Param5=7`, `Param6=50`, soit environ 13–39 % aux niveaux
  d'aura 1–13, sans le passif PD2 additionnel `blvl/2`.
- Au cold start complet du 12 août 2026, D2RLoader a appliqué `17/17` fichiers
  de patches et activé `19/19` plugins, sans rejet ni échec, puis atteint
  `24/24`. La relecture mémoire a confirmé les 19 écritures et les huit records
  exacts. Les tables source/runtime `skills.txt` et `monumod.txt` sont
  hash-identiques; Aura Enchanted reste à `6/6/12`.

La référence sémantique
`D2MOO@19019806df7f3e877fa105b05395d1e3597e2316:source/D2Game/src/MONSTER/MonsterUnique.cpp:601-652`
explique les champs et le flux historique; aucune adresse, structure ou ABI
32 bits n'a été transposée.

## Floating Damage — projection native des unités

- `RENDER_ProjectUnitToScreen 0x76A7D0` possède six appels directs sous le
  build 92777. Le chemin natif `0x198F75` lui passe le contexte de rendu en
  `RCX`, une `Unit*` en `RDX`, une sortie de deux `float` en `R8` et `R9B=1`,
  puis teste `AL` et convertit les deux coordonnées.
- Ce même caller borne ensuite Y avec `UI_GetNativeHeight 0x7F4A0` et X avec
  `UI_GetNativeWidth 0x7F510`. La sortie est donc dans l'espace UI natif; un
  overlay ImGui doit la convertir vers sa propre largeur et sa propre hauteur.
- `RENDER_GetThreadContextRoot 0x685750` retourne la racine TLS du renderer;
  les callers natifs lisent son contexte actif à `+0x20`. Sa signature stricte
  de 37 octets est unique.
- `CLIENT_GetUnitByIdAndType 0x9A5D0` résout `(unitId, unitType) -> Unit*`.
  L'appel à `0x198D66` charge exactement ces deux champs depuis un paquet, et
  la signature stricte de 32 octets distingue cette table client de la fonction
  adjacente `0x9A5A0`.
- Le test runtime invalide l'appel direct de Floating Damage 1.2.5 depuis sa
  passe d'overlay : le hook HP capture bien les dégâts, mais le témoin de
  projection n'est jamais atteint. La racine de contexte est liée au TLS du
  thread de rendu et n'est pas une primitive appelable arbitrairement depuis
  le thread DirectX/ImGui du plugin.
- Floating Damage 1.2.6 observait l'entrée native `0x76A7D0` avec un
  hook MinHook étroit. L'original s'exécute sur le thread choisi par D2R; après
  un résultat réussi, le plugin copie seulement `(type, id, x, y, tick)` dans
  un cache atomique borné. L'overlay ne conserve aucun pointeur `Unit` et ne
  rappelle aucune primitive renderer. Une entrée absente, périmée ou hors des
  bornes ImGui supprime le popup; aucun fallback sur le joueur n'est permis.
- Le témoin gameplay invalide ce modèle passif : seuls les monstres que les
  consommateurs UI de D2R projettent naturellement entrent dans le cache. Dans
  un groupe, la cible principale apparaît seule; son popup peut ensuite être
  masqué à l'expiration de 250 ms malgré une durée configurée de 850 ms.
- Floating Damage 1.2.7 utilise le même propriétaire de hook, mais chaque hit
  dépose l'identifiant de sa cible dans un registre atomique dédupliqué. Sur le
  thread de rendu, le hook résout chaque demande avec
  `CLIENT_GetUnitByIdAndType 0x9A5D0` et appelle le trampoline original avec le
  contexte courant. Les projections échouées invalident explicitement le cache;
  aucun pointeur client ni pixel périmé n'est conservé.
- Le témoin gameplay invalide aussi ce rendez-vous conditionnel : pendant un
  déplacement, seule la cible principale continue d'être rafraîchie et les
  autres popups du groupe disparaissent avant leur durée configurée.
- `CLIENT_UpdateCameraOffsets 0xB9B90` résout le joueur local, calcule les
  offsets caméra depuis ses coordonnées et les dimensions natives, puis applique
  le déplacement caméra optionnel. Son unique callsite `0x93D79` se trouve dans
  la passe client principale; sa signature stricte de 33 octets est unique.
- Floating Damage 1.2.8 appelle d'abord cette mise à jour originale, récupère la
  racine TLS renderer et traite ensuite le registre multi-cibles à chaque passe
  caméra. `RENDER_ProjectUnitToScreen` n'est plus hookée : elle est appelée
  directement avec le contexte courant après la mise à jour de la caméra.
- L'audit de collision ne trouve aucune référence à `0xB9B90` dans le
  PluginPack épinglé et aucun propriétaire concurrent dans les logs installés.
  Le cold start complet passe; le comportement gameplay reste à observer.
- L'absence de `DPS` observée avec 1.2.8 est indépendante de `0xB9B90` : le
  binaire runtime `plugin-items.dll` issu du fork expérimental
  `RuffDood/D2RL-Plugins:codex/pluginpack-foundation` embarque le prototype
  RuffnecKk ExtendedItemStats et un renderer D3D12/ImGui de repli. Cette
  composition n'existe pas dans l'upstream officiel eezstreet. Le binaire ne
  contient pas le nom d'export
  `FloatingDamageRegisterExternalOverlay`; les deux renderers posaient donc
  leurs detours concurrents selon le timing du démarrage.
- Floating Damage 1.2.9 détecte les exports
  `ExtendedItemStatsOwnsTooltipPipeline` et
  `ExtendedItemStatsTransformTooltip`, laisse le renderer embarqué s'installer
  d'abord, puis pose son detour en dernier. La preuve runtime observe la queue
  DirectX 12, le premier `Present`, l'initialisation ImGui et la première frame
  soumise. Vincent confirme le compteur `DPS`, l'ancrage sur les monstres et
  les popups simultanés sur un groupe.

## Community Pack Item Durability - post-compilation des Items

- `DATATBLS_CompileItemsTxt 0x315FD0` est appelee par l'orchestrateur gouverne
  au callsite `0x301773`, apres la compilation d'ItemTypes.
- La fonction compile trois tables de records de stride `0x1C0`, les concatene
  dans le vecteur `DataTables+0x15A0`, puis construit les index associes.
- Sa signature instruction-alignee de 32 octets est
  `48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D AC 24 C0 E6 FF FF`.
- Le port Item Durability 1.2.1 appelle d'abord l'original, puis applique une
  passe idempotente sur les records actifs. Le getter `0x314110` reste vanilla.
- Deux cold starts de la pile complete ont valide les deux ordres : Pack avant
  CubeOutputQuantity 1.0.2, puis Cube avant Pack. Cube charge ses trois hooks;
  la passe Items traite les contextes 2 et 3, dont 800 items/117 item types et
  42 records ranged/2 types reparables dans le contexte expansion.

## MapSense — premier marqueur d'unité dans l'automap native

- `AUTOMAP_RenderUnit 0xD76E0` reçoit `(Unit*, AutomapContext*)`. Sa signature
  stricte de 32 octets est unique et aucun propriétaire concurrent n'apparaît
  dans le PluginPack eezstreet épinglé. L'ancien observateur MapSense qui visait
  la même entrée demeure hors de la cible CMake.
- Le chemin natif appelle `UNITS_GetClientCoordX 0x34AF60`,
  `UNITS_GetClientCoordY 0x34AFB0`, puis
  `AUTOMAP_ProjectClientCoordinatesToScreen 0xD4910`. Le couple projeté est comparé au
  rectangle `context+0x18/+0x1C/+0x20/+0x24`, puis transmis sans translation
  additionnelle à l'icône native `0xD6DB0`.
- `UI_GetNativeWidth 0x7F510` et `UI_GetNativeHeight 0x7F4A0` bornent l'espace
  UI natif. Leur égalité exacte avec `ImGui::DisplaySize` reste une gate
  dynamique : MapSense n'applique un ratio que lorsque les deux échelles sont
  finies, positives et uniformes; toute discordance supprime le marqueur.
- `UNITS_GetUnitMode 0x34AB60` lit `Unit+0x0C`. Le témoin unique `0x51F280`
  prouve le type monstre 1, `MonsterData*` à `Unit+0x10`, les flags de rang à
  `MonsterData+0x1A` et le masque haut rang `0x0E`. Le bit minion `0x10` reste
  corroboré historiquement; son rejet de la catégorie normale est conservateur.
- Le prototype 0.5.0 appelle toujours l'original en premier et exactement une
  fois, filtre un monstre normal vivant dans un rayon circulaire fixe de 60,
  puis publie seulement un POD atomique expirant après 120 ms. Aucun pointeur
  D2R ne traverse vers le thread Present et aucune énumération globale n'est
  ajoutée. Le témoin gameplay doit encore confirmer modes, classification,
  alignement, zoom et comportement centre/gauche/droite/ultrawide.

## MapSense — unités de projection et topologie extérieure Vis/Warp

### Hypothèse rejetée — visibilité native de l'automap et du menu Pause

- PrimeMH au commit `92b6a97d8e56346f8b63a88bb647c1af044d2c8b`
  décrit bien un tableau `MenuStates` où `pause_menu_visible` est à `+0x09` et
  `automap_visible` à `+0x0A`. Son clone local utilise toutefois encore une
  ancienne base codée en dur, `0x1EBD158`; cette structure ne permet donc pas
  de transposer directement les offsets vers le runtime 3.3.93847.
- L'association proposée entre cette structure et les octets `0x2AA6A69` /
  `0x2AA6A6A` était une inférence non démontrée à partir de deux écritures
  adjacentes dans la grande routine de recherche/initialisation `0x1234D0`.
  Les instructions `SETE` à `0x12351D` et `0x1235A9` prouvent leurs seules
  destinations, pas la sémantique des deux octets.
- La preuve runtime du 27 août 2026 réfute l'hypothèse : `0x2AA6A6A` restait à
  zéro pendant que l'automap native était visiblement ouverte. Le candidat
  MapSense 0.10.2 qui exigeait cet octet a donc supprimé tous les marqueurs et
  a été retiré du runtime. Les quatre identifications ont été supprimées de
  `known-rvas.json`; elles ne doivent plus être consommées comme états UI.
- MapSense 0.10.3 traite Tab et Escape à leur message Win32 initial pour vider
  immédiatement ses pixels mis en cache, puis laisse le prochain passage
  gouverné d'`AUTOMAP_RenderUnit` republier les marqueurs au retour en jeu.
  Cette correction ne revendique aucune nouvelle identification native.
- Le contrat visuel est `automap_visible != 0 && pause_menu_visible == 0`.
  Lors du premier passage visible vers caché, MapSense invalide également les
  projections et marqueurs mémorisés : aucune frame âgée de 250 ms ne peut
  réapparaître sur le menu Pause ou après une fermeture remappée.

- Les getters `0x34AF60/0x34AFB0` lisent `Unit+0x38 -> Path+0x08/+0x0C` :
  ce sont les coordonnées client/dimétriques déjà consommées par
  `AUTOMAP_ProjectClientCoordinatesToScreen 0xD4910`, pas les subtiles monde.
  L'écrivain du Path conserve séparément les subtiles à `+0x10/+0x14`, appelle
  `PATH_ConvertSubtileToClientCoordinates 0x334E00`, puis range le résultat à
  `+0x08/+0x0C`. Le corps gouverné de `0x334E00` prouve exactement
  `clientX = 16 * (subtileX - subtileY)` et
  `clientY = 8 * (subtileX + subtileY)`.
- Le résolveur MapSense produisait waypoint et sorties en subtiles
  (`gameTile * 5 + relativeSubtile`), mais la candidate 0.9.6 passait ces
  valeurs directement à `0xD4910` tout en projetant le joueur avec les getters
  client. La 0.9.7 a corrigé cette unité; son témoin gameplay du 26 août montre
  toutefois deux erreurs résiduelles distinctes : le preset du waypoint ne
  coïncide pas avec la position finale de son objet actif, et le centre d'une
  bordure de room ne coïncide pas avec l'ouverture traversable réelle.
- Le waypoint 0.9.8 reste d'abord sélectionné par le preset objet exact et son
  bit `ObjectsTxt.SubClass & 0x40`. Le témoin `0x3289EE` prouve ensuite le
  pointeur `ActiveRoom*` à `DrlgRoom+0x58`; `DUNGEON_GetFirstUnitInRoom
  0x2EFD90` retourne `ActiveRoom+0xA8`, et `UNITS_GetNextUnitInRoom 0x34B4A0`
  suit `Unit+0x160`. Après validation type objet, classe et room, MapSense
  conserve sans conversion les coordonnées de l'Unit données par
  `0x34AF60/0x34AFB0`, exactement comme `AUTOMAP_RenderUnit`. L'absence de
  room ou d'Unit active produit un résultat partiel retryable, jamais le preset
  approximatif.
- Les sorties directes entre niveaux extérieurs ne sont pas garanties dans le
  seul vecteur de rooms voisines déjà matérialisé. Le wrapper pur
  `DRLGLEVEL_GetVisArray 0x360880`, ABI `(uint8 context, Level*) -> int32_t*`,
  lit le niveau et son Drlg puis délègue à `0x360800`. Celui-ci retourne le
  tableau dynamique `Vis[8]` du noeud `Drlg+0x118` (`levelId +0x00`, Vis
  `+0x04`, Warp `+0x24`, suivant `+0x48`) ou le repli LevelsTxt à `+0x48`.
- `DRLGLEVEL_GetWarpId 0x3DAAD0`, ABI
  `(uint8 context, Level*, uint8 slot) -> int32`, applique la même sélection
  dynamique et lit `Warp[slot]`, avec repli LevelsTxt à `+0x68`. La 0.9.8
  accepte seulement une paire réciproque dont les deux Warp valent `-1` et
  dont les rooms portent le bit de slot `1 << (slot + 4)` à `DrlgRoom+0x50`.
- Le linker natif mutateur `0x361750` prouve pour ce chemin `Warp=-1` la règle
  géométrique : les gaps des rectangles `DrlgRoom+0x60/+0x64/+0x68/+0x6C`
  doivent être strictement inférieurs à six game tiles sur les deux axes. La
  0.9.8 utilise ce contrat seulement pour identifier les paires candidates;
  aucun centre ou midpoint géométrique de room ne sert encore de destination.
- `DUNGEON_GetCollisionGridFromRoom 0x2EFB30` retourne `ActiveRoom+0x38` et
  possède une signature stricte unique de 32 octets. Le témoin unique
  `0x36697B` prouve `CollisionGrid+0x20` pour le pointeur de cellules `uint16`,
  l'origine X/Y à `+0/+4` et la largeur à `+8`; `0x3669E6` prouve la hauteur à
  `+0x0C`. Le constructeur `0x363A90` copie les coordonnées subtiles de room
  dans cet en-tête et place les cellules inline à `+0x28`.
- Pour une paire de rooms cardinalement adjacentes, MapSense 0.9.8 balaie le
  bord de collision de la room source déjà active et sa cellule immédiatement
  intérieure. Une position appartient au passage uniquement si les deux masks
  ont le bit mur `1` absent. Les runs de deux subtiles ou moins sont rejetés;
  le centre du plus large run valide devient l'unique ancre, avec un tie-break
  déterministe. Si la room ou sa collision n'est pas encore active, aucune
  ligne approximative n'est publiée et le refresh reste retryable.
- Le parcours des niveaux déjà présents est borné et cyclique-sûr depuis
  `Drlg+0x868`, via `Level+0x1B8`, avec `Level+0x1C8` pour le Drlg et
  `Level+0x1F8` pour l'identifiant. `DRLG_GetLevel 0x3267C0`,
  `DRLG_InitLevel 0x3271C0`, le constructeur de liens `0x361750`, le rebuild
  `0x3608A0`, l'insertion vectorielle `0x3612E0` et toute fonction
  Add/RemoveRoomData ne sont jamais appelés par le nouveau chemin : une donnée
  absente reste partielle et retryable.
- La candidate source 0.9.8 ajoute les signatures strictes des accès Unit,
  ActiveRoom et CollisionGrid à l'empreinte fail-closed commune aux cibles
  3.2.92777 et 3.3.93847. Le build Release `/W4 /WX` et CTest `1/1` passent le
  26 août 2026. Elle n'est ni déployée ni lancée; l'alignement exact observé en
  jeu reste donc `NOT RUN`.
- Le témoin gameplay 0.9.9 affine le défaut intérieur : Tamoe Highland expose
  `raw=1 native=0 exact=0`, Barracks expose également un RoomTile brut mais
  aucune sortie exacte, et Jail Level 1 ne conserve que le lien de retour vers
  Barracks. La ligne mauve Pit est absente et les lignes vertes Barracks/Jail 1
  ne sont jamais publiées. Ce n'est pas une erreur de politique de niveaux :
  les cibles 7→26, 28→29 et 29→30 sont déjà exactes dans la table explicite.
- `DRLGWARP_ResolveRoomTileLink 0x3DA9A0` explique la perte. Après avoir
  matérialisé la source, `0x3DA9C6` lit sa chaîne à `DrlgRoom+0x78`,
  `0x3DA9D0/0x3DA9D4` lisent `RoomTile+0x20 -> LvlWarp+0x2C`, puis
  `0x3DA9FB` suit immédiatement `RoomTile+0x00` vers le `DrlgRoom`
  destination. Le helper exige ensuite à `0x3DA9FE..0x3DAA15` une chaîne
  réciproque à destination et retourne nul si elle n'est pas encore
  matérialisée. Ce gate réciproque est utile à l'ABI complète du helper, mais
  il n'est pas requis pour identifier le niveau cible du preset source.
- La 0.10.0 initialise d'abord, dans le DRLG client gouverné, la cible verte et
  les cibles mauves configurées qui figurent parmi les huit voisins `Vis` du
  niveau courant, par `DRLG_GetLevel 0x3267C0` puis
  `DRLG_InitLevel 0x3271C0` lorsque nécessaire. Elle matérialise ensuite chaque
  room source par `DRLGROOM_CreateActiveRoom 0x3289A0`, lit directement le
  `DrlgRoom*` destination à `RoomTile+0x00`, obtient son LevelId par
  `DRLGROOM_GetLevelId 0x360FC0`, et associe `LvlWarp+0x2C` au `PresetUnit`
  type 5 exact. Le témoin fail-closed de 15 octets à `0x3DA9FB` couvre la
  lecture directe et la preuve de layout; aucun centre de room ni autre point
  approché n'est réintroduit.
- Les tests hors jeu 0.10.0 couvrent toute la progression explicite de l'acte I,
  dont Stony Field→Underground Passage 1, Underground Passage 1→Dark Wood,
  Barracks→Jail 1, Jail 1/2/3, Cathedral et Catacombs 1/2/3/4. Les branches
  configurées Pit Level 1 et Underground Passage Level 2 sont vérifiées comme
  destinations mauves. Release `/W4 /WX`, quatre exports et CTest `1/1`
  passent; la DLL n'est ni déployée ni lancée.
- La trace runtime 3.3.93847 du 29 août 2026 établit que les rafales
  `sFillLocation()` ne sont pas propres à la route du monastère : elles sont
  émises pendant la matérialisation native de rooms demandée notamment par la
  collision, les RoomTile, les waypoints et Reveal. Dans `sFillLocation
  0x3E1DA0`, la branche d'index négatif à `0x3E1F24` appelle uniquement le
  logger à `0x3E1F2B`, puis rejoint à `0x3E1FD8` le chemin qui saute déjà le
  remplissage. Le CALL exact `E8 60 FC 63 00` est unique dans le corpus commun.
  MapSense 0.12.3 peut donc NOPer ces cinq octets par le service suivi de
  D2RLoader sans autoriser aucun accès hors limites ni modifier la construction
  des rooms.
- Références sémantiques uniquement :
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`, notamment
  `D2Common/src/Units/Units.cpp:283-323,582-594` et
  `D2Common/src/D2Dungeon.cpp:1303-1310`, ainsi que
  `D2RMH@32d55b8ab9a3e9b380103e73e3c8d328cd4f3ad4`,
  `d2mapapi/mapdata.cpp:55-168,240-380`, pour l'intersection des ouvertures de
  collision. Aucune adresse, structure ou ABI 32 bits n'est transposée.

## Burn Damage Fix — production générique et Fire Resistance

- Le corpus commun aux cibles 92777 et 93847 contient à `0x44CB32` le témoin
  unique `81 C3 3C 01 00 00 41 0F 48 DE`. Le chemin a déjà multiplié le Burn
  existant dans `EBX` et avancé le seed unité dans `R8D`, puis additionne à
  tort l'ID de stat `316` comme dommage plat.
- Les stats `burningmin=316`, `burningmax=317` et
  `passive_fire_mastery=329` sont identiques dans les tables 3.2 et 3.3. Le
  producteur missile `0x465799/0x465B40` confirme qu'elles décrivent un range
  de dommage et une maîtrise, pas une constante de DPS.
- Le helper RNG natif `0x4501E0` avance le seed une seule fois puis réduit le
  low32 courant par masque pour une puissance de deux ou modulo sinon. Le
  relais Burn consomme donc `R8D` sans nouvel appel RNG et conserve la borne
  maximum exclusive prouvée sémantiquement par
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`.
- `SUNITDMG_ApplyBurnDamage 0x451380` stocke ensuite le Burn sous forme de
  `HPREGEN` négatif. Le record Fire D2R de `0x40` octets porte résistance `39`,
  maximum `40`, pierce `333`, pierce d'immunité `189`, absorb `%/plat`
  `142/143`, index de réduction `2`, flag `+0x28=1` et log flag `8`.
- Le troisième argument du résolveur `0x4523E0` est conservé dans `R13D`. Le
  témoin unique `0x45251F` prouve qu'une valeur non nulle remplace toute
  résistance positive par zéro via `min(résistance,0)`, puis `0x452658` saute
  aussi l'absorb. La 2.0.0 appelait avec `1` et neutralisait donc par erreur
  résistance positive et immunité. La 2.1.0 appelle avec `0`, utilise les
  sentinelles absorb `-1/-1` et garde explicitement `reductions[2]=0` pour
  exclure MDR sans supprimer les défenses Fire.
- Cette fonction rejette une durée ou un dommage non positif, puis appelle
  `STATES_ToggleState 0x3354C0` avec le défenseur, le state `burning` 115 et
  `enable=1`. Les tables vanilla 3.2, vanilla 3.3 et BKVince relient toutes le
  state 115 à l'overlay `burning` 224, dont l'asset est
  `Expansion\\On_Fire`; l'absence d'overlay signalée n'est donc pas une absence
  de mapping dans les données actuelles.
- Le compilateur StatesTxt passe explicitement un stride `0x44` au callsite
  unique `0x3083D7`, puis installe le vecteur résultant à
  `DataTables+0x290` au témoin `0x30843C`. `STATES_ToggleState` appelle
  `GetItemDataContext 0x34A0E0`, transmet le byte à
  `GetDataTablesForContext 0x300A90` et lit le nombre de states à
  `DataTables+0x298` dans le témoin unique `0x3354E0`.
- Le premier cold start 2.2 a refusé proprement le chargement parce que le
  témoin de stride englobait aussi le `CALL` suivant, que l'intégration du
  compilateur TXT de D2RLoader peut rediriger avant le chargement des plugins.
  Le préfixe instruction-aligné de 22 octets arrêté avant ce `CALL` reste unique
  à `0x3083D7` et prouve intégralement l'argument `0x44`; la cible du `CALL`
  n'est ni consommée ni possédée par Burn Damage Fix.
- La séquence de descripteurs native à `0x307EB3` associe le type
  name-to-word `0x16` à l'offset record `+0x02`, puis aux offsets
  `+0x04/+0x06/+0x08`; le premier word d'overlay est donc directement prouvé à
  `StateRecord+0x02`. L'initialiseur unique de records `0x44` à `0x394640`
  écrit `0xFFFF0000` à `+0`, ce qui combine state id zéro à `+0` et sentinelle
  d'overlay vide `0xFFFF` à `+2`. Ces offsets et cette sentinelle proviennent du
  binaire D2R gouverné, pas d'une transposition de structure D2MOO.
- Burn Damage Fix 2.2 réutilise le hook d'application déjà possédé à
  `0x451380` et n'ajoute aucun hook exécutable. Juste avant le trampoline
  original, il vérifie contexte, count, base, stride, id 115, alignement et page
  writable, puis effectue un compare/exchange atomique strict
  `overlay1 224 -> 0xFFFF`. Une valeur déjà vide est acceptée; toute valeur
  custom est préservée. La mutation reste process-local, ne réécrit aucun
  `states.txt` et est restaurée au déchargement seulement si la DLL possède
  encore la même cellule inchangée.
- Burn Damage Fix 2.0 utilisait `STATES_CheckState 0x3351B0` uniquement comme
  témoin passif après une application positive : il ne créait aucun overlay.
  Le gameplay BKVince du 26 août 2026 a confirmé le DoT, le kill-credit/XP et
  deux states actifs (`resolved=2`, `burning-state=2/0`). Ce comportement est
  conservé ici comme preuve historique, pas comme description de la branche
  2.1.
- Burn Damage Fix 2.1 emprunte `UNITS_SetOverlay 0x349020` sans le patcher et
  rejoue l'overlay `fire_hit` 81 sur la couche 0. Il le fait une première fois
  après une application Burn positive dont le state `burning` 115 est confirmé,
  puis périodiquement pendant les événements de stat-regeneration. Chaque replay
  exige encore le state 115 actif et `SUNIT_IsDead 0x34C2C0 == 0`; il cesse donc
  naturellement à l'expiration du Burn ou à la mort. Aucun pointeur d'unité,
  GUID ou état parallèle n'est conservé entre deux callbacks.
- `D2GAME_EVENTS_PlayerEventDispatcher 0x42CE30` possède l'ABI native à six
  arguments `(game, unit, eventType, callbackArg0, callbackArg1,
  callbackArg2) -> void`. Il borne `eventType` à `0..14`, sélectionne
  `0x42E600` pour le type 3 et possède deux xrefs directes, `0x48CB2F` et
  `0x48CBC4`. Son entrée unique de 32 octets est `48 89 5C 24 08 48 89 6C 24
  10 48 89 74 24 20 57 48 83 EC 30 49 63 D8 41 8B F9 48 8B F2 48 8B E9`.
- `MONSTERMODE_EventHandler 0x447420` possède la même ABI native à six
  arguments. Il vérifie le type monstre, borne l'événement à `0..14`,
  sélectionne `D2GAME_MONSTER_ApplyStatRegen 0x448C00` pour le type 3 et possède
  une seule xref directe, `0x48C83F`. Son entrée unique de 39 octets est `48 89
  5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 50 80 3D 61 76 66 02
  00 41 8B E9 49 63 F8 48 8B DA 48 8B F1`.
- Ces deux dispatchers sont synchrones dans le chemin serveur de la file
  d'événements. Le replay précède le callback original, vérifie explicitement le
  type joueur 0 ou monstre 1, ne requiert ni résolution GUID, ni collection
  globale, ni worker thread, puis transmet les six arguments originaux exactement
  une fois.
- Le témoin unique `0x42E615` lit le frame serveur à `Game+0x170`. Le témoin
  unique de 38 octets à `0x44DF40` lit la difficulté à `Game+0x104`, le contexte
  data à `Game+0x106`, puis appelle `GetDifficultyRecord(dataSet,difficulty)` à
  `0x300830`. La 2.1.0 vérifie ces deux layouts avant toute lecture directe.
- Les CALL uniques `0x42E634` et `0x448CA0` vers `EVENT_SetEvent` prouvent que
  les callbacks reprogramment le type 3 à `gameFrame+1`, mais ne sont pas
  retenus comme hooks : `plugin-misc` possède l'entrée monstre `0x448C00` et
  peut court-circuiter son callsite interne. Les dispatchers sont en amont de
  cette divergence.
- `EVENT_SetEvent 0x48B720` possède sept arguments natifs, et non six :
  `(game, unit, eventType, expireFrame, customId, customParam, arg7) -> void`.
  Après `sub rsp,0x48`, son prologue lit les arguments entrants 5, 6 et 7 à
  `[rsp+0x70]`, `[rsp+0x78]` et `[rsp+0x80]`. La sémantique du septième argument
  reste inconnue; le prototype D2MOO à six arguments est sémantique seulement.
- `UNITS_SetOverlay 0x349020` est confirmé par 39 appels directs, son
  allocation/mise à jour d'une stat-list au flag `0x80`, son ABI
  `(unit, overlayId, unusedLayer) -> void` et sa signature unique de 24 octets.
  Le témoin interne unique de 30 octets à `0x34916C` prouve que le setter écrit
  l'ID dans `unit_dooverlay` stat `178` (`0xB2`); `0x80` n'est pas un ID de stat.
  Cette stat retient le dernier write direct et ne prouve pas qu'une animation
  reste active. Burn Damage Fix 2.1 compte un overlay étranger puis applique
  `fire_hit` selon l'arbitrage natif last-write-wins. Le setter cible bien
  l'unité, mais une particule déjà émise peut rester à son emplacement; le replay
  périodique émet les suivantes à la position courante.
- `SUNITDMG_ApplyResistancesAndAbsorb 0x4523E0` reste un seam partagé que Burn
  Damage Fix ne hooke pas. La 2.1.0 exige soit sa signature vanilla unique de
  32 octets, soit exactement un inline hook suivi par DiagnosticsService et
  possédé par `monsterdisplay`; tous les témoins internes de record, résistance,
  pierce, réduction et absorb restent stricts. Toute modification inconnue,
  non suivie ou multi-propriétaire refuse le chargement.
- L'audit de coexistence ne trouve aucun propriétaire de `0x42CE30` ou
  `0x447420` parmi Monster Display, Bind And Summon, Melee Splash et les autres
  composants actifs de la Suite. Les ressemblances dans Bind And Summon sont
  des entrées `.pdata`, pas du code. Monster Display partage seulement
  `0x4523E0`; BKVCombat emprunte `UNITS_SetOverlay`; Melee Splash possède
  `0x44C030`; et le seam `plugin-misc 0x448C00` est volontairement évité.

## Resistance Floor — plancher configurable et affichage natif

- La cible produit D2R 3.3.93847 réutilise le corpus natif vérifié provenant du
  build 92777. `SUNITDMG_ApplyResistancesAndAbsorb 0x4523E0` reçoit son contexte
  en RCX et le record de dommage courant en RDX depuis l'unique callsite
  `0x44EC5A`, au cœur d'une boucle de douze records de stride `0x40`.
- Deux chemins distincts appliquent le plancher vanilla. `0x4524C4` porte
  `B9 9C FF FF FF 3B D9 0F 4F CB EB 51`; `0x4524E7` porte
  `B9 9C FF FF FF 3B D9 0F 4F CB 8B D8`. Le préfixe de dix octets apparaît
  exactement deux fois et chaque témoin étendu est unique.
- Le contexte conservé dans RSI expose le défenseur `Unit*` à `+0x10`, réutilisé
  par `STATES_CheckState` à `0x452508`. Le record conservé dans R14 expose son
  stat de résistance à `+0x08`. Un relais interne peut donc sélectionner un
  plancher par défenseur et limiter l'effet aux stats 36, 37, 39, 41, 43 et 45
  sans posséder l'entrée partagée du résolveur.
- Les plafonds Physical et Elemental/Magic à `0x4524D6` et `0x4524DE` restent
  distincts et possédés par plugin-items. L'ancien A/B Burn Fire Resistance a
  déjà prouvé qu'un hook d'entrée à `0x4523E0` empêchait Monster Display de
  charger; Resistance Floor ne modifie donc que les deux `MOV ECX,-100`.
- `D2GAME_GetMinionOwner 0x4A53C0` fournit l'ABI gouvernée
  `Unit* (Unit* monster)`. Une chaîne propriétaire bornée distingue joueur,
  unité possédée par un joueur et monstre sans propriétaire; les types inconnus
  ou cycles reviennent au plancher vanilla.
- La fiche de personnage applique séparément un `MOV EAX,-100` dont l'opérande
  commence à `0x14E729A`. Le témoin unique depuis `0x14E728C` est
  `8D 4F 4B 83 F9 5F 7C 05 B9 5F 00 00 00 B8 9C FF FF FF`; les quatre branches
  lisent les max-resists 40, 42, 44 et 46 avant de rejoindre ce même clamp. Ce
  site gouverne donc l'affichage natif Fire, Lightning, Cold et Poison.
- Physical et Magic n'ont pas de slot numérique natif. Ils restent couverts par
  le plancher gameplay, sans affichage personnalisé ni dépendance inter-plugin.

## Skill Trees Revamp (anciennement FourthSkillTree Framework) — persistance dynamique des skills de classe

- La cible produit est D2R 3.3.93847 et réutilise le corpus natif gouverné de
  provenance 92777. `DATATBLS_GetClassSkillCount 0x33CB30` retourne le compte
  compilé par classe depuis le contexte actif; sa signature stricte de 32 octets
  est unique. `DATATBLS_GetClassSkillIdByIndex 0x33DDE0` retourne l'identifiant
  SkillsTxt d'un index de classe et possède également une signature stricte de
  32 octets unique.
- Le writer de l'en-tête D2S appelle `0x33CB30` à `0x534519`, puis stocke `AL`
  dans le byte `NumSkills`. Le témoin à wildcards
  `E8 ?? ?? ?? ?? 49 8B CF 88 45 9A E8` est unique.
- Le writer de la section skills écrit le magic `if` (`0x6669`) à `0x52F557`,
  puis boucle de zéro jusqu'au compte dynamique obtenu à
  `0x52F58A/0x52F5D7`. Pour chaque index, il appelle `0x33DDE0`, résout le skill
  par `SKILLS_GetSkillById 0x33DCD0`, lit son rang de base par
  `SKILLS_GetBaseLevel 0x33D1E0` et écrit exactement un byte. Il n'existe donc
  aucune constante 30 dans cette boucle native de sérialisation.
- Le lecteur vérifie le même magic `if` à `0x52EC98`, utilise le compte sauvé
  pour parcourir les bytes, borne chaque index contre le compte compilé courant,
  ajoute les rangs par le chemin natif puis avance le curseur de `2 + count` à
  `0x52ED52..0x52ED5A`. Une sauvegarde plus longue reste donc structurellement
  délimitée sans section propriétaire.
- La référence de format épinglée
  `D2SSharp@f26f21897db5c0075e74defca1e31d1930080750` confirme séparément que
  `Character.NumSkills` est un byte lu et écrit dans l'en-tête
  (`src/D2SSharp/Model/Character.cs:32-33,102-104,143-145`) et que la section
  `if` contient un tableau de rangs de longueur `skillCount`, un byte par skill
  (`src/D2SSharp/Model/SkillsSection.cs:7-15,36-37,78-103`). Sa constante 30 est
  le défaut vanilla de la bibliothèque, pas une borne présente dans le layout
  sérialisé.
- Cette preuve ferme l'hypothèse d'un format D2S intrinsèquement limité à 30 :
  le chemin statique natif est déjà piloté par le compte de skills compilé et
  peut représenter jusqu'à 255 entrées par le byte d'en-tête. Elle ne remplace
  pas le gate runtime. Le témoin 3.3 ferme désormais compilation, allocation,
  Save and Exit, relecture investie et respec immédiat; rank-zero sauvegardé,
  hôte/joiner et la matrice 3.2 restent ouverts avant toute promesse publique.
- Une lecture runtime contrôlée de D2R 3.3.93847 prouve que la case `0x3B` du
  tableau serveur à `D2R+0x1D2A790` contient le pointeur
  `D2GAME_PACKETCALLBACK_Rcv0x3B_AllocateSkillPoints 0x4B3EE0`. Le handler
  exige cinq octets, lit l'identifiant de skill à `packet+1` et le marqueur de
  ranks supplémentaires à `packet+3`, borne l'identifiant contre le nombre
  total de lignes SkillsTxt compilées, résout le skill, son `MaxLvl` et son rang
  de base, puis applique les rangs par le helper serveur `0x438670`.
- Aucun accès à `SkillPage`, `SkillRow` ni `SkillColumn` n'existe dans ce
  callback. L'autorité serveur d'allocation est donc démontrée indépendante de
  la page UI; le témoin runtime-native ferme aussi allocation et relecture
  investie du 31e skill sous 3.3.93847.
- Le respec autoritaire `D2GAME_PLAYER_ResetStatsAndSkills 0x580F20`, reçu par
  l'opcode `0x39`, appelle `D2GAME_PLAYER_ResetSkills 0x4360F0`. Cette fonction
  parcourt la liste compilée complète des skills de la classe, retire chaque
  rang de base et crédite leur somme dans le stat 5. Elle n'applique ni filtre
  `SkillPage` ni borne 30; le témoin dynamique confirme le retour immédiat du
  31e skill à zéro sous 3.3.93847. Sa sauvegarde rank-zero reste à observer.
- `UI_DispatchMessage 0x843D90` demeure la propriété unique du broker
  `plugin-skills`; RemoteStash redirige seulement le callsite étroit
  `UI_ButtonWidget_OnClick+0xE2`. Skill Trees Revamp doit composer avec ce broker
  et ne pas installer un second hook sur l'entrée commune.
- Le probe d'allocation du 25 août place le skill 456 sur une cellule native
  Barbarian et étend une sauvegarde gameplay courante de 30 à 31 rangs, avec
  en-tête, checksum et marqueur `JM` cohérents. D2R affiche le personnage level
  99 au menu mais ferme pendant sa matérialisation; le `.d2s` reste
  byte-identique. La greffe directe ne fournit donc aucune preuve d'allocation
  investie et ne sera pas utilisée comme base du prochain témoin.
- Un témoin Amazon 31-rangs entièrement écrit par le runtime courant se
  matérialise, investit le skill 456, conserve son rang 1 après Save and Exit et
  relecture, puis revient immédiatement à zéro par Akara. Vincent réinvestit
  avant la sauvegarde finale : son 31e byte à 1 est attendu; la persistance
  rank-zero post-respec reste NOT RUN.
- Le mode `native-allocation` de ce témoin vidait explicitement
  `reqskill1/2/3`. Le nouveau mode `native-prerequisite` prépare séparément le
  même skill 456 Amazon avec `reqskill1 = Inner Sight`, afin de demander le
  refus parent-zéro puis l'acceptation parent-rang-1 sans confondre ce gate avec
  l'UI page 4. Vincent a confirmé le refus à parent zéro, l'acceptation après
  investissement d'Inner Sight et le même comportement après Akara. Le save
  final réinvesti conserve les deux rangs à 1; une répétition sauvegardée à
  rank-zero a été retirée du gate par décision produit.
- `SKILLTREE_SetPageState 0x14C3B10..0x14C3B86` porte l'ABI observée
  `void(panel, requestedPage)`. Il borne la demande à 0..3, écrit le `int32`
  global `SKILLTREE_CurrentPageState 0x3BBDCBC`, puis appelle la reconstruction.
  Son empreinte stricte de 32 octets est unique. Ses quatre callers couvrent le
  payload `ActivateTab` à `0x14C699D`, l'avance avec wrap à
  `0x14C6BBC..0x14C6BE6` et le recul avec wrap à
  `0x14C6BED..0x14C6C07`.
- `SKILLTREE_RebuildPageWidgets 0x14C7720..0x14C826E` porte l'ABI observée
  `void(panel)` et une empreinte stricte unique de 32 octets. Il active et
  recolore exactement quatre paires Tab/Text dans la boucle
  `0x14C7850..0x14C789E`. Le scan exhaustif des comparaisons RIP-relatives
  contre `3` trouve sept références au même global `0x3BBDCBC` : interaction
  widget `0x14C4084`, allocation `0x14C7428`, puis gates de reconstruction
  `0x14C7907`, `0x14C7986`, `0x14C7BC0`, `0x14C820B` et `0x14C822B`. Elles
  réservent ensemble l'état 3 à General Skills; seule la branche différente de
  3 énumère les widgets ordinaires et leurs identifiants de skill.
- Le layout manette confirme `Tab3`, `TextTab3 = @GeneralSkills`,
  `CommonSkillsContainer` et `ItemSkillsContainer`. Le passage à cinq états
  doit donc étendre ensemble le setter, les deux wraps, la boucle à quatre et
  chaque gate General Skills, puis déplacer General Skills à 4.
- Les résolveurs `SKILLTREE_FindTextTabWidget 0x14C5EA0` et
  `SKILLTREE_FindTabWidget 0x14C5F60` prennent l'ABI observée `(panel, index)`
  et cherchent respectivement le nom TextTab/index et Tab/index. Leurs
  signatures strictes de 32 octets, qui diffèrent par leur source de format et
  leur helper de type de widget, sont uniques dans le corpus commun.
- FourthSkillTree 0.2.0 applique onze changements d'immédiats via D2RLoader :
  maximum setter `3 -> 4`, deux wraps `3 -> 4`, boucle Tab/Text `4 -> 5` et les
  sept gates de page `3 -> 4`. Avant toute écriture, une empreinte fail-closed
  couvre les sept témoins natifs de 32 octets, les onze instructions exactes,
  la plage de l'image PE et la valeur signée courante `0..3`. Les noms de build
  sont seulement diagnostiques; `UI_DispatchMessage` n'est ni appelé ni hooké.
  Le build et les checks statiques passent, mais le démarrage et le parcours des
  cinq états restent NOT RUN jusqu'au test humain de Vincent.
- `CounterTemplate` est un `TextBoxWidget` sans fond dans les layouts souris et
  manette. Les carrés sombres de rang, cadres d'icônes et flèches sont contenus
  dans les `skillBackgroundFile` de classe; le fond Amazon prouve que la cellule
  synthétique choisie n'a aucun carré peint. Le chiffre flottant observé est
  donc une limitation attendue du validator 0.1.0, pas un défaut du renderer.
  Le framework devra créer un chrome générique de cellule après preuve de son
  constructeur et de son asset, sans réécrire les fonds propres aux mods.

## Discipline de promotion

Une adresse n'entre dans `known-rvas.json` qu'apres preuve par structure de
controle, octets/signature, caller/callee ou validation runtime. Les simples
ressemblances et les anciennes adresses 2.4 restent dans cette page avec une
confiance explicite.

## Scaling des monstres par area level en Normal — garde BKVince

- Le chemin commun 92777/93847 dérive `r15d` de l'index de difficulté effectif :
  zéro en Normal, un en Nightmare et deux en Hell. Le couple natif à
  `0x543D32` est `test r15d,r15d; je 0x543D50`; le saut à `0x543D35` exclut
  donc directement le chemin d'area level en Normal.
- La room passe par le wrapper null-safe `0x2EFC10`, qui retourne zéro en
  l'absence de room puis relaie `DRLGROOM_GetLevelId 0x360FC0`. L'appelant
  conserve ce véritable `LevelId` dans `[rsp+0xB8]` à `0x543C60` avant les
  tests d'éligibilité.
- L'ancienne réécriture RuffnecKk `45 85 F6 7E 19` est invalidée. Le flux
  `0x543CE3..0x543D14` écrase `r14d` avec un booléen avant le gate; il ne teste
  pas le `LevelId` positif annoncé. Le cold start et le témoin mercenaire du
  6 août prouvaient une absence de crash, pas l'effet de scaling.
- La patch externe de `yinyin333333` neutralise correctement le saut Normal
  avec `90 90` à `0x543D35`, mais le test pile complète BKVince du 27 août a
  produit l'assertion `eLevelId > 0` de `LvlTbls.cpp:284` pendant le chargement.
  Le même profil a ensuite chargé avec l'ancien garde, ce qui rend l'admission
  d'un appel BKVince sans room par la version yinyin hautement probable; l'unité
  exacte reste à identifier et ne doit pas être inventée.
- Vincent retient donc une correction privée BKVince Expansion-only. La séquence
  unique de dix octets à `0x543D2D`,
  `80 FA 01 74 1E 45 85 FF 74 19`, devient
  `83 BC 24 B8 00 00 00 00 7E 19`, soit
  `cmp dword ptr [rsp+0xB8],0; jle 0x543D50`. Un `LevelId` positif rejoint le
  chemin d'area level, y compris en Normal; zéro conserve le niveau monstre de
  base. Les contrôles `noRatio`, boss, desecrate et monster-region suivants
  restent natifs.
- Ce remplacement retire volontairement le gate classic-game. Il appartient
  seulement au profil BKVince Expansion et doit être absent de la RuffnecKk
  D2RLoader Suite. Le cold start pile complète est passé le 27 août; un témoin
  effectif Normal et le cas sans room restent requis avant qualification
  gameplay.

## Shadow Master AI Fix — sélection de cible indépendante

- `AITHINK_ShadowMaster 0x5CD740` est le gestionnaire moderne identifié pour
  la cible D2R 3.3.93847. Son flux natif récupère le propriétaire et sa cible,
  conserve le leash à distance carrée 144, énumère les candidats par
  `AITHINK_TargetCallback_ShadowMaster 0x5D1360`, puis exécute la sélection
  non-combat centrée sur le propriétaire et le fallback à distance carrée
  1024. Les signatures d'entrée strictes de 32 octets des deux fonctions sont
  uniques dans `.text`.
- `0x5CDBB3` porte `74 25`, un `JE` qui contourne normalement le bloc seulement
  lorsque la cible du propriétaire est nulle. Le bloc validé affecte la cible
  du propriétaire à la Shadow à `0x5CDBD3`. Écrire `EB 25` contourne toujours
  cette affectation tout en rejoignant le chemin natif de leash. Le témoin de
  16 octets commençant à `0x5CDBA8` est unique.
- `0x5CDD55` porte `75 74`, un `JNE` qui contourne la sélection non-combat
  seulement lorsqu'un combat est déjà actif. Le fallthrough préfère la cible
  du propriétaire, puis le candidat le plus proche du propriétaire. Écrire
  `EB 74` conserve systématiquement la cible acquise indépendamment. Le témoin
  de 16 octets commençant à `0x5CDD50` est unique.
- Le helper courant `SKILLS_SrvDo049_ApplyShadowSummonBonuses 0x56D770` prouve
  que les deux défauts historiques de SrvDoFunc 49 sont déjà corrigés. Le gate
  `CMP skillLevel,1` suivi de `JL` accepte le niveau 1; les boucles avancent
  ensemble six couples AuraStat/AuraStatCalc et quatorze couples
  PassiveStat/PassiveCalc. Aucun patch SrvDoFunc 49 n'est ajouté.
- Le port ne modifie aucune table TSV. Le conseil historique Attack/NU et grand
  MeleeRng visait des minions purement distants et ne convient pas aux Shadow
  Warrior/Master natives capables d'attaques de mêlée.
- La référence sémantique est
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`,
  `source/D2Game/src/AI/AiThink.cpp:14302-14482,14959-15038` et
  `source/D2Game/src/SKILLS/SkillAss.cpp:1975-2043`. Aucune adresse, structure
  ou ABI 32 bits n'est transposée vers D2R.
# Currency Stash Deposit — routage Advanced Stash natif (D2R 3.3.93847)

- Le corpus natif gouverné provenant du build `92777` reste l'image d'analyse
  autoritaire réutilisée pour la cible courante `D2R.exe 3.3.93847`.
- Les handlers inventaire legacy `0x228AB0` et moderne `0x2C7540` suivent la
  même branche Ctrl-clic : si le stash `0x18` est ouvert, ils appellent
  `0x15A0B0(item)` puis dispatchent `0x2AAAA0(widget,event)` lorsque le résultat
  est vrai. La branche de stash grille ordinaire est distincte.
- `0x15A0B0` obtient le class id de l'objet et le passe à `0x15F320`, qui cherche
  cette clé dans une table de hachage runtime. Le prédicat prouve donc une
  inscription Advanced Stash active; lire seulement le byte
  `AdvancedStashStackable` ne prouverait pas qu'une destination existe.
- `0x2AAAA0` requiert d'abord que `0x1C7360(item)` retourne zéro. Cette garde
  refuse notamment une condition stat-list mode 2 ou l'état joueur `0x36`.
  Elle fait partie du contrat de sécurité reproduit par Currency Stash Deposit.
- L'action résout ensuite le joueur local, appelle `0x46DA50(player)` pour le
  proxy de destination, lit la page source et invoque
  `CLIENT_TransferItemToInventoryPage` `0x15F8B0` avec page destination `4`,
  page source dans `R9B`, mode `1` et une sortie de placement. Elle termine par
  `0x1A0780(3,null,0,0,false)`.
- `0x46DA50` obtient l'inventaire du joueur et traverse sa chaîne auxiliaire de
  corps jusqu'au record accepté par les deux prédicats natifs. Son retour est
  passé sans transformation comme destination du transfert Advanced Stash.
- Currency Stash Deposit ne doit donc ni synthétiser un widget, ni parser les noms
  d'onglets, indices ou coordonnées d'un layout. Il énumère l'inventaire,
  revalide parent/page/GUID/code et les deux gardes natives, puis appelle un
  transfert par étape sur le thread UI.
- `ITEMS_GetInvPage` `0x36CFE0` demeure possédé par Cube Output Quantity. Le
  plugin lit `ItemData+0x55` via `UNITS_GetItemData`, conformément à la preuve
  MassID, afin de ne pas dépendre du prologue d'un autre propriétaire.
- Les signatures strictes de `0x15A0B0`, `0x15F8B0`, `0x1A0780`, `0x1C7360` et
  `0x46DA50` sont chacune uniques dans `.text`. Le gameplay, RemoteStash distant
  et la matrice hôte/joiner restent des gates ouverts avant toute revendication
  fonctionnelle publique.

## Hit Chance Bounds — clamp défensif de la fiche de personnage

- Le runtime courant D2R 3.3.93847 réutilise le corpus natif vérifié provenant
  du build 92777. Le patch BKVince initial appliquait bien ses sept écritures en
  mémoire, mais la capture gameplay conservait `5%` pour
  `Average chance %s will hit you`.
- Le chemin offensif de la fiche de personnage passe par le calculateur
  `0x1514700` puis son formateur à `0x14E7FE0`. Les opérandes déjà gouvernés à
  `0x15149C2/0x15149CD` et `0x14E8068/0x14E8073` appartiennent à ce chemin; les
  deux premiers ne constituent pas un second chemin gameplay autoritaire.
- Le chemin défensif distinct appelle `0x15149F0` depuis `0x14E8242`, conserve
  son résultat brut dans `EBX`, puis appelle le formateur `0x1514CA0` depuis
  `0x14E8262` avec la chance en `ECX` et la sortie texte en `RDX`.
- Le formateur défensif réappliquait le clamp vanilla dans la séquence unique
  `83 F9 05 7D 07 BF 05 00 00 00 EB 0A B8 5F 00 00 00` à `0x1514CBD`.
  L'opérande basse est `0x1514CBF`; l'opérande haute commence à `0x1514CCA`.
- La même fonction sélectionne ensuite les chaînes localisées 10104
  `charmontohit1X` et 10105 `charmontohit2X`. La signature stricte
  `B9 78 27 00 00 E8 ?? ?? ?? ?? 4C 8B CB 89 7C 24 20` est unique à
  `0x1514DBD`, ce qui rattache le clamp oublié au texte observé sans inférence
  fondée sur la seule proximité.
- Le correctif minimal ajoute `05 -> 00` à `0x1514CBF` et
  `5F 00 00 00 -> 64 00 00 00` à `0x1514CCA`. Le jet gameplay gouverné à
  `0x44BD56` reste distinct; la validation visuelle de l'infobulle ne remplace
  pas une validation fonctionnelle du combat.

## Revive Overhaul 2.1 — sélection client, aura active et marqueur IA natif

- `SKILLS_ValidateReviveTarget 0x55A510` porte l'ABI observée
  `(game, caster, target) -> int32`. `SKILLS_SrvDo058_Revive 0x55E7E0`
  l'appelle à `0x55E8EC`; `SKILLS_SrvSt21_Revive 0x560470` rejoint le même
  validateur à `0x5604A8`. Le plugin conserve donc `SrvStFunc 21`,
  `SrvDoFunc 58`, `CltStFunc 24`, `CltDoFunc` vide et `SelectProc 3`; aucun
  edit TSV ni remplacement par les callbacks legacy 39/36 n'est requis.
- Le test externe de 2.0.1 a compté sept admissions serveur et deux couples
  aura capturés/restaurés, mais `SelectProc 3` ne surlignait pas le corps de
  haut rang, l'aura restait inactive et les compteurs IA demeuraient à zéro.
  Cette preuve runtime invalide deux hypothèses de 2.0 : écrire seulement le
  right-skill pointer ne réactive pas l'aura, et l'état spécial IA 7 n'est pas
  un classificateur suffisant pour tous les Revives admis.
- `CLIENT_ValidateReviveTarget 0x96600` est le prédicat de sélection client. Il
  vérifie d'abord le cadavre, puis appelle `AIUTIL_CanUnitSwitchAi 0x34C730`
  avec les quatre gates optionnels à vrai au site unique `0x96635`, retour
  `0x9664D`, avant de poursuivre ses restrictions natives. Revive Overhaul
  hooke l'entrée partagée `0x34C730`, mais remplace `checkUnique` par faux
  uniquement pour ce return-site et uniquement pour le masque de rang
  `0x000E`. Tous les autres callers et tous les contrôles suivant l'appel
  restent natifs.
- Le fallback serveur continue d'appeler le validateur vanilla en premier. En
  cas de refus il se limite au même masque de rang, revalide `CorpseSel`,
  `Revive`, mort et consommation, puis appelle l'original
  `0x34C730(target,true,false,true,true)`. Les états, modes, flags,
  boss/prime-evil, unités scriptées et `SwitchAI` demeurent fail-closed; les
  act bosses ne sont toujours pas admis dans 2.1.
- Le helper de transformation appelé à `0x55E91B` obtient le right skill à
  `0x55FAE1`, puis le vide avec `SKILLS_SetRightActiveSkill 0x33EF10`. La
  version 2.0 rappelait seulement ce setter après succès. Or ce setter résout
  le skill et écrit `skillList+0x10`; il n'exécute pas le reste du chemin
  d'activation.
- `MONSTERUNIQUE_UMod30_AuraEnchanted 0x495CD0` crée le skill choisi à
  `0x495F4B`, puis appelle `D2GAME_AssignSkill 0x438A70` à `0x495F64` avec
  `(monster,0,skillId,-1)`. L'entrée `0x438A70` appelle elle-même
  `SKILLS_SetRightActiveSkill` à `0x438B09`, puis poursuit l'activation et la
  synchronisation natives. Sa signature stricte de 32 octets est unique.
- Revive Overhaul 2.1 capture toujours exclusivement le tuple déjà choisi par
  MonUMod 30 via `MONSTERUNIQUE_GetUMods 0x38E310` et
  `UNITS_GetRightSkill 0x34B400`. Après un `SrvDoFunc 58` réussi il rejette un
  skill droit concurrent, puis rejoue `0x438A70(target,0,skillId,ownerGuid)`.
  Aucun skill, aura, niveau ni tirage n'est inventé ou relancé.
- Le témoin unique `0x55EB48` prouve que le chemin natif Revive active d'abord
  le unit flag `0x80000000`, puis appelle
  `STATES_ToggleState(monster,96,true)` à `0x55EB6E`. L'état 96 est donc un
  marqueur D2R direct du Revive, indépendant de `pettype` et de l'état spécial
  de la table IA.
- Le hook `D2GAME_MONSTERS_AiFunction03 0x4A3A20` classe désormais l'unité par
  `STATES_CheckState 0x3351B0(monster,96)`. Les hooks distance, vitesse et
  marche restent doublement bornés par ce scope TLS et leurs return-sites
  exacts. La pile conserve ainsi les réglages de leash même pour un skill
  custom utilisant un autre `pettype`, sans toucher les autres familiers.
- La politique courante supprime toute allowlist de build-name. L'identité
  annoncée par D2RLoader est seulement journalisée; avant tout hook, 2.1
  vérifie les entrées natives et les trois témoins uniques client, marqueur
  Revive et Aura Enchanted. Une empreinte différente refuse proprement le
  chargement. Les qualifications 92777 et 93847 restent deux matrices runtime
  distinctes; l'une ne prouve pas l'autre.
- L'audit du PluginPack épinglé et de la Suite ne trouve aucun propriétaire
  concurrent pour `0x55A510`, `0x55E7E0` ou `0x34C730`. La compilation Release
  et le test de politique 2.1 passent; les cold starts pile complète et les
  témoins gameplay frais demeurent requis avant release.
- Références sémantiques uniquement :
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`,
  `source/D2Game/src/SKILLS/SkillNec.cpp:1490-1623`,
  `source/D2Game/src/AI/AiUtil.cpp:1324-1348` et
  `source/D2Game/src/MONSTER/MonsterUnique.cpp:84-92,601-652`. Aucune adresse,
  structure ni ABI 32 bits n'est transposée.

## Cast Triggers — événement doactive et niveau source

- Le handler serveur central `0x43ACB0` porte l'ABI observée
  `(game, unit, skillId, skillLevel, a5, a6, a7) -> int32`. Ses huit callsites
  directs distinguent les exécutions manuelles `a5=1,a6=0,a7=0` des casts
  d'item `a6=1`. Le retour non nul est produit seulement après le SrvDoFunc ou
  le missile serveur réussi; Cast Triggers dispatch donc après ce retour.
- Le lookup contextuel `0x097790` utilise les tables `+0x11B0/+0x11B8` et le
  stride SkillsTxt `0x2EC` à `0x09780B`. Les offsets `flags +0x24`,
  `anim +0x30`, `seqtrans +0x32` conservent l'ordre sémantique D2MOO après les
  champs modernes insérés. Un record non répétitif accepte `SC=10` ou `SQ=18`
  transitant vers `SC`. Un record portant le bit `repeat` 11 accepte également
  `SQ -> SQ`, forme compilée d'Inferno dans BKVince 3.3, et emprunte la cadence
  channeling gouvernée sans exception par skill ID.
- L'événement natif `doactive` est l'index 4. Aucun appel natif du dispatcher
  n'a été trouvé avec cet index; le wrapper `0x44D570` accepte un dommage nul,
  construit le contexte attendu et appelle `0x5881E0`. Le plugin ne remplace
  aucune fonction de table d'événements.
- EventFunc20 `0x583B30` lit l'identifiant de stat dans le high word de son
  payload, obtient la chance sur l'unité, effectue le jet modulo 100, puis
  décode le skill id et son niveau à l'aide du shift/masque contextuels. Il
  appelle `0x5896E0` aux callsites `0x583C7F/0x583CB4` ou `0x589820` à
  `0x583CE1`. Melee Splash reste propriétaire de l'entrée EventFunc20 et
  transmet le chemin natif; Cast Triggers ne la hooke pas.
- La branche `ItemTgtDo` de EventFunc20 exige une unité cible à `0x583C50`; si
  elle est nulle, le callback retourne succès à `0x583CF6` sans caster. Avec
  une cible, elle appelle `0x5896E0(target,skill,level,target,0)`. La branche
  ordinaire appelle `0x5896E0(source,skill,level,target,1)` ou, sans unité,
  prend les coordonnées de premier point par `0x34B940/0x34B8F0` avant
  `0x589820(source,skill,level,x,y,1)`. Cela explique génériquement pourquoi un
  proc auto/subject comme Frost Nova disparaissait après une source sans cible
  comme Teleport; aucune paire de skills particulière n'est en cause.
- Les helpers D2R `UNITS_GetDynamicPath 0x34AE80`, `PATH_GetX 0x341A20`,
  `PATH_GetY 0x341A30` et `PATH_GetDirection 0x341990` portent respectivement
  les ABI `(Unit*) -> DynamicPath*`, `(DynamicPath*) -> int32`,
  `(DynamicPath*) -> int32` et `(DynamicPath*) -> uint8`. Leurs signatures
  strictes de 68, 16, 16 et 16 octets sont chacune uniques. Le premier retourne
  `Unit+0x38`; les trois autres lisent le X courant `+0x02`, le Y courant
  `+0x06` et un byte directionnel brut `+0xBD`. Le callsite D2R `0x47B1F0`
  sérialise ce byte avec les coordonnées courantes. Le setter `0x342860` borne
  les valeurs à `0x4D`, et le test Cast Triggers du 27 août a observé `73` : le
  domaine 0..63 de D2MOO n'est donc pas transposable au D2R actuel.
- Le premier candidat Cast Triggers avait quantifié ce byte avec la table
  D2MOO; Chain Lightning visait haut-gauche pendant que Fire Ball partait vers
  le bas. Cette route est rejetée. Le candidat suivant capturait seulement
  `UNITS_GetPathFirstPointX/Y 0x34B8F0/0x34B940` avant le handler source. Il a
  produit un faux positif puis une instabilité directionnelle reproductible :
  un point accepté par le caster de position ne constitue pas à lui seul un
  contrat de facing fiable.
- `PATH_SetDirection 0x342860` a l'ABI observée
  `void (DynamicPath*, uint8)`. Son corps exact de 34 octets est unique, possède
  20 xrefs natives, retourne sur path nul, borne toute valeur `>= 0x4D` à
  `0x4D`, puis écrit le byte obtenu à `path+0xBD` et `path+0xBC`. Le premier
  candidat setter a correctement restauré son entrée, mais cette entrée était
  périmée : 16 Chain Lightning différemment visées ont toutes journalisé la
  direction pré-handler `73`, et le kiting a reproduit le décalage Fire Ball.
  Le remplacement post-handler a lui aussi observé `73` malgré des points visés
  différents et a échoué en gameplay. Cast Triggers rejette donc les deux
  timings et retire `PATH_GetDirection`/`PATH_SetDirection` de son chemin actif
  et de son empreinte.
- `SKILLITEM_HandleItemEffectSkill 0x589930` démontre le contrat natif utile :
  il sauvegarde l'unité cible et le premier point courants, installe
  temporairement soit l'unité reçue soit les coordonnées reçues, exécute le
  handler central, puis restaure la cible ou le point original. Le candidat
  Cast Triggers qui échantillonnait ce descripteur avant le handler a échoué :
  les coordonnées courantes de l'unité ne sont pas censées égaler le premier
  point, et tous les casts Chain Lightning observés ont produit
  `target-matches-aim=0` malgré des cibles visibles valides.
- Les mêmes logs donnent à Chain Lightning les flags compilés
  `0x280C00A0400` et `unit-target-semantics=0`. Les flags Skills.txt ne peuvent
  donc pas décider génériquement si le handler a consommé une unité. La
  référence sémantique D2MOO confirme que `SKILLS_SrvDo026_ChainLightning`
  délègue sa destination au créateur de missile : celui-ci utilise
  `SUNIT_GetTargetUnit` lorsqu'une unité existe, sinon le premier point X/Y.
  Aucune adresse ni ABI D2MOO n'est transposée.
- Le candidat qui observait les wrappers
  `UNITS_GetPathFirstPointX/Y 0x34B8F0/0x34B940` a amélioré les clics sur unité,
  mais les logs 93847 ont compté 29 descripteurs unité, 34 self/none et zéro
  position. Le désassemblage explique ce résultat : les wrappers tail-jumpent
  vers `PATH_GetFirstPointX/Y 0x341CC0/0x341CD0`, tandis que le chemin
  Shift-ground de Chain Lightning appelle directement ces accesseurs bas
  niveau et contourne donc les deux hooks. Les accesseurs lisent les words
  `DynamicPath+0x10/+0x12` et possèdent respectivement 29 et 26 xrefs natives.
- Le candidat intermédiaire capture d'abord le `DynamicPath*` exact du joueur avec
  `UNITS_GetDynamicPath 0x34AE80`, puis observe dans un scope TLS
  `SUNIT_GetTargetUnit 0x48FE20` et les accesseurs bas niveau
  `PATH_GetFirstPointX/Y 0x341CC0/0x341CD0`. Les appels X/Y ne sont retenus que
  lorsque leur pointeur est exactement celui du joueur source. Une unité
  retournée devient le descripteur unité; une résolution nulle suivie d'une
  paire X/Y complète devient position; aucun accès devient self/none. Les
  wrappers et `PATH_GetX/Y` ne sont plus hookés ni fingerprintés. Aucun facing
  n'est reconstruit ou écrit, et aucun repli vers une autre cible n'est essayé.
- La matrice BKVince du 28 août a invalidé ce contrat comme source principale :
  War Cry arrivait systématiquement au handler sans aucun appel aux trois
  accesseurs observés, et Taunt y arrivait parfois sans appel malgré une cible
  valide. Le handler d'un skill ne consomme donc pas nécessairement le
  descripteur d'entrée qui a déclenché ce cast. Cette observation reste utile
  comme repli pour les ticks de channeling exécutés hors du scope d'entrée,
  mais elle ne constitue plus la visée autoritaire d'un cast manuel initial.
- Les callbacks serveur des actions joueur, référencés par la table
  `0x1D2A790` pour les opcodes `0x01..0x12`, convergent vers deux exécuteurs
  communs. Les actions de position, Shift et maintien compris, atteignent
  `0x4FDB40` avec l'ABI observée
  `int32 (game, player, x, y, argument, activeSkill)`. Les actions sur unité
  atteignent `0x4F8DE0` avec l'ABI observée
  `int32 (game, player, targetType, targetGuid, activeSkill, argument6,
  argument7)`; cette fonction résout elle-même la cible par
  `SUNIT_GetServerUnit 0x48FE80`. Les prologues stricts de 36 et 28 octets, et
  le témoin strict de 50 octets du resolver, sont chacun uniques dans le corpus
  gouverné.
- Les deux exécuteurs appellent synchroniquement la finalisation du mode joueur
  à `0x42D2C0`, mais la finalisation ne contient aucun appel direct au handler
  central. Le runtime 3.3.93847 confirme cette séparation : sur 64 handlers
  admissibles, aucun n'a exécuté pendant le scope TLS de l'exécuteur d'entrée.
  La position ou l'identité d'unité doit donc survivre à son retour dans un
  record borné par `(game, player, skill)`; l'unité est conservée comme
  type/GUID et résolue de nouveau seulement au handler correspondant. Le record
  d'un channel reste disponible pour les ticks suivants et tout nouvel input du
  joueur le remplace. L'observation dans le handler demeure le repli.
- Le témoin strict de 24 octets à `0x33DBA0` lit `D2Skill+0x00` comme pointeur
  SkillsTxt et teste le word du record à `+0x00`. Cast Triggers valide cette
  séquence avant de lire ce word comme identifiant natif du skill actif afin
  d'associer l'input au handler différé.
- Les ABI des casters sont `(caster,skillId,skillLevel,target,flag)` pour
  `0x5896E0` et `(caster,skillId,skillLevel,x,y,flag)` pour `0x589820`. Leurs
  signatures strictes étendues à 45 et 43 octets sont uniques. Cast Triggers
  substitue son marqueur positif réservé uniquement dans son TLS `doactive`,
  puis suspend ce contexte pendant le cast déclenché afin qu'aucun proc
  imbriqué ne l'hérite.
- D2MOO PropertyFunc11 masque le niveau avec `63` avant de l'ajouter au skill
  décalé de six bits. Le premier contrat `max=64` produisait donc un niveau
  interne zéro : le proc fonctionnait, mais le testeur a observé que la ligne
  `descfunc=15` disparaissait complètement. Le chemin D2R `descfunc=15` commence
  à `0x2D7F38`, décode le même layer skill/niveau et suit des branches de
  présentation jamais prévues pour un niveau de proc nul. Le contrat 0.1.0
  réserve désormais `max=63`, valeur positive que le plugin remplace pendant
  son seul dispatch; les niveaux fixes disponibles sont 1..62. Le fixture doit
  encore fournir la preuve visuelle fraîche de la ligne et du niveau effectif.
- Le patch PluginPack Whirlwind CTC à `0x589736` et son équivalent position à
  `0x58986B` se trouvent dans les corps natifs, après les prologues possédés par
  Cast Triggers. Aucun overlap de bytes n'est présent; le cold start pile
  complète reste néanmoins le gate de coexistence autoritaire.
- La politique du 25 août 2026 retire toute allowlist de build-name/version.
  Cast Triggers journalise l'identité reçue puis vérifie 26 témoins exacts
  avant le premier hook, dont les deux exécuteurs d'entrée, le resolver serveur,
  la séquence unique à `0x43ACEC` qui encode `game+0x106` et l'appel SkillsTxt,
  et la séquence unique à `0x09780B` qui encode le stride `0x2EC`.
  Toute différence refuse le chargement; une correspondance n'est pas une
  revendication de qualification pour un build qui n'a pas sa propre matrice.
- Références sémantiques uniquement :
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`,
  `source/D2Game/src/SKILLS/Skills.cpp:2445-2582`,
  `source/D2Game/src/SKILLS/SkillItem.cpp:1632-1680,1925-2040` et
  `source/D2Common/src/Items/ItemMods.cpp:3634-3698`. Aucune adresse,
  structure ni ABI 32 bits n'est transposée.

## Player sequence tables — baseline D2R 3.3.93847

- `SKILLS_GetSeqNumFromSkill` à `0x33DBC0` lit pour un joueur le byte
  `SkillsTxt.seqnum` à `+0x33` au site `0x33DC42`. Le chemin monstre reste
  distinct; aucune équivalence avec `monseq.txt` n'est inférée.
- `DATATBLS_GetSeqRecordFromUnit` à `0x3CB890` indexe la table runtime
  `0x2386650[seqnum]`, sélectionne une des 14 classes d'armes via la table
  `0x2386730`, puis choisit le record selon le mode et la frame.
- Le descripteur mesure 24 octets. La preuve native à `0x3CB987` construit
  `index * 3 * 8`; les champs capturés sont pointeur de records, nombre de
  frames de séquence, nombre de frames d'animation et QWORD auxiliaire `0x100`.
- Un record mesure six octets : `uint16 sequence`, puis les bytes `mode`,
  `frame`, `direction` et `event`. La baseline contient 808 records, 47
  tableaux runtime et 44 contenus uniques.
- La table contient un slot nul suivi de 25 groupes actifs et 14 classes
  d'armes, soit 350 routes : 235 présentes et 115 nulles. Les groupes 24
  `Cleave` et 25 `Mirrored Blades` sont propres au runtime courant par rapport
  à l'oracle D2MOO utilisé.
- Les 23 groupes legacy et 34 tableaux nommés par
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316` correspondent exactement aux
  routes et octets courants qu'ils décrivent. D2MOO reste une preuve sémantique
  seulement; aucune adresse, structure ou ABI 32 bits n'est transposée.
- Tous les groupes, descripteurs et records capturés sous D2R 3.3.93847 ont un
  témoin byte-exact dans l'image d'analyse gouvernée. Le groupe 6 `Inferno` a
  deux seeds statiques identiques (`0x1992660` et `0x1992DF0`), mais une route
  runtime unique; l'extracteur conserve explicitement cette ambiguïté au lieu
  de fabriquer une unicité.
- Le seed statique du mapping des 14 classes est unique à `0x19EAF70` et
  concorde avec la table runtime `0x2386730`.
- Les sorties normalisées, le manifeste de hashes, le captureur runtime et les
  TSV déterministes sont gouvernés par
  `Mission/player-sequence-tables-3.3.md`. Cette phase ne prouve pas encore le
  contrat de propriété, la durée de vie, le remplacement de longueurs variables
  ni l'autorité multijoueur; ces points bloquent toute implantation.

## Gear Swap Inventory — BodyLoc étendus autonomes

- Le handler serveur `D2GAME_SERVER_HandleWeaponSwap` à `0x4BEA50` porte l'ABI
  observée `(game, player, packet, packetSize) -> int32` et exige exactement
  30 octets. Il est atteint après la résolution du binding joueur : un plugin
  l'observant conserverait donc un remapping de `W` vers `X` sans prendre un
  second hook de `UI_DispatchMessage`.
- Le handler appelle le batch commun de changement d'état d'objets à
  `0x471E90`. Le validateur final à `0x474700` accepte les emplacements vanilla
  utiles au weapon set, au body, au cursor et au stash, mais refuse la page
  physique protégée 6. Cela invalide une seconde page, pas le mapping BodyLoc
  autonome maintenant retenu.
- Le binaire D2RCore exact de la baseline implémente
  `InventoryServiceV1::registerPlayerPage` à sa RVA privée `0x2A5620` avec un
  état global single-provider. La première inscription possède la page
  physique 6; toute inscription suivante est refusée comme conflit.
- La pile BKVince active contient `d2rl-charm-inv.dll`. Ses références de
  services et son log frais prouvent qu'il possède déjà cette page protégée,
  son panneau, son bouton et une grille `10 x 4`. Une seconde page Gear Swap ne
  peut donc pas coexister avec Charm Inventory sous V1.
- La collision V1 reste une preuve gouvernée, mais Vincent rejette cette
  dépendance. L'architecture sélectionnée utilise les BodyLoc secondaires
  13–20 et laisse Charm Inventory actif sans consommer son service de page.
- Le sérialiseur lit `ItemData+0x54`, borne le BodyLoc à 15 et écrit quatre bits
  au witness unique `0x37CB37`. Les lecteurs quatre bits uniques commencent à
  `0x3781F7` et `0x3789A4`, puis rangent le résultat dans `ItemData+0x54`.
  Le format expérimental conserve `0..12`; `15` devient le sentinel suivi de
  trois bits donnant `13..20`. Le même chemin étant partagé avec les paquets
  d'items, le prototype doit refuser le multijoueur.
- `INVENTORY_GetItemFromBodyLoc` à `0x3886D0` et
  `INVENTORY_PlaceItemInBodyLoc` à `0x3891E0` ont des prologues uniques et deux
  gardes `<=12`. `INVENTORY_ResolveOccupancyGrid` à `0x38B070` copie width et
  height du descripteur, alloue `width*height` pointeurs et refuse ensuite des
  dimensions différentes. Le descripteur body runtime `0x237B620` doit donc
  être vérifié à `13 x 1` puis étendu à `21 x 1` avant toute allocation joueur.
- Aucune corruption de sauvegarde n'a été observée. Le risque est une hypothèse
  acceptée qui sera testée uniquement sur des personnages jetables hashés;
  retirer le plugin avec des objets en 13–20 est interdit jusqu'à preuve d'une
  procédure de récupération.

## Armageddon et Hurricane en chance-to-cast

- Le helper serveur d'effet d'objet à `0x589930` refuse immédiatement une
  ligne SkillsTxt dont le word `ItemEffect` à `+0x20A` vaut zéro. Une fixture
  Fallen a reproduit l'assertion `ptSkill->nItemEffect != 0`; forcer
  temporairement `ItemEffect=1` supprime cette assertion sans faire apparaître
  Armageddon.
- Le callback partagé `SrvDo124` à `0x575DE0` appelle
  `UNITS_GetUsedSkill`, puis refuse le lancement si le noeud absent ou son
  SkillsTxt ne désigne pas le skill demandé. C'est le second gate indépendant
  du défaut CtC.
- La liste de skills est à `Unit+0x100`, son premier noeud à `+0x00` et le
  used skill à `+0x18`. Un noeud D2Skill porte son SkillsTxt à `+0x00`, son
  suivant à `+0x08`, son seed `Param1` à `+0x24`, son niveau à `+0x40`, son
  owner GUID à `+0x4C` et le filtre du resolver à `+0x54`.
- L'active callback Armageddon à `0x574E90` résout obligatoirement le skill via
  `SKILLS_GetHighestLevelSkillFromUnitAndId` à `0x33DD40`, puis consomme et
  renouvelle `Param1`. L'active callback Hurricane à `0x575600` ne dépend plus
  d'un noeud de skill une fois l'état initial créé.
- Le mécanisme retenu dans `Mission/armageddon-ctc-fix.md` reste synchrone et
  borné : `ItemEffect` est restauré après le helper, le used skill synthétique
  est stack-local pendant SrvDo124, et le noeud Armageddon synthétique est lié
  seulement pendant chaque active callback. Aucun noeud fabriqué n'est
  persistant ni sérialisé.
- Le témoin d'expiration à 250 frames a confirmé que l'état natif s'arrête, mais
  a révélé que la table interne 0.1.0 conservait encore sa graine. La 0.1.1
  emprunte sans le hooker `STATES_CheckState 0x3351B0`, dont la signature
  stricte de 32 octets est unique, pour effacer l'entrée avant le callback si
  l'état a disparu. Après un retour zéro, une seconde vérification efface
  l'entrée seulement si le callback vient de retirer l'état.
- Le retour du callback ne constitue pas seul une preuve d'expiration : la
  référence sémantique D2MOO supprime l'événement et retourne zéro si l'état est
  absent (`SkillDruid.cpp:1074-1081`), mais peut aussi retourner zéro après
  avoir replanifié l'événement lorsque la pièce ou la création du missile ne
  convient pas (`SkillDruid.cpp:1122-1150`). Le prédicat d'état est donc le
  discriminateur correct; aucune adresse D2MOO n'est transposée.
- La preuve sémantique est corroborée par
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`, fichiers
  `D2Game/src/SKILLS/SkillItem.cpp` et
  `D2Game/src/SKILLS/SkillDruid.cpp`. Aucune adresse ni structure 32 bits n'est
  transposée. Les RVA et octets ci-dessus proviennent exclusivement du corpus
  natif gouverné commun aux cibles 3.2.92777 et 3.3.93847.

## Skill Trees Revamp — constructeur page IV et positions libres

- Le constructeur logique des widgets de classe est borné à
  `0x14C4520..0x14C5E96`. Le wrapper unique `0x14C6C70` l'appelle avant
  `SKILLTREE_RebuildPageWidgets 0x14C7720`; ce dernier rafraîchit donc des
  enfants existants et ne constitue pas leur factory.
- La boucle native du constructeur émet les couples `(Tab0, SkillPage 1)` à
  `(Tab2, SkillPage 3)`. Modifier son seed `3 -> 4` déplacerait ces pages et
  contaminerait un état réutilisé. Le seed exact à `0x14C488A` porte un
  témoin unique de 28 octets qui initialise `R14D`, `R13D` et `[rsp+0x3C]` à
  3, puis `EAX` et `[rsp+0x38]` à 0. La couture minimale prouvée est son tail à
  `0x14C5325` : neuf octets uniques, couverts par un témoin unique de 34 octets
  commençant à `0x14C5313`.
- Après la troisième page, un relais feuille peut installer `R13D=4` et
  `[rsp+0x3C]=4`, puis reprendre le corps à `0x14C48B0`. Après cette itération
  `(EAX=4,R13D=3)`, il restaure `R13D=0`, `[rsp+0x3C]=0`, `EAX=3` et
  `[rsp+0x38]=3` avant la continuation `0x14C532E`. Désactivé, il reproduit
  exactement le test et la branche vanilla.
- L'entrée ciblée `0x14C48B0` possède un témoin exact unique de 22 octets :
  elle résout d'abord `TabEAX` par `SKILLTREE_FindTabWidget`, puis rejoint le
  tail si le tab manque. Le relais ne peut pas viser ce corps sans que ce témoin
  et la continuation aient tous deux passé l'empreinte fail-closed.
- Après cette boucle, la factory General Skills reste historiquement fixée à
  `Tab3` : le bloc unique de 15 octets à `0x14C536C` charge le nom littéral,
  conserve `RCX=SkillTreePanel` et appelle `UI_FindChildWidgetByName`. Le
  remplacement 0.8.0, désassemblé byte-exact, fait `push 4; pop r14; mov
  edx,r14d; mov rcx,r12; call SKILLTREE_FindTabWidget 0x14C5F60`. Il est
  stack-neutral, ne modifie aucun flag, sélectionne `Tab4` par une ABI déjà
  gouvernée et laisse `R14D=4` alimenter naturellement
  `SKILLTREE_FindTextTabWidget` pour `TextTab4`. Le patch complet de 15 octets
  et son contexte successeur unique à `0x14C537B` sont fingerprintés. Le témoin
  unique de 19 octets à `0x14C53B9` prouve ensuite `mov edx,r14d; mov rcx,r12;
  call SKILLTREE_FindTextTabWidget` et ferme la chaîne fail-closed jusqu'à
  `TextTab4`; aucun troisième hook renderer n'est requis.
- Le parent résolu est sauvegardé à `[rsp+0x68]`, puis rechargé dans `RSI` sous
  le témoin unique `0x14C5854`. Le bloc unique `0x14C59D0` cherche ensuite
  `CommonSkillsContainer` et `ItemSkillsContainer` sous ce même tab. Le guard
  unique `0x14C59F6` abandonne la population si l'un des deux manque ou si le
  root du panel ne fournit pas son `ButtonTemplate` à `panel+0x8F8`. Le contrat
  softcode clavier/souris exige donc `Tab4`, `TextTab4`, `ActivateTab:4`, les
  deux conteneurs et un `SkillSelectButtonWidget` racine nommé
  `ButtonTemplate`.
- Le chemin General/Item conserve les catégories natives : le conteneur Common
  reçoit la catégorie 3, tandis que les catégories 4 à 6 alimentent le
  conteneur Item. Les oskills font partie de ce second ensemble, mais le corpus
  ne justifie pas de le décrire comme un filtre « oskills seulement ». L'onglet
  combiné reste non investissable et ne crée aucun sixième état.
- Au rebuild, le témoin unique de 33 octets à `0x14C7BA6` lit l'état courant et
  passe dynamiquement cet index à `SKILLTREE_FindTabWidget` avant le gate
  General. Les gates déjà déplacés de 3 à 4 font donc naturellement reconstruire
  `Tab4`; aucune seconde retargetisation de lookup n'est nécessaire.
- Chaque skill est obtenu depuis le compte dynamique de classe, alloué sur
  `0xB88` octets, construit par `UI_ButtonWidget_Constructor 0x86E920`, reçoit
  son payload natif, clone son `CounterTemplate` par
  `UI_CloneWidgetTree 0x855010`, puis est attaché par
  `UI_Widget_AddChild 0x854DE0`. Rejouer la boucle pour la page IV conserve
  donc les vrais clics, tooltips, compteurs et chemins d'allocation D2R.
- Le callsite unique `0x14C50F1` appelle
  `UI_SetWidgetLocalPosition 0x856FB0`. Son témoin contextuel unique commence à
  `0x14C50D8`; le setter a l'ABI observée `(widget, PointI*)` et écrit seulement
  les coordonnées locales `widget+0x70/+0x74`. Skill Trees Revamp peut ainsi
  remplacer `x/y` à ce seul callsite et laisser ses 98 autres consommateurs
  natifs intacts.
- Deux chemins indépendants à `0x14C7536` et `0x14C7D28` lisent le pointeur de
  payload à `SkillWidget+0x668`, puis l'identifiant signé 32 bits à
  `payload+0`. Cet identifiant correspond à l'ordinal physique zéro-based de
  la ligne `skills.txt`; la colonne documentaire `*Id` n'est pas autoritaire.
- Côté producteur, le témoin exact unique de 21 octets à `0x14C5020`
  contient le `LEA` de `SkillWidget+0x668` à `0x14C5027`; le `LEA` nu n'est pas
  unique. Le bloc exact unique de 20 octets à `0x14C50A0` publie ensuite le
  pointeur temporaire dans ce slot avant le call de position. Le contexte
  successeur de 21 octets à `0x14C50F6` est également unique.
- La boucle accepte un nombre dynamique de lignes de skill et construit chaque
  record correspondant à la page active. Le bloc exact unique de 59 octets à
  `0x14C4A9A` prouve les clamps `SkillColumn 1..3` et `SkillRow 1..6`. Le témoin
  unique de 26 octets à `0x14C4B0A` passe ensuite `SkillPage` dans `R8D`, la
  colonne clampée moins un dans `R9D` et la rangée clampée moins un comme
  cinquième argument au formatter; plusieurs cellules étendues peuvent donc
  partager un nom dynamique.
- La table locale historique est remise à zéro par neuf stores XMM à
  `0x14C4992..0x14C49D1`, puis le widget complété est écrit à `0x14C529F` selon
  `6 * clampedColumnIndex + clampedRow`, soit un des 18 slots. L'audit exhaustif
  des 1 435 instructions du corps borné trouve uniquement cette initialisation
  et cette écriture : aucune lecture, comparaison, fuite d'adresse, transmission
  à un callee, destruction ou cleanup. Un alias écrase donc un pointeur jamais
  consommé; cette table n'impose aucune limite fonctionnelle démontrée à 18.
- Le rebuild lit le nombre réel d'enfants à `0x14C7CDF`, les énumère par index
  avec `UI_GetChildWidgetByIndex` à `0x14C7CF5`, puis charge le payload à
  `SkillWidget+0x668` et son Skill ID signé à `payload+0` dans le témoin
  `0x14C7D28`. Le chemin d'interaction indépendant à `0x14C7536` consomme la
  même identité. Les noms clampés et la table locale ne sont donc pas l'identité
  opérationnelle des skills.
- La source 0.5.0 retire le plafond artificiel et le refus d'alias sans nouveau
  hook. Une page étendue, une page de plus de 18 skills ou une page contenant un
  alias clampé exige une position résolue pour chaque skill; les collisions X/Y
  finales restent refusées. Cette conclusion est une preuve statique commune
  aux builds gouvernés 3.2.92777 et 3.3.93847, pas une qualification runtime.
- Les carrés sombres, cadres et flèches vanilla sont peints dans le fond de
  page; `CounterTemplate` est textuel. Ce lot ne déplace ni ne synthétise ce
  chrome, les connexions, les titres ou les fonds. Ces surfaces restent un lot
  distinct avant toute qualification visuelle générique.
- Le callsite unique `0x14C5214` clone précisément ce `CounterTemplate` par
  `UI_CloneWidgetTree 0x855010`. `RCX=RDI` porte le SkillWidget dont le payload
  `+0x668` est déjà publié, `RDX=[rsp+0x60]` porte le template et le clone attaché
  revient dans `RAX`. Un wrapper étroit pourrait donc appeler l'original une
  fois, filtrer l'ordinal Skill ID puis déplacer seulement le clone avec
  `UI_SetWidgetLocalPosition 0x856FB0`. Le setter de rectangle complet
  `UI_SetWidgetLocalRect 0x857000` copie `{x,y,w,h}` dans `+0x70..+0x7C`. Cette
  couture ne produit cependant aucun carré ni aucune flèche, et n'est pas
  requise lorsque l'empreinte composite tient déjà dans la zone utile.
- L'audit de l'overlay automatique identifie le page container créé/finalisé à
  `0x14C4904`, attaché à `TabN` par le callsite unique `0x14C4925`, puis utilisé
  comme parent de chaque SkillWidget à `0x14C5284`. Un overlay ajouté comme son
  premier enfant hériterait de la visibilité native de `TabN` à
  `0x14C7850..0x14C786B`, du thread UI de
  `SKILLTREE_InitializePanelWidgets 0x14C6C70` et de la destruction récursive de
  `UI_Widget_AddChild 0x854DE0`/`UI_Widget`.
- `UI_ImageWidget_SetFilename 0x859B20` est identifié à haute confiance par son
  appel `0x14C4979` sur le `TabN/Background` layout-backed. Son ABI observée
  `(ImageWidget*, StringView*)` copie le chemin logique à `+0x108`, charge via
  `0x858990`, puis met à jour l'asset et sa géométrie intrinsèque par
  `0x859710/0x8574D0`. Le candidat factory `0x859BF0` reste à confiance moyenne
  et n'est pas appelé. Le contrat 0.8.0 exige plutôt un dernier enfant softcodé
  `ChromeContainer` sous le Tab, avec six templates `ImageWidget` déjà typés et
  non interactifs. Le wrapper de `0x14C4925` clone ces templates avec le helper
  gouverné `UI_CloneWidgetTree 0x855010`, pose leurs rectangles avec
  `UI_SetWidgetLocalRect 0x857000`, puis délègue à `UI_Widget_AddChild
  0x854DE0`. Le ChromeContainer précède ainsi le page container natif et ses
  SkillWidgets dans l'ordre des enfants, sans factory ni cast nouveau.
- `UI_GetCumulativeWidgetScale 0x1E6750`, identifié par une signature d'entrée
  unique de 31 octets, suit récursivement le parent `Widget+0x30` et multiplie
  chaque `Widget+0x80`. Le factory `UI_ButtonWidget_Factory 0x86EE60`, lui aussi
  identifié par une signature unique, alloue exactement `0xB88` octets et
  tail-call le constructeur `0x86E920` avec le nom et le parent préservés.
- Le rendu du ButtonWidget à `0x8F07BD..0x8F0853` multiplie explicitement le
  scale parental par son propre `+0x80` avant de dimensionner son image. Le
  `CounterTemplate` est cloné à `0x14C5214` avec ce SkillWidget comme parent et
  les enfants sont rendus par `0x855B00`; la propagation parentale est donc
  fermée, mais le renderer texte concret du compteur reste à lier byte-exact.
- Le tooltip de compétence à `0x14C42BA..0x14C4429` calcule la position écran,
  multiplie le scale parental par `SkillWidget+0x80`, applique ce produit à la
  largeur et à la hauteur, puis transmet le rectangle obtenu au dessin. Le
  placement du tooltip suit donc un SkillWidget compacté.
- Le hit-test générique `0x8566E0..0x8567E8` et la conversion inverse des
  coordonnées parentes à `0x8572B0..` consomment eux aussi les scales. La vtable
  ButtonWidget `0x1CE4148` est toutefois nulle dans les deux images PE statiques
  gouvernées : aucun slot concret ne peut encore être attribué honnêtement au
  SkillWidget. Skill Trees Revamp ne compacte donc pas douze rangées par un
  write direct de scale et conserve le refus des chevauchements réels.
- Le candidat diagnostic 0.7.1 embarque une sonde passive, activée
  seulement avec les diagnostics existants. La couture de position déjà
  possédée publie le dernier SkillWidget géré de la page IV; la couture de fin
  de page déjà possédée ne l'inspecte qu'à `RestoreTerminal`, donc après le
  clone `CounterTemplate 0x14C5214` et l'attachement `0x14C5284`. Elle
  journalise une fois la vtable du SkillWidget, ses slots `+0x18/+0x80`, puis
  jusqu'à soixante-quatre enfants avec vtable, rectangle et scale; le rectangle connu
  `{148,82,52,52}` identifie le compteur. Aucun widget n'est muté et aucun hook
  n'est ajouté. La sonde 0.7.1 est compilée et déployée, mais non exécutée comme
  témoin runtime autorisé. Ce témoin doit encore lier le slot de hit-test et le
  renderer du CounterTemplate avant implantation.
- `UI_DispatchMessage 0x843D90` demeure exclu et reste sous son propriétaire
  existant. Aucun propriétaire concurrent des coutures `0x14C5325` et
  `0x14C50F1` n'a été trouvé dans la Suite ou les cinq plugins eezstreet.
- Toutes les surfaces ci-dessus sont byte-exact dans le corpus gouverné commun
  aux builds `3.2.92777` et `3.3.93847`. La source 0.8.0 conserve l'empreinte
  fail-closed et les deux hooks renderer, puis ajoute seulement le retarget
  factory `0x14C536C` aux onze mutations de panneau historiques. Build, tests,
  audit DLL, déploiement et runtime 0.8.0 ne sont pas exécutés dans ce lot.
- Les deux hooks renderer sont installés pass-through avant les douze patches de
  panneau, le retarget factory étant le dernier, et leur gate ne s'ouvre
  qu'après le commit complet des quatorze mutations. Le SDK courant ne
  fournit ni transaction atomique ni unpatch : tout commit partiel demeure
  chargé, inactif, comptabilisé et exige un cold restart. La baseline gouvernée
  D2RLoader 1.1 conserve les DLL chargées pendant la vie du processus et appelle
  l'unload seulement au shutdown; le hot reload n'est pas revendiqué.

## MapSense 0.12.0 — table cliente complète et tombe correcte de Duriel

- `CLIENT_GetUnitByIdAndType 0x9A5D0` résout par RIP relatif la table cliente à
  `D2R+0x2A23910`, masque le bucket avec `0x7F` et applique un stride de
  `0x400` octets par type. Cela prouve 128 pointeurs de bucket par type; le type
  monstre 1 commence donc à `table+0x400`.
- Le helper `0x9F270` charge une tête de bucket, compare `Unit+0x08` à l'id et
  `Unit+0x00` au type, puis suit `Unit+0x158`. Son bloc complet de 41 octets et
  l'entrée de 28 octets de `0x9A5D0` sont fingerprintés fail-closed. MapSense
  parcourt uniquement les 128 buckets monstres, toutes les 50 ms au plus,
  avec plafonds par bucket et par scan; aucun pointeur Unit n'est conservé.
- Cette collecte remplace la dépendance incorrecte au sous-ensemble déjà choisi
  par `AUTOMAP_RenderUnit`. Le rayon reste un cercle euclidien en vraies
  coordonnées DynamicPath world-subtile, borné à 30..220. Des compteurs séparés
  `0..80`, `81..140`, `141..220` et `>220` rendent la portée observable.
- `DRLG_GetHoradricStaffTombLevelId 0x326A70` retourne `Drlg+0x120`; MapSense
  n'accepte que les ids 66..72. Le target Level est initialisé seul, puis la
  sortie publiée réutilise l'ancre RoomTile exacte déjà résolue par Navigation.
- `QUESTRECORD_GetQuestState 0x325C50` est appelé sur le record client de la
  difficulté courante chargé depuis `D2R+0x2A48778`, dont le témoin unique est
  à `0x114C20`. L'index sémantique A2Q6=14 et RewardGranted=0 vient de
  `D2MOO@3b21043b99e987bad41cf0f7b49f1f246db52d5c`; RVA, ABI, pointeur et octets
  x64 viennent exclusivement du corpus D2R gouverné.
- Avant RewardGranted, la même ancre de tombe correcte est une destination de
  quête rouge. Après RewardGranted, elle devient la progression verte pour le
  farm de Duriel. Un record, un id ou une sortie encore indisponible reste
  `PartialRetryable`; aucune tombe approximative n'est publiée.

## MapSense 0.13.0 — contrat natif générique des objets et noms de boss

- `UNITS_GetObjectInteractType 0x34AD40` possède un corps complet unique de
  72 octets dans le corpus commun 92777/93847. Il exige un `Unit` type 2, lit
  `ObjectData*` à `Unit+0x10`, puis retourne l'octet
  `ObjectData.InteractType` à **`+0x08`**. Cette preuve x64 remplace toute
  transposition du layout D2MOO 32 bits. MapSense couvre l'entrée et le chemin
  de layout complet avant d'appeler l'accessor.
- `OBJECTS_IsShrine 0x34C470` fournit le classifieur runtime générique : type 2,
  `Unit+0x10`, record `ObjectsTxt` compilé à `ObjectData+0`, puis test du bit
  `0x01` de `SubClass` à `ObjectsTxt+0x127`. Son corps strict de 30 octets est
  unique. Une ligne custom qui conserve ce contrat moteur est donc reconnue
  sans identifiant BKVince ou vanilla.
- La sélection native confirme que `InteractType` est **l'index de ligne
  `ShrinesTxt` sans décalage**. `DATATBLS_GetShrinesTxtRecordCount 0x38FE00`
  lit le compte actif à `DataTables+0x19B8`; le témoin intérieur unique
  `0x50E510` soustrait un, effectue le tirage borné, puis ajoute un. Le domaine
  aléatoire est donc exactement `1..count-1` et la ligne 0 n'est pas choisie.
  `DATATBLS_GetShrinesTxtRecord 0x38FE40` borne ce même index, lit la base à
  `DataTables+0x19B0` et applique un stride compilé de `0x1C`.
- L'entrée véritable de `OBJECTS_InitFunction01_Shrine` est **`0x50E450`**;
  `0x50E510` n'est que sa boucle de sélection, et ne doit pas être publiée
  comme une fonction. Après les remaps natifs `4->2`, `5->3` et `16->18`,
  l'initialiseur passe le même index à `UNITS_SetObjectInteractType 0x34E9D0`
  (`ObjectData+0x08`) et à `DATATBLS_GetShrinesTxtRecord`, puis tail-jump vers
  `UNITS_SetShrineTxtRecordInObjectData 0x34EE70` (`ObjectData+0x10`). Les
  corps D2R uniques recoupent la sémantique de
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`,
  `source/D2Game/src/OBJECTS/Objects.cpp:487-546` et
  `source/D2Common/src/Units/Units.cpp:1972-1982`, sans transposer d'adresse ni
  de layout 32 bits.
- Le prédicat générique fail-closed recommandé pour publier un **texte de
  shrine** est **`InitFn == 1 && (SubClass & 0x01) != 0`**. `InitFn == 1` seul
  est trop large : dans le `objects.txt` BKVince actif, `Obelisk2` porte
  `InitFn=1`, mais `SubClass=2` et `OperateFn=17`; ce serait un faux positif.
  L'intersection exige à la fois le classifieur shrine du moteur et
  l'initialiseur qui assigne réellement une ligne `ShrinesTxt`. Une ligne de mod
  custom demeure donc reconnue si elle conserve ces deux contrats, sans
  allowlist d'ID ou de nom.
- Audit gouverné byte-exact/CRLF du `objects.txt` BKVince actif : 93 lignes ont
  `InitFn=1`, 92 ont aussi le bit `SubClass 0x01`, et l'unique exception est
  `Obelisk2`. Les classes `HealingWell`, `ManaWell1..5`, `JungleHealWell`,
  `HellWell` et `HellManaWell1` portent volontairement `InitFn=1`,
  `SubClass=1`, `OperateFn=2` : malgré leur nom de classe, ce sont des objets à
  sémantique shrine qui reçoivent un vrai record `ShrinesTxt` et leur buff doit
  pouvoir être nommé. Les 14 puits ordinaires `Fountain1..8`, `TombWell`,
  `WellExp`, `WellSnowy`, `WellBaal`, `WellTemple` et `WellIceCave` utilisent
  `InitFn=16`, `OperateFn=22` et `SubClass=0` ou `32`; ils restent donc exclus.
  `OBJECTS_InitFunction16_Well 0x50E600` prouve cette séparation : son corps
  complet unique de 22 octets calcule seulement
  `InteractType = 2 * low8(ObjectsTxt.Parm2)` depuis `ObjectsTxt+0x13C` et
  n'assigne jamais de record `ShrinesTxt`.
- La table compilée active est bornée par
  `DATATBLS_GetObjectsTxtRecordCount 0x38FC70` (`DataTables+0x1530`) et résolue
  par `DATATBLS_GetObjectsTxtRecord 0x38FD00` (base `+0x1528`, stride
  `0x168`, retour nul hors borne). Les dispatchers natifs lisent `InitFn` à
  `+0x15C` et `OperateFn` à `+0x15E`; la corrélation byte-exact entre
  `objects.bin` officiel 3.3 et `objects.txt` établit aussi `SubClass +0x127`
  et `Lockable +0x12F`.
- Le contrat chest fail-closed accepte `InitFn=3` (`ObjectInitChest`) ou
  `InitFn=57` (`ObjectInitBetterChest`). `OperateFn=4` seul est insuffisant :
  la ligne vanilla `MephistoBridge` le porte avec `InitFn=45`, donc serait un
  faux coffre. Inversement, `IceCaveEvilUrn` emploie `InitFn=3` avec
  `OperateFn=68`; le `InitFn` est le meilleur invariant générique démontré.
  Un nouveau callback custom dont la sémantique n'est pas connue reste caché
  ou exige un override sémantique mod-local par clé stable, jamais une liste
  numérique extraite de BKVince.
- Seulement après cette classification coffre, le bit `0x80` de
  `InteractType` signifie locked et les sept bits bas portent le type de trap.
  Le producteur natif de chest lit `Lockable +0x12F` puis pose/efface le bit
  haut à `0x50EA3C`; les consommateurs `0x58E461` et `0x58E837` séparent
  respectivement `>>7` et `&0x7F`. Le dispatcher `0x594630` n'accepte que les
  valeurs `<10`; `0` signifie aucune trap, `1..9` trapped et toute valeur
  supérieure reste invalide/fail-closed.
- `UNITS_GetObjectRuntimeFlagsC8 0x4903D0` retourne l'octet `Unit+0xC8`. Le
  chemin chest `0x58E48D` teste explicitement son bit `0x01`, et la réplication
  cliente copie ce même octet dans `Unit+0xC8`. Après classification coffre,
  ce seul bit autorise l'étoile sparkly/super-chest; les autres bits et objets
  demeurent opaques.
- Les racks se classent sans allowlist par le callback actif :
  `OperateFn=19` pour armor rack et `OperateFn=20` pour weapon rack. Cette voie
  reconnaît aussi les lignes custom qui réutilisent les callbacks moteur; les
  callbacks inconnus restent cachés.
- `UNITS_GetSuperUniqueIndex 0x38E3D0` est couvert par son corps strict complet
  de 92 octets, unique dans le corpus commun. Il vérifie deux fois le type 1,
  lit `MonsterData*` à `Unit+0x10`, retourne le `uint16` à
  `MonsterData+0x2A`, et retourne `-1` sur null, mauvais type ou données
  absentes. Une valeur positive n'est jamais un nom : elle doit être bornée
  par la `SuperUniques.txt` du mod actif, puis résolue par sa localisation
  active. Les boss fixes non-SU ne peuvent être nommés qu'à partir de la ligne
  `MonStats.txt` active et de ses flags `boss`/`primeevil`; les enums statiques
  PrimeMH et le profil test BKVince ne font pas autorité.
- Le filtre de niveau courant partagé par les monstres et POI est maintenant
  entièrement fingerprinté : entrée unique de 32 octets de
  `UNITS_GetRoom 0x34B440`, témoin branches/layout de 36 octets à `0x34B461`,
  accessor complet `ACTIVEROOM_GetDrlgRoom 0x192B20` prouvant
  `ActiveRoom+0x18`, puis corps complet de 14 octets de
  `DRLGROOM_GetLevelId 0x360FC0`. Les deux producteurs refusent donc de se
  charger si une fonction ou un layout de cette chaîne diverge.
- Après durcissement de ces empreintes, la DLL Release se compile sans warning
  traité en erreur et le test `ruffneckk-mapsense-policy` passe. Il s'agit
  d'une validation hors jeu; l'approbation visuelle et la qualification runtime
  complète 0.13.0 restent distinctes.

## 2026-08-30 — ISC12 canonical G1–G4 codec planner

- G1 ajoute neuf mutations sémantiques d'un octet réparties sur quatre
  fenêtres intérieures uniques : `0x37AB2B`, `0x37B7D4`, `0x37F186` et
  `0x37F983`. Elles passent les widths `9→12`, les sentinelles
  `0x1FF→0xFFF` et conservent le seed `previousStatId = -1`. Les entrées
  decoder/serializer restent une preuve statique d'identité et d'ownership au
  ledger, jamais des témoins runtime du groupe; le plan ISC12 ne les revendique
  pas. Le prototype RuffnecKk ExtendedItemStats et son build de fork RuffDood
  les ont historiquement hookées, sans que cela soit attribuable au PluginPack
  officiel eezstreet. Le CALL intérieur du lecteur suivant reste toutefois exact :
  `0x37B7DC` résout le thunk gouverné `0xA1B6C0`; toute redirection est rejetée
  par le préflight avant la première écriture.

- G2 auxiliaire est fermé statiquement par cinq sites mutables exacts uniques :
  CALL exhaustif `0x531A6D` dans la fenêtre `0x531A54`, readers
  premier/suivant `0x530A99`/`0x530BA3`, writer ID `0x5340C0` et terminator
  `0x534139`. Le témoin unique `0x530A6B` gouverne le marker
  `0x6667` et sa branche de rejet; les champs valeur/param pilotés par
  `ItemStatCost` et le contrôle du count compilé restent inchangés. Seuls les
  immédiats ID width `9→12` et terminator `0x1FF→0xFFF` mutent.
- G3 régulier possède les deux CALLs exhaustifs `0x52EC4A`/`0x530A34`, les
  readers `0x53395E`/`0x533A93`, writer `0x5352F6`, terminator `0x5353A8`,
  CALL finalize `0x5353BD` et publication de statut `0x5353C7`. Le témoin
  unique `0x533924` gouverne le marker/bounds moderne. Les huit sites
  constituent un groupe atomique distinct de G2.
- Les consumers de champs sont également gouvernés : `0x530B69` lit en G2
  `CsvParamBits` vers un paramètre 16 bits puis une valeur fixe de 32 bits;
  `0x533A38` et `0x533A52` lisent en G3 le paramètre 16 bits puis dispatchent
  `CsvBits<32` signed/unsigned et `CsvBits==32` unsigned. Le préflight refuse
  aussi tout ID référençant une ligne `CsvBits==0`, comme les readers natifs.
- G4 preview/frontend consomme ce même format sans writer propre. La version
  exacte 105 atteint exclusivement la branche B `0x61D647`/`0x61D690`, dont
  width/sentinelle passent à 12 bits/`0xFFF`. La branche A legacy
  `0x61D247`/`0x61D290` demeure volontairement 9 bits/`0x1FF` comme témoin.
  Le troisième site mutable est le CALL de copie `0x61CF90`, gardé avant B.
- Les seize signatures de mutation G2–G4 ont chacune exactement un match dans
  `.text`. Avec les quatre fenêtres G1, le plan canonique hors runtime contient
  quatre groupes, 20 fenêtres mutables exactes, 49 slots gouvernés et 57 témoins
  runtime inchangés. Il préflight le set G1–G4 complet avant la première
  écriture et classe toute écriture ou flush incertain après ce point comme un
  commit exigeant un cold restart. Aucune de ces mutations n'est publiée.
- G2 alloue exactement `0x4000` octets à `0x534006`; son cap source
  `0x533EAD` et son consommateur `0x53405C` conservent au plus `0x200`
  entrées. Les témoins `0x5340D2`/`0x5340FD` prouvent par entrée la formule
  `12 + CsvParamBits + 32` et son back-edge lié au snapshot. Le snapshot
  clean-sheet impose maintenant `CsvBits <= 32` et `CsvParamBits <= 16`, selon
  les représentations natives 32/16 bits. Le témoin unique `0x5351DD` prouve
  le cas spécial G3 : `CsvBits == 32` saute le clamp `1<<CL` réservé aux
  largeurs 1..31 et rejoint directement l'écriture 32 bits. Le témoin
  `0x535308` charge `CsvParamBits`, réduit le paramètre à 16 bits puis applique
  son guard avant l'écriture, ce qui ferme l'autre moitié du contrat 32/16.
  Le témoin des écritures `0x535352` inclut leur back-edge contre R15 et relie
  ainsi le cap `0x200` au nombre réel d'entrées sérialisées.
  Le pire G2 complet vaut donc
  30 732 bits, soit 3 842 octets plus le marker de deux octets, dans `0x4000`.
  Le coût exact du seul passage 9→12 pour 512 IDs et leur sentinelle reste
  1 539 bits, soit au plus 193 octets selon l'alignement.
- G3 reçoit sa fenêtre de sortie du caller (`0x52F0C3`/`0x52F0E7`), borne son
  snapshot à `0x200` entrées au témoin unique `0x535162`, et son unique caller
  teste le retour à `0x52F522`. Le writer refuse une fenêtre trop petite pour
  son marker à `0x535115`; l'initializer `0xA1B650` met le flag +0x20 à zéro,
  le core `0xA1B7A0` avance les cursors et `0xA1B72C` pose le flag sticky sur
  overflow. Le leaf natif `0xA1B610` calcule le used-end dans RAX.
- Le guard préparé copie dans la page RX persistante un leaf sans stack ni
  registre non volatil : il reproduit exactement le RAX de `0xA1B610` puis
  charge `[bitstream+0x20]` dans EDX. Le CALL `0x5353C2` reçoit un rel32 lié
  uniquement à cette entry par l'autorité loader; ses quatre bytes sont écrits
  et flushés avant le site non chevauchant `0x5353C7`, où `33 C0→8B C2` publie
  EDX strictement en dernier. L'état intermédiaire reste sémantiquement vanilla.
  Les bytes résolus déjà identiques sont des no-op confirmés, mais le range CALL
  complet est flushé. Les bytes `0x5353F0+` appartiennent au PDATA/unwind de la
  fonction suivante et ne sont jamais utilisés comme cave.
- Les deux callers de l'owner player-save sont exhaustifs : `0x41360B` passe
  un buffer stack neuf de `0x4000`; la chaîne
  `0x41E138`/`0x41E186`/`0x41E1E9` alloue et passe `0x8000`. G3 reçoit toutefois
  seulement l'espace restant après un préfixe variable, donc ces capacités
  totales ne remplacent pas le guard du flag d'overflow. Avant publication, la
  façade devra réserver le relais pour la vie du processus avant le premier
  write codec. `CommitPreparedCodecPatchSet` exige maintenant un
  `NativePublicationQuiescenceLease` RAII opaque et vivant pendant chaque
  fingerprint, write et flush. Le SDK loader épinglé ne fournit aucun issuer
  de production et aucun caller loader n'existe. Une perte du lease avant toute
  écriture refuse sans mutation; après la première tentative, elle impose un
  cold restart sans hot rollback.
- Le préflight pur G2/G3 parcourt sans allocation le marker `0x6667`, chaque ID
  12 bits, les champs param/value gouvernés, le cap 512 et la sentinelle
  `0xFFF`; il refuse schéma dangereux, ID hors table, troncation et sentinelle
  absente sans modifier sa sortie. Les fonctions uniques `0x530A00` et
  `0x533760` restent intactes; leurs trois callers exhaustifs sont maintenant
  préparés vers des entries RX process-lifetime. Chaque FRAME ASM transmet
  l'ABI six arguments à un helper `noexcept`, qui accepte seulement v105,
  copie au plus 3 844 octets, tient le lock partagé du snapshot immuable durant
  préflight + appel natif + postcheck et renvoie le statut natif malformé
  `0x12` en cas de rejet. Les trois épilogues exacts uniques
  `0x530BCF`/`0x5338ED`/`0x533ABF` prouvent ce contrat. Un succès natif dont le
  cursor diffère du used-end prédit provoque un fail-fast. Ces rel32 restent
  non publiés.
- La preview alloue puis lit au plus `0x4000` octets (`0x61CF41` et
  `0x61CF81`); son seul gate immédiat exige seulement huit octets. Le thunk
  exact unique `0xA1B6C0` relie les quatre appels ID au cœur du bitreader; le
  core succès unique `0xA1BAD0` avance les cursors, tandis que `0xA1BA92` charge
  la longueur totale, détecte l'overrun et pose `[stream+0x20]=1`. Les boucles
  preview ne lisent pas ce drapeau. Le wrapper G4 préparé appelle l'owner
  partagé `0xA1E110` exactement une fois, après quoi il valide uniquement la
  copie terminée `[temp,temp+N)`: magic, version exacte 105, taille déclarée,
  checksum, contexte `<4`, marker `0x6667` exigé et validé par le wrapper, IDs sous le snapshot,
  champs bornés, cap 512 et sentinelle. Un rejet retourne zéro au gate natif,
  qui rejoint l'exit exact `0x61D87D`, libère `temp` et retourne false.
  G4 reste non publié, mais son contrat de bornes est maintenant fermé.
- Le bloc unique `0x61CF95` exige le magic vanilla `0xAA55AA55` après l'appel
  `0xA1E110`. La trace complète corrige une première lecture trop large :
  `0xA1E110` ne relit pas le filesystem, mais copie sous verrou depuis le buffer
  `SaveObject+0x08`, borné par `SaveObject+0x10`. Le seam G10 `0x9FC654`
  remplace ce même buffer par le payload intérieur accepté avant publication de
  l'état 3; ses callers transmettent ensuite le même SaveObject à la preview.
  G10 domine donc G4 et aucun second unwrap frontend n'est requis. Ce témoin
  verrouille plutôt l'ordre attendu : enveloppe validée/retirée, puis magic
  vanilla du payload intérieur. Les témoins supplémentaires `0xA1E194` et
  `0xA1E1C6` prouvent source/longueur/capacité puis unlock/retour de la copie;
  `0x61D43F`, `0x61D5E4`, `0x61D87D` et le corps complet unique
  `GetDataTablesForContext 0x300A90` ferment respectivement contexte, layout B,
  cleanup et domaine 0..3. Le lookup ItemStatCost peut retourner null et B le
  déréférence sans contrôle, d'où l'obligation du préflight ID<rowCount.

## 2026-08-30 — ISC12 G9 0x9C/0x9D static transport audit

- `D2GAME_SendItemAction9C 0x479CD0` et
  `D2GAME_SendItemAction9D 0x479EA0` passent tous deux `R9D=0` au serializer
  `0x375EE0`. Ils sérialisent donc un seul nœud, jamais tout l'arbre socketé.
  Les fenêtres uniques de 158/163 octets à `0x479D85` et `0x479F76` prouvent
  simultanément ce flag, la capacité `0xF4`, les headers de 8/13 octets, la
  comparaison diagnostique `0xFC`, la queue via `0x4817F0` puis l'appel du
  walker `0x481B50`. Le root `0x9C` est natif jusqu'à 244 octets; chaque `0x9D`
  est sûr jusqu'à 239 octets.
- La comparaison `>0xFC` ne rejette pas le paquet : elle appelle un helper de
  diagnostic puis rejoint le store de longueur et la queue. En `0x9D`, 240..242
  octets donnent des totaux 253..255, 243..244 wrapent la longueur sur un octet,
  et un overflow du serializer retourne zéro à `0x375F25`, produisant un paquet
  header-only de 13 octets. Le même retour produit huit octets en `0x9C`.
- La fenêtre unique de 102 octets `0x481BAD` prouve l'amorce `GetFirstItem`,
  l'appel `0x479EA0` à `0x481BFE`, `GetNextItem` et la branche arrière tant
  qu'un enfant demeure. Le budget est donc une séquence :
  root 9C/9D, puis un 9D indépendant par descendant. Tout guard correct doit
  préflight tout l'arbre avant le premier envoi; refuser seulement l'enfant
  laisserait un état partiellement publié.
- Pour la provenance historique seulement, les hooks d'entrée `0x12E2C0`,
  `0x12E490`, `0x374BF0`, `0x374FF0`, `0x375EE0` et `0x4817F0` appartiennent au
  prototype RuffnecKk ExtendedItemStats, ensuite exercé dans un build du fork
  expérimental `RuffDood/D2RL-Plugins:codex/pluginpack-foundation`. Ils ne sont
  pas présents dans l'upstream officiel eezstreet et ne constituent ni une
  dépendance ni un gate produit ISC12. Les six témoins G9 restent des fenêtres
  intérieures natives exactes ne revendiquant aucune de ces entrées. Le témoin
  9D commence à `0x12E4B0`; il prouve un buffer `0x101` et la longueur minimale
  13, pas un upper bound client `0xFC`.
- Le planner pur ISC12 calcule `ceil((F + 12*T)/8)` par nœud, où `T` compte un
  token par record et un token sentinelle `0xFFF` par liste, puis applique le cap
  du root selon son type : 244 octets pour `0x9C`, 239 pour `0x9D`, puis 239 à
  tous les descendants `0x9D`. Il valide maintenant un snapshot packed complet
  avant son premier callback : counts, offsets, indices, enfants partagés,
  cycles, nœuds inaccessibles et payload tardif hors budget produisent tous zéro
  callback. Le preorder depth-first `{root, premier enfant, ses descendants,
  sibling suivant}` reproduit le walker natif : le producer 9D queue à
  `0x47A001`, récursive via `0x47A014`, puis le walker parent avance à
  `0x481C06`.
- Le nombre de sockets disponibles et le nombre d'enfants occupés sont deux
  cardinalités distinctes. La référence sémantique épinglée
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`,
  `source/D2Common/src/Items/Items.cpp:6668-6694`, compte et écrit les enfants
  occupés, tandis que `:7211-7214` écrit séparément
  `STAT_ITEM_NUMSOCKETS`; `:3270-3282` compare encore capacité et occupation.
  Le planner impose donc `listed == occupied` et `occupied <= capacity`, jamais
  leur égalité systématique. La borne D2MOO de 3 bits/7 enfants reste seulement
  sémantique; la limite encodée exacte de D2R doit être promue avant de fixer la
  capacité d'une transaction native.
- Vincent retient le 30 août 2026 **G9-A native-only**. Le planner ferme le
  contrat logique hors runtime, mais aucun transport de production ne l'appelle
  encore et zéro callback ne prouve pas encore zéro queue D2R. Aucun transport
  externe ne fait partie du produit. Aucun runtime ni save n'a été lancé pendant
  cet audit.
- Un preflight par seconde sérialisation au hook d'entrée est rejeté. `0x9C`
  sauvegarde puis modifie temporairement les flags `ItemData+0x18` à
  `0x479D54/0x479D67` avant la sérialisation, puis les restaure à
  `0x479DEC/0x479DFD`. `0x9D` applique le même traitement à `0x479F23..0x479F3B`
  et peut aussi modifier temporairement `ItemData+0x54` à `0x479F4A..0x479F5E`.
  Une sérialisation scratch exécutée à l'entrée ne voit donc pas nécessairement
  les mêmes octets que le producer réel. Le wrapper `0x375EE0` reçoit sept
  arguments : item, destination, capacité, `bServer`, `bSaveItemInv`,
  `bGamble`, callback/sink; le layout et l'idempotence d'un sink recréé ne sont
  pas gouvernés non plus.
- Au moment de l'audit statique initial, le seam natif étroit recommandé n'était
  pas encore implanté. Il conservait une seule sérialisation native et devait
  détourner seulement les deux CALL de queue :
  `0x479E10 -> 0x4817F0` pour `0x9C` et `0x47A001 -> 0x4817F0` pour `0x9D`.
  Des wrappers aux entrées `0x479CD0/0x479EA0` ouvriraient une transaction TLS;
  les deux relays copieraient synchroniquement chaque paquet stack sans
  l'envoyer, puis le wrapper root publierait la séquence via l'entrée queue
  intacte seulement après validation globale. Les signatures d'entrée uniques,
  les CALL rel32 exacts et les séquences prep/restore ont été relevés. Restent à
  fermer avant implantation : bornes de profondeur/nœuds, réentrance TLS,
  cardinalité sémantique StatLists/sentinelles/compound stats, stabilité de
  l'arbre pendant la traversée, durée de vie des relays et publication
  quiescente des quatre sites. Une panne de queue pendant le flush final ne peut
  pas être rollbackée; la garantie visée demeure exactement zéro queue pour un
  rejet de validation. `D2GAME_QueueServerPacket` reçoit une longueur `size_t`
  dans `R8`; la fenêtre unique de 39 octets `0x4818B6` prouve qu'elle consomme
  synchroniquement la plage `[packet, packet+length]`, donc le relay doit copier
  le paquet stack avant son retour. Le flush tardif conserve l'ordre des appels,
  mais décale les effets internes de queue après la sérialisation de tous les
  descendants; cette différence de timing/backpressure reste à qualifier.

## 2026-08-30 — ISC12 G9-A native wrapper/relay ABI promotion

- Le workbench gouverné commun aux builds 92777 et 93847 est vérifié avant cette
  promotion. `D2GAME_SendItemAction9C 0x479CD0` possède l'ABI exacte
  `void(client RCX, item RDX, action R8B, temporaryFlags R9D,
  gamble:uint32 stack arg5)`. Ses onze callers directs sont `0x4790DD`,
  `0x47977D`, `0x479B3C`, `0x479BA0`, `0x47D5A1`, `0x47D60D`, `0x47E8FB`,
  `0x47EEF5`, `0x480022`, `0x48007D` et `0x4802D2`; aucun ne consomme `RAX`.
  `D2GAME_SendItemAction9D 0x479EA0` possède l'ABI
  `void(client RCX, parent RDX, item R8, action R9B,
  temporaryFlags:uint32 stack arg5, gamble:uint32 stack arg6)`. Ses douze
  callers directs sont `0x477574`, `0x478804`, `0x4797C8`, `0x479814`,
  `0x479848`, `0x47E934`, `0x47E99C`, `0x480300`, `0x480388`, `0x4808B4`,
  `0x481514` et le walker `0x481BFE`; eux aussi ignorent tous `RAX`.
- Les deux entrées commencent par les cinq octets complets
  `40 53 55 56 57`, soit `push rbx/rbp/rsi/rdi`. Les continuations
  `0x479CD5` et `0x479EA5` commencent respectivement par `push r14` et
  `push r15`. Le producer 9C possède ensuite une allocation `0x160`, un delta
  de frame total `0x188`, son cookie à `RSP+0x150` et son arg5 entrant à
  `RSP+0x1B0`; 9D possède `0x170`, `0x198`, cookie `RSP+0x160` et args5/6 à
  `RSP+0x1C0/0x1C8`. Un `JMP rel32` cinq octets est donc instruction-aligned :
  le trampoline rejoue exactement les quatre pushes puis rejoint `entry+5`.
  Les signatures complètes uniques de 32 octets restent les témoins de
  préflight, car les cinq octets volés seuls sont identiques entre les entrées.
- Le trampoline dix octets `40 53 55 56 57 E9 <rel32 entry+5>` exige une table
  d'unwind process-lifetime. Son `UNWIND_INFO` minimal exact est
  `01 05 04 00 05 70 04 60 03 50 02 30` : version 1, prologue cinq octets et
  quatre `UWOP_PUSH_NONVOL` aux offsets 5/4/3/2 pour RDI/RSI/RBP/RBX. Les
  `UnwindData` du PDATA réhydraté statique ne décodent pas de manière fiable;
  ils ne prouvent donc pas l'unwind natif. Avant publication, le runtime doit
  obtenir `RtlLookupFunctionEntry(entry+5)`, valider les pushes/allocations live,
  enregistrer le trampoline par `RtlAddFunctionTable` et garder code, table et
  unwind jusqu'à la fin du processus. Un wrapper C++ doit nettoyer le TLS sous
  SEH `finally` ou équivalent; la seule RAII `/EHsc` ne ferme pas un SEH natif.
- Les CALLs mutables sont exactement `0x479E10: E8 DB 79 00 00`, continuation
  `0x479E15`, et `0x47A001: E8 EA 77 00 00`, continuation `0x47A006`. Ils
  transmettent tous deux `void(client RCX, packetStack RDX,
  zeroExtendedByteLength:size_t R8)`. Aucun xref ne cible les CALLs, leurs
  continuations ou les intérieurs `0x479D85/0x479F76`; les 23 appels directs
  recensés passent par les deux entrées wrappers. Un relay TLS-inactif après
  publication est donc une invariant violation fail-closed : il supprime
  l'envoi et arme l'état fatal au lieu de contourner le preflight.
- Les anciens témoins contenant les CALLs sont désormais scindés et chacun est
  unique : `0x479D85+139`, CALL `0x479E10+5`, continuation walker
  `0x479E15+14`, puis témoin d'épilogue code `0x479E23+30` jusqu'au `RET`
  inclus/end-exclusive `0x479E41`; ensuite `0x479F76+139`, CALL
  `0x47A001+5`, continuation walker `0x47A006+19`, puis témoin d'épilogue code
  `0x47A019+30` jusqu'au `RET` inclus/end-exclusive `0x47A037`. Les témoins
  d'épilogue prouvent seulement les bytes de load/xor/check du stack cookie,
  deallocation, pops et `RET`; le `.pdata` statique n'est pas autoritatif pour
  les futurs wrappers d'entrée. Leur contrat unwind demeure un gate runtime
  par `RtlLookupFunctionEntry`. Aucune fenêtre déclarée inchangée ne chevauche
  donc une mutation. L'entrée queue intacte
  `0x4817F0+22` fixe l'ABI `(client, bytes, size_t)->void`; le témoin unique
  `0x4818B6+39` construit `[bytes,bytes+length]` avant l'appel du sink. Chaque
  relay doit copier le paquet stack synchroniquement avant son retour, puis le
  wrapper root détache une batch immuable et la flush directement via
  `0x4817F0` seulement après validation complète. Le retour `void` ne permet
  aucun rollback si une panne arrive après le début de ce flush.
- La topologie imbriquée est synchrone et même-thread. Les deux producers
  appellent `0x481B50` après leur relay; le walker appelle chaque enfant à
  `0x481BFE` comme `9D(même client, item parent actif, enfant, action 0x12,
  temporaryFlags propagés, gamble 0)`. Une frame TLS nested peut donc exiger
  exactement ces invariants, un seul relay du bon opcode par producer et aucune
  9C nested. Une 9D entrée depuis l'état Idle demeure une root légitime. Pendant
  un rejet, le corps et le walker natifs terminent afin de restaurer leur état,
  mais chaque relay reste supprimé; succès et réentrée de flush nécessitent des
  banks distinctes afin qu'une batch en cours ne puisse pas être écrasée.
- Les quatre hooks, relays, trampolines et témoins sont maintenant gouvernés
  comme **source préparée**, jamais comme runtime qualifié ou publié. Restent
  bloquants : autorité loader de quiescence, unwind live, nettoyage SEH,
  reentrance pendant le flush, validation des octets capturés et des paquets
  header-only 8/13, stabilité inter-thread de l'arbre, effets de queue différés,
  backpressure et preuve zéro queue pour chaque rejet root/middle/sibling/deep.
  Aucun runtime, paquet ou sauvegarde n'a été exécuté pendant cette promotion.

## 2026-08-30 — ISC12 G1 bounded complete-item serializer and G9 cardinalities

- Le corps natif complet de sérialisation d'un item commence à `0x37D140`.
  Son prologue exact unique de 37 octets, jusqu'à `sub rsp,rax`, fixe `RBP` à
  `final RSP+0x100`, sonde
  puis alloue une frame de `0x1970` octets. L'outer serializer ne possède qu'un
  CALL direct vers ce corps, à `0x3800F0`, couvert par la fenêtre unique
  `0x3800E8`. Ces témoins appartiennent au corpus gouverné 92777 commun aux
  builds ciblés 3.2.92777 et 3.3.93847 par équivalence native byte-exacte; aucun
  RVA ni ABI D2MOO n'est transposé.
- À `0x37F08A`, un `memset` exact unique initialise seulement `0x7FC` octets à
  `[RBP+0x60]`: **511 DWORDs**, donc les indices directs `0..510`. Le snapshot
  suivant commence à `[RBP+0x860]`; sa fenêtre unique `0x37F09B` passe une
  capacité `0x1FF` au helper `0x2F64E0`. Le corps unique `0x2F6527` prouve
  `min(listCount,capacity)` puis la copie de records de huit octets comme deux
  DWORDs. Le DWORD `[RBP+0x85C]` entre les deux régions n'est pas initialisé.
- La fenêtre unique `0x37F0D0` charge un record à
  `[RBP+index*8+0x860]`, effectue `sar eax,16; movzx edi,ax`, puis vérifie
  directement `EDI < DataTables.ItemStatCostRowCount`. L'index de la table de
  suppression est donc le stat ID uint16 lui-même, sans translation ordinale.
  Dès que la sentinelle ISC12 devient 4095, l'ID valide 511 lit le DWORD hors
  table à `+0x85C`; l'ID 512 alias le premier DWORD du snapshot, et les IDs
  suivants avancent dans la frame. Le plan G1 historique limité aux
  width/sentinel ne peut donc pas être publié tel quel.
- Un audit exhaustif des opérandes mémoire basés sur `RBP` dans le corps
  `0x37D140` trouve exactement dix formations/accès à la région
  `[RBP+0x60,RBP+0x860)`: le pointeur du `memset`, une lecture indexée dynamique
  à `0x37F17C` et huit writes fixes de partenaires composés. Les couples natifs
  sont `17→18`, `48→49`, `50→51`, `52→53`, `54→55,56` et `57→58,59`; leurs
  fenêtres exactes uniques sont promues dans `isc12/native-sites.json`. Chaque
  primaire écrit un token ID et plusieurs valeurs, puis la comparaison supprime
  le ou les records partenaires. Remplacer globalement la comparaison par
  `test esi,esi` casserait donc les composés vanilla.
- Le site gouverné complet est la fenêtre exacte unique de **50 octets**
  `[0x37F174,0x37F1A6)`. Ses 42 premiers octets finissent après
  `cmp eax,edx` et constituent un témoin prerequisite/body, pas à eux seuls le
  remplacement complet. Les mutations sont confinées à
  `[0x37F17C,0x37F1A1)`; le `test esi,esi` et sa branche à `0x37F174`, ainsi que
  le CALL natif du bit writer à `0x37F1A1`, restent inchangés. Le relay borné
  doit conserver la comparaison originale pour `statId<511` et rejoindre le
  writer directement pour `511..4094`, puisque la valeur a déjà été prouvée
  non nulle et tous les partenaires composés hard-codés sont sous 511. Sa
  publication doit fingerprint simultanément owner/frame, caller, table,
  snapshot/helper, extraction directe et les huit writes composés.
- Dans le même corps complet, la séquence commençant à `0x37D60B` énumère tous
  les enfants immédiats de l'inventaire de l'item, encode
  `min(occupiedChildren,7)` sur exactement trois bits à
  `0x37D66F..0x37D678`, et écrit donc 7 pour toute occupation supérieure. Ce
  champ demeure présent lorsque les producers `0x9C/0x9D` désactivent la
  récursion inline. `STAT_ITEM_NUMSOCKETS` (`0xC2`) est écrit séparément plus
  loin avec les `SaveBits` de sa ligne ItemStatCost; le natif n'impose pas leur
  égalité. Le preflight doit exiger `occupied<=7` et `occupied<=capacity`.
- La boucle des stat lists admet au maximum la base, cinq slots set et une
  runeword, soit sept listes/sentinelles structurelles. Chaque liste copie au
  plus 511 records; au plus 511 tokens ID peuvent survivre par liste, mais les
  `SaveBits`, valeurs nulles, composés et le skip numérique 326 réduisent le
  nombre réel. Le plafond structurel est donc 3577 tokens ID et sept
  sentinelles, sans preuve qu'un item réel puisse atteindre ce maximum. G9 doit
  valider les octets effectivement capturés plutôt que dériver la taille depuis
  le nombre brut de records.
- Le walker `0x481B50` énumère chaque enfant, appelle le producer 9D à
  `0x481BFE`, puis le chemin 9D rappelle le walker à `0x47A014`. Aucun compteur
  natif de profondeur, de nœuds totaux ou de cycle n'a été trouvé. La borne
  trois bits est locale à un nœud et ne borne pas l'arbre; ISC12 doit définir
  ses propres limites de profondeur/nœuds et refuser les cycles. Toutes ces
  conclusions sont des preuves statiques; cold start, round-trip, paquets et
  sauvegardes réelles restent **NOT RUN**.

## 2026-08-31 — ISC12 G0-BBE width census and publication-authority audit

- Le corpus commun gouverné est vérifié avant l'audit : image canonique
  `CC59119D…A914715`, image d'analyse `673E8C0B…0E63AB`, index 105 850
  fonctions / 1 057 329 références et self-test PASS. Les preuves `.text`
  ci-dessous sont byte-identiques pour les builds couverts 92777/93847; aucun
  RVA ou ABI D2MOO n'est transposé.
- Le census exhaustif du compilateur générique `0xA24290` trouve sept CALLs
  natifs : `0x3B46F9`, `0x3B483D`, `0x3B4A4D`, `0x3B4CCD`, `0x3B4EFD`,
  `0x3B4FBD` et `0x3B533D`. Tous utilisent le mapper universel `0x3B58A0`.
  Trois frontends seulement — `0x3B54E0`, `0x3B55A0`, `0x3B6220` — appellent
  le même resolver core `0x3B5AA0`.
- Le mapping de domaines natif distingue les pools fixes Skills (`+0x138`),
  SkillDesc (`+0x150`), Missiles (`+0x188`), Items combinés (`+0x228`),
  Properties val1..val7 (`+0x12C0`) et le pool condition-calc partagé
  Items/SetItems/UniqueItems/TCEx (`+0xD90`), plus un bridge générique
  dynamique. Le témoin `0x3B5307/0x3B533D` appartient à ce dernier pool
  condition-calc; il ne doit donc jamais être étiqueté comme un scope Skill.
- Le selector exact unique `0x3B5B58` accepte les slots 3..8 et consulte les
  six DWORDs exacts à `0x3B61B0`. Le slot numérique 5 rejoint ainsi
  `0x3B5D80`, seule branche du core qui charge
  `DataTables.ItemStatCostLinker` à `+0x1270`. Cette branche appelle le lookup
  `0xA121F0`, puis écrit l'ordinal retourné par EAX comme DWORD dans `[RBX]`.
  Aucun masque `0x1FF`, shift neuf bits ou store étroit n'existe sur ce trajet.
- Le compilateur générique teste d'abord la plage signée -128..127 à
  `0xA245FC`; au-delà, `0xA24661` teste -32768..32767 et émet le token 5 suivi
  de `word [RSI+1]=BX`. Chaque ID ISC12 valide `512..4094` est donc encodé
  exactement sur 16 bits. Le decoder exact unique `0xA235D5` borne deux octets,
  fait `movsx EDX,word [R12]`, appelle le callback et avance de deux octets.
  Comme `4094 < 0x8000`, le signe ne change aucune valeur ISC12 valide.
- Le census du generic evaluator `0xA234B0` trouve huit CALLs natifs. Sept
  contextes D2R câblés à une table —
  `0x3B460B`, `0x3B49AC`, `0x3B4C1F`, `0x3B4E51`, `0x3B5136`, `0x3B5296`
  et `0x3B54BB` — passent la table `0x1D09C50` et le count 9; le huitième
  appel générique `0xA24A1A` passe une table et un count nuls. Le callback
  `0x3B33F0..0x3B34F9` copie l'ID EDX vers EDI, vérifie le row count
  `DataTables+0x1260`, puis retransmet le DWORD complet à
  `STATLIST_UnitGetStatValue`, `STATLIST_GetUnitBaseStat` ou
  `STATLIST_GetUnitStat`, sans troncature.
- Une attestation gouvernée ferme les deux arêtes protégées le 31 août 2026 à
  `2026-08-31T12:44:45.5847343Z`. Elle lit le processus D2RLoader PID `39116`,
  créé le 30 août à `17:12:21-04:00`, déjà ouvert par
  `D2RLoader.exe -txt -mod BKVince -txt -offline` sur le runtime officiel D2R
  `3.3.93847`, base `0x140000000`, avec seulement
  `PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ` (`0x1010`). Les hashes
  sur disque sont `.build.info`
  `2EBCAD0521DBF038D5A7FE5395E96B4BEF6D4F0774F7B1F840E03C3DE9CB067A`,
  `D2R.exe`
  `E1F5436E3D9687F644EF16938B1B183D1FDEF434F18CF66D852CF68F48CC8936`,
  `D2RLoader.exe`
  `A926DAA85DE85EADCF98FDB5FB30143CC32D6B4913EB21A6416EBDAA78945128`
  et `D2RCore.dll`
  `013B047612BFF0EB564891037508FD43D03AFDFB20BFEB9B5BC683B36559FFC6`.
  Aucune permission de mutation n'est demandée et aucune écriture n'est
  tentée.
- Avant de croire les RDATA, la capture confirme byte-exact les deux ancres
  `.text`: `0x3B59CA = 488D15BB4C9401` et
  `0x3B54A3 = 488D05A6479501`. Les 16 octets protégés à `0x1CFA68C` valent
  `73746174000000000000000073657466`, soit la chaîne NUL-terminée `stat`; leur
  SHA-256 est
  `83626D991B1A0AD789DDB50D993623989AB1C1257879530005CEE0CA411F0E4F`.
- La capture corrige le modèle statique : `0x1D09C50` n'est pas une suite de
  neuf QWORDs, mais neuf records de 16 octets
  `{callback VA:qword, arity:dword, padding:dword}`. Les callbacks/arity sont
  `3B3200/2`, `3B3210/2`, `3B3220/2`, `3B32F0/2`, `3B33A0/2`,
  `3B33F0/2`, `3B3500/3`, `3B3590/2`, `3B3640/FFFFFFFF`; tous les paddings
  sont zéro. Le record 5 à `0x1D09CA0` pointe donc exactement vers le callback
  full-width `0x3B33F0`. Les 144 octets bruts capturés sont
  `00323B4001000000020000000000000010323B4001000000020000000000000020323B40010000000200000000000000F0323B40010000000200000000000000A0333B40010000000200000000000000F0333B4001000000020000000000000000353B4001000000030000000000000090353B4001000000020000000000000040363B4001000000FFFFFFFF00000000`,
  SHA-256 session/base `B551BA77BCF256B537D3CDB750C0B0246C0C05DA45675AF74EBEFB98522A1D1A`.
  Après remplacement des neuf VA par leurs RVA, le SHA-256 portable est
  `E8B9B76F9D7320BDFA8129F8D22B0CB19B450AC4D655F53A8D2E56E47CF224ED`.
  D2MOO demeure seulement une corroboration sémantique; la fermeture repose
  maintenant sur les données natives D2R 3.3.
- Le ledger promeut dix surfaces G0-BBE proof-only, toutes avec
  `targetValue = unchanged`; son validateur interdit désormais toute mutation
  dans ce groupe. Les signatures `.text` sont exactes et uniques et les deux
  memberships protégés passent de `identified` à `ready`. G0-BBE est fermé
  pour la largeur et l'appartenance natives. Ce lot ne crée aucun patch BBE,
  ne publie aucun octet et n'exécute aucun cold start ISC12; seuls des reads
  externes ont attesté l'instance déjà ouverte.
- L'audit séparé du PluginSDK v3 épinglé
  `4933e2c42cb2592958cd0df3b6dc5003102252d1` confirme que les services publics
  s'arrêtent à l'ID 15 et n'exposent ni quiescence, ni transaction de patch, ni
  exclusion de tous les consumers D2R. `D2RLoaderLoadPlugin` est une fenêtre
  d'installation plausible, pas une garantie documentée. Émettre le lease
  ISC12 depuis ce callback serait donc une hypothèse et demeure interdit.
- Le minimum honnête est une transaction synchrone loader-owned, additive au
  SDK, qui exécute un callback non évadable pendant une phase startup réellement
  quiescente, sérialise les publishers, lie le token au thread/owner/epoch et
  empêche toute reprise sur résultat `Poisoned`. ISC12 devra coordonner sous
  cette seule transaction le preflight global G0 + G10 + codec, la réservation
  process-lifetime, les commits dans cet ordre — codec conservant
  G9/G2/G4/G1/G3 — puis publier readiness et `operational` en dernier. Un mutex
  d'installateurs, une suspension de
  threads implantée par le plugin ou un token sans garantie loader ne satisfait
  pas ce contrat.
- Le refus actuel `enabled=true` avant mutex, préparation et écriture reste
  donc le seul comportement production démontré. Même avec un futur issuer,
  les publishers locaux actuels ne doivent pas être appelés séquentiellement :
  `InstallLoaderExtension` ne publie que G0, le commit codec n'a aucun caller et
  G10 n'est pas installé. Le runtime de publication reste bloqué jusqu'à
  l'autorité loader, aux adapters full-set et à l'attestation live unwind;
  G0-BBE n'est plus un blocker de membership.
- Le coordinateur full-set production-neutral est maintenant implanté et
  compilé sans caller production. Sa machine one-shot preflight G0, G10 et
  codec avant toute réservation/écriture, réserve la durée de vie processus une
  fois, commit les trois domaines dans cet ordre, garde l'ordre interne codec
  G9/G2/G4/G1/G3 et publie la readiness seulement à la fin. Un résultat
  incertain ou post-write appelle exactement une fois l'état poison, devient
  terminal et n'essaie aucun rollback. Les tests exécutables couvrent lease
  absente/révoquée, chacun des trois rejets de preflight, réentrance,
  mutate-then-uncertain, monotonie terminale et move/release unique.
- Le build gouverné Release `/W4 /WX` passe CTest `4/4`; la DLL a le SHA-256
  `FF8D16AF4A6DBCB9BD3AD86A6A6DFCBB4553D26A200DF161B1065C6A5DFE5286`.
  Le coordinateur n'est encore qu'un contrat d'orchestration : G0, G10 et
  codec doivent être séparés en adapters preflight/commit immuables, puis liés
  sans caller production. La proposition SDK gouvernée remplace le faux modèle
  de rollback atomique par un service `NativePublication` optionnel,
  synchrone, loader-owned, owner/thread/epoch-bound et no-resume sur
  `Poisoned`. Le véritable issuer et sa barrière consommateurs restent
  impossibles à implanter sans source D2RLoader/Core upstream.

## 2026-08-31 — ISC12 et les providers D2RCore 1.1/1.2

- Le ZIP officiel `D2RLoader-1.2.0-beta.zip` vaut
  `2AABEF2E6838CA3611EA3CB74D318C3BB792549CC4FC6C7D53933245667417D9`.
  Le runtime installé est byte-identique au ZIP pour `D2RLoader.exe`
  (`651FA9EB33083088349224B1624819F63ED79596F808950CF6468B5D82F7132E`)
  et `D2RCore.dll`
  (`876957AE7AEF627BAC3E56592CA15888A8AAF9B952ED79EDCC1CF6351B3F93CF`).
- D2RLoader compose trois surfaces déjà gouvernées sans en changer l'ABI.
  `D2R+0x31EC89` rejoint `D2RCore!LoadExcelTable`; G1 à
  `D2R+0x37F1A1` rejoint `D2RCore!WriteItemSaveStatId`; G3 à
  `D2R+0x535303` rejoint `D2RCore!WritePlayerSaveStatId`. Les relais proches
  sont des `FF 25` vers des slots vivants et chaque provider retransmet les
  arguments au propriétaire natif exact. ISC12 conserve ces CALLs et ne mute
  que ses propres largeurs/corps gouvernés.
- Les deux providers de stat ID 1.2 ont des corps exacts
  `[0x6364C0,0x63652B)` et `[0x636550,0x6365BC)`, des tuples PDATA/unwind
  exacts et retransmettent `RCX/EDX/R8` une fois à
  `D2R+0xA1B710`. Leur bitmap TLS privé ne suit que `EDX < 0x200`; un ID
  supérieur n'est ni tronqué ni rejeté, il manque seulement au census de
  compatibilité D2RLoader. L'enveloppe et le hash de schéma ISC12 restent
  l'autorité de persistance locale; la couverture metadata/handshake des IDs
  élevés demeure un gate réseau distinct et ne justifie pas d'étendre ce
  bitmap privé.
- Le second caller player-save forme un autre contrat indivisible. Vanilla
  charge `R13D=0x8000` à `0x41E138`, alloue/zéroise ce nombre d'octets et
  appelle directement `D2R+0x52F090` à `0x41E207`. D2RLoader 1.2 change
  seulement la capacité en `0xFFFF`, puis redirige le même CALL via
  `D2R+0x3E2A4AC` et son slot `0x3E29D00` vers
  `D2RCore!WritePlayerSaveWithEnvironmentCapture` à `0x634650`. Le provider
  1.2 possède PDATA `[0x634650,0x636068)`, unwind `0x50EFD0`, corps SHA-256
  `A4A0E2A5E70AEFB613016739E914225CEE2A20BB06F13197CEAC182E11648667`
  et slot `D2RCore+0x5372C0` résolu exactement vers `D2R+0x52F090`. Le
  provider 1.1 équivalent possède PDATA `[0x563D80,0x565103)`, unwind
  `0x452480`, corps SHA-256
  `66C61BC1678375C9E373FD2141F409244B450D1411906B7FF8C27C1241E69F6A`
  et slot `D2RCore+0x480DE8`. Chaque corps réémet `RCX/RDX/R8/R9` et les deux
  arguments stack avant un unique appel natif.
- L'admission ISC12 reste fail-closed : seul le couple exact
  `0x8000 + direct 0x52F090` ou le couple exact
  `0xFFFF + provider D2RCore attesté` est accepté. Le second exige l'export
  RVA exact, le hash du corps complet, PDATA, les 36 octets unwind, une chaîne
  bornée de sauts inconditionnels, le slot vivant et l'entrée exacte du
  serializer natif. Aucun masque générique ni réécriture du provider n'a été
  ajouté.
- Un cold start full-stack du candidat précédent de 422 912 octets
  (`F87546F8D2A940D419464EDC444E08DD1AF1E45D1721C8FBBF2825214E09A0F5`)
  a vérifié `LoadExcelTable` puis `WritePlayerSaveStatId`, avant de refuser
  proprement le couple dynamique encore inconnu à `0x41E138`, sans mutation
  ni readiness ISC12. Après l'attestation ci-dessus, deux builds Release
  reproductibles `/W4 /WX` passent CTest `5/5`; le nouveau candidat de
  425 472 octets vaut
  `4B928E117B930BCA2B9B0F8E3D2CDE8FF10F1C1F4FE2923ECCA5B1BCF6BD69AD`.
  Son prochain cold start attend seulement que la session BKVince humaine
  active libère le runtime; aucune sauvegarde ni action gameplay ISC12 n'a
  encore été exécutée.

## 2026-08-31 — MapSense 0.13.21 : frontière statique des noms distants

- La cause vérifiée des libellés 0.13.20 décalés était interne à MapSense :
  le collecteur fabriquait un point au centre de l'enveloppe des `DrlgRoom`
  de chaque `Level`, sans preuve native d'une sortie ou d'un waypoint. Ce
  producteur générique est supprimé; un nom distant ne peut plus être publié
  qu'avec un `RoomTile` ou un `PresetUnit` exact.
- `DRLGROOM_CreateActiveRoom 0x3289A0` sépare trois préconditions statiques
  et la création complète. Son témoin strict à `0x3289B3` teste le bit 24
  avant `DRLGROOM_LoadTileLibraries 0x3F3970`, le bit 25 et le type 2 avant
  `DRLGPRESET_AddPresetUnitDescriptors 0x3DE0E0`, puis le bit 20 avant
  `DRLGROOM_BuildRoomAndActiveRoom 0x328FD0`.
- Le corps strict de `0x328FD0` prouve la frontière recherchée : il assure
  d'abord les liens statiques par `0x3608A0`, appelle
  `DRLGROOM_InitializeStaticRoomGrids 0x3F38D0`, puis
  `DRLGROOM_AddStaticMapTiles 0x3F3930`, et seulement après appelle
  l'allocateur d'`ActiveRoom 0x326480`. MapSense 0.13.21 résout et appelle les
  quatre phases statiques nécessaires, mais ne résout jamais `0x326480` dans
  le nouveau chemin de capture.
- `DRLGROOM_AddStaticMapTiles` pose le bit `HAS_ROOM` 20 à `DrlgRoom+0x50`.
  Après copie bornée des descripteurs exacts, MapSense appelle
  `DRLGROOM_ReleaseRoomData 0x3F3AA0(room, 0)` seulement pour les rooms dont
  il possède le lease statique, seulement si `DrlgRoom+0x58` est toujours
  nul. Le témoin strict à `0x3F3ADC` prouve l'effacement de `+0x58` puis le
  test et l'effacement du bit 20.
- Le sélecteur est borné aux rooms dont les flags `+0x50` portent les bits
  de warp `0x00000FF0` ou de waypoint `0x00030000` et dont le descripteur
  requis manque. Une room déjà active ou déjà marquée `HAS_ROOM` n'est ni
  reprise ni nettoyée. Le scheduler existant reste progressif, un `Level` par
  callback UI; aucun travail en masse n'est réintroduit au clic ou au
  changement d'acte.
- D2MOO reste une référence sémantique pour la distinction entre
  descripteurs statiques `RoomTile`/`PresetUnit` et unités actives; tous les
  RVA, signatures, flags, offsets et ABI x64 ci-dessus proviennent du corpus
  gouverné commun 92777/93847. La build Release `/W4 /WX` et CTest `1/1`
  passent avant qualification runtime; la preuve gameplay et FPS reste à
  produire.

## 2026-08-31 — MapSense 0.13.22 : témoin seed et atlas externe fail-closed

- Le chemin statique 0.13.21 restait un producteur de données D2R éloignées et
  conservait donc un coût gameplay observable. Le candidat 0.13.22 ne démarre
  plus ce scheduler : le clic Reveal Act/All ne matérialise aucune room
  distante, aucun `ActiveRoom`, aucune collision, unité ou table de monstre.
- Le témoin strict de 129 octets à `D2R+0x326E89` prouve la relation native du
  seed. Le constructeur initialise `{mapSeed, 0x29A}`, calcule
  `mapSeed * 0x6AC690C5 + 0x29A`, stocke le seed original à `Drlg+0x840` et le
  mot bas du résultat à `Drlg+0x860`. MapSense lit les deux et refuse la
  requête si cette relation ne correspond pas; les constantes sont couvertes
  par trois vecteurs de test, dont le seed `0x12345678` de la preuve libd2.
- Un worker privé lance `RuffnecKkMapSenseMapgen.exe` caché, hors des threads
  gameplay/UI/render. Le protocole texte versionné transporte uniquement les
  niveaux, sorties, waypoints exacts et rectangles de rooms. Avant toute
  publication, MapSense exige une égalité exacte entre l'origine et tous les
  rectangles du niveau courant déjà généré par D2R et ceux produits pour le
  même seed, la même difficulté et le même acte.
- Les réponses tardives sont rejetées par génération de session et numéro de
  requête. Le graphe publié est borné à la composante connectée du niveau
  courant, ce qui exclut les branches spéciales déconnectées. Toute absence,
  erreur, divergence géométrique, dépassement ou timeout échoue fermé et
  laisse le Reveal natif actif sans réactiver l'ancien chargement distant.
- Le générateur prototype repose sur le commit libd2
  `ac4d735e57fcab6a3c356f810bb256da95a93716`. Un benchmark Release antérieur
  a généré les cinq actes en environ 45–48 ms et l'acte III en environ 6–7 ms;
  cette mesure est celle du processus externe et ne constitue pas encore une
  qualification FPS en jeu. Le build MapSense Release `/W4 /WX` et CTest
  `1/1` passent; la preuve live doit encore confirmer le seed, les coordonnées,
  l'exhaustivité des noms et l'absence de chute FPS.
- La qualification live sur le runtime officiel D2R 3.3.93847 confirme le
  témoin exact pour le seed `1395822899`, difficulté 2, acte 2, niveau 75 : le
  helper publie 28 niveaux connectés, 60 sorties canoniques, 9 waypoints et le
  témoin byte-exact des 48 rectangles courants en 22–34 ms. Aucun échec de
  seed, de protocole ou de géométrie n'est observé.
- Cette même preuve invalide toutefois la publication label-only par le
  projecteur natif local. Avec un clip natif `3840x2160`, le catalogue complet
  `60/9/28` n'admet que `1/1/0` sortie/waypoint/fallback dans le viewport de
  Kurast Docks; un déplacement manuel de l'automap supérieur à 1 000 pixels ne
  rend aucun autre waypoint distant visible. Le projecteur D2R fonctionne
  donc comme un projecteur du viewport local, pas comme un atlas d'acte.
- Le chemin 0.13.22 arrête correctement la boucle de replay illimitée observée
  auparavant, mais le clic `revealmap` natif coûte encore 937–953 ms et une
  passe de réconciliation déjà en vol ajoute 828–829 ms. Avant toute
  productisation, cette passe redondante doit être supprimée et l'atlas
  externe doit posséder sa propre projection/présentation gouvernée; republier
  davantage de points dans le projecteur local ne peut pas satisfaire
  l'exhaustivité visible.

## 2026-09-01 — MapSense : spike des cellules automap natives

- Le callback automap standard `0xD2240` reçoit une `ActiveRoom*`, résout le
  LevelId par `0x2EFC10`, lit le record `Levels` par
  `DATATBLS_GetLevelDefRecord 0x32C200`, prend son `Layer` à `record+0x08`,
  obtient le propriétaire avec `AUTOMAP_GetOrCreateLayer 0xD5360`, puis appelle
  `AUTOMAP_RevealActiveRoom 0xD6550`. Le champ `Levels.Layer` est donc bien
  l'identité de sauvegarde automap du niveau, et non l'index de sprite DC6
  transporté par le prototype libd2.
- Un propriétaire de layer mesure `0xB0` octets. `0xD5360` le place dans la
  chaîne globale issue de `D2R+0x2A2CF60`, conserve le pointeur courant à
  `D2R+0x2A2CF68` et initialise quatre arbres natifs à `+0x08`, `+0x30`,
  `+0x58` et `+0x80`. `0xD6550` envoie respectivement les tiles floor et wall
  vers les deux premiers arbres par `AUTOMAP_BuildTileCell 0xD5160`; les
  objets admissibles rejoignent le troisième par `0xD52B0`. La nomenclature
  floor/wall/object/extra de D2MOO 1.10f confirme seulement la sémantique; les
  tailles, offsets et ABI ci-dessus proviennent exclusivement du x64 gouverné.
- La valeur copiée dans chaque nœud est une clé exacte de 12 octets :
  `{uint16_t zero, int16_t frame, int32_t x, int32_t y}`. La conversion native
  `COORD_ConvertGameTileToAutomap 0x334EF0`, suivie de la division par dix de
  `0xD5160`, donne `x=(tileX-tileY)*8` et `y=(tileX+tileY)*4`; une orientation
  d'au moins `0x10` ajoute ensuite `24` à Y. Cette élévation est indépendante
  du choix d'arbre : `0xD6550` choisit `owner+0x08` ou `owner+0x30` selon que
  la tile provient du tableau floor ou wall. MSA1 v2 transporte donc séparément
  `wallTree` et `raised`; aucune orientation ne peut remplacer la provenance.
- `AUTOMAP_InsertCell 0xD1460` recherche d'abord la clé par `0xD4B70`. Une clé
  existante ne provoque aucune allocation; sinon le jeu alloue un nœud de
  `0x30` octets avec son allocateur, copie les 12 octets à `node+0x20`, met à
  jour le compteur `tree+0x20` et équilibre l'arbre par `0xA8EF0`. Les strictes
  signatures d'entrée de `0xD1460`, `0xD4B70`, `0xD5160`, `0xD5360`,
  `0xD6550`, `0x32C200` et `0x334EF0` sont chacune uniques dans `.text`.
- Le cycle de vie est également natif. Lors d'un changement de layer, le trajet
  démarré à `0xD1710` appelle `0xD6230`; `0xD7CE0` parcourt les quatre arbres et
  sérialise frame/X/Y en mots dans le format automap du jeu, puis `0xD4D60`
  libère les nœuds. Le teardown à `0xD3120` vide de la même manière les quatre
  arbres de chaque propriétaire, libère le bloc `0xB0` et remet les globals à
  zéro. Une implantation correcte ne doit donc conserver ni libérer elle-même
  aucun pointeur de nœud.
- **Verdict statique : PASS borné.** Des cellules seed-exactes peuvent être
  publiées dans l'automap native sans `DrlgRoom`, `ActiveRoom`, collision,
  monstre ni objet actif, à condition d'exécuter l'insertion sur le thread UI
  natif, d'utiliser les primitives natives et d'envoyer chaque niveau dans son
  propre `Levels.Layer`. Le dessin terrain ImGui 0.13.22 doit être supprimé :
  garder les deux terrains reproduirait nécessairement le doublon et le flash
  observés.
- Deux limites empêchent de transformer ce PASS statique en promesse gameplay.
  Premièrement, fusionner tout un acte dans chaque layer courant dupliquerait
  des dizaines de milliers de cellules dans plusieurs entrées de sidecar et
  viole donc le contrat retenu. Deuxièmement, ce spike ne prouve pas encore les
  noms distants : labels et waypoints restent un consommateur distinct au-dessus
  de l'automap native. Les montages spéciaux Lut Gholein/Pandemonium/Harrogath
  utilisent aussi les sheets libd2 1–3 et doivent rester sur leur chemin town
  natif tant qu'un contrat x64 équivalent n'est pas établi.
- Aucune injection ni qualification runtime n'a été exécutée. Le prochain gate
  autorisé est un prototype fail-closed qui publie progressivement les seules
  cellules MaxiMap d'un niveau vers son `Levels.Layer`, vérifie la déduplication
  native et mesure temps d'insertion, taille du sidecar et FPS Tab ouvert avant
  toute généralisation aux cinq actes. Les 30 276 cellules de l'acte III sont
  une charge à mesurer, pas une preuve de performance.

### Correction du propriétaire actif après le témoin runtime

- Le témoin gameplay suivant a invalidé l'appel actif à
  `AUTOMAP_GetOrCreateLayer 0xD5360` pour prépublier des layers étrangers : un
  changement par waypoint est resté plus de 40 secondes au chargement, le
  processus a atteint environ 17,65 Go de mémoire privée et les logs ont
  enregistré 38 202 pompes `native-atlas-replay`. Cette corrélation runtime ne
  prouve pas seule l'allocation causale exacte, mais elle rencontre le trajet
  destructif déjà prouvé statiquement.
- `D2R+0x2A2CF68` est le pointeur du propriétaire automap actuellement actif.
  Le témoin à `0xD541E` le compare au propriétaire candidat et appelle
  `0xD1710` en cas de différence; ce dernier sérialise les arbres courants par
  `0xD6230` et libère leurs nœuds par `0xD4D60`. Une boucle qui demande des
  layers étrangers peut donc provoquer des cycles de changement, sauvegarde et
  libération que MapSense ne possède pas.
- La frontière corrigée est stricte : `0xD5360` demeure uniquement un témoin de
  signature; MapSense lit `D2R+0x2A2CF68` à chaque pompe UI, exige que
  `owner+0x00 == Levels.Layer(currentLevel)`, écrit seulement les niveaux de ce
  layer dans les arbres `+0x08/+0x30`, puis oublie immédiatement le pointeur.
  Un owner nul ou différent attend de façon bornée et échoue fermé; aucune
  création, commutation, restauration ni conservation de propriétaire n'est
  permise.

### Limite signée du sérialiseur et crash de transition 0.13.24

- Le crash du 1er septembre 2026 lors du waypoint Acte III vers Dry Hills est
  un accès invalide de première chance à `D2R+0x12D399E`, dans la copie
  optimisée appelée par le trajet de sérialisation. Le dernier log MapSense
  complet attestait exactement `19202/7556/11646` cellules
  tentées/insérées/dédupliquées dans le layer 57; la requête Acte II avait
  commencé, mais aucune complétion de son layer n'avait pu suivre. L'automap
  demeurait donc visuellement sur l'Acte III parce que la commutation avait
  échoué avant le remplacement du propriétaire.
- `AUTOMAP_SerializeLayerOwner 0xD6230` délègue chaque arbre à
  `AUTOMAP_SerializeCellTree 0xD7CE0`. La lecture instruction par instruction
  corrige ici l'interprétation initiale : le walker saute un nœud lorsque le
  premier octet de sa clé à `node+0x20` est non nul. Chaque clé de tag zéro
  émise ajoute trois mots, soit six octets. L'épilogue `0xD7E3F` lit à
  `[output+0x08]` le nombre de mots émis, le double en 16 bits et sign-étend la
  longueur. La limite sûre est donc `floor(32767/6)=5461` **records tag-zéro
  émis**, et non 5 461 nœuds totaux par arbre. Le registre de crash
  `RBP=0xFFFFB118` ferme toujours la causalité pour les clés MapSense, toutes
  de tag zéro : `0xB118=45336=7556*6`, puis cette valeur sign-étendue a été
  consommée comme une taille de copie non bornée.
- Le générateur 0.13.24 avait jeté `PlacedTile.pass` et utilisé
  `orientation>0x0F` à la fois comme arbre et comme élévation. Sur les quatre
  seeds gouvernés, aucune cellule générée n'est `raised`, ce qui envoyait donc
  toute la géométrie dans l'arbre floor. MSA1 v2 conserve maintenant
  `wallTree=(pass!=0)` pour les outdoors et la provenance DS1 floor/wall pour
  les presets, tandis que `raised` reste exclusivement l'offset Y natif.
- La preuve ABI de `AUTOMAP_FindCellInsertionPoint 0xD4B70` donne
  `(tree, FindResult*, key) -> FindResult*`, avec un résultat de 16 octets
  `{node, insertionSlot}`. Un `insertionSlot` nul prouve un doublon; MapSense
  peut alors avancer sans allocation, même lorsque `tree+0x20` dépasse 5 461.
  Pour une clé absente seulement, la DLL parcourt l'arbre de façon bornée,
  compte les records tag-zéro réellement sérialisables et refuse l'appel à
  `AUTOMAP_InsertCell` s'il ferait dépasser 5 461 records émis. Elle conserve
  en plus la vérification SEH de la variation exacte du compteur total avant et
  après une vraie insertion. Ce garde-fou ne rend pas une géométrie incomplète
  acceptable; il garantit seulement qu'une régression future échoue fermé au
  lieu de corrompre la transition d'acte. La matrice helper MSA1 v2 passe les
  cinq actes et quatre seeds avec des cellules floor et wall distinctes et des
  artefacts byte-déterministes.

### Correction du garde de parcours MapSense 0.13.29

- Le témoin runtime 0.13.28 a acquis correctement le propriétaire actif et lu
  `636/197` nœuds floor/wall, puis a échoué avant de publier un maximum émis.
  La comparaison avec le walker gouverné `0xD7D10..0xD7E1A` a identifié une
  erreur dans le garde MapSense, pas dans le layout natif : les remontées de
  parents étaient ajoutées au nombre de nœuds déjà visités.
- Sur le dernier nœud d'un arbre non trivial, `visited == total`; la première
  remontée devenait artificiellement `total + 1` et déclenchait le fail-closed.
  La version 0.13.29 borne désormais séparément les nœuds visités et les liens
  suivis pour calculer le successeur. Le layout reste inchangé : minimum à
  `tree+0x08`, liens parent/left/right à `node+0x00/+0x08/+0x10`, sentinelle
  égale à l'adresse du tree et compteur total à `tree+0x20`.

### Cellules restaurées non resérialisées MapSense 0.13.30

- Le runtime 0.13.29 ferme le faux négatif du walker et atteint ensuite la
  vraie limite native : `4691/5461` records tag-zéro émis dans les arbres
  floor/wall de Kurast Docks. La géométrie reste donc incomplète si MapSense
  traite ses cellules synthétiques comme de nouvelles cellules explorées.
- Le chargeur sidecar natif `0xD5C3E..0xD5C75` relit frame/X/Y, écrit
  explicitement `uint16 1` à `AutomapCellKey+0x00` au témoin `0xD5C6F`, puis
  appelle `AUTOMAP_InsertCell 0xD1460`. Ces cellules tag-1 appartiennent au
  même arbre rendu que les cellules ordinaires. Le comparateur
  `AUTOMAP_FindCellInsertionPoint 0xD4B70` ignore ce tag et ordonne seulement
  Y, X et frame; la déduplication reste donc exacte face à une cellule native
  déjà présente.
- `AUTOMAP_SerializeCellTree 0xD7CE0` saute précisément les clés dont le
  premier octet à `node+0x20` est non nul. MapSense 0.13.30 réutilise donc le
  tag natif restauré `1` pour son atlas synthétique : rendu natif conservé,
  zéro ajout au payload sidecar signé 16 bits et suppression du scan complet
  de l'arbre sur le thread UI. L'intention persistante seed/difficulté de
  MapSense demeure la source de reconstruction.
- Le switch natif détruit les arbres actifs et les cellules tag-1 ne sont pas
  ajoutées de nouveau au sidecar. Une complétion MapSense n'est donc réutilisée
  que tant que son `Levels.Layer` reste le dernier layer actif. Après un vrai
  changement de layer, revenir au précédent invalide sa complétion et republie
  sa géométrie depuis le cache exact, par pompes bornées.

## 2026-09-01 — ISC12 G9 : invariant du walker et qualification runtime finale

- Le désassemblage gouverné du walker socketé `0x481B50` ferme l'invariant
  exact des descendants : le producer enfant 0x9D reçoit les flags du parent
  avec le bit `0x08` forcé, soit
  `childTemporaryFlags = parentTemporaryFlags | 0x08`; l'action vaut `0x12`,
  le parent est l'item actif et gamble vaut zéro. La validation précédente par
  simple égalité de flags était donc trop stricte et a été corrigée.
- Le candidat final ajoute un self-test runtime du transactionnel G9. Il
  atteste un arbre accepté 3/3, un dépassement de 64 nœuds rejeté avant tout
  callback de queue, et une réentrance pendant flush contenue après exactement
  un callback avec passage terminal en `Fatal`. Le sink `0x4817F0` retourne
  `void`; aucun rollback n'est revendiqué après le début d'un flush réel.
- Des transactions gameplay réelles ferment les deux racines 0x9C et 0x9D.
  Une Gothic Plate socketée avec deux runes produit un arbre de trois nœuds :
  `nodes=3`, `captured=3`, `queued=3`, `staging-error=0`,
  `flush-error=0`. Les mêmes preuves repassent en portée globale.
- Le candidat final mesure 446 464 octets et vaut
  `1311F1C4BE44B0918F34C32007C3A19D35D240D8B72DCAD8C1853EEE53EC11B5`.
  Deux builds Release reproductibles, la DLL déployée et CTest `5/5`
  concordent. Les cold starts mod-local, global puis mod-local conservent 36
  plugins chargés, les cinq eezstreet et 17 patches.
- Les fixtures D2S sérialisent les IDs 512/4094 avec les valeurs 12/94 et les
  préservent sur deux cycles froids. Le D2I contrôlé de 68 307 octets conserve
  le SHA-256 `7375F2F7…E96507F` après extinction complète, réaffiche l'armure et
  rejoue son arbre 3/3. Ces preuves ferment le cœur D2S/D2I/G9; le multijoueur,
  G5–G8 et le census fixed-byte restent hors de cette promotion.

## 2026-09-01 — Scripted Domains RE-AI-1 : dispatch normal et impossibilité de l’append in-place

- `npm run re:d2r33 -- status` vérifie l’image canonique, l’image d’analyse,
  l’index et le projet Ghidra du corpus commun 92777/93847. Le runtime final à
  tester demeure D2R 3.3.93847; aucune matrice runtime n’est revendiquée ici.
- `AITHINK_GetAiTableRecord 0x4A36C0` possède trois callers directs :
  `0x4A276A`, `0x4A27B7` et `0x4A2BD1`. Son entrée stricte de 17 octets est
  unique. Les deux premiers callers gouvernent l’initialisation et les
  transitions du callback; le troisième sélectionne le profil de ciblage du
  think courant.
- Le chemin normal lit le WORD signé `MonStatsTxt+0x52`, puis la séquence
  unique `B9 9B 00 00 00 66 3B C1 73 19` à `0x4A3791` exige
  `0 <= AI < 155`. L’index accepté est multiplié par `0x20` et ajouté à la base
  `0x2396E90`, référencée uniquement à `0x4A379F` et par le fallback à
  `0x4A37B9`.
- Le special state non nul admissible utilise la même stride `0x20` et la base
  `0x23981F0`, référencée à `0x4A376E`. L’égalité
  `0x2396E90 + 155*0x20 == 0x23981F0` prouve que la table normale et la table
  des special states sont contiguës. Sur D2R, élargir seulement la borne à 156
  ferait donc lire la première special state comme nouvelle AI; écrire cette
  entrée la corromprait. Le patch Harvest 1.10f n’est pas transposable tel quel.
- Le dispatch x64 lit la catégorie à `record+0x00`. Le chemin
  d’initialisation compare le callback principal à `record+0x10`, teste le
  callback d’entrée à `record+0x08` et le callback de transition à
  `record+0x18`. La taille x64 `0x20` est ainsi prouvée directement; la
  structure legacy D2MOO n’est utilisée que comme sémantique.
- Le callback principal reçoit `Game*`, `Unit*` et `D2AiTickParam*` selon l’ABI
  Windows x64. Le stockage local construit par le caller prouve le sous-ensemble
  minimal suivant : `D2AiControl* +0x00`, cible `+0x10`, distance `+0x20`,
  témoin de combat `+0x24`, `MonStatsTxt* +0x28` et `MonStats2Txt* +0x30`.
  Cette preuve ferme le contexte minimal du bridge sans prétendre connaître la
  structure complète.
- `AITACTICS_IdleInNeutralMode 0x4A6D10` est le premier helper de fallback
  fermé. Il normalise zéro à un frame, place le monstre en mode neutre, appelle
  `0x48B890(game, unit, eventType=2, customId=0)`, puis
  `EVENT_SetEvent 0x48B720(game, unit, eventType=2,
  Game+0x170+frames, 0, 0, ...)`. Sa séquence interne unique à `0x4A6D71`
  commence par `45 8D 41 02 E8 16 4B FE FF 44 8B 8F 70 01 00 00`.
- La référence sémantique est
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`,
  `source/D2Game/src/AI/AiThink.cpp:17051-17408`,
  `source/D2Game/src/AI/AiTactics.cpp:243-266` et
  `source/D2Game/include/AI/AiGeneral.h:19-29,80-86`. Aucune adresse, structure
  ou ABI 32 bits n’est transposée.
- La direction D2R retenue pour instruction est un hook unique et fail-closed
  du résolveur : déléguer tout special state et toute unité non liée à
  l’original, puis retourner un record bridge possédé par la DLL uniquement
  pour une classe présente dans une table custom `aiscript`. Les inconnues
  restantes sont la catégorie bridge, les helpers attaque/chase/retraite/
  errance/cast, leur contrat de rescheduling, le cycle de vie GUID et
  l’ownership runtime. Aucune DLL ni injection n’est encore créée.

## 2026-09-01 — Scripted Domains RE-AI-2 : catégorie bridge, actions et continuation

- Le switch à `0x4A2BD6` est borné à `0..6`; son témoin strict de 28 octets est
  unique. La table de saut donne exactement `0→0x4A2CF1`, `1→0x4A2BF2`,
  `2→0x4A2C7A`, `3→0x4A2BF2`, `4→0x4A2C31`, `5→0x4A2CA0` et
  `6→0x4A2CC9`.
- Les catégories `1/3` appellent le wrapper `0x4A69A0` et ne dispatchent le
  callback qu’avec une cible. La catégorie `2` appelle directement le
  sélecteur nullable `0x595750`, puis rejoint le callback sans test de nullité.
  La catégorie `4` saute le callback principal. Les catégories `5/6` appellent
  `0x4A6760`; `5` exige ensuite une cible tandis que `6` continue même après le
  fallback exécuté par ce wrapper. Le premier record bridge retient donc la
  **catégorie 2** : ciblage natif, callback garanti et aucune action automatique
  avant Lua.
- `AIUTIL_SelectTargetForAiThink 0x595750` suit l’ABI x64
  `Unit*(Game*, Unit*, D2AiControl*, int32_t* distance, int32_t* combat,
  uint8_t context)`. Son corps se termine à `0x595F7F`; le census de ses appels
  directs ne contient ni `EVENT_SetEvent 0x48B720` ni `0x48B890`. Sa signature
  stricte de 32 octets est unique.
- Les primitives terminales V1 sont maintenant fermées dans l’image :
  `AITACTICS_ChangeModeAndTargetUnit 0x4A78E0` pour l’attaque,
  `AITACTICS_UseSkill 0x4A7BC0` pour le cast,
  `AITACTICS_WalkToTargetUnitWithFlags 0x4A8740` pour le chase,
  `D2GAME_AICORE_Escape 0x4A7DF0` pour la retraite et
  `AITACTICS_WalkCloseToUnit 0x4A8320` pour l’errance. Leurs signatures
  strictes étendues sont uniques. Les deux feuilles de marche convergent vers
  `AITACTICS_MoveToTarget 0x4A8A10`, lui aussi identifié par une entrée unique.
- Les ABI utiles sont respectivement `(game, unit, mode, target)`,
  `(game, unit, mode, skillId, target, x, y, flag)`,
  `(game, unit, target, flags16)`,
  `(game, unit, target, distance8, deleteAiEvent32)` et
  `(game, unit, radius8)`. Aucune structure legacy n’est utilisée pour fixer
  ces largeurs ou l’ordre x64.
- `UseSkill` appelle lui-même `AITACTICS_IdleInNeutralMode` pour 10 frames si
  le pipeline de mode refuse le cast. Les quatre autres primitives remontent
  leur rejet sans idle. Le contrat V1 est donc asymétrique mais fermé : une
  action acceptée laisse exclusivement le pipeline de modes natif poursuivre;
  un rejet attaque/chase/retraite/errance, une absence de leaf ou une erreur
  Lua appelle une fois l’idle natif; un rejet de cast n’ajoute aucun second
  idle. On ne sonde ni ne force un `AITHINK` immédiatement après un succès,
  parce qu’un mode/une animation accepté possède alors légitimement la suite.
- Le `monai.txt` officiel 3.3 et sa copie `base/monai.txt` sont byte-identiques,
  contiennent les 155 records attendus et valent
  `7c7c7b8866c46078356bcc6118789699351004f39edea92922425699d6aa86a8`.
  Le round-trip TSV est byte-exact. La table native vit toutefois dans une zone
  virtuelle non adossée au PE brut; aucune extraction live n’est nécessaire
  pour fermer la catégorie bridge par le dispatch lui-même.
- Référence sémantique seulement :
  `D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`, notamment
  `AiThink.cpp:17363-17407`, `AiTactics.cpp:31-88,172-178,214-265,305-369,
  454-565` et `AiUtil.cpp:671-864`. Le prochain gate n’est plus le choix des
  actions : il porte sur le lifecycle GUID, l’ownership du hook, les budgets et
  l’empreinte fail-closed complète. Aucune DLL ni injection n’est créée par ce
  lot statique.

## 2026-09-01 — Scripted Domains RE-AI-3 : lifecycle V1, ownership, autorité et budgets

- Le lifecycle le plus sûr ne consiste finalement pas à reproduire la table
  GUID de Harvest. La V1 ne publie aucun GUID, pointeur ou identifiant stable et
  ne conserve aucun état par unité. Son userdata porte seulement la génération
  de session et un token de think, puis est invalidé sur tous les retours du
  `lua_pcall`. Mort, despawn et réutilisation de GUID ne peuvent donc laisser
  d’entrée fantôme; un blackboard par unité rouvrira un gate distinct s’il
  devient un besoin mesuré.
- PluginSDK API v3
  `4933e2c42cb2592958cd0df3b6dc5003102252d1` fournit les frontières nécessaires.
  Les événements `GameJoined/GameLeft` publient un `sessionGeneration` depuis
  le thread UI; le callback ne touche pas la VM et publie seulement une valeur
  atomique, puis demande une activation `runOnGameThread`. Son callback réclame
  l’ancienne VM, initialise la nouvelle génération et capture le thread
  autoritaire; tout think antérieur ou provenant d’un autre thread reste stock.
  `PluginFlags::Server | NativeHooks` borne la DLL au rôle gameplay et
  `runOnGameThread` est explicitement indisponible sur le client TCP/IP distant.
  Aucun channel ou accord de hash client n’est requis pour une IA dont l’hôte
  demeure l’unique autorité.
- Le census textuel de toutes les sources, patches JSON/TOML et missions du
  workspace ne trouve aucun propriétaire de `AITHINK_GetAiTableRecord
  0x4A36C0`. Un snapshot read-only du D2R officiel 3.3.93847 déjà lancé avec 36
  plugins, les cinq eezstreet et 17 patches retrouve les préfixes vanilla du
  résolveur, du dispatch, de la catégorie 2, du handoff, du sélecteur et des
  sept helpers. `DiagnosticsServiceV1` permet de transformer ce constat en
  invariant : exiger `Unchanged/0 owner` avant l’installation gérée, puis
  `Tracked/InlineHook/1 owner/self` au premier think de chaque session. Toute
  autre réponse désactive Lua et délègue à l’original.
- L’empreinte V1 compte 22 fenêtres exactes, instruction-aligned et uniques :
  type/class d’unité; construction du tick `0x4A2ADA/117`; dispatch/catégorie/
  handoff; entrée et branches special/normal du résolveur; sélecteur; puis
  entrée et témoin terminal de chaque action. Le témoin tick vaut
  `85EE58C57F0381F78B286B2B05B9636883A408CB508EBFCF51BA07F4600F5FA9`;
  les branches special et normal valent respectivement `0916D113…B38193` et
  `77EB5135…C44BFD`.
- Ce recensement corrige une erreur documentaire du lot précédent. Les anciens
  préfixes attaque/cast/errance de 31/26/32 octets finissaient au milieu d’une
  instruction et deviennent 32/28/34. Le préfixe chase de 28 octets avait deux
  matches et devient la fonction complète de 39 octets; celui du mouvement de
  22 octets avait cinq matches et devient un garde aligné unique de 62 octets.
  Les témoins terminaux attaque/cast/retraite/errance de 40/70/35/64 octets et
  le reschedule idle de 16 octets sont également uniques.
- Les limites de sécurité V1 sont fixées avant code : source texte `256 KiB`,
  arbre `256` nodes/profondeur `32`, heap session `16 MiB`, croissance par think
  `64 KiB`, `25 000` instructions contrôlées toutes les `500`, une seule action
  terminale, quarantine après trois erreurs ou trois thinks Lua au-delà de
  `2 ms`, et un log détaillé par script toutes les cinq secondes. Les critères
  de release, encore non mesurés, sont p99 hors helper natif `<=50 us/think` et
  enveloppe agrégée `<=2 ms` par update serveur de 40 ms dans la densité retenue.
- Le gate RE est fermé sans source de DLL, sans déploiement et sans gameplay.
  Le prochain lot doit utiliser l’incubation RuffnecKk Suite, matérialiser ces
  contrats dans les tests et conserver `aiscript` vide/default-off avant tout
  prototype runtime.

## 2026-09-02 — Revive Overhaul 2.1.2 : propriété du gate client

- Le test gameplay BKVince 93847 de 2.1.1 chargeait la pile complète, mais les
  corps Champion, Unique et SuperUnique restaient identifiables sans produire
  d'action Revive au clic droit. Rakanishu conservait pourtant
  `corpseSel=1`, `revive=1`, `switchai=1` et aucun flag boss/prime-evil.
- Le `CALL` direct `E8 E3 60 2B 00` à `0x96648` est la surface minimale : il
  transmet les cinq arguments vrais à `AIUTIL_CanUnitSwitchAi 0x34C730`, puis
  le sélecteur reprend à `0x9664D` et conserve toutes ses restrictions
  ultérieures.
- Revive Overhaul 2.1.2 ne hooke plus l'entrée partagée `0x34C730` et ne dépend
  plus de `_ReturnAddress()`. Il possède uniquement `0x96648` par
  `PatchCallRel32`; son relais change `checkUnique` seulement pour le masque de
  rang `0x000E`. Le fallback serveur continue d'appeler directement la fonction
  native non hookée avec `(true,false,true,true)`.
- Cette correction est compilée et testée statiquement, mais sa qualification
  gameplay reste ouverte jusqu'au redéploiement et au nouveau test complet.

## 2026-09-01 — Extended Act Level IDs : résolveur central et layout `Levels.Act`

- `DRLG_ResolveActFromLevelId 0x326710` suit l'ABI x64 observée
  `uint8 (uint8 dataContext, int32 levelId)`. Son corps utile autonome va de
  `0x326710` à `0x3267A6`, même si l'entrée pdata englobante commence plus tôt.
  Il obtient le vecteur `ActInfo` par `0x3006E0`, sélectionne les DataTables du
  contexte par `GetDataTablesForContext 0x300A90`, lit le compte à `+0x108`,
  parcourt à rebours des records de `0x8C` octets et compare le Level ID au
  `rangeStart` signé à `record+0x04`. Il renvoie l'index d'acte zéro-based ou
  zéro si aucune plage ne correspond.
- Le census du workbench commun trouve **113 appels directs**. La signature
  stricte de 48 octets
  `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 30 8B F2 0F B6 D9 E8 B7 9F FD FF 0F B6 CB 48 8B F8 E8 5C A3 FD FF 8B 98 08 01 00 00 83 EB 01 78 43 90`
  n'a qu'un match, à `0x326710`, dans l'image commune 92777/93847.
- Une sonde D2RLoader temporaire, exécutée le 1er septembre 2026 sur le runtime
  officiel D2R `3.3.93847` avec toute la pile BKVince active, a interrogé
  `DataTableServiceV1` pendant `DataTablesLoaded`. Les banques Classic et LoD
  exposent 137 lignes, RotW 147; les trois ont `rowSize=0x18C`. Sur tous les
  Level IDs présents et les frontières `1/40/75/103/109`, l'unique octet qui
  reproduit `Levels.txt → Act` est `row+0x0D`. Une entrée en partie BKVince a
  ensuite observé `dataContext=3`, égal à la valeur ABI `Bank::Rotw=3`.
  Preuve locale :
  `analysis-cache/extended-act-level-ids-probe/evidence/20260901-1918/ruffneckk-extended-act-level-ids-probe.log`
  (SHA-256 `F81B7F76F28D36CC173D7D7A8CB83888D9D026BAF0C5C5B801B40B8B5FC6BA6D`).
- D2MOO 1.10f explique pourquoi cette surface existe :
  `DRLG_GetActNoFromLevelId` applique les seuils fixes
  `{1,40,75,103,109,1024}` et porte déjà le commentaire
  `Lookup the act from Levels.txt`. Cette référence est strictement sémantique;
  aucune adresse, structure ou ABI 32 bits n'est transférée.
- Le contrat produit peut donc rester minimal : un hook propriétaire du seul
  résolveur central, des caches immuables copiés après chaque révision de tables
  et une sélection `dataContext 1..3 → Bank 1..3`. Avant de publier un cache,
  la DLL doit vérifier le service API v4, `rowSize=0x18C`, les actes ancres et
  chaque valeur `0..4`; `dataContext=0`, cache non prêt, ID absent ou valeur
  invalide reprennent toujours l'original. Le nom du build reste diagnostique
  et n'entre dans aucune allowlist.
- Le produit final `0.1.0` ferme aussi le test hors plage du résolveur. Un
  fixture TSV CRLF temporaire `Id=147 / Act=0`, construit sans modifier la
  source BKVince, fait passer la banque RotW de 147 à 148 lignes. L'appel de
  l'entrée native hookée journalise alors `Act index 0 (Act 1), data context 3,
  source=Levels.txt`. Le fixture est ensuite retiré et le `levels.txt` runtime
  retrouve byte-exact le SHA-256
  `A46B5438164ADB1FB9540890103594EA48A79AFA2478CB6865D2E6DB5795EB04`.
  Preuve locale :
  `analysis-cache/extended-act-level-ids-product/evidence/20260901-1947-functional-fixture/ruffneckk-extended-act-level-ids.fixture.log`.

## 2026-09-04 — Extended Act Level IDs 2.0.0 : limite 1023 et codec de visibilité

- Le témoin `0x330446` porte la comparaison exacte
  `cmp qword ptr [rsi+0x1468],0x400` suivie d'un `jb`. Sa signature stricte de
  36 octets est unique : le runtime admet donc au plus **1023 records Levels**,
  soit les IDs canoniques contigus `0..1022`. La DLL ne patch pas cette garde;
  elle la vérifie et refuse un cache vide, non contigu ou supérieur à 1023.
- Les constructeurs serveur `0x47D2D0` (paquet `0x07`, room in sight) et
  `0x47EAF0` (`0x08`, room out of sight) suivent tous deux l'ABI observée
  `void (D2Client*, int32 levelId, uint16 x, uint16 y)`. Leurs appelants
  fournissent le Level ID complet dans `EDX`, mais les constructeurs stockent
  seulement `DL` à l'octet `+5`; X et Y restent des WORD à `+1/+3`. Leurs
  signatures strictes de 33 octets sont uniques dans le corpus commun.
- Les consommateurs client `0x129B80` et `0x129C30` relisent symétriquement
  X/Y comme WORD et le Level ID `+5` comme BYTE, puis appellent les bridges
  `0x2EF320` et `0x2EF700`. Ces bridges conservent R8D et terminent dans
  `DUNGEON_SetClientIsInSight 0x328680` et
  `DUNGEON_UnsetClientIsInSight 0x328780`. Les deux fonctions cibles ont l'ABI
  observée `void (uint8 dataContext, Act*, int32 levelId, int32 x, int32 y,
  Room*)`, passent le Level ID complet à `DRLG_GetLevel` et possèdent des
  signatures d'entrée uniques de 32 et 29 octets.
- La v2 préserve le framing six octets. Pour les IDs `256..1022` seulement,
  elle place un marqueur dans X bit 15, les deux bits hauts du Level ID dans X
  bits 14..13 et conserve la coordonnée dans X bits 12..0. Le décodeur restaure
  l'ID et X avant le consommateur natif. Le contrat exige donc X `0..8191`;
  l'audit TSV BKVince courant mesure un maximum de fin de monde X=5200 et
  Y=4100. Les IDs `0..255` appellent strictement le producteur original avec
  leurs arguments inchangés.
- `D2Client+0x270` fournit le GUID joueur utilisé pour l'autorisation du
  destinataire. Le témoin intérieur unique `0x485B51` couvre simultanément ce
  GUID, le type d'unité `+0x26C`, le cache joueur `+0x278`, le jeu `+0x2B0`,
  les flags `+0x4E0` et l'appel à `SUNIT_GetServerUnit`. Le joueur local annoncé
  par `LocalPlayerReady` est autorisé directement; chaque joiner doit avoir
  réussi le channel privé PluginSDK avec le token exact
  `0x454C494456320001`, puis annoncer son GUID. Une absence, incompatibilité,
  déconnexion ou destination inconnue supprime le paquet étendu au lieu
  d'envoyer un ID tronqué.
- La source 2.0.0 ne possède aucun hook ni codec D2S/D2I, ne redimensionne ni
  bitset de waypoint ni portal flags et n'introduit aucune migration de save.
  D2MOO confirme seulement la sémantique historique et les tailles six octets
  des paquets `0x07/0x08`; aucune adresse, structure ou ABI Legacy n'est
  transférée. Les requêtes client Town Portal `0x45` et waypoint `0x49`
  transportent historiquement un Level ID sur 32 bits, mais leurs handlers D2R
  exacts et leur comportement avec un vrai niveau `>255` restent un gate de
  qualification, pas une preuve de release.
- Le build Release et les tests de codec passent hors jeu. Aucun déploiement,
  cold start ou gameplay 2.0.0 n'a été exécuté : ces actions restent soumises
  au gate séparé `d2r-runtime-validation` et la publication demeure bloquée.

## 2026-09-04 — Extended Act Level IDs 2.0.2 : barrière Town Portal à huit bits

- Le gate runtime officiel 3.3.93847 conserve la pile complète et la fixture
  same-act `109 ↔ 256`. MapSense observe successivement
  `109 → 256 → 109 → 256 → 109 → 256`, toujours Act 4, avec
  `room-witness=35` au niveau 256. Les voyages physiques aller et retour sont
  donc fermés indépendamment du Town Portal.
- Deux secondes après la dernière entrée au niveau 256, la création du Town
  Portal déclenche exactement `BC_ASSERT: eLevelIdLocal <= 255` dans
  `D2Game\src\Skills\Skills.cpp:4120`. La pile fraîche passe par les retours
  `0x436075`, `0x432F27`, `0x46FD81`, `0x581965`, `0x4F52CB`, `0x4C144C` et
  `0x4F30BD`. Le processus a été arrêté sans choisir Continue; aucun test
  d'entrée dans le portail n'est revendiqué.
- `D2GAME_CreateLinkPortal 0x435DD0` possède un préfixe strict unique de
  32 octets. Son témoin intérieur unique à `0x436061` appelle d'abord
  `DUNGEON_GetLevelIdFromRoom 0x2EFC10`, compare le DWORD retourné à `0xFF`,
  produit l'assertion puis exécute `movzx edx,dil` avant
  `UNITS_SetObjectInteractType 0x34E9D0`. Ce setter écrit exactement un octet à
  `ObjectData+0x08`. Supprimer seulement l'assertion transformerait donc
  Level 256 en destination `0`; ce n'est pas une correction acceptable.
- La troncature persiste en aval. `SUNIT_GetPortalOwner 0x490070` relit
  `InteractType` par le getter `uint8` `0x34AD40`, le zéro-étend pour
  `DRLG_ResolveActFromLevelId`, puis utilise les coordonnées de room distante à
  `ObjectData+0x24/+0x28` pour trouver ou streamer le propriétaire. Son entrée
  stricte de 32 octets est unique.
- Le transport de l'état portail est lui aussi étroit. Le corps serveur unique
  à `0x47F650` range `UNITS_GetObjectInteractType` à l'octet `+2` du paquet
  `0x60`; le consommateur client unique `0x1CB1C0` lit ce même octet et appelle
  le setter byte. Le codec v2 existant des paquets room-visibility `0x07/0x08`
  ne couvre donc ni la construction, ni l'état, ni la consommation d'un Town
  Portal étendu.
- D2MOO au commit
  `19019806df7f3e877fa105b05395d1e3597e2316` confirme uniquement la sémantique
  dans `Skills.cpp:3030-3074`, `SUnit.cpp:1365-1386` et
  `SCmd.cpp:2057-2068`; aucune adresse, structure ni ABI 32 bits n'est
  transposée.
- Le mécanisme candidat sûr n'est pas un élargissement global
  d'`ObjectData.InteractType`, champ partagé par de nombreuses classes
  d'objets. Il exige au minimum un sidecar de session autoritaire et strictement
  limité aux portails, une identité/lifecycle de portail gouvernée, tous les
  producteurs et consommateurs concernés, puis un codec compatible-peer pour
  les paquets portails. Avant tout code, il reste à fermer le census création,
  interaction, destruction, réutilisation de GUID, paquet initial `0x51`,
  paquet d'état `0x60`, host/joiner et refus Battle.net/incompatible.
- Les tables, la DLL et les neuf fichiers `QtyTester` ont été restaurés
  byte-exact; aucun processus ne demeure. Le rapport local complet est sous
  `analysis-cache/extended-act-level-ids-v2/runtime/20260904-gameplay-level-256-return-portal-2.0.2/`.

## 2026-09-04 — Extended Act Level IDs : census Town Portal 1023

- Le gate autorisé `GO reverse engineering Town Portal 1023` est strictement
  read-only : aucun hook, patch, build, déploiement, lancement du jeu ou accès
  aux sauvegardes n'a été exécuté. Le workbench commun 92777/93847, son image,
  son index et les deux références épinglées D2MOO/D2RL-Plugins ont été
  revérifiés avant l'analyse.
- Le constructeur central `D2GAME_CreatePortalObject 0x432CE0` couvre les
  portails de skills, d'objets, de missiles et de quêtes. Ses quatorze appels
  directs sont `0x4649BD`, `0x46FD7C`, `0x4F2F67`, `0x548DD8`, `0x550668`,
  `0x591040`, `0x5A67F7`, `0x5D9526`, `0x5DA3E8`, `0x5DA52E`, `0x5DA758`,
  `0x5DA8F8`, `0x5DD341` et `0x5E24C4`; les patcher individuellement serait
  plus fragile que qualifier le constructeur central. L'appel Town Portal à
  `0x46FD7C` fournit la classe objet `0x3B`, conserve la destination complète
  en registre et mémorise le GUID du portail source dans PlayerData après
  succès.
- `D2GAME_CreatePortalObject` appelle `D2GAME_CreateLinkPortal 0x435DD0` à
  `0x432F22`. Le second constructeur conserve la destination complète pour la
  résolution de l'acte, de la room et du spawn, mais son garde `0x436061`
  refuse la room source au-dessus de 255 puis réduit son Level ID au byte écrit
  dans `ObjectData+0x08`. Ses deux seuls appels directs sont le constructeur
  central et l'initialiseur statique Act V à `0x593D40`; ce dernier n'est pas
  une preuve du mécanisme Legacy de retarget.
- L'opération autoritaire est `OBJECTS_OperateFunction15_Portal 0x58F680`.
  Elle vérifie le joueur et les permissions, appelle
  `SUNIT_GetPortalOwner 0x490070` à `0x58F7CB`, relit le byte `InteractType`
  pour les records de niveau à `0x58F803`/`0x58F8D8`, obtient les Level IDs
  complets des rooms à `0x58F9DB`/`0x58F9F2`, déclenche le changement de niveau
  à `0x58FA06` puis le déplacement à `0x58FA2E`. Quand le joueur possède le
  portail dynamique, les deux moitiés sont retirées à `0x58FB9B` et
  `0x58FBF6`. La découpe PDATA fragmente artificiellement cette fonction; son
  entrée stricte unique et ces callsites servent de témoins gouvernés.
- Le paquet initial objet `0x51` est construit par
  `D2GAME_PACKETS_SendPacket0x51_ObjectSpawn 0x47CE40`, appelé une seule fois
  à `0x53885E`. Son format exact reste long de 14 octets : opcode `+0`, type
  `+1`, GUID DWORD `+2`, object ID WORD `+6`, X WORD `+8`, Y WORD `+0x0A`,
  animation `+0x0C` et `InteractType` byte `+0x0D`. Le client
  `CLIENT_HandlePacket0x51_ObjectSpawn 0x129D70` transmet ce dernier byte à la
  création de l'objet; `CLIENT_CreateObjectFromPacket 0x99510` n'est pas
  promu, son préfixe de 32 octets n'étant pas unique.
- Le paquet d'état portail est construit par la fonction complète
  `D2GAME_PACKETS_SendPacket0x60_PortalState 0x47F620`, dont le corps déjà
  identifié à `0x47F650` est conservé comme témoin intérieur. Le paquet mesure
  12 octets : opcode `+0`, flags `+1`, destination `InteractType` byte `+2`,
  GUID DWORD `+3`, X/Y WORD propriétaire `+7/+9` et Level ID byte de la room
  courante du joueur propriétaire à `+0x0B`. Le producteur à `0x5388A8`
  obtient ce dernier ID comme DWORD mais n'en transmet que l'octet bas à
  `0x5388E4`. Le client
  `CLIENT_HandlePacket0x60_PortalState 0x1CB1C0` applique `+2` au setter,
  `+7/+9` aux coordonnées propriétaire et `+0x0B` à son champ room. Le gate
  runtime 2.1.0 prouve ensuite que les deux valeurs huit bits sont distinctes
  et ne portent aucun invariant d'égalité.
- Le lifecycle inactif est fermé nativement. Dans
  `SUNITINACTIVE_CompressUnitIfNeeded 0x504260`, le test exact à `0x5042ED`
  force les classes `59/60` dans le chemin spécial de compression. La fonction
  `SUNITINACTIVE_CompressInactiveUnit 0x5045D0` alloue pour un objet un nœud
  de `0x38` octets; pour ces portails, il conserve le GUID à `+0x20` mais
  seulement le byte `InteractType` à `+0x28`. Lors du streaming inverse,
  `SUNITINACTIVE_RestoreInactiveUnits 0x503790` recrée l'objet avec ce même
  GUID et remet le byte par `UNITS_SetObjectInteractType` à
  `0x5039A6`/`0x503AA2`. Un pointeur `Unit*` n'est donc jamais une identité
  sidecar sûre et la compression ne constitue pas une destruction logique.
- `D2GAME_RemovePlayerPortal 0x4C8650` efface le GUID PlayerData, résout la
  paire puis retire les deux moitiés par `SUNIT_RemoveUnit 0x48FAA0` à
  `0x4C8718` et `0x4C8726`. Le constructeur lié retire aussi sa source sur
  échec à `0x435EBB`. Un hook global de `SUNIT_RemoveUnit` ne peut cependant
  pas gouverner seul la durée de vie du sidecar, car la compression inactive
  peut emprunter ce même retrait physique sans supprimer l'identité logique.
- Le corpus D2R statique ne contient aucune implantation démontrée du retarget
  Legacy par opcode `0x45`. `D2GAME_CreateLinkPortal` n'a que les deux appels
  précités et les sept xrefs directs de `SUNIT_GetPortalOwner` correspondent à
  la création, la synchronisation, l'opération et le nettoyage déjà recensés.
  La table hydratée ne permet pas de qualifier son slot réseau. Il serait donc
  incorrect de promouvoir le chemin D2MOO `PlrMsg.cpp:3271-3308` comme preuve
  D2R; toute prise en charge future exige une preuve séparée de la table de
  dispatch vivante.
- Trois architectures ont été comparées. Supprimer l'assertion est rejeté car
  `256` devient `0`. Élargir `ObjectData`, les nœuds inactifs et les paquets est
  rejeté car ces ABI sont partagées, accroissent fortement le rayon de
  régression et rompent le protocole stock. L'architecture retenue pour un
  prochain gate est un sidecar de paire indexé par
  `{génération de session, Game*, GUID}`, limité d'abord à la classe dynamique
  `59`, avec hooks centraux et scopes TLS très étroits.
- Dans ce contrat, la création capture atomiquement les deux GUID, les deux
  Level IDs complets, leur relation et leurs témoins low-byte. Un scope autour
  de `D2GAME_CreateLinkPortal` autorise le getter de room à présenter seulement
  le low byte au garde stock; le sidecar conserve l'identité complète. Des
  scopes autour de `SUNIT_GetPortalOwner` et de
  `OBJECTS_OperateFunction15_Portal` permettent aux résolveurs d'acte et de
  record d'utiliser le full ID seulement pour le portail validé. Toute absence,
  incohérence de classe, de GUID, de paire, de génération ou de low byte échoue
  fermée sans repli tronqué.
- Les paquets `0x51` et `0x60` peuvent conserver leurs tailles en réutilisant le
  codec coordonnée déjà gouverné pour `0x07/0x08` : les bits hauts du Level ID
  sont marqués dans X uniquement pour un destinataire compatible, puis le
  client décode une copie locale, restaure X et publie/rafraîchit son sidecar.
  La compression survit par GUID/session; la création peut remplacer une
  entrée, les suppressions autoritaires peuvent l'effacer opportunément et la
  sortie de session doit tout invalider.
- Aucun propriétaire de hook concurrent n'a été trouvé pour les seams portail
  proposées dans les add-ons RuffnecKk, BKVince, TCP ou la référence épinglée
  D2RL-Plugins. Extended Act Level IDs possède déjà le canal privé, le mapping
  peer→joueur, la génération de session et le codec coordonnée requis; aucun
  fichier de configuration supplémentaire n'est justifié.
- `NetworkServiceV1` ne permet pas d'énumérer tous les clients actifs d'une
  partie. Une simple décision par destinataire empêcherait un paquet étendu
  invalide, mais ne prouverait pas que tous les joiners étaient compatibles
  avant la mutation serveur du portail. L'implantation peut donc être qualifiée
  localement/offline en premier, mais la revendication TCP hôte/joiner reste
  bloquée jusqu'à un census natif de la liste clients ou un service SDK
  gouverné. Sur Battle.net ou sans preuve complète de compatibilité, un portail
  `>255` doit être refusé avant toute mutation native.
- Le portail et son sidecar sont strictement de session. Aucun format D2S/D2I,
  bitset de waypoint ou codec de sauvegarde n'est impliqué; retirer la DLL ou
  redémarrer élimine cet état éphémère. Cette conclusion ne vaut pas encore
  qualification gameplay : implantation et runtime exigent chacun un `GO`
  distinct.
- D2MOO au commit épinglé
  `19019806df7f3e877fa105b05395d1e3597e2316` sert uniquement de référence
  sémantique pour `Skills.cpp:3006-3149`, `ObjMode.cpp:3125-3278`,
  `Player.cpp:510-550`, `SUnit.cpp:1335-1384`, `SUnitMsg.cpp:72-83`,
  `SCmd.cpp:2057-2068` et `SUnitInactive.cpp:49-999`; aucune adresse, structure
  ou ABI 32 bits n'est transposée.

## 4 septembre 2026 — implantation Town Portal 1023 local/offline

- Extended Act Level IDs `2.1.0` implante le sidecar de session retenu pour les
  seuls portails dynamiques classe `59`. Chaque endpoint conserve
  `{sessionGeneration, Game*, GUID, counterpartGUID, destinationLevelId,
  nativeLowLevelId}` dans une publication copy-on-write; chaque lookup exige
  la paire réciproque, la classe, le `Game*`, la génération et le low byte.
- `D2GAME_CreateLinkPortal 0x435DD0` possède l'ABI D2R vérifiée
  `(Game*, owner, sourcePortal, destinationLevelId, sourceLevelId) ->
  linkedPortal`. Le caller witness unique `0x432F12` prouve le cinquième
  argument sur la pile. Pendant cet appel seulement,
  `DUNGEON_GetLevelIdFromRoom 0x2EFC10` rend le low byte uniquement au retour
  exact `0x43605F`; tous ses autres appels conservent le Level ID complet. La
  création exige le propriétaire `LocalPlayerReady` et un contrat sain; tout
  rejet ou échec de publication du sidecar survient avant un appel natif non
  scopé et empoisonne le trafic classe `59` restant pour la session.
- `SUNIT_GetPortalOwner 0x490070` réinjecte le full ID dans le résolveur d'acte
  sous TLS, puis exige que l'owner réellement résolu soit le GUID réciproque.
  `OBJECTS_OperateFunction15_Portal 0x58F680` autorise ensuite les records
  complets uniquement pour le joueur local et après cette validation.
- L'implantation initiale voulait hooker les entrées partagées
  `DATATBLS_GetLevelsTxtRecord 0x32C4A0` et
  `DATATBLS_GetLevelDefRecord 0x32C200`. L'audit de coexistence a montré que
  MapSense vérifie et appelle directement `0x32C200`; ce mécanisme aurait donc
  été dépendant de l'ordre de chargement. La version retenue laisse les deux
  entrées byte-exactes et redirige uniquement les calls portail uniques
  `0x58F819` et `0x58F8EE` vers deux relays proches.
- Les builders et handlers `0x51`/`0x60` conservent leurs tailles natives de
  14/12 octets. Le serveur encode les deux bits hauts du Level ID dans X
  uniquement pour le `LocalPlayerReady`; le client valide le marqueur, les low
  bytes et le record connu, restaure X dans une copie locale et délègue au
  handler stock. Une opération ou un paquet distant est refusé : TCP
  hôte/joiner et Battle.net ne sont pas revendiqués par ce gate.
- Toutes les signatures de fonctions, de callsites et de layouts utilisées
  ont exactement une occurrence dans le corpus commun 92777/93847. Deux builds
  Release propres passent `CTest 1/1` sans warning et sont byte-identiques :
  78 336 octets, SHA-256
  `1803A73E0894C2A8916DD5BD32793525E4F795B13652CFC488786247AB6045B6`.
  Les exports restent les trois points D2RLoader et les métadonnées portent
  `RuffnecKk / 2.1.0`.
- Aucun hook de save, changement D2S/D2I, déploiement, lancement du jeu,
  paquet de release ni push n'appartient à ce gate. La validation runtime
  Level 256 → Harrogath → Level 256 exige un prochain `GO` distinct.

## 4 septembre 2026 — échec runtime Town Portal 2.1.0 avant retour

- Le cold start local/offline 2.1.0 avec 1023 records ferme les prérequis :
  empreintes acceptées, `RotW=1023`, pile complète et startup `24/24`. La
  création depuis Level 256 puis la première traversée vers Harrogath passent.
  Avant la tentative de retour, le client assert dans
  `DATATBLS_GetLevelsTxtRecord` avec `eLevelId > 0` violé.
- La pile fraîche place l'adresse de retour à `0xC1893`. Le callsite exact
  `0xC188E`, dans le chemin client de texte/nom d'objet, appelle d'abord
  `UNITS_GetObjectInteractType 0x34AD40`, zéro-étend le résultat byte, puis
  appelle `DATATBLS_GetLevelsTxtRecord 0x32C4A0`. Il lit ensuite la clé de nom
  du record à `+0xFD` et passe par `LANG_GetStringByKey`. Pour un portail vers
  Level 256, le lookup reçoit donc `0`, ce qui explique directement l'assert.
- Le caller serveur du paquet `0x60` à `0x5388CA` appelle auparavant
  `SUNIT_GetPortalOwner 0x490070`. Ce dernier retourne l'unité propriétaire
  stockée — le joueur — et non le portail réciproque, conformément à la
  référence sémantique D2MOO épinglée. Le code prend alors le Level ID de la
  room actuelle de ce joueur à `0x5388A8`, son low byte en R8B, et ses
  coordonnées en R9W/stack avant l'appel `0x5388E4`.
- Le builder `0x47F620` place séparément l'`InteractType` destination du
  portail à `packet[2]` et l'argument R8B, niveau de room du propriétaire, à
  `packet[0x0B]`. Ces octets peuvent légitimement différer. Le send hook 2.1.0
  les confond via `linkedLevelId == endpoint.nativeLowLevelId`; le receive
  hook renforce l'erreur via `packet[0x0B] == packet[2]`. Le refus frais du
  codec juste avant l'assertion est cohérent avec cette mauvaise invariant.
- Les handlers clients `0x51` et `0x60` décodent le full ID, restaurent X dans
  une copie puis appellent le handler stock, mais ne publient pas de sidecar
  client par GUID/session. Le consommateur `0xC188E` n'a donc aucun moyen de
  retrouver 256 après le store natif huit bits.
- L'entrée partagée `0x32C4A0` possède 44 callsites directs. Elle reste une
  mauvaise seam globale, notamment à cause de MapSense. La correction doit
  d'abord gouverner un sidecar client alimenté par `0x51/0x60`, puis recenser
  et rediriger seulement les consommateurs portail nécessaires, en commençant
  par l'appel UI `0xC188E`; les garde-fous stock et les autres callsites doivent
  rester byte-exacts.
- Ce gate runtime est **FAIL** et n'autorise aucune correction. La DLL 2.1.0,
  les fixtures et les sauvegardes ont été restaurées; aucun processus ne
  demeure. Les preuves locales sont sous
  `analysis-cache/extended-act-level-ids-v2/runtime/20260904-town-portal-1023-local-offline-2.1.0/`.

## 2026-09-04 — Potion Auto Pickup 2.0.0 stacking contract

- `ITEMS_GetMinStack` at `0x371AB0` resolves the concrete compiled
  `ItemsTxt` row and reads the `minstack` DWORD at `+0xEC`.
- `ITEMS_GetTotalMaxStack` at `0x3719E0` reads `maxstack` at `+0xF0`, adds
  item stat 254, and caps the effective result at the 9-bit limit of 511.
- `ITEMS_GetSpawnStack` at `0x372AD0` reads the `spawnstack` DWORD at
  `+0xF4`. Setting `minstack=1`, `spawnstack=1`, and the configured
  `maxstack` after `DataTablesLoaded` was the prototype's initialization
  strategy. It does not establish persistent quantity: the September 5
  compact-save audit below invalidates that earlier inference.
- `GetDataTablesForContext` at `0x300A90` accepts the four native context
  values `0..3`, while `DataTableServiceV1` exposes the three supported banks
  as Classic `1`, LoD `2`, and RotW `3`. BKVince gameplay has already been
  observed on RotW context `3`; a compiled-row mutation must therefore
  validate and cover all three banks `1..3`, not the non-authoritative range
  `0..2`.
- `ITEMS_GetAutoStack` at `0x36A850` returns the resolved `ItemTypesTxt`
  `AutoStack` byte at `+0x13`. Its two direct callers are the authoritative
  pickup/stack path (`0x475C5B`) and an item-interaction path (`0x53DE4B`).
- `INVENTORY_FindBackPackItemForStack` at `0x386E00` has ABI
  `(inventory, item, excludedItem) -> item`. It uses the native equality
  predicate at `0x375960`, reads `STAT_QUANTITY`, compares the result with
  `ITEMS_GetTotalMaxStack`, and returns only a compatible partial stack.
- `D2GAME_StackItemIntoInventory` at `0x4754C0` transfers quantity through
  native server synchronization and repeats until the source is exhausted
  or no compatible partial stack remains. `D2GAME_TryAutoStackPickedItem` at
  `0x4759E0` reaches it for generic items only after both native stackable and
  auto-stack gates pass.
- `ITEMS_CheckIfAutoBeltable` at `0x373D70` has ABI `(inventory, item) ->
  int32`; the pickup path calls it at `0x471C06`, then calls
  `INVENTORY_GetFreeBeltSlot` at `0x471C31`.
- `D2GAME_ExecuteItemUseEffect` at `0x581680` has ABI
  `(game, player, item, target) -> void`. Its only direct caller is
  `0x4F591E`, returning at `0x4F5923`. The routine decrements quantity only
  for Book type `0x12`; after it returns, caller byte `packet[1] == 0` takes
  the native retain-object branch at `0x4F592C -> 0x4F5A23`. Therefore a
  prototype attempted to consume a stacked potion by calling
  `SynchronizeItemAndBoundSkillQuantity` (`0x46F090`) with delta `-1` and
  clearing that byte only when the pre-use potion quantity is greater than
  one and the byte remains nonzero. This is not a validated consumption
  contract: the byte is an input removal flag, not an effect-success result,
  and player testing subsequently reported persistent belt icons.
- The complete-item path in save format 105 carries a quantity-present bit
  followed by a 9-bit value. This does not apply to compact potion items.
  The former recommendation to interpret any absent/zero `STAT_QUANTITY` as
  one was unsafe: only a positively identified legacy one-bottle object can
  be initialized. A zero read alone cannot distinguish absent, empty or failed
  access; the new policy keeps those states separate.

## 2026-09-05 — Potion Stacking refactor: policy pass, native integration open

The approved design keeps family limits local to belt slots and uses the
effective loader material limit outside belt. The pure `Stacking` contract in
Suite `plugins/potion-auto-pickup/src/policy.hpp` is intentionally not bound to
native adapters. It distinguishes a vacant destination from an existing empty
item, refuses ambiguous quantity reads and legacy objects pending migration,
and computes `min(source, destination room)` without truncating existing stacks
after a cap decrease. Tests enumerate 512 source values by 512 destination
values at six caps (1, 5, 10, 99, 255, 511), plus manual partial transfers,
bottom-up belt routing and storage overflow. They prove arithmetic and policy,
not native commit atomicity, a live counter or save compatibility.

### Persistence correction — new blocking evidence

- Read-only inspection through `scripts/build-data/tsv.js` of BKVince `misc.txt`
  found `compactsave=1` on the examined `hp1`, `hp5`, `mp5`, `rvs`, `rvl`,
  `vps`, `yps`, `wms` and `mfp` rows. These are source-table observations, not
  a fresh runtime memory capture. The prototype only patches `stackable`,
  `minstack`, `maxstack` and `spawnstack`, not `compactsave` or item flags.
- Native `0x377E30..0x377EA2` resolves the concrete item's compiled row through
  `0x314110` and returns its byte at `+0x153`; the pinned D2RL-Plugins layout
  names this field `nCompactSave`. It has ABI `(item) -> int32`.
- Outer serializer `0x37FFD0` loads existing ItemData flags at `+0x18` through
  `0x3800E0`. It calls `0x377E30` at `0x380061` and sets bit 21 when the table
  requests compact save (`0x38006A`). A false table value does not clear an
  already-set bit 21. The test at `0x3800D0` selects compact serializer
  `0x37CA20` via `0x3800D9`, or complete serializer `0x37D140` via `0x3800F0`.
- The complete serializer's quantity section reads stat 70 at `0x37EC3D`.
  The bounded compact path `0x37CA20..0x37D140` has no ordinary quantity-stat
  read; its separate advanced-stash payload does not turn it into a native
  quantity stack. D2SSharp's pinned implementation independently corroborates
  the split: `Item.cs:181-190` selects the format, `:213-271` reads a compact
  item without Quantity, and `:388-399` reads Quantity inside the complete
  path only. Pin: `f26f21897db5c0075e74defca1e31d1930080750`.
- Consequently, changing `maxstack` and adding stat 70 alone does not prove
  that potion stacks survive serialization. Changing only the table's compact
  flag is also insufficient for existing items. A controlled compact-to-complete
  migration (flags, valid full-item fields, client synchronization and rollback)
  must be designed and proven before implementing it. No serialization hook,
  table rewrite, item-flag mutation or save conversion was executed here.
- This is a demonstrated persistence defect in the prototype's assumptions,
  not proof that any particular player's saved potions have already been lost.
  The causal contribution to the reported zero tooltip/ghost icon still needs
  runtime observation of the concrete item before and after consumption.

### Advanced Stash client path — not a proven server conversion

- Existing `CLIENT_TransferItemToInventoryPage 0x15F8B0` has a dedicated branch
  for an auxiliary owner with state `0xD1`, distinct from ordinary packet
  `0x54`. At `0x15FA95` it checks the item's Advanced Stash registration.
  The path at `0x160016` calls helper `0x159A30` with `(item, destination)`.
- `0x159A30..0x159AB5` resolves class and source/destination unit IDs. Its call
  at `0x159AA5` passes opcode `0x63`, operation 0, source item ID, destination
  owner ID and three zero DWORDs to `0xEC880`. That builder queues exactly
  25 bytes via `CLIENT_QueueOutgoingPacket` at `0xEC8D0`; opcode is at +0,
  followed by the six DWORD arguments at +1, +5, +9, +13, +17, +21.
- This proves only the outgoing deposit request, not how the server accounts
  for an ordinary quantity stack. The authoritative deposit/withdrawal handlers,
  effective material-cap reader, partial credits/debits and synchronization
  remain open. The native Advanced Stash counter must not be replaced with
  an ordinary stat-70 read or charged one bottle per multi-bottle item.
- The installed loader log at 2026-09-05 12:39:31.956 explicitly reports
  `d2rcore.stash.set_materials_limit = 255`. The inspected D2RCore.dll SHA-256
  is `2130A98D0B879696116A7DDDE5C11AE8C91942B54B8276DB43E02074A715BBC8`,
  the promoted 1.2.1 artifact. The log is evidence for that run's setting,
  not a supported getter for a portable plugin; no D2RCore private field is
  consumed by the new policy.

### Consumption and display — verified facts versus missing contracts

- `D2GAME_HandleUseItemPacket 0x4F40C0` stores input R8 at `[rsp+0x78]` at
  `0x4F4105`. At `0x4F4196..0x4F41A9` it compares that input's byte +1 to a
  property-derived removal flag returned by `0x308E80`, before executing any
  item effect. It is not an effect-success output. `0x581680` calls the effect
  callback at `0x58178D`; the boolean return is checked locally but not exposed
  to the prototype's void hook as a success result. A valid replacement must
  preserve pre-effect item-state validation and prove post-effect debit/removal.
- `ITEMS_BuildQuantityDescription 0x2C1E50..0x2C1F54` has observed ABI
  `void(item, wideCharacterAppendBuffer)`. It reads stat 70, obtains the total
  max stack and appends localized `ItemStats1i` plus a separator. Its four
  direct callers are `0x2BD82B`, `0x2BE124`, `0x2BE822`, `0x2BEDF0`.
  The strict first 32 bytes are unique:
  `48 89 5C 24 20 56 48 81 EC 40 01 00 00 48 8B 05 64 94 70 02 48 33 C4 48 89 84 24 30 01 00 00 45`.
  This is a scoped candidate for omitting managed-potion quantity tooltips;
  it is not the icon-counter renderer and was not hooked in this lot.
- The new `ShouldDrawCounter` policy includes a real quantity of one and
  excludes unknown, empty and uninitialized quantities. It excludes Advanced
  Stash to preserve the existing native count. Its test is not a rendering
  implementation or a visual qualification.

### Query provenance and limits

Commands: `re:d2r33 status`, `known`, `function`, `xrefs`, `bytes`; pinned
`ref:d2rlplugins status` and `ref:d2ssharp status/search`; read-only Capstone
windows through the existing `d2r32.py` image helpers after SHA verification.
The `.pdata`-derived containing-function query splits several relevant logical
bodies mid-instruction (for example `0x15F9C4` starts after a REX byte) and the
lazy Ghidra query of `0x15F8B0` returned a preceding entry `0x15F723`. That
decompilation was not used as ABI evidence; bounded continuous disassembly
from proven entry/call targets was used instead. No binary was redumped or
reimported; the local index is refreshed for the two new governed names.
No new native mutation, game launch or runtime deploy
was performed. Baseline 1.2.2 remains a candidate, not an integration claim.

## 2026-09-04 — Extended Act Level IDs : sidecar client Town Portal

- Le census direct de `UNITS_GetObjectInteractType 0x34AD40` compte 28
  lecteurs. Les seuls lecteurs clients sont `0x9A39E` (test du bit de signe
  pour le verrouillage), `0x1CB118` (index de shrine) et `0xC1882`. Ce dernier
  est le seul qui alimente `DATATBLS_GetLevelsTxtRecord 0x32C4A0`, à l'appel
  exact `0xC188E`; aucune autre lecture client `Levels` issue de l'InteractType
  byte n'est présente dans le corpus commun.
- Le helper UI à `0xC17E0` reçoit `(outputString, Unit*)`, conserve l'unité en
  RSI, récupère sa classe et son contexte, exige `ObjectsTxt+0x127 & 4`, puis
  appelle le getter byte et le record `Levels`. Le motif de contexte
  `48 8B CE E8 B9 94 28 00 0F B6 D0 40 0F B6 CF E8 0D AC 26 00 48 C7 C3 FF FF FF FF`
  n'a qu'une occurrence à `0xC187F`. Après `0xC188E`, le record non nul fournit
  la clé de nom à `+0xFD`; un record nul emprunte le fallback stock string ID
  `0x150D`.
- `CLIENT_HandlePacket0x51_ObjectSpawn 0x129D70` lit GUID `+2`, classe `+6`,
  X/Y `+8/+0x0A` et InteractType `+0x0D`, puis appelle
  `CLIENT_CreateObjectFromPacket 0x99510` à `0x129DD8`. Ce helper crée l'objet
  puis écrit explicitement le byte réseau par
  `UNITS_SetObjectInteractType 0x34E9D0` à `0x99582`.
- `CLIENT_HandlePacket0x60_PortalState 0x1CB1C0` résout le GUID `+3` par
  `CLIENT_GetUnitByIdAndType 0x9A5D0` avec le type objet `2`, écrit la
  destination low byte `+2`, puis conserve séparément les coordonnées
  propriétaire `+7/+9` et son niveau de room `+0x0B`. Ce dernier byte n'est
  ni une seconde destination ni un témoin d'égalité.
- Les 30 xrefs du setter n'exposent que trois écritures clientes : `0x99582`,
  `0x1CB1E9` et `0x1CB5D3`. La troisième est dans le constructeur d'objet à
  `0x1CB410`, où les classes 59/60 reçoivent une valeur initiale avant que le
  helper de création réseau n'applique le byte du paquet. Elle ne justifie pas
  un hook supplémentaire.
- Le contrat statique retenu ajoute un sidecar client distinct de la paire
  serveur : `{sessionGeneration, portalGuid, destinationLevelId,
  nativeLowLevelId}`. Les handlers `0x51/0x60` déjà possédés restaurent X et
  délèguent d'abord au stock, puis publient une copie immuable. `0x51` évince
  toujours le même GUID avant un spawn classe 59; `0x60` revalide immédiatement
  l'unité vivante, sa classe, son GUID et son low byte avec `0x9A5D0`. Aucun
  `Unit*` n'est conservé.
- Le sidecar doit être limité à 1024 entrées, effacé à chaque génération de
  session et empoisonné sur overflow, allocation ou incohérence. Le relay exact
  `0xC188E` peut placer RSI dans R8 puis tail-jumper vers un résolveur qui
  n'emploie le full ID que pour une entrée classe 59 parfaitement concordante
  et connue dans le contexte courant. Le chemin nul stock évite l'assertion en
  cas de `low=0` sans entrée ou de contrat empoisonné; les objets ordinaires et
  portails vanilla gardent l'appel original.
- Hooker globalement `0x32C4A0`, élargir ObjectData/les paquets ou ignorer
  l'assertion restent rejetés. Le callsite relay est le seam au plus petit
  rayon, conserve MapSense et les 43 autres callers, et réutilise sans nouveau
  propriétaire les hooks clients `0x51/0x60` de la DLL.
- Références : D2MOO épinglé
  `19019806df7f3e877fa105b05395d1e3597e2316` pour la sémantique uniquement;
  D2RL-Plugins épinglé `dc75b49ffbb67b887d7757ee00ee9a03bcde5d8a`
  sans propriétaire concurrent trouvé. Le gate est un PASS statique read-only;
  implantation et runtime demeurent séparés.

## 2026-09-04 — consolidation publique du corpus natif gouverné

- Le checkpoint compare le registre public antérieur `0dd02e7f` au corpus
  consolidé : 180 nouvelles identités natives sont promues, dont 177 en
  confiance `high` et 3 en confiance `medium`. Elles couvrent 101 fonctions,
  37 patch-sites, 23 call-sites, 14 témoins de layout, 3 anciens objets
  `callsite` et 2 données.
- Les 180 identités satisfont le contrat minimal du registre : `rva`, `name`,
  `kind`, `confidence`, `source` et `notes` sont présents et non vides; chaque
  RVA respecte la syntaxe hexadécimale gouvernée et chaque note conserve une
  description technique substantielle. Aucun objet à confiance `low` n'est
  promu par ce checkpoint.
- L'audit de provenance a trouvé 118 citations non publiques réparties sur 72
  entrées : chemins `analysis-cache`, missions encore non versionnées et
  contrats de plugins encore locaux. Ces citations ont été retirées du
  registre public et remplacées par le présent `findings.md`, qui conserve les
  conclusions compactes par chantier. Les missions déjà versionnées et les
  références externes épinglées restent citées.
- Après normalisation, aucune entrée du registre ne dépend d'un chemin local ou
  non suivi pour sa provenance publique. Les images, index SQLite, projets
  Ghidra, clones, logs et corpus bruts restent volontairement sous
  `analysis-cache` et ne sont pas distribués.
- Quatre identités apparaissent dans plusieurs objets avec des sources et notes
  de consommateurs distinctes : `DATATBLS_GetObjectsTxtRecordCount`,
  `DATATBLS_GetObjectsTxtRecord`, `UNITS_GetUnitType` et
  `INVENTORY_ResolveOccupancyGrid`. Elles ne sont pas fusionnées par ce
  checkpoint afin de ne pas perdre les contrats spécifiques déjà consignés;
  leurs RVA, noms et kinds concordent exactement.
- Cette consolidation est une promotion documentaire et statique du corpus
  commun 3.2.92777/3.3.93847. Elle ne remplace ni les empreintes fail-closed de
  chaque plugin ni leur qualification runtime. Steam 3.3.93787 reste un
  candidat admissible non qualifié tant que les surfaces réellement utilisées
  ne sont pas prouvées byte-exact.

## 2026-09-04 — paquet Town Portal 0x60 et caller client 0xFE30C

- Le gate read-only part du runtime 2.1.1 : le plugin a refusé un paquet
  `0x60` « outside the local codec contract », puis
  `DATATBLS_GetLevelsTxtRecord 0x32C4A0` a asserté avec une adresse de retour
  `0xFE30C`. Aucun plugin, table, profil runtime, processus ou save n'a été
  modifié pendant le census.
- Le contrôle d'identité client n'est pas la cause démontrée du refus.
  `D2Client+0x270` est bien un GUID de joueur : le témoin unique `0x537CAC`
  le transmet comme tel, et d'autres chemins natifs l'emploient avec le type
  `D2Client+0x26C` pour résoudre l'unité serveur. En local/offline, ce GUID est
  le même contrat que `LocalPlayerReady.playerId` déjà validé lors de la
  création du portail.
- Le codec de coordonnées 2.1.1 est en revanche structurellement insuffisant.
  Il réserve les bits X `15..13` et exige donc `X <= 8191`. La fixture jouée
  Level 256 possède `OffsetX=3300`, `OffsetY=2400`, `SizeX=40` et
  `SizeY=52`. La référence sémantique D2MOO épinglée initialise les coordonnées
  de room depuis `nTileXPos/nTileYPos` dans `DrlgDrlg.cpp:512-522`, puis
  `DUNGEON_GameTileToSubtileCoords` multiplie X et Y par cinq dans
  `D2Dungeon.cpp:1323-1326`. Toute coordonnée X de cette fixture tombe donc
  dans `[16500,16700)`, hors codec. Le refus 0x60 est inévitable lorsque le
  propriétaire est encore dans cette room; cacher les bits hauts dans X doit
  être abandonné pour Town Portal.
- Le builder `D2GAME_PACKETS_SendPacket0x60_PortalState 0x47F620` reçoit
  `(D2Client*, portal Unit*, ownerRoomLevelLow, ownerX, ownerY)` et produit
  exactement 12 octets. Le caller principal conserve le Level ID complet dans
  R12D à `0x5388E4`; le caller secondaire le conserve dans EDI à `0x593012`.
  Les deux ne placent que R12B/DIL en R8B. Leurs contextes stricts à
  `0x5388CA` et `0x592FFA` n'ont chacun qu'une occurrence. Ces callsites
  permettent de publier le full owner-room ID dans un sidecar local sans
  modifier le paquet ni ses coordonnées.
- `CLIENT_HandlePacket0x60_PortalState 0x1CB1C0` écrit le byte paquet
  `+0x0B` à `Unit+0x1BA`; le témoin `0x1CB23A` est unique. Ce champ est le
  low byte du niveau courant du propriétaire et reste distinct de la
  destination `ObjectData.InteractType` provenant de `packet[2]`.
- La fonction `0xFE1F0` est le prédicat client de disponibilité d'un Town
  Portal avec ABI observée `(localPlayer Unit*, portal Unit*) -> bool`. Son
  entrée stricte de 32 octets est unique. Elle exige un joueur et un objet
  type `2`, classe `59`, valide propriétaire ou parti, récupère le contexte
  de données du joueur, puis relit `Unit+0x1BA` pour
  `DATATBLS_GetLevelsTxtRecord` à `0xFE307` et
  `DATATBLS_GetLevelDefRecord` à `0xFE333`. Elle conserve ensuite les gardes
  Act online et quêtes natives. Pour Level 256, le byte zéro explique
  exactement l'assertion retournant à `0xFE30C`.
- Les contextes des deux calls, à `0xFE2FC` et `0xFE328`, sont uniques. La
  seam minimale est donc deux relays exacts qui transmettent RBX comme portail
  vivant à un résolveur, récupèrent seulement un full owner-room ID validé par
  `{session, GUID, classe, low byte, contexte, record}`, puis rappellent les
  getters originaux. L'entrée partagée `0x32C4A0`, la logique propriétaire,
  le parti, les restrictions online et les quêtes restent inchangés.
- Le mécanisme recommandé pour le prochain gate d'implantation est un sidecar
  process-local par GUID, alimenté avant l'envoi stock depuis les deux
  callsites qui possèdent encore le DWORD complet. Les wrappers clients
  `0x51/0x60` gardent l'éviction et la revalidation du portail vivant, mais ne
  décodent plus X; les paquets restent byte-exacts vanilla. Cloner entièrement
  `0xFE1F0`, hooker globalement les getters ou employer un faux record proxy
  sont rejetés pour leur rayon ABI, leur collision MapSense ou leur sémantique
  Act/quête incorrecte.
- Verdict : **PASS statique read-only**. Les identités stables sont promues
  dans `known-rvas.json`. Une implantation, un build et tout nouveau runtime
  exigent des gates séparés; save/reload, waypoint, automap et réseau restent
  ouverts.

## 2026-09-04 — implantation Town Portal 2.1.2 process-local sans codec

- Le gate autorisé implante les seams issus du census sans nouvelle hypothèse
  native. Le codec de coordonnées reste réservé à la visibilité des rooms
  `0x07/0x08`; les builders et handlers Town Portal `0x51/0x60` reçoivent les
  coordonnées et champs stock inchangés.
- Les calls `0x5388E4` et `0x593012` sont redirigés par relays exacts. Le
  premier transporte R12D et le second EDI comme sixième argument privé vers
  le publisher sidecar, tout en conservant client, portail, low byte, X et Y
  dans l'ABI stock du builder `0x47F620`.
- Les relays `0xFE307/0xFE333` exécutent `mov r8,rbx` avant les résolveurs
  scoped. Ceux-ci exigent portail type 2/classe 59, GUID, destination low,
  owner-room low, génération, contexte et record cohérents avant de substituer
  le full owner-room ID. Ils ne modifient ni les getters partagés ni le reste
  du prédicat natif `0xFE1F0`.
- La revalidation d'unicité donne une occurrence exacte pour chacun des
  contextes `0xFE1F0`, `0xFE2FC`, `0xFE328`, `0x1CB23A`, `0x5388CA` et
  `0x592FFA`. L'audit ne trouve aucun propriétaire concurrent dans les autres
  sources RuffnecKk, les patches/configs BKVince ou la référence eezstreet
  épinglée `dc75b49ffbb67b887d7757ee00ee9a03bcde5d8a`.
- Deux builds Release indépendants sont byte-identiques : 91 648 octets,
  SHA-256
  `0710A1FFB1442115F5F8236BFDAB5393F3B12D7CA38665600F73BC881189FEF8`.
  Verdict : **PASS STATIQUE** pour l'implantation; aucune conclusion runtime
  n'est revendiquée.

## 2026-09-05 — Doll Explosion 0.1.0, mort complète et transporteur natif sûr

- La couture initialement candidate à `0x44535F` est rejetée pour les sept
  Dolls classiques : l'entrée de mort `0x444F50` résout le MonStats contextuel
  à `0x44501F`, lit le byte `+0x3E` et saute toute la branche d'explosion à
  `0x44504F` lorsque `deathDmg` est absent. Les IDs BKVince 212, 213, 214,
  215, 216, 690 et 691 ont ce champ vide; un hook du seul callsite serait donc
  inerte. Le bit `deathDmg` est le bit 20 du DWORD de flags, soit `0x10` dans
  le byte `+0x3E`, corroboré sémantiquement par D2MOO
  `19019806df7f3e877fa105b05395d1e3597e2316` sans transposition d'ABI.
- L'entrée `0x444F50` possède une signature unique de 48 octets et l'ABI
  observée `int32(game, modeChange)`, avec l'unité mourante à
  `modeChange+8`. Son découpage PDATA reste fragmenté et n'est pas employé
  comme borne logique. La DLL appelle l'original exactement une fois puis
  planifie son effet. Elle revalide type/classe, l'état Revive 96, contexte,
  difficulté, record MonStats, chemin et coordonnées avant cette mutation.
- Le callsite unique `0x445322..0x445370` ferme les onze arguments de
  `CreateSkillMissile 0x4333F0` : `(game, missileId, owner, skillId,
  skillLevel, nextCounter, originX, originY, targetX, targetY, check)`. La
  fonction possède 24 appels directs; Doll Explosion ne la hooke pas et exige
  sa signature d'entrée unique.
- Le transporteur retenu est `baalcorpseexplodedelay`, missile 587. Sa ligne
  UTF-8 sans EOL, SHA-256
  `EE37A176A002DF6F7CF83D36BA0E4EEBA46D34DDB6438C69D47CC881E9FE858C`,
  est identique dans vanilla 3.2, vanilla 3.3 et BKVince : `pSrvDo=1`, aucun
  `pSrvHit`, `Range=10`, aucun CelFile et aucun dommage serveur. Le candidat
  `armageddoncontrol` 577 est rejeté car son `pSrvHit=56` peut produire un
  effet offensif si le sidecar disparaît.
- `DATATBLS_GetMissilesTxtRecordForContext 0x166040` expose `pSrvDo` WORD à
  `+0x2C` et `pSrvHit` WORD à `+0x2E`. Le dispatcher `0x466CE0` indexe la
  table live `0x2380E80`; son callsite unique `0x466E46` retire le missile
  lorsque le callback renvoie 2. L'entrée 1 doit toujours résoudre le handler
  basique `0x455750`, qui tail-jumpe vers `0x466B40` à `0x45584C`.
- Le gestionnaire générique `0x466B40` possède une signature unique de 32
  octets, l'ABI `int32(game, missile)` et exactement deux xrefs natifs : le
  tail-jump `0x45584C` et l'appel `0x457B44`. La DLL appelle son trampoline
  avant toute décision; un retour différent de 2 conserve le sidecar, et un
  retour 2 déclenche l'explosion avant que le dispatcher retire le missile.
- Les témoins uniques `0x3BB18A` et `0x3BC411` ferment `Unit+0x10` comme
  MissileData, sa durée totale signée 16 bits à `+0x10` et son compteur courant
  flottant à `+0x14`. La création native initialise les deux compteurs avec la
  même portée : le constructeur appelle `MISSILES_SetTotalFrames 0x3BDBC0`
  puis `MISSILES_SetCurrentFrame 0x3BD450`, tandis que le callback générique
  lit le compteur courant par `MISSILES_GetCurrentFrame 0x3BB1E0`, le décrémente
  puis le réécrit avant de terminer à zéro. `MISSILES_GetTotalFrames 0x3BC3E0`
  fournit la vérification indépendante du WORD. Une instance 587 n'est
  revendiquée qu'après vérification du type 3, de la classe, du GUID et des
  deux valeurs initiales à 10; la DLL règle ensuite les deux valeurs par ces
  quatre helpers fingerprintés et les relit avant d'armer le délai configuré.
- Le visuel `monstercorpseexplode` 117 est lui aussi identique dans les trois
  tables, SHA-256 de ligne UTF-8 sans EOL
  `F3E1010E5B7FF8B47B02ACFD89073AA57989047D56AD2266B4A66078CEC37146`.
  La DLL exige son record compilé `pSrvDo=1/pSrvHit=0` et une instance type 3,
  classe 117 avant le dommage.
- L'AoE `0x44A120` possède cinq appels directs. Le callsite vanilla unique de
  mort à `0x4455D2` ferme dix arguments : `(game, missile, x, y, radius,
  D2Damage*, 0, 0, null, 0x581)`. Le record fait 0x180 octets, le physique
  réside à `+0x18`, le stockage inline à `+0x40/+0x48/+0x50`, le sentinel
  possédé à `+0x158`, et le destructeur gouverné reste `0x4496E0`.
- Les 19 nouvelles signatures d'entrée, callsites, helpers et témoins ont chacune une
  occurrence exacte dans l'image canonique. Aucun propriétaire concurrent des
  entrées `0x444F50` et `0x466B40` n'est trouvé dans les sources RuffnecKk,
  les patches BKVince ou D2RL-Plugins
  `dc75b49ffbb67b887d7757ee00ee9a03bcde5d8a`.
- Verdict : **PASS STATIQUE** pour les coutures et l'ABI de l'incubation.
  Délai frame-exact, attribution, corps consommé, résistances, player count,
  TCP/IP, unload et coexistence pile complète demeurent explicitement des
  gates runtime.

## 2026-09-05 — Potion Stacking : migration native et frontière Loader du stash

Vincent confirme que `qtytester` est son testeur et accepte la perte de potions;
le retour sans plugin conservant les quantités n'est plus un gate du prototype.
Cette précision ne qualifie ni la lisibilité des saves ni le gameplay. Aucun
fichier de personnage n'a été modifié et aucun processus n'a été relancé.

### Format et première intégration source

- `0x37FFD0..0x3801C5` est le sérialiseur interne appelé par le wrapper
  `0x375EE0` et récursivement à `0x380159`. Son ABI observée conserve item,
  bit-writer, server, saveSockets, gamble et un sixième argument opaque. Le
  wrapper et la récursion transmettent les deux arguments de pile. Le retour
  est un entier 32 bits; le contrôle d'overflow reste dans le wrapper.
- La capture des flags `ItemData+0x18` et l'écriture des flags précèdent le
  branchement compact/complet à `0x3800D0`. Le hook de développement est à
  l'entrée, avant toute capture ou écriture de bits, jamais au milieu d'un
  item déjà sérialisé. Le témoin unique de 54 octets à `0x3800BF` ferme le
  branchement, les appels et la transmission de l'argument opaque.
- Le getter `0x377E30` lit `compactsave` compilé à `+0x153`; le témoin
  `0x377E90`, `0F B6 80 53 01 00 00`, est unique. Le hook initialise à un
  uniquement un item compact géré, quantité zéro et compteur Advanced Stash
  nul; il relit la quantité puis efface seulement le bit compact 21. Une
  quantité complète déjà nulle reste nulle/refusée. Une quantité compacte
  déjà positive conserve sa valeur. Les lignes compilées passent aussi à
  `compactsave=0`; leur restauration conditionnelle porte désormais cinq
  champs, pas quatre. Il n'y a aucune modification des TXT.
- Le témoin unique de 64 octets à `0x37EC1A` ferme stackable `+0x142`, la
  présence, stat 70 et les neuf bits de quantité de la forme complète. La
  fonction bornée `0x34A500..0x34A535` ferme `Unit+0x10 -> ItemData`.
- Le hook `0x2C1E50` omet l'appender natif entier pour les seules potions
  gérées. Aucun parsing/localisation ni rendu de compteur n'est ajouté.
- Les sept tableaux d'octets nouveaux du jeu ont été comparés indépendamment
  aux octets de l'image d'analyse vérifiée : taille déclarée exacte, égalité
  byte-exact et une seule occurrence chacun. Les deux nouveaux hooks sont
  inscrits dans le manifeste d'écritures du produit.

### Preuve du compteur et du plafond réellement utilisés par le Loader

Diagnostic strictement read-only du processus BKVince existant, PID `29364`,
image de jeu à `0x140000000`, D2RCore à `0xC0DE5000000`. Ces adresses et ce PID
sont des observations de cette session, jamais des constantes d'implantation.
Artefact D2RCore de la baseline promue, SHA-256 diagnostique
`2130A98D0B879696116A7DDDE5C11AE8C91942B54B8276DB43E02074A715BBC8`.

- Le getter natif `0x46D9A0..0x46D9B4` renvoie un **byte** de
  `ItemData+0x9C` et n'est pas patché à son entrée. Ses treize xrefs natifs ne
  suffisent donc pas à décrire le comportement du Loader.
- Le call du widget de stash à `0x2CE509` pointe réellement vers le relay
  `0x143E2A39C`, dont `FF 25 disp32` lit `0x143E29B38` et résout
  **D2RCore RVA `0x3146F0`**. La destination rel32 de l'image d'analyse à ce
  callsite est différente : il faut résoudre la chaîne active, pas exiger
  l'adresse de relay d'une ancienne session.
- Cette fonction Core complète de 89 octets, `0x3146F0..0x314749`, appelle
  `UNITS_GetItemData` par l'import Core `+0x5A8E00`, puis lit un **DWORD** à
  `ItemData+0x9C`, pas un byte. Elle compare ce compteur au DWORD Core
  `+0x5A7530` et produit une valeur adaptée aux branches natives `99` du
  widget. Son retour n'est donc pas un compteur brut à exposer au joueur.
- Core `0x314680..0x3146C6` confirme indépendamment la comparaison DWORD
  compteur/plafond; Core `0x10D220` renvoie ce même plafond par un getter de
  sept octets. Le chargement de configuration écrit le champ source `+8`
  vers cette globale à `0x10AF1A`. La lecture live vaut **255**, cohérente
  avec le log `d2rcore.stash.set_materials_limit = 255`.
- Le merge ancien à `0x4FAB35` conserve pourtant son immédiat `99` dans ce
  processus : cet immédiat ne peut pas servir de lecteur de limite effective.
- La source du plugin vérifie les 89 octets Core exacts et uniques, la chaîne
  active call/relay/pointeur, l'import ItemData, les allocations/protections
  et la plage lisible du plafond **avant les hooks**. `VirtualQueryEx` en
  lecture seule confirme les plages de cette instance. Aucun code Core
  n'est appelé ou modifié et aucune version/hash n'autorise le chargement.
  La présence d'une fonction dans Core sans cette chaîne active ne suffit pas.
- La migration examine désormais le DWORD complet. Les compteurs `256`,
  `65536` et `0xFFFFFFFF` doivent rester des payloads de stash, jamais des
  zéros ordinaires. Ce garde-fou est testé. Le pointeur de plafond est
  résolu mais **n'est pas encore branché aux transferts ni au TOML**.

### Rendu, consommation et limites de cette preuve

Le widget de stash construit un sous-widget natif `+0x618`, taille `0x230`,
par `0x2CDE10`, puis appelle sa méthode draw `vtable+0x18` à `0x2CE8CD`.
La vtable live `0x1CF4610` résout cette méthode à `0x86D410`. Le renderer,
son texte, son layout et son cycle de vie dans inventory/belt restent des
candidats; aucune construction de widget ni surimpression n'est implantée.

Le dispatcher d'effet `0x581680` appelle son vrai callback à `0x58178D` et
teste son booléen AL à `0x581795`. Le byte d'entrée testé par le handler à
`0x4F592C` n'est toujours pas ce résultat. L'ancien hook de consommation
n'est pas qualifié par cette preuve; il reste à remplacer après fermeture du
batch de mutations, de la synchronisation et des suppressions de dernière
bouteille. La nouvelle normalisation a seulement été placée avant l'effet.

Build Release DLL et tests : **PASS**, CTest ciblé **2/2 PASS**. Les
1 572 864 scénarios arithmétiques restent couverts, avec migration unique,
non-résurrection, payload DWORD et refus d'une destination belt existante à
zéro. SHA-256 DLL locale de développement :
`20EEE2B821B2E38506C7FA0FE91BB27F5B141D8DD76E0CCA16C4937D3BCBD40C`.
Audit ciblé : 25 plages Potion, 4 450 comparaisons, aucun chevauchement déclaré.
L'audit global reste en échec sur l'absence de Shadow Master AI dans le
manifeste, hors de ce lot; aucun verdict de compatibilité pile complète.

**Lot partiel, non déployé et non livrable.** Restent : plafonds effectifs par
destination, split/transfert manuel vers belt, conversion dépôt/retrait du
stash avancé, consommation, compteur natif, découverte des potions du mod
actif, save/reload et gameplay/TCP-IP. La preuve du format ne ferme pas ces
gates et n'autorise pas une promesse de saves sans corruption.

## 2026-09-05 — MapSense 1.0.2 r3 : admission GPS marche et Téléportation

Le `GO` de Vincent ouvre une preuve hors jeu pour les deux modes, dans le
niveau courant. `scripts/reverse-engineering/mapsense-gps-native-audit.js`
vérifie l'image canonique SHA-256
`CC59119DC2A6C7D43D088098FC162EAFA4AE1299B2079126AEF43C1ACA914715`,
21 témoins uniques, 19 relations CALL et l'entrée joueur du sélecteur de
taille. Les identifications stables sont promues comme témoins limités dans
`known-rvas.json`; aucun hook ni ABI complet n'est qualifié.

| Surface | Preuve statique | Limite |
| --- | --- | --- |
| `0x4ACED6` → `0x4FE1F0` | Limite de commande 50 par axe, inclusive | Ni viewport ni succès de cast |
| `0x554C74` | Lecture Levels+0x0E : 0 refuse, 2 contrôle `0x804`, autres non-zéro passent à la relocation | Identité Teleport candidate; slot callback 27 initialisé non prouvé |
| `0x350A20` → `0x351720` → `0x363F20` | Source et cible, tailles, ajustement des extrémités et appel du traceur | Parcours exact et frontières non qualifiés |
| `0x491D66` → `0x362F90` | Placement avec taille de l'unité et masque `0x1C09` | Recherche libre pouvant déplacer l'arrivée |
| `0x348206`, table `0x34829C` | Type joueur 0 sélectionne retour taille 2 à `0x34822A`; accessor cache `0x34B580` relié | Sélecteur de motif, pas rayon deux |
| `0x364DD3` → `0x366920` | Taille 2 sélectionne une croix; centre et quatre voisins attestés par trois témoins | Marche complète, branches rapides et frontières restent à qualifier |
| `0x38DFB0` → `0x2EFDE0` | Recherche de coordonnées dans room courante puis sa liste native | Ne prouve ni le constructeur ni l'équivalence des near-room générées |

`D2MOO@19019806df7f3e877fa105b05395d1e3597e2316`,
`source/D2Game/src/SKILLS/SkillSor.cpp:874-898` et
`source/D2Common/src/Units/Units.cpp:2768-2860` ont fourni les indices
sémantiques; les adresses et appels viennent de l'image D2R vérifiée. Les
fragments PDATA/Ghidra ne sont pas utilisés comme frontières logiques fiables.

Le probe Suite `plugins/mapsense/mapgen/src/gps_proof.zig` passe 600 routes
sur 300 fixtures du terrain libd2 épinglé, sans erreur du modèle ponctuel.
Le contrôle séparé de croix relève 1 709 visites sans dégagement sur 68 routes
de marche et 459 sur 105 routes Téléportation. Ses 2 724 sauts comprennent
60 sauts soumis à la règle de trace non reproduite par le routeur épinglé.
Le résultat porte `gpsAdmitted=false`, `d2rRuntimeCompared=false`.

Le parseur World lit Teleport/Objects embarqués au lieu du contexte mod actif.
`mapsense-gps-data-audit.mjs` confirme zéro différence Teleport sur les 137
IDs communs mais dix IDs BKVince supplémentaires hors table embarquée.
Les entrées sont round-trippées byte-exact en mémoire, sans écriture TSV.
La mission et `suite:plugins/mapsense/mapgen/GPS-PROOF.md` conservent la matrice,
ses limites et le prochain lot : adaptation, comparaison au terrain D2R,
puis intégration. Aucun runtime, déploiement, nouveau paquet réseau ou
changement de sauvegarde dans ce lot.

## 2026-09-05 — Potion Stacking : décision de retrait avant batch et vrai résultat d'effet

Continuation autorisée après « prêt à tester », puis « continue ». Aucune
instance D2R/D2RLoader n'était active au relevé initial; aucun processus n'a
été lancé, aucune DLL runtime remplacée et aucune save écrite. La source
autoritaire reste `suite:plugins/potion-auto-pickup/`. La baseline promue
Loader 1.2.1 / SDK API 3 `4933e2c42cb2592958cd0df3b6dc5003102252d1` reste
utilisée; la candidate 1.2.2 n'est pas promue par ce travail. Le pin PluginPack
`dc75b49ffbb67b887d7757ee00ee9a03bcde5d8a` a été revérifié propre.

### Preuve de rétention, indépendante du résultat de l'effet

L'ancien contournement modifiait le byte de paquet après le batch d'état :
il ne pouvait pas annuler proprement un détachement déjà préparé. Il est
supprimé de la source, ainsi que la lecture de pile et `_ReturnAddress`.

- `ITEMS_ShouldRemoveOnUse 0x308E80..0x308F77` retourne **AL booléen**,
  ABI `(item)`. Le Book `0x12` retourne faux; les autres items résolvent
  leur descripteur d'utilisation et testent son byte `+8`. Onze appels
  directs : `1623C8`, `1624EA`, `1C7BD7`, `1C7C8E`, `229652`, `2451A0`,
  `2C7706`, `2C7794`, `2CAEF1`, `4D9F6D`, `4F4196`.
- Le builder `0xED1E0..0xED2E1` émet le paquet `0x26`, taille 36.
  Son argument CL devient le flag `+1`. Les trois entrées de suivi belt
  sont initialisées à `FF/0/0`; leur copie n'a lieu que lorsque le flag est
  vrai et le mode R8d vaut 2 (`0xED26E`). Les calls client `0x1C7C11` et
  `0x1C7CDB` reçoivent le booléen du prédicat partagé.
- Le handler serveur `0x4F40C0` résout l'item du paquet puis appelle le
  même prédicat à `0x4F4196`. `0x4F419B..0x4F41AE` compare AL au flag
  du paquet et refuse une divergence. Le nouveau hook ne contourne pas
  cette validation, notamment si client et serveur ne voient pas le même
  dernier item.
- Flag faux : `0x4F426F` évite la préparation de suppression/décalage
  belt. Après lecture de l'état courant, `0x4F4830` copie cet état vers
  l'état désiré : le batch natif conserve donc le même emplacement.
- Après l'effet appelé à `0x4F591E`, `0x4F592C` laisse passer les updates
  de retrait et la suppression `0x43EC10` seulement lorsque le flag est
  vrai. La dernière bouteille suit ce chemin **sans débit plugin à zéro**.

Implantation : hook SDK de cinq octets à `0x308E80`, qui appelle d'abord
l'original. Il remplace vrai par faux seulement pour une potion gérée,
ordinaire, sans payload Advanced Stash DWORD, de quantité connue 2..511.
Il ne modifie ni stat client ni paquet. Une seule bouteille, une quantité
inconnue/zéro ou un item non géré conserve la décision native.

### Observer l'effet et synchroniser seulement un succès prouvé

Dans `0x581680..0x5817E4`, les arguments du vrai callback sont préparés à
`0x581778` : RCX game, RDX player, R8 item si spellicon vaut FF, sinon target.
`0x58178D` est `41 FF D1` (CALL R9), puis `4C 8B 74 24 58` restaure R14;
`0x581795` teste **AL**, le résultat réel. Le R14 initial avait été sauvé
à `0x58171A`. Le callback a l'ABI `bool(game, player, effectTarget)`.

La source remplace exactement ces huit octets par CALL rel32 + trois NOP
explicites, via une seule opération transactionnelle SDK `PatchBytes`.
Le relais proche de 19 octets contient :

```text
4C 8B 74 24 60             mov r14,[rsp+60h]
FF 25 00 00 00 00         jmp qword ptr [rip]
<observer address u64>
```

Le nouvel appel a ajouté une adresse de retour : `+0x60` désigne donc le
slot `+0x58` du caller. Le relais leaf ne change pas RSP et n'ajoute pas
de CALL. Le quatrième argument R9 reste le callback original. L'observateur
C++ l'appelle une fois, transmet AL sans modification, puis le retour
traverse les trois NOP et atteint le TEST natif. Le relais publié demeure
mappé jusqu'à la fin du processus; seule une allocation non publiée dont
la protection RX échoue est libérée immédiatement.

Le hook existant `0x581680` installe un frame TLS imbriquable, rétabli par
`__finally`. Zéro observation reste Unknown; une observation est Applied
ou Rejected; plusieurs deviennent Unknown sans retour ultérieur à Applied.
Après le dispatcher, seul Applied avec quantité initiale >1 **et quantité
encore identique** appelle `0x46F090(game,player,item,-1)`.

Cette routine lit stat 70, appelle AddUnitStat à `0x46F163`, résout le client
à `0x46F16E` et transmet la nouvelle stat par `0x47A190` à `0x46F18B`.
Un débit de pile 2..511 laisse 1..510 et évite sa branche spéciale de
passage à zéro. Les updates de skills liés restent ceux du helper natif.
Ce comportement ne modifie pas la gestion native d'un effet rejeté sur la
dernière bouteille. Aucun verdict de gameplay ou de réseau n'en découle.

### Empreinte, coexistence et tests

Cinq nouveaux tableaux du source ont été extraits et comparés à l'image
d'analyse gouvernée, tous byte-exacts et uniques :

| Témoin | RVA | Taille |
|---|---|---:|
| entrée prédicat | `0x308E80` | 32 |
| callback, arguments, restore, AL | `0x581778` | 31 |
| validation serveur | `0x4F4192` | 29 |
| conservation de l'état | `0x4F481E` | 25 |
| absence de décalage belt demandé | `0xED26E` | 23 |

Le save R14 de cinq octets est également vérifié avant toute mutation.
Le manifeste déclare 27 plages Potion et le relais de 19 octets. Audit
ciblé : **4 806 comparaisons, aucun chevauchement déclaré**. Aucune prise
de l'entrée `0x4F40C0` possédée par Transmogrify; la plage Infinite Quantities
Book `0x5817BD` reste intacte. Le contrôle global `Test-NativeWrites.ps1`
et son CTest self-test échouent encore sur le patch Shadow Master AI absent
du manifeste, hors de ce lot. Ils ne sont pas comptés comme réussis.

Build Release DLL/tests : PASS. CTest Potion seul : **1/1 PASS**, comprenant
les 1 572 864 transferts, la migration, le prédicat sur 1..511, le scénario
5→4→3→2→1→suppression native et les observations rejetées/ambiguës.
Un test Windows x64 exécute réellement le relais de production contre un
caller synthétique local : quatre arguments, callback vrai/faux, R14 restauré
et slot de pile vérifiés. Il ne charge ni n'appelle D2R et ne prouve pas
le gameplay, le déroulement d'exceptions natives ni la compatibilité globale.

### Renderer et plafonds : progrès d'analyse seulement

Le compteur du stash est un widget **embarqué** à parent `+0x618`, non
un pointeur. Son constructeur `0x2CDE10` remplit 0x230 octets; le constructeur
parent alloue 0x848. `0x854710` initialise le parent du widget à `+0x30`,
sans insertion démontrée dans une liste d'enfants. Les octets de certaines
chaînes/vtables du corpus statique sont nuls : aucune valeur de ces chaînes
n'est inventée. Les réglages de style chargés en jeu restent à identifier.

Le draw texte `0x86D410` prépare un style de 0xA4 octets via `0x86D1B0`,
qui copie 0xA0 octets de paramètres et gère la sélection/restauration de
font. Le wrapper `0x902E20..0x902EFA` reçoit texte UTF-8, rect, style et
scale en XMM3, puis choisit le renderer HD/legacy. Ces lectures offrent une
piste sans nouveau widget persistant, **pas encore un adapter implantable** :
style exact du stash, coordonnées, clipping et coexistence restent ouverts.

Pour les limites, `0x47558E` appelle `ITEMS_GetTotalMaxStack` sur la
destination du merge auto pickup. Le getter `0x3719E0..0x371A96` lit le
maxstack compilé, ajoute stat 254 et borne à 511. Ses autres usages et la
pose d'une grosse pile sur une case belt vide ne sont pas couverts par ce
seul callsite; aucun remplacement global approximatif n'est ajouté.

**Toujours un lot de développement non déployé.** La consommation est
maintenant branchée en source, mais reste à éprouver en jeu. Compteur,
limites par destination, transferts manuels, conversion Advanced Stash,
découverte des potions du mod actif et save/reload restent ouverts.
SHA-256 du build local final :
`FDF4E3D819A4CD0090A5E6D7D6ABF5E63B3855E8F2E353E80E35407A7C55448D`.
La DLL installée reste inchangée :
`E046FE727F64F6BB204CA0ECB470829DA0C18EC5FABAEAEB1CD70043754E6AE2`.
