import * as vscode from 'vscode';
import * as fs from 'fs';
import * as path from 'path';
import { parse, ParseResult } from './lang/parser';
import { analyze, AnalyzerResult, buildScopeAt, lookup, Sym } from './lang/analyzer';

const LANGUAGE = 'script';
const MODULE_EXT = '.script';

// ═════════════════════════════════════════════════════════════════════
// Caches
// ═════════════════════════════════════════════════════════════════════

interface CachedModule {
    uri: vscode.Uri;
    doc: vscode.TextDocument;
    version: number;
    parseResult: ParseResult;
    result: AnalyzerResult;
}

/** Clé = `uri.toString()`. Invalidé dès qu'un document change. */
const moduleCache = new Map<string, CachedModule>();

/** Clé = `importerDir|importPath`. `null` = introuvable (cache négatif). */
const importResolutionCache = new Map<string, vscode.Uri | null>();

function invalidateModule(uri: vscode.Uri): void {
    moduleCache.delete(uri.toString());
}

// ═════════════════════════════════════════════════════════════════════
// Résolution d'imports
// ═════════════════════════════════════════════════════════════════════

async function loadModule(fsPath: string): Promise<CachedModule | undefined> {
    try {
        const uri = vscode.Uri.file(fsPath);
        const doc = await vscode.workspace.openTextDocument(uri);
        const key = uri.toString();
        const cached = moduleCache.get(key);
        if (cached && cached.version === doc.version && cached.doc === doc) {
            return cached;
        }
        const src = doc.getText();
        const parsed = parse(src);
        const result = analyze(parsed, src);
        const entry: CachedModule = { uri, doc, version: doc.version, parseResult: parsed, result };
        moduleCache.set(key, entry);
        return entry;
    } catch {
        return undefined;
    }
}

async function resolveImport(
    importPath: string,
    fromUri: vscode.Uri,
    workspaceRoot: string | undefined
): Promise<CachedModule | undefined> {
    const fromDir = path.dirname(fromUri.fsPath);
    const withExt = importPath.endsWith(MODULE_EXT) ? importPath : importPath + MODULE_EXT;

    // Chemins rapides : relatif à l'importateur, puis à son parent
    // (couvre la convention `scripts/mobs/zombie.script` → `import "monstres"`).
    const candidates = [
        path.join(fromDir, withExt),
        path.join(fromDir, importPath),
        path.join(path.dirname(fromDir), withExt),
        path.join(path.dirname(fromDir), importPath),
    ];
    for (const c of candidates) {
        if (fs.existsSync(c)) return loadModule(c);
    }

    // Chemin lent : recherche dans le workspace, bornée et mise en cache.
    const cacheKey = `${fromDir}|${importPath}`;
    if (importResolutionCache.has(cacheKey)) {
        const cached = importResolutionCache.get(cacheKey);
        return cached ? loadModule(cached.fsPath) : undefined;
    }

    if (workspaceRoot) {
        try {
            const matches = await vscode.workspace.findFiles(
                `**/${withExt}`, '**/node_modules/**', 8
            );
            if (matches.length > 0) {
                // Préférer le fichier le plus proche de l'importateur.
                let best = matches[0], bestDist = Infinity;
                for (const m of matches) {
                    const dist = path.relative(fromDir, m.fsPath).split(/[\\/]/).length;
                    if (dist < bestDist) { bestDist = dist; best = m; }
                }
                importResolutionCache.set(cacheKey, best);
                return loadModule(best.fsPath);
            }
        } catch { /* ignore */ }
    }

    importResolutionCache.set(cacheKey, null);
    return undefined;
}

// ═════════════════════════════════════════════════════════════════════
// Utilitaires
// ═════════════════════════════════════════════════════════════════════

function toRange(loc: { line: number; col: number; end?: number }, doc: vscode.TextDocument): vscode.Range {
    const start = new vscode.Position(loc.line - 1, loc.col - 1);
    if (loc.end === undefined) return new vscode.Range(start, start);
    const end = doc.positionAt(Math.min(loc.end, doc.getText().length));
    return new vscode.Range(start, end.line < start.line ? start : end);
}

function mapSymKind(s: Sym): vscode.CompletionItemKind {
    switch (s.kind) {
        case 'function': return vscode.CompletionItemKind.Function;
        case 'class': return vscode.CompletionItemKind.Class;
        case 'method': return vscode.CompletionItemKind.Method;
        case 'field': return vscode.CompletionItemKind.Field;
        case 'parameter': return vscode.CompletionItemKind.Variable;
        case 'namespace': return vscode.CompletionItemKind.Module;
        case 'import': return vscode.CompletionItemKind.Module;
        case 'enum-value': return vscode.CompletionItemKind.EnumMember;
        default: return vscode.CompletionItemKind.Variable;
    }
}

function toSymbolKind(s: Sym): vscode.SymbolKind {
    switch (s.kind) {
        case 'class': return vscode.SymbolKind.Class;
        case 'function': return vscode.SymbolKind.Function;
        case 'namespace': return vscode.SymbolKind.Namespace;
        case 'method': return vscode.SymbolKind.Method;
        case 'field': return vscode.SymbolKind.Field;
        case 'enum-value': return vscode.SymbolKind.EnumMember;
        default: return vscode.SymbolKind.Variable;
    }
}

function toDocumentSymbol(s: Sym, doc: vscode.TextDocument): vscode.DocumentSymbol {
    const range = toRange(s.loc, doc);
    const sym = new vscode.DocumentSymbol(s.name, s.detail ?? '', toSymbolKind(s), range, range);
    if (s.children) for (const c of s.children) sym.children.push(toDocumentSymbol(c, doc));
    return sym;
}

/** Cherche `name` parmi les symboles de premier niveau (récursivement dans les namespaces). */
function findModuleSymbol(mod: CachedModule, name: string): Sym | undefined {
    const search = (list: Sym[]): Sym | undefined => {
        for (const s of list) {
            if (s.name === name) return s;
            if (s.children) { const hit = search(s.children); if (hit) return hit; }
        }
        return undefined;
    };
    return search(mod.result.symbols);
}

function guessTypeFromSym(sym: Sym | undefined, an: AnalyzerResult): string | undefined {
    if (!sym?.detail) return undefined;
    if (an.classes.has(sym.detail)) return sym.detail;
    return undefined;
}

// ═════════════════════════════════════════════════════════════════════
// activate
// ═════════════════════════════════════════════════════════════════════

export function activate(ctx: vscode.ExtensionContext) {
    const diags = vscode.languages.createDiagnosticCollection(LANGUAGE);
    ctx.subscriptions.push(diags);

    const reanalyze = (doc: vscode.TextDocument) => {
        if (doc.languageId !== LANGUAGE) return;
        const src = doc.getText();
        const parsed = parse(src);
        const an = analyze(parsed, src);
        const items = an.diagnostics.map(d => {
            const start = new vscode.Position(d.line - 1, d.col - 1);
            const end = doc.positionAt(Math.min(d.endOffset, src.length));
            const r = new vscode.Diagnostic(
                new vscode.Range(start, end.line > start.line ? start : end),
                d.message,
                d.severity === 'error' ? vscode.DiagnosticSeverity.Error : vscode.DiagnosticSeverity.Warning
            );
            r.source = 'script';
            return r;
        });
        diags.set(doc.uri, items);
    };

    vscode.workspace.textDocuments.forEach(reanalyze);

    // Édits : invalide le cache du module + re-diagnostique (debounced).
    const timers = new Map<string, NodeJS.Timeout>();
    ctx.subscriptions.push(vscode.workspace.onDidChangeTextDocument(e => {
        if (e.document.languageId !== LANGUAGE) return;
        invalidateModule(e.document.uri);
        const key = e.document.uri.toString();
        if (timers.has(key)) clearTimeout(timers.get(key)!);
        timers.set(key, setTimeout(() => { reanalyze(e.document); timers.delete(key); }, 200));
    }));

    ctx.subscriptions.push(vscode.workspace.onDidOpenTextDocument(reanalyze));
    ctx.subscriptions.push(vscode.workspace.onDidCloseTextDocument(d => {
        diags.delete(d.uri);
        invalidateModule(d.uri);
    }));

    // ── Complétion ────────────────────────────────────────────────────
    ctx.subscriptions.push(vscode.languages.registerCompletionItemProvider(LANGUAGE, {
        async provideCompletionItems(document, position) {
            const src = document.getText();
            const parsed = parse(src);
            const an = analyze(parsed, src);
            const offset = document.offsetAt(position);
            const scope = buildScopeAt(src, offset, an, parsed);

            const before = src.substring(0, offset);
            const memberMatch = before.match(/([a-zA-Z_][\w]*)\s*\.\s*([a-zA-Z_]*)$/);

            if (memberMatch) {
                const ownerName = memberMatch[1];
                const ownerSym = lookup(scope, ownerName);

                // `m.` où `m` est un import → membres du module importé
                if (ownerSym?.kind === 'import' && ownerSym.importPath) {
                    const wsRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
                    const mod = await resolveImport(ownerSym.importPath, document.uri, wsRoot);
                    if (mod) {
                        const items: vscode.CompletionItem[] = [];
                        for (const s of mod.result.symbols) {
                            const it = new vscode.CompletionItem(s.name, mapSymKind(s));
                            it.detail = s.detail ?? s.kind;
                            items.push(it);
                        }
                        return items;
                    }
                    return [];
                }

                // Accès membre classique : `obj.` / `this.` / `super.`
                const ownerType = guessTypeFromSym(ownerSym, an);
                if (ownerType && an.classes.has(ownerType)) {
                    const ci = an.classes.get(ownerType)!;
                    const items: vscode.CompletionItem[] = [];
                    for (const [name, sym] of ci.fields) {
                        const it = new vscode.CompletionItem(name,
                            sym.kind === 'enum-value'
                                ? vscode.CompletionItemKind.EnumMember
                                : vscode.CompletionItemKind.Field);
                        it.detail = sym.detail;
                        items.push(it);
                    }
                    for (const [name, sym] of ci.methods) {
                        const it = new vscode.CompletionItem(name, vscode.CompletionItemKind.Method);
                        it.detail = sym.detail;
                        items.push(it);
                    }
                    return items;
                }
                return [];
            }

            // Complétion générale : mots-clés + builtins + symboles visibles
            const items: vscode.CompletionItem[] = [];
            const kw = [
                'let', 'var', 'const', 'fn', 'class', 'interface', 'abstract', 'extends', 'implements',
                'override', 'new', 'if', 'else', 'while', 'do', 'for', 'in', 'return', 'break', 'continue',
                'true', 'false', 'nil', 'and', 'or', 'not', 'is', 'as', 'this', 'super', 'static',
                'namespace', 'import', 'async', 'await', 'enum', 'operator', 'factory'
            ];
            for (const k of kw) items.push(new vscode.CompletionItem(k, vscode.CompletionItemKind.Keyword));

            const builtins = [
                'print', 'type', 'assert', 'len', 'str', 'num', 'list', 'push', 'pop', 'insert',
                'remove', 'clear', 'index_of', 'reverse', 'sort_numbers', 'range', 'map', 'class_of',
                'keys', 'values', 'has', 'erase', 'upper', 'lower', 'trim', 'sub', 'find', 'replace',
                'starts_with', 'ends_with', 'split', 'join', 'format', 'clock', 'sleep'
            ];
            for (const b of builtins) items.push(new vscode.CompletionItem(b, vscode.CompletionItemKind.Function));

            for (const [name, sym] of scope.symbols) {
                const it = new vscode.CompletionItem(name, mapSymKind(sym));
                it.detail = sym.detail ?? sym.kind;
                items.push(it);
            }
            return items;
        }
    }, '.', ' '));

    // ── Définition (Ctrl+clic) ────────────────────────────────────────
    ctx.subscriptions.push(vscode.languages.registerDefinitionProvider(LANGUAGE, {
        async provideDefinition(document, position) {
            const src = document.getText();
            const parsed = parse(src);
            const an = analyze(parsed, src);
            const offset = document.offsetAt(position);
            const scope = buildScopeAt(src, offset, an, parsed);
            const wsRoot = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;

            // Cas 0 : curseur dans la chaîne d'un `import "…"` → ouvrir le fichier.
            const lineText = document.lineAt(position.line).text;
            const importRe = /import\s+(["'])([^"']+)\1/g;
            let im: RegExpExecArray | null;
            while ((im = importRe.exec(lineText)) !== null) {
                const quotePos = im.index + im[0].indexOf(im[1]);
                const strEnd = quotePos + im[2].length + 2;
                if (position.character >= quotePos && position.character <= strEnd) {
                    const mod = await resolveImport(im[2], document.uri, wsRoot);
                    if (mod) return new vscode.Location(mod.uri, new vscode.Position(0, 0));
                }
            }

            // Identifiant sous le curseur
            const wordRange = document.getWordRangeAtPosition(position, /[a-zA-Z_][\w]*/);
            if (!wordRange) return undefined;
            const word = document.getText(wordRange);

            // Cas 1 : `m.foo` / `this.foo` / `super.foo` / `obj.foo`
            const beforeRange = new vscode.Range(new vscode.Position(0, 0), wordRange.start);
            const before = document.getText(beforeRange);
            const m = before.match(/([a-zA-Z_][\w]*)\s*\.\s*$/);

            if (m) {
                const ownerName = m[1];
                const ownerSym = lookup(scope, ownerName);

                // 1a. Membre d'un import : `m.Monstre` → Monstre dans monstres.script
                if (ownerSym?.kind === 'import' && ownerSym.importPath) {
                    const mod = await resolveImport(ownerSym.importPath, document.uri, wsRoot);
                    if (mod) {
                        const sym = findModuleSymbol(mod, word);
                        if (sym) return new vscode.Location(mod.uri, toRange(sym.loc, mod.doc));
                        // Les classes sous namespace sont dans `classes` mais pas toujours
                        // en top-level `symbols` — second passage.
                        if (mod.result.classes.has(word)) {
                            const ci = mod.result.classes.get(word)!;
                            return new vscode.Location(mod.uri, toRange(ci.decl.loc, mod.doc));
                        }
                    }
                    return undefined;
                }

                // 1b. Membre de classe classique
                let ownerType: string | undefined;
                if (ownerName === 'this') {
                    ownerType = lookup(scope, 'this')?.detail;
                } else if (ownerName === 'super') {
                    const thisType = lookup(scope, 'this')?.detail;
                    if (thisType && an.classes.has(thisType)) ownerType = an.classes.get(thisType)!.superClass;
                } else {
                    ownerType = guessTypeFromSym(ownerSym, an);
                }

                if (ownerType && an.classes.has(ownerType)) {
                    const ci = an.classes.get(ownerType)!;
                    const sym = ci.methods.get(word) ?? ci.fields.get(word);
                    if (sym) return new vscode.Location(document.uri, toRange(sym.loc, document));
                    // Remonter la hiérarchie
                    let cur = ci.superClass;
                    const seen = new Set<string>();
                    while (cur && !seen.has(cur) && an.classes.has(cur)) {
                        seen.add(cur);
                        const parent = an.classes.get(cur)!;
                        const s = parent.methods.get(word) ?? parent.fields.get(word);
                        if (s) return new vscode.Location(document.uri, toRange(s.loc, document));
                        cur = parent.superClass;
                    }
                }
                return undefined;
            }

            // Cas 2 : identifiant simple dans le scope
            const sym = lookup(scope, word);
            if (sym) return new vscode.Location(document.uri, toRange(sym.loc, document));

            // Cas 3 : nom de classe directement
            if (an.classes.has(word)) {
                const ci = an.classes.get(word)!;
                return new vscode.Location(document.uri, toRange(ci.decl.loc, document));
            }

            return undefined;
        }
    }));

    // ── Outline ───────────────────────────────────────────────────────
    ctx.subscriptions.push(vscode.languages.registerDocumentSymbolProvider(LANGUAGE, {
        provideDocumentSymbols(document) {
            const src = document.getText();
            const parsed = parse(src);
            const an = analyze(parsed, src);
            return an.symbols.map(s => toDocumentSymbol(s, document));
        }
    }));
}

export function deactivate() { /* rien */ }