# Reverse engineering persistant

Ce dossier conserve les petits artefacts gouvernes qui evitent de recommencer
les analyses binaires a chaque session : manifestes de build, index de RVA,
notes et commandes reproductibles.

Les executables reconstruits depuis la memoire, projets Ghidra, bases SQLite et
clones de reference restent exclusivement dans `analysis-cache/`. Ce dossier
est ignore par Git et exclu du cadastre, car il contient des artefacts locaux
volumineux et potentiellement soumis aux droits du jeu.

La cible courante est D2R 3.3.93847. La commande `npm run re:d2r33 -- status`
vérifie le [corpus commun gouverné](d2r-3.2.92777/README.md), dont le nom conserve
la provenance historique 92777. L'identité native utile 92777/93847 est établie ;
ne pas reconstruire un second atelier pour cette image équivalente. Une image
différente, notamment Steam 93787 sans preuve propre, reste à qualifier.

The [reconstruction modules](d2r-3.2.92777/modules/README.md) let you search
for a behavior before starting from addresses. The first topic is skill payment:
`npm run re:d2r33 -- topic blood_mana`. Each result links an explanation to
registry symbols and states its limitations.

## Aide-mémoire de test en jeu

Le guide [`spawn` cheat command](cheat-spawn.md) rassemble la syntaxe complète,
les options, les qualités et des exemples fondés sur les codes d'items actuels
de BKVince. Il s'agit d'une référence pratique importée dont la provenance
runtime reste à confirmer, pas d'une preuve native promue pour un build D2R.

## Références externes épinglées

Le registre [references.json](references.json) gouverne les clones de sources
externes conservés sous `analysis-cache/references/`. Il fixe leur dépôt amont,
leur commit, leur licence, leur portée et leur format de citation sans
versionner leurs sources dans Diablo.

D2MOO est la première référence enregistrée. Il décrit Diablo II 1.10f et sert
uniquement à retrouver l'intention gameplay, des noms et des formes de flux de
contrôle. Ses adresses, ordinals, structures et ABI 32 bits ne sont jamais des
preuves pour D2R. Chaque image native différente exige sa qualification propre.

`eezstreet/D2RL-Plugins` est la référence d'intégration du PluginPack. Son clone
propre épinglé reste intact sous `analysis-cache/references/D2RL-Plugins`; les
prototypes RuffnecKk sont développés dans une copie de travail distincte des
workbenches natifs. Les fichiers CMake, headers et sources du commit épinglé font
autorité pour le squelette réel. Le README amont peut être en retard sur le
code — au commit courant, il décrit encore une configuration INI et une DLL
partagée alors que les sources utilisent `D2RPlugins.json` et une bibliothèque
statique `plugin-shared`.

```powershell
npm run re:refs -- list
npm run ref:d2moo -- status
npm run ref:d2moo -- bootstrap
npm run ref:d2moo -- search durability
npm run ref:d2moo -- symbol ITEMS_UpdateDurability
npm run ref:d2moo -- update
npm run ref:d2rlplugins -- status
npm run ref:d2rlplugins -- search sgptDataTables
npm run ref:d2rlplugins -- symbol D2UnitStrc
```

`update` est volontairement explicite : il avance le clone local et le commit
du manifeste vers la branche amont configurée. Une preuve issue du clone est
citée sous la forme
`D2MOO@19019806df7f3e877fa105b05395d1e3597e2316:source/...:ligne`.
Une preuve PluginPack suit le même principe :
`D2RL-Plugins@dc75b49ffbb67b887d7757ee00ee9a03bcde5d8a:src/...:ligne`.

## Références web visuelles

Le registre distingue également les références web qui ne sont ni des clones
épinglés ni des preuves natives. Le
[Dimentio D2R UI Asset Browser](https://i.dimentio.dev/d2r/) permet de rechercher
et prévisualiser des rendus PNG d'assets UI par chemin, dimensions, type et tags.
Son [index public](https://i.dimentio.dev/d2r/assets.json) exposait 3 028 entrées
lors de son inspection du 26 juillet 2026.

Cette référence sert uniquement à orienter un travail visuel et à retrouver un
nom d'asset. Elle ne déclare ni build D2R, ni provenance, ni licence et ne peut
donc prouver aucune version, RVA, signature, ABI ou comportement natif. Les
layouts BKVince, les fichiers `.sprite` locaux et l'installation ciblée restent
les sources runtime autoritaires. Ne pas aspirer, versionner ou redistribuer la
collection distante sans établir séparément sa provenance et les permissions.
