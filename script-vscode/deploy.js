// deploy.js
const fs = require('fs');
const path = require('path');
const os = require('os');

const pkg = require('./package.json');
const extRoot = path.join(os.homedir(), '.vscode', 'extensions');
const fullName = `${pkg.publisher}.${pkg.name}`;
const target = path.join(extRoot, `${fullName}-${pkg.version}`);

// 1) Supprime les anciennes versions de l'extension
if (fs.existsSync(extRoot)) {
    for (const entry of fs.readdirSync(extRoot)) {
        if (entry.startsWith(`${fullName}-`) && entry !== path.basename(target)) {
            fs.rmSync(path.join(extRoot, entry), { recursive: true, force: true });
            console.log(`  - ancienne version supprimée : ${entry}`);
        }
    }
}

// 2) Copie les fichiers nécessaires (pas besoin de src/, ni de node_modules,
//    car l'extension n'a aucune dépendance runtime)
fs.mkdirSync(target, { recursive: true });
const filesToCopy = [
    'package.json',
    'language-configuration.json',
    'out',           // JS compilé (main: ./out/extension.js)
    'syntaxes',      // grammaire TextMate
    'README.md',
    'CHANGELOG.md',
    'LICENSE',
];
for (const f of filesToCopy) {
    const src = path.join(__dirname, f);
    if (!fs.existsSync(src)) continue;
    fs.cpSync(src, path.join(target, f), { recursive: true });
    console.log(`  + ${f}`);
}

console.log(`\n✅ Extension déployée dans : ${target}`);
console.log('   → Redémarrez VSCode ou exécutez "Developer: Reload Window"');