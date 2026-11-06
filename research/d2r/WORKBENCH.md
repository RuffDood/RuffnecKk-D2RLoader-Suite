# Captured D2RLoader game-image workbench

This historical directory stores the native code captured from running
`D2RLoader.exe`, with D2RCore active. [Native corpus identity](../NATIVE-CORPUS.md)
is authoritative for provenance and the meaning of version labels.

`92777`, `93847`, the filenames and `re:d2r32` / `re:d2r33` aliases are
historical installation/tooling labels. This is not proof of independently
captured retail 3.2/3.3 code or equivalence between those retail executables.
Reuse this image when its required native surfaces match; inspect current
Loader/Core implementations and hooks separately.

## Understand a behavior

The [reconstruction modules](modules/README.md) connect a gameplay question to
explanations, documentary pseudocode, and native evidence.

```powershell
npm run re:d2r33 -- status
npm run re:d2r33 -- topic blood_mana
npm run re:d2r33 -- topic "coût en vie"
npm run re:d2r33 -- known blood_mana
```

The first topic is [skill payment](modules/skill-costs.md). Results state their
limitations and resolve functions from the existing registry. Static evidence
does not constitute in-game qualification.

## Argument contracts and imported references

- [Argument contract format and authoring rules](argument-contracts.md): record arguments, return values, evidence and unresolved limits separately from identification confidence.
- [Generated contract catalogue](argument-contracts.generated.md): structured views of contracts in `known-rvas.json`.
- [Build 93847 reference intake](references/rva-93847-intake.md): preserved user-supplied catalogue, provenance, conflicts and address-overlap inventory.

Use `node scripts/reverse-engineering/argument-contracts.mjs --query 0x33AA00`
from the repository root to inspect current structured contracts without rebuilding
the native index. New or revised native identifications should follow the contract
authoring rules; legacy entries are backfilled only from established evidence.

## Demarrage rapide

Depuis la racine du depot :

```powershell
npm run re:d2r32 -- status
npm run re:d2r32 -- self-test
npm run re:d2r32 -- known tome
npm run re:d2r32 -- function 0x5817BD
npm run re:d2r32 -- xrefs 0x46F090
npm run re:d2r32 -- bytes "41 B9 ?? ?? ?? ??"
npm run re:d2r32 -- reference d2moo durability
npm run re:d2r32 -- reference d2moo ITEMS_UpdateDurability --symbol
npm run re:d2r32 -- reference d2rlplugins sgptDataTables
npm run re:d2r32 -- reference d2rlplugins D2UnitStrc --symbol
```

`function` desassemble uniquement la fonction concernee a partir de la table
d'exception x64; il repond en quelques secondes et produit une sortie compacte.
`xrefs` utilise un index SQLite construit une seule fois. `known` interroge les
RVA gouvernes, les patches D2RLoader et les missions existantes.

Pour une decompilation plus riche :

```powershell
npm run re:d2r32:ghidra -- status
npm run re:d2r32:ghidra -- function 0x441B10 180
```

Le projet Ghidra importe une seule fois le `.text` brut a sa vraie base. Chaque
commande `function` desassemble et decompile uniquement la fonction demandee,
puis conserve ce travail dans le projet. Utiliser l'index compact `xrefs` pour
les references globales; les xrefs Ghidra ne couvrent que les fonctions deja
analysees paresseusement.

## Initialisation ou nouveau poste

```powershell
npm run re:d2r32:init -- -ImagePath "C:\chemin\D2R-3.2.92777-decrypted.exe"
npm run re:d2r32:ghidra-import
```

L'initialiseur refuse toute image dont la taille ou le SHA-256 differe du
manifeste. Pour un futur build D2R, creer un nouveau dossier et un nouveau
manifeste; ne jamais remplacer silencieusement l'image 92777.

## Contenu local non versionne

`analysis-cache/` contient :

- `images/` : reconstruction PE canonique et dechiffree;
- `index/d2r32.sqlite` : fonctions, xrefs, chaines, patches et references JSON;
- `ghidra/` : projet analyse persistant;
- `corpus/` : resultats intermediaires des recherches precedentes;
- `references/` : snapshots locaux des sources SDK/plugins utiles.

Le fichier [workbench.json](workbench.json) epingle le build, les hashes de
sections et les noms de projet. [known-rvas.json](known-rvas.json) ne contient
que des identifications suffisamment prouvees; les hypotheses doivent rester
marquees `low` tant qu'elles ne sont pas validees.

[findings.md](findings.md) est le relais humain compact entre les sessions : il
resume les preuves deja acquises, les hypotheses rejetees ou encore ouvertes et
la prochaine requete utile. Le consulter avant les corpus bruts evite de payer
a nouveau leur lecture complete.

`reference` interroge les clones externes gouvernés par
`reverse-engineering/references.json` et imprime des citations stables incluant
le commit, le chemin et la ligne. D2MOO reste une référence sémantique 1.10f :
toute correspondance avec le build 92777 doit ensuite être prouvée par
`function`, `xrefs`, `bytes` ou Ghidra.

Two local images remain pinned. The canonical reconstructed PE contains
Loader-process code captured on 2026-07-17. The analysis PE preserves that code
and supplies `.pdata`, `_RDATA` and `.rodata` from the installed disk PE.
The latter metadata has separate provenance: its 105,850 x64 function ranges
are query aids and can split or misbound a logical function. Use exact code
and control flow when a query stops inside a function. Ghidra reuses the
captured `.text` and the existing function map.

## Discipline

- Ne jamais committer une image D2R, un projet Ghidra ou la base SQLite.
- Ne jamais reutiliser un RVA sur un autre build sans nouvelle preuve.
- Commencer par `status`, puis consulter `known` et `xrefs` avant tout nouveau
  scan global.
- Ajouter les nouvelles identifications stables a `known-rvas.json` avec leur
  source et leur niveau de confiance.
- Conserver des octets `expected` stricts et valider le comportement en jeu.
