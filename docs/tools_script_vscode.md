# Extension VS Code pour le langage Script

`script-vscode/` · extension `script-lang` (version 0.1.0)

## Présentation

L'extension prend en charge, dans VS Code, les fichiers `.script` du langage
[Script](libs_infrastructure_script.md) : mods de voxelia, scripts de jeu
de la sandbox. Elle apporte :

- **la coloration syntaxique** : grammaire TextMate
  `syntaxes/script.tmLanguage.json`, portée `source.script` ;
- **la configuration du langage** (`language-configuration.json`) :
  commentaires, paires de délimiteurs, indentation ;
- **des diagnostics** : erreurs de syntaxe et d'analyse ;
- **la complétion** ;
- **aller à la définition**, y compris à travers les `import` entre
  fichiers ;
- **les symboles du document** (plan du fichier).

L'analyse est réécrite en TypeScript dans `src/lang/` (`lexer.ts`,
`parser.ts`, `analyzer.ts`), sur le modèle du lexeur et du parseur C++ du
module `script`.

## Fichiers

| Fichier | Rôle |
|---|---|
| `package.json` | Manifeste : langage `script` (extension `.script`), grammaire, activation `onLanguage:script`, VS Code ≥ 1.75 |
| `src/extension.ts` | Activation, fournisseurs (diagnostics, complétion, définition, symboles), caches, résolution des imports |
| `src/lang/*.ts` | Lexeur, parseur et analyseur |
| `syntaxes/script.tmLanguage.json` | Grammaire de coloration |
| `language-configuration.json` | Commentaires, crochets, indentation |
| `out/` | JavaScript compilé (point d'entrée `out/extension.js`), **non versionné** : à produire par `npm run compile` |

## Installation

Il faut Node.js et npm. Depuis la racine du dépôt :

```sh
cd script-vscode
npm install          # typescript, @types/vscode, @types/node
npm run compile      # tsc -p .  → out/
```

Pour l'utiliser dans VS Code, liez le dossier dans les extensions de
l'utilisateur, puis rechargez la fenêtre :

```sh
ln -s "$PWD" ~/.vscode/extensions/script-lang
```

Pour la développer : ouvrez `script-vscode/` dans VS Code et lancez
« Run Extension » (F5). `npm run watch` recompile en continu.

> Le manifeste ne fournit pas de commande d'empaquetage (`vsce`) : la
> méthode ci-dessus (lien dans `~/.vscode/extensions`) est la plus directe.
