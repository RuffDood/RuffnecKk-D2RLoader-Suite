# Workbench natif commun D2R 92777 / 93847

Ce workbench conserve le corpus historique 3.2.92777 et les preuves natives
communes gouvernées pour D2R 3.3.93847. Il évite de redumper, réimporter et
rescanner le même binaire à chaque conversation. Utiliser `re:d2r33` pour le
runtime courant ; les commandes historiques `re:d2r32` ci-dessous restent
disponibles. Une autre image exige ses propres preuves de compatibilité.

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

Deux images locales sont volontairement conservees. L'image `decrypted` garde
le SHA-256 exact deja cite par la mission de portage. L'image `analysis` en est
une derivee deterministe : son `.text` canonique est inchange, tandis que
`.pdata`, `_RDATA` et `.rodata` sont rehydrates depuis le `D2R.exe` installe du
meme build. Cette derivee fournit 105 850 bornes de fonctions x64 exploitables
par l'index. Ghidra recoit ensuite uniquement le `.text` brut et ces bornes de
fonctions, ce qui contourne les imports/TLS volontairement invalides du binaire
protege et evite une analyse globale couteuse.

## Discipline

- Ne jamais committer une image D2R, un projet Ghidra ou la base SQLite.
- Ne jamais reutiliser un RVA sur un autre build sans nouvelle preuve.
- Commencer par `status`, puis consulter `known` et `xrefs` avant tout nouveau
  scan global.
- Ajouter les nouvelles identifications stables a `known-rvas.json` avec leur
  source et leur niveau de confiance.
- Conserver des octets `expected` stricts et valider le comportement en jeu.
