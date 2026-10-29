import { T, Tok } from './lexer';
import { ClassDecl, FnDecl, LetDecl, Loc, Method, ParseResult, Stmt, TypeRef } from './parser';

export type SymKind = 'variable' | 'function' | 'class' | 'method' | 'field' | 'parameter' | 'namespace' | 'import' | 'enum-value';

export interface Sym {
    name: string;
    kind: SymKind;
    loc: Loc;
    detail?: string;
    children?: Sym[];
    superClass?: string;
    declaredAt?: string;
    /** Pour `kind === 'import'` : le spécificateur du `import "…"`. */
    importPath?: string;
}

export interface ClassInfo {
    name: string;
    decl: ClassDecl;
    superClass?: string;
    fields: Map<string, Sym>;
    methods: Map<string, Sym>;
}

export interface AnalyzerResult {
    symbols: Sym[];                        // top-level symbols (for outline)
    scopeChain: Map<string, Sym[]>;        // for completion at various depths
    classes: Map<string, ClassInfo>;       // by class name
    diagnostics: { message: string; line: number; col: number; endOffset: number; severity: 'error' | 'warning' }[];
}

export function analyze(parseResult: ParseResult, src: string): AnalyzerResult {
    const diagnostics = [...parseResult.diags];
    const scopeChain = new Map<string, Sym[]>();
    const classes = new Map<string, ClassInfo>();
    const root: Sym[] = [];
    const scopeId = '0';
    scopeChain.set(scopeId, root);

    let scopeCounter = 1;
    const newScope = (): string => `s${scopeCounter++}`;

    // 1) Collect all top-level declarations to allow forward references.
    walkTop(parseResult.statements, root, [], newScope, scopeChain, classes, src, diagnostics);

    // 2) Resolve members of classes (fields+methods, including inherited ones)
    for (const ci of classes.values()) resolveClassMembers(ci, classes);

    return { symbols: root, scopeChain, classes, diagnostics };
}

function inferTypeOf(expr: any): string | undefined {
    if (!expr) return undefined;
    switch (expr.kind) {
        case 'call': {
            // `Zombie(...)` ou `new Zombie(...)` → `Zombie` (nom de classe présumé).
            // `m.SomeClass(...)` → `SomeClass` (le dernier segment).
            const c = expr.callee;
            if (c?.kind === 'ident') return c.name;
            if (c?.kind === 'member') return c.name;
            return undefined;
        }
        default:
            return undefined;
    }
}

function walkTop(stmts: Stmt[], scope: Sym[], path: string[], newScope: () => string, scopeChain: Map<string, Sym[]>, classes: Map<string, ClassInfo>, src: string, diags: AnalyzerResult['diagnostics']) {
    for (const s of stmts) {
        switch (s.kind) {
            case 'let': {
                const sym: Sym = { name: s.name, kind: 'variable', loc: s.loc, detail: s.declKind };
                if (s.type) {
                    // Annotation explicite : `let z: Zombie = ...`
                    sym.detail = s.type.path.join('.');
                } else if (s.init) {
                    if (s.init.kind === 'import') {
                        sym.kind = 'import';
                        sym.importPath = s.init.path;
                        sym.detail = `module "${s.init.path}"`;
                    } else {
                        // Inférence minimale : `let z = Zombie(...)` → détail `Zombie`.
                        const inferred = inferTypeOf(s.init);
                        if (inferred) sym.detail = inferred;
                    }
                }
                scope.push(sym);
                break;
            }
            case 'fn': {
                scope.push({ name: s.name, kind: 'function', loc: s.loc, detail: fnSignature(s) });
                break;
            }
            case 'class': {
                const ci: ClassInfo = {
                    name: s.name,
                    decl: s,
                    superClass: s.bases.length > 0 ? s.bases[0].join('.') : undefined,
                    fields: new Map(),
                    methods: new Map()
                };
                classes.set(s.name, ci);
                const sym: Sym = { name: s.name, kind: s.isEnum ? 'class' : 'class', loc: s.loc, detail: describeClass(s) };
                scope.push(sym);
                break;
            }
            case 'namespace': {
                const ns: Sym = { name: s.name, kind: 'namespace', loc: s.loc, children: [] };
                scope.push(ns);
                walkTop(s.body, ns.children!, [...path, s.name], newScope, scopeChain, classes, src, diags);
                break;
            }
            case 'block':
                walkTop(s.body, scope, path, newScope, scopeChain, classes, src, diags);
                break;
            case 'if':
                walkTop([s.then], scope, path, newScope, scopeChain, classes, src, diags);
                if (s.else) walkTop([s.else], scope, path, newScope, scopeChain, classes, src, diags);
                break;
            case 'while':
            case 'for':
                walkTop([s.kind === 'while' ? s.body : s.body], scope, path, newScope, scopeChain, classes, src, diags);
                break;
            default:
                break;
        }
    }
}

function resolveClassMembers(ci: ClassInfo, classes: Map<string, ClassInfo>, seen: Set<string> = new Set()) {
    if (seen.has(ci.name)) return;
    seen.add(ci.name);

    // Resolve superclass first
    if (ci.superClass && classes.has(ci.superClass)) {
        resolveClassMembers(classes.get(ci.superClass)!, classes, seen);
        const parent = classes.get(ci.superClass)!;
        for (const [k, v] of parent.fields) ci.fields.set(k, v);
        for (const [k, v] of parent.methods) ci.methods.set(k, v);
    }

    for (const f of ci.decl.fields) {
        ci.fields.set(f.name, { name: f.name, kind: 'field', loc: f.loc, detail: f.type ? typeToString(f.type) : f.kind });
    }
    for (const m of ci.decl.methods) {
        ci.methods.set(m.name, { name: m.name, kind: 'method', loc: m.loc, detail: fnSignature(m) });
    }
    // Enum values act like static class constants
    for (const e of ci.decl.enumEntries) {
        ci.fields.set(e.name, { name: e.name, kind: 'enum-value', loc: e.loc, detail: `value of ${ci.name}` });
    }
    // Reflect inherited members into the class Sym for outline
    // (not necessary for functionality, but nice for outline tree)
}

function describeClass(c: ClassDecl): string {
    if (c.isEnum) return `enum ${c.name}`;
    if (c.isInterface) return `interface ${c.name}`;
    if (c.isAbstract) return `abstract class ${c.name}`;
    return `class ${c.name}`;
}

function fnSignature(d: FnDecl | Method): string {
    const params = d.params.map(p => p.type ? `${p.name}: ${typeToString(p.type)}` : p.name).join(', ');
    const ret = d.returnType ? `: ${typeToString(d.returnType)}` : '';
    const tp = 'typeParams' in d && d.typeParams.length > 0 ? `<${d.typeParams.join(', ')}>` : '';
    return `${d.name}${tp}(${params})${ret}`;
}

export function typeToString(t: TypeRef): string {
    const base = t.path.join('.');
    const args = t.args && t.args.length > 0 ? `<${t.args.map(typeToString).join(', ')}>` : '';
    return base + args + (t.nullable ? '?' : '');
}

// ── Scope resolution helpers (used by definition & completion) ──────

export interface Scope {
    symbols: Map<string, Sym>;
    parent?: Scope;
}

export function buildScopeAt(source: string, offset: number, analyzer: AnalyzerResult, parseResult: ParseResult): Scope {
    // Simplification pragmatique : on construit une portée globale + ajoute les symboles
    // locaux « visibles » à l'offset en parcourant les blocs qui l'englobent.
    // (Pour une analyse parfaite, il faudrait un vrai "find enclosing node" ; ceci
    // couvre 95 % des cas d'usage : variables de fonction, paramètres, champs de classe.)
    const global = new Map<string, Sym>();
    for (const s of analyzer.symbols) global.set(s.name, s);
    const sc: Scope = { symbols: global };

    // Ajoute les symboles locaux (params, lets, for-vars) en parcourant récursivement
    const collectLocals = (stmts: Stmt[], enclosing: Scope) => {
        for (const s of stmts) {
            if (!enclosingContains(s, offset)) continue;
            switch (s.kind) {
                case 'fn': {
                    const inner = new Map(enclosing.symbols);
                    for (const p of s.params) inner.set(p.name, { name: p.name, kind: 'parameter', loc: p.loc });
                    inner.set(s.name, { name: s.name, kind: 'function', loc: s.loc });
                    const innerScope = { symbols: inner, parent: enclosing };
                    collectLocals(s.body, innerScope);
                    return; // deepest wins
                }
                case 'class': {
                    const inner = new Map(enclosing.symbols);
                    for (const f of s.fields) inner.set(f.name, { name: f.name, kind: 'field', loc: f.loc });
                    for (const m of s.methods) inner.set(m.name, { name: m.name, kind: 'method', loc: m.loc });
                    inner.set('this', { name: 'this', kind: 'variable', loc: s.loc, detail: s.name });
                    // `bases` est un tableau de chemins (string[]) — on garde le premier, qui
                    // est la classe parente (les bases suivantes sont des owners de l'hôte).
                    const superName = s.bases.length > 0 ? s.bases[0].join('.') : '';
                    inner.set('super', { name: 'super', kind: 'variable', loc: s.loc, detail: superName });
                    const innerScope = { symbols: inner, parent: enclosing };
                    // Walk method bodies that contain offset
                    for (const body of classMethodBodies(s, offset)) collectLocals(body, innerScope);
                    // Also collect field initializer locals (rare) — skip for brevity
                    return;
                }
                case 'let':
                    enclosing.symbols.set(s.name, { name: s.name, kind: 'variable', loc: s.loc });
                    break;
                case 'block':
                    collectLocals(s.body, enclosing);
                    break;
                case 'if':
                    collectLocals([s.then], enclosing);
                    if (s.else) collectLocals([s.else], enclosing);
                    break;
                case 'while':
                    collectLocals([s.body], enclosing);
                    break;
                case 'for': {
                    enclosing.symbols.set(s.varName, { name: s.varName, kind: 'variable', loc: s.varLoc });
                    collectLocals([s.body], enclosing);
                    break;
                }
                case 'classmember-method':
                    if (s.method.body) collectLocals(s.method.body, enclosing);
                    break;
                default:
                    break;
            }
        }
    };
    collectLocals(parseResult.statements, sc);
    return sc;
}

function enclosingContains(s: Stmt, offset: number): boolean {
    // Approximate: does this statement's span contain the offset?
    const startOff = offsetFromLoc(s.loc);
    // We don't have end offset in Loc, so use a heuristic: assume statement encloses if its start < offset
    // and the next top-level statement starts after. Since we don't track ends precisely, we accept
    // any statement that starts before the offset and is part of the same file.
    // For a more robust solution we'd add an `endOffset` field to Loc.
    return startOff <= offset;
}

function classMethodBodies(c: ClassDecl, offset: number): Stmt[][] {
    const bodies: Stmt[][] = [];
    for (const m of c.methods) if (m.body) bodies.push(m.body);
    return bodies;
}

function offsetFromLoc(loc: Loc): number {
    return loc.end; // approximation (end offset of the token that started this statement)
}

export function resolveTypeOf(expr: any, scope: Scope, classes: Map<string, ClassInfo>): string | undefined {
    if (!expr) return undefined;
    switch (expr.kind) {
        case 'ident': {
            const sym = lookup(scope, expr.name);
            if (sym?.detail && /^(class|interface|enum) /.test(sym.detail)) return sym.name;
            return sym?.detail;
        }
        case 'call': {
            const c = expr.callee;
            if (c.kind === 'ident') {
                const ci = classes.get(c.name);
                if (ci) return c.name;
            }
            return undefined;
        }
        case 'member': {
            const parentType = resolveTypeOf(expr.object, scope, classes);
            if (!parentType) return undefined;
            const ci = classes.get(parentType);
            if (!ci) return undefined;
            const f = ci.fields.get(expr.name);
            if (f?.detail && classes.has(f.detail)) return f.detail;
            return undefined;
        }
        case 'this': {
            const thisSym = lookup(scope, 'this');
            return thisSym?.detail;
        }
        case 'super': {
            const superSym = lookup(scope, 'super');
            return superSym?.detail;
        }
        default: return undefined;
    }
}

export function lookup(scope: Scope, name: string): Sym | undefined {
    let s: Scope | undefined = scope;
    while (s) {
        const sym = s.symbols.get(name);
        if (sym) return sym;
        s = s.parent;
    }
    return undefined;
}

// ── Diagnostics: unknown identifiers ──────────────────────────────

export function checkUnknownIdentifiers(parseResult: ParseResult, scope: Scope, classes: Map<string, ClassInfo>): AnalyzerResult['diagnostics'] {
    const diags: AnalyzerResult['diagnostics'] = [];
    const visitor = (expr: any) => {
        if (!expr) return;
        switch (expr.kind) {
            case 'ident': {
                // Skip if it's a known symbol or a builtin
                if (lookup(scope, expr.name)) return;
                if (isBuiltin(expr.name)) return;
                // Do not flag: property names of object literal, field init context
                // Also: could be a class field of `this` implicitly — skip if we're in a method scope with this
                diags.push({ message: `identifiant inconnu \`${expr.name}\``, line: expr.loc.line, col: expr.loc.col, endOffset: expr.loc.end, severity: 'warning' });
                return;
            }
            case 'member': visitor(expr.object); return;
            case 'call': visitor(expr.callee); expr.args.forEach(visitor); return;
            case 'binary': visitor(expr.left); visitor(expr.right); return;
            case 'unary': visitor(expr.operand); return;
            case 'index': visitor(expr.object); visitor(expr.index); return;
            case 'list': expr.items.forEach(visitor); return;
            case 'map': expr.entries.forEach((e: any) => { visitor(e.key); visitor(e.value); }); return;
            case 'lambda': expr.body.forEach(visitStmt); return;
            default: return;
        }
    };
    const visitStmt = (s: Stmt) => {
        switch (s.kind) {
            case 'let': visitor(s.init); return;
            case 'assign': visitor(s.target); visitor(s.value); return;
            case 'expr': visitor(s.expr); return;
            case 'block': s.body.forEach(visitStmt); return;
            case 'if': visitor(s.cond); visitStmt(s.then); if (s.else) visitStmt(s.else); return;
            case 'while': visitor(s.cond); visitStmt(s.body); return;
            case 'for': visitor(s.iterable); visitStmt(s.body); return;
            case 'return': visitor(s.value); return;
            case 'fn': s.body.forEach(visitStmt); return;
            case 'class': s.body.forEach(visitStmt); return;
            case 'classmember-field': visitor(s.init); return;
            case 'classmember-method': (s.method.body ?? []).forEach(visitStmt); return;
            case 'namespace': s.body.forEach(visitStmt); return;
            default: return;
        }
    };
    parseResult.statements.forEach(visitStmt);
    return diags;
}

const BUILTINS = new Set([
    'print', 'type', 'assert', 'len', 'str', 'num', 'list', 'push', 'pop', 'insert', 'remove',
    'clear', 'index_of', 'reverse', 'sort_numbers', 'range', 'map', 'class_of', 'keys', 'values',
    'has', 'erase', 'upper', 'lower', 'trim', 'sub', 'find', 'replace', 'starts_with', 'ends_with',
    'split', 'join', 'format', 'clock', 'sleep', 'parse', 'encode', 'read_file', 'write_file', 'load',
    'require', 'std', 'math', 'physics', 'future'
]);
function isBuiltin(name: string): boolean { return BUILTINS.has(name); }