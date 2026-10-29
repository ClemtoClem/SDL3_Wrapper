import { T, Tok, lex } from './lexer';

// ── AST ────────────────────────────────────────────────────────────────

export interface Loc { line: number; col: number; end: number }

export interface TypeRef {
    path: string[];        // ex: ['std', 'vector'] ou ['Forme'] ou ['list']
    args?: TypeRef[];
    nullable?: boolean;
    loc: Loc;
}

export interface Param {
    name: string;
    type?: TypeRef;
    loc: Loc;
}

export interface Field {
    name: string;
    kind: 'let' | 'var' | 'const';
    isStatic: boolean;
    type?: TypeRef;
    loc: Loc;
}

export interface Method {
    name: string;
    params: Param[];
    returnType?: TypeRef;
    isStatic: boolean;
    isAsync: boolean;
    isAbstract: boolean;
    isOverride: boolean;
    isFactory: boolean;
    typeParams: string[];
    body?: Stmt[];
    loc: Loc;              // position du nom
}

export interface EnumEntry {
    name: string;
    loc: Loc;
}

export interface ClassDecl {
    kind: 'class';
    name: string;
    isInterface: boolean;
    isAbstract: boolean;
    isEnum: boolean;
    typeParams: string[];
    bases: string[][];
    interfaces: string[][];
    fields: Field[];
    methods: Method[];
    enumEntries: EnumEntry[];
    loc: Loc;
    body: Stmt[];          // statements exécutés (pour la navigation)
}

export interface FnDecl {
    kind: 'fn';
    name: string;
    params: Param[];
    returnType?: TypeRef;
    isAsync: boolean;
    typeParams: string[];
    body: Stmt[];
    loc: Loc;
}

export interface LetDecl {
    kind: 'let';
    name: string;
    declKind: 'let' | 'var' | 'const';
    type?: TypeRef;
    init?: Expr;
    loc: Loc;
}

export interface ImportDecl {
    kind: 'import';
    name: string;         // nom de la variable qui reçoit le module
    path: string;         // chaîne du import
    loc: Loc;             // position du nom
}

export interface NamespaceDecl {
    kind: 'namespace';
    name: string;
    body: Stmt[];
    loc: Loc;
}

export type Decl = ClassDecl | FnDecl | LetDecl | ImportDecl | NamespaceDecl;

// Expressions (minimal — juste pour la navigation et la complétion)
export interface Expr { kind: string; loc: Loc; [k: string]: any }

export type Stmt =
    | Decl
    | { kind: 'expr'; expr: Expr; loc: Loc }
    | { kind: 'assign'; target: Expr; value: Expr; loc: Loc }
    | { kind: 'block'; body: Stmt[]; loc: Loc }
    | { kind: 'if'; cond: Expr; then: Stmt; else?: Stmt; loc: Loc }
    | { kind: 'while'; cond: Expr; body: Stmt; loc: Loc }
    | { kind: 'for'; varName: string; varLoc: Loc; iterable: Expr; body: Stmt; loc: Loc }
    | { kind: 'return'; value?: Expr; loc: Loc }
    | { kind: 'classmember-field'; field: Field; init?: Expr; loc: Loc }
    | { kind: 'classmember-method'; method: Method; loc: Loc };

export interface Diag { message: string; line: number; col: number; endOffset: number; severity: 'error' | 'warning' }

export interface ParseResult {
    statements: Stmt[];
    diags: Diag[];
    tokens: Tok[];
    errors: string[];
}

// ── Parser ─────────────────────────────────────────────────────────────

export function parse(src: string): ParseResult {
    const { tokens, errors: lexErrs } = lex(src);
    const diags: Diag[] = lexErrs.map(e => ({ message: e.message, line: e.line, col: e.col, endOffset: 0, severity: 'error' as const }));
    const p = new Parser(tokens, src, diags);
    const statements = p.parseProgram();
    return { statements, diags, tokens, errors: lexErrs.map(e => e.message) };
}

class Parser {
    private pos = 0;
    private allowMapLiteral = true;

    constructor(private toks: Tok[], private src: string, private diags: Diag[]) {}

    private peek(off = 0): Tok { return this.toks[Math.min(this.pos + off, this.toks.length - 1)]; }
    private advance(): Tok { const t = this.peek(); if (t.kind !== T.EOF) this.pos++; return t; }
    private check(k: T): boolean { return this.peek().kind === k; }
    private match(k: T): boolean { if (this.check(k)) { this.advance(); return true; } return false; }

    private err(msg: string, tok: Tok = this.peek()): void {
        this.diags.push({ message: msg, line: tok.line, col: tok.col, endOffset: tok.end, severity: 'error' });
    }

    private expect(k: T, what: string): Tok | null {
        if (this.check(k)) return this.advance();
        this.err(`attendu ${what}, trouvé \`${this.peek().text || 'EOF'}\``);
        return null;
    }

    // Recovery: skip to next statement-starting token
    private sync(): void {
        while (!this.check(T.EOF)) {
            if (this.check(T.SEMI)) { this.advance(); return; }
            const k = this.peek().kind;
            if (k === T.KW_LET || k === T.KW_VAR || k === T.KW_CONST ||
                k === T.KW_FN || k === T.KW_CLASS || k === T.KW_INTERFACE ||
                k === T.KW_ABSTRACT || k === T.KW_ENUM || k === T.KW_NAMESPACE ||
                k === T.KW_IMPORT || k === T.KW_IF || k === T.KW_WHILE ||
                k === T.KW_FOR || k === T.KW_RETURN || k === T.KW_DO) {
                return;
            }
            this.advance();
        }
    }

    parseProgram(): Stmt[] {
        const out: Stmt[] = [];
        while (!this.check(T.EOF)) {
            const start = this.pos;
            const s = this.parseStatement();
            if (s) out.push(s);
            if (this.pos === start) { this.advance(); this.sync(); }
        }
        return out;
    }

    private parseStatement(): Stmt | null {
        while (this.match(T.SEMI)) { /* skip */ }
        const t = this.peek();
        switch (t.kind) {
            case T.KW_LET: case T.KW_VAR: case T.KW_CONST: return this.parseLet();
            case T.KW_FN: return this.parseFn();
            case T.KW_ASYNC: {
                if (this.peek(1).kind === T.KW_FN) { this.advance(); return this.parseFn(true); }
                return this.parseExprStmt();
            }
            case T.KW_CLASS: return this.parseClass(false, false);
            case T.KW_INTERFACE: return this.parseClass(false, true);
            case T.KW_ABSTRACT: {
                this.advance();
                if (this.check(T.KW_CLASS)) return this.parseClass(true, false);
                this.err('attendu `class` après `abstract`');
                return null;
            }
            case T.KW_ENUM: return this.parseEnum();
            case T.KW_NAMESPACE: return this.parseNamespace();
            case T.KW_IMPORT: return this.parseImport();
            case T.LBRACE: return this.parseBlock();
            case T.KW_IF: return this.parseIf();
            case T.KW_WHILE: return this.parseWhile();
            case T.KW_DO: return this.parseDoWhile();
            case T.KW_FOR: return this.parseFor();
            case T.KW_RETURN: return this.parseReturn();
            case T.KW_BREAK: case T.KW_CONTINUE: this.advance(); return { kind: 'expr', expr: { kind: 'noop', loc: { line: t.line, col: t.col, end: t.end } }, loc: { line: t.line, col: t.col, end: t.end } };
            default: return this.parseExprStmt();
        }
    }

    private locOf(t: Tok): Loc { return { line: t.line, col: t.col, end: t.end }; }

    private parseType(): TypeRef | undefined {
        if (!this.match(T.COLON)) return undefined;
        return this.parseTypeExpr();
    }

    private parseTypeExpr(): TypeRef {
        const t = this.peek();
        const path: string[] = [];
        if (this.check(T.KW_FN)) { this.advance(); path.push('fn'); }
        else if (this.check(T.KW_NIL)) { this.advance(); path.push('nil'); }
        else {
            const id = this.expect(T.IDENT, 'un nom de type');
            if (!id) return { path: ['?'], loc: this.locOf(t) };
            path.push(id.text);
            while (this.match(T.DOT)) {
                const nx = this.expect(T.IDENT, 'un nom après `.`');
                if (nx) path.push(nx.text);
            }
        }
        let args: TypeRef[] | undefined;
        if (this.match(T.LT)) {
            args = [];
            do { args.push(this.parseTypeExpr()); } while (this.match(T.COMMA));
            this.expect(T.GT, '`>` (fin des paramètres du type)');
        }
        const nullable = this.match(T.QUESTION);
        return { path, args, nullable, loc: this.locOf(t) };
    }

    private parseTypeParams(): string[] {
        const out: string[] = [];
        if (!this.match(T.LT)) return out;
        do {
            const id = this.expect(T.IDENT, 'un nom de paramètre de type');
            if (id) out.push(id.text);
            if (this.match(T.KW_EXTENDS)) this.parseTypeExpr();
        } while (this.match(T.COMMA));
        this.expect(T.GT, '`>`');
        return out;
    }

    private parseLet(): LetDecl | null {
        const kw = this.advance(); // let|var|const
        const kind: 'let' | 'var' | 'const' = kw.kind === T.KW_VAR ? 'var' : kw.kind === T.KW_CONST ? 'const' : 'let';
        const id = this.expect(T.IDENT, 'un nom de variable');
        if (!id) { this.sync(); return null; }
        const type = this.parseType();
        let init: Expr | undefined;
        if (this.match(T.ASSIGN)) init = this.parseExpression();
        this.match(T.SEMI);
        return { kind: 'let', name: id.text, declKind: kind, type, init, loc: this.locOf(id) };
    }

    private parseParams(): Param[] {
        const out: Param[] = [];
        if (!this.match(T.LPAREN)) { this.err('attendu `(`'); return out; }
        if (!this.check(T.RPAREN)) {
            do {
                const id = this.expect(T.IDENT, 'un nom de paramètre');
                if (!id) break;
                const type = this.parseType();
                out.push({ name: id.text, type, loc: this.locOf(id) });
            } while (this.match(T.COMMA));
        }
        this.expect(T.RPAREN, '`)`');
        return out;
    }

    private parseFn(isAsync = false): FnDecl | null {
        this.advance(); // fn
        const id = this.expect(T.IDENT, 'un nom de fonction');
        if (!id) { this.sync(); return null; }
        const typeParams = this.parseTypeParams();
        const params = this.parseParams();
        const returnType = this.parseType();
        const body = this.parseBody();
        return { kind: 'fn', name: id.text, params, returnType, isAsync, typeParams, body, loc: this.locOf(id) };
    }

    private parseBody(): Stmt[] {
        const saved = this.allowMapLiteral;
        this.allowMapLiteral = true;
        const out: Stmt[] = [];
        if (!this.match(T.LBRACE)) { this.err('attendu `{`'); return out; }
        while (!this.check(T.RBRACE) && !this.check(T.EOF)) {
            const start = this.pos;
            const s = this.parseStatement();
            if (s) out.push(s);
            if (this.pos === start) { this.advance(); }
        }
        this.expect(T.RBRACE, '`}`');
        this.allowMapLiteral = saved;
        return out;
    }

    private parseBlock(): Stmt {
        const open = this.peek();
        const body = this.parseBody();
        return { kind: 'block', body, loc: this.locOf(open) };
    }

    private parseClass(isAbstract: boolean, isInterface: boolean): ClassDecl | null {
        this.advance(); // class|interface
        const id = this.expect(T.IDENT, isInterface ? "un nom d'interface" : 'un nom de classe');
        if (!id) { this.sync(); return null; }
        const typeParams = this.parseTypeParams();
        const bases: string[][] = [];
        const interfaces: string[][] = [];
        if (this.match(T.KW_EXTENDS)) {
            do {
                const p: string[] = [];
                const first = this.expect(T.IDENT, 'un nom de classe');
                if (first) p.push(first.text);
                while (this.match(T.DOT)) { const n = this.expect(T.IDENT, ''); if (n) p.push(n.text); }
                if (isInterface) interfaces.push(p); else bases.push(p);
                if (this.check(T.LT)) { this.advance(); do { this.parseTypeExpr(); } while (this.match(T.COMMA)); this.expect(T.GT, '`>`'); }
            } while (this.match(T.COMMA));
        }
        if (this.match(T.KW_IMPLEMENTS)) {
            do {
                const p: string[] = [];
                const first = this.expect(T.IDENT, "un nom d'interface");
                if (first) p.push(first.text);
                while (this.match(T.DOT)) { const n = this.expect(T.IDENT, ''); if (n) p.push(n.text); }
                interfaces.push(p);
            } while (this.match(T.COMMA));
        }
        const fields: Field[] = [];
        const methods: Method[] = [];
        const enumEntries: EnumEntry[] = [];
        const body: Stmt[] = [];
        if (!this.match(T.LBRACE)) { this.err('attendu `{`'); this.sync(); return { kind: 'class', name: id.text, isInterface, isAbstract, isEnum: false, typeParams, bases, interfaces, fields, methods, enumEntries, loc: this.locOf(id), body }; }
        while (!this.check(T.RBRACE) && !this.check(T.EOF)) {
            while (this.match(T.SEMI)) { /* skip */ }
            if (this.check(T.RBRACE)) break;
            const start = this.pos;
            this.parseClassMember(fields, methods, body);
            if (this.pos === start) { this.advance(); }
        }
        this.expect(T.RBRACE, '`}`');
        return { kind: 'class', name: id.text, isInterface, isAbstract, isEnum: false, typeParams, bases, interfaces, fields, methods, enumEntries, loc: this.locOf(id), body };
    }

    private parseClassMember(fields: Field[], methods: Method[], body: Stmt[]): void {
        let isStatic = false, isAbstract = false, isOverride = false, isFactory = false, isAsync = false;
        for (;;) {
            if (this.match(T.KW_STATIC)) { isStatic = true; continue; }
            if (this.match(T.KW_ABSTRACT)) { isAbstract = true; continue; }
            if (this.match(T.KW_OVERRIDE)) { isOverride = true; continue; }
            if (this.match(T.KW_FACTORY)) { isFactory = true; continue; }
            if (this.match(T.KW_ASYNC)) { isAsync = true; continue; }
            break;
        }
        const t = this.peek();
        if (t.kind === T.KW_LET || t.kind === T.KW_VAR || t.kind === T.KW_CONST) {
            this.advance();
            const kind: 'let' | 'var' | 'const' = t.kind === T.KW_VAR ? 'var' : t.kind === T.KW_CONST ? 'const' : 'let';
            const id = this.expect(T.IDENT, 'un nom de champ');
            if (!id) { this.sync(); return; }
            const type = this.parseType();
            let init: Expr | undefined;
            if (this.match(T.ASSIGN)) init = this.parseExpression();
            this.match(T.SEMI);
            const f: Field = { name: id.text, kind, isStatic, type, loc: this.locOf(id) };
            fields.push(f);
            body.push({ kind: 'classmember-field', field: f, init, loc: this.locOf(id) });
            return;
        }
        if (t.kind === T.KW_OPERATOR || t.kind === T.KW_FN || t.kind === T.LPAREN) {
            if (!this.match(T.KW_FN) && !this.check(T.KW_OPERATOR) && !this.check(T.LPAREN)) {
                this.err('attendu `fn`, `operator` ou une déclaration de champ');
                this.sync(); return;
            }
            let name = '';
            let nameLoc: Tok = t;
            if (this.match(T.KW_OPERATOR)) {
                const op = this.advance();
                name = 'operator' + op.text;
                nameLoc = op;
            } else if (this.check(T.KW_OPERATOR)) {
                // operator as member (rare)
                this.advance();
                const op = this.advance();
                name = 'operator' + op.text;
                nameLoc = op;
            } else {
                // `fn` was matched already, or we came directly at `(` for an operator
                const id = this.expect(T.IDENT, 'un nom de méthode');
                if (!id) { this.sync(); return; }
                name = id.text === 'constructor' ? 'init' : id.text === 'destructor' ? 'deinit' : id.text;
                nameLoc = id;
            }
            const typeParams = this.parseTypeParams();
            const params = this.parseParams();
            const returnType = this.parseType();
            let methodBody: Stmt[] | undefined;
            if (isAbstract || isOverride || (this.check(T.LBRACE) === false && !this.check(T.LBRACE))) {
                // fallthrough: if next is not `{`, it's a signature only
            }
            if (this.check(T.LBRACE)) {
                methodBody = this.parseBody();
            } else {
                this.match(T.SEMI);
            }
            const m: Method = {
                name, params, returnType, isStatic: isStatic || isFactory,
                isAsync, isAbstract: isAbstract,
                isOverride, isFactory, typeParams,
                body: methodBody, loc: this.locOf(nameLoc)
            };
            methods.push(m);
            body.push({ kind: 'classmember-method', method: m, loc: this.locOf(nameLoc) });
            return;
        }
        this.err('membre de classe attendu');
        this.sync();
    }

    private parseEnum(): ClassDecl | null {
        const kw = this.advance();
        const id = this.expect(T.IDENT, 'un nom de type énuméré');
        if (!id) { this.sync(); return null; }
        const enumEntries: EnumEntry[] = [];
        const methods: Method[] = [];
        const fields: Field[] = [];
        const body: Stmt[] = [];
        if (!this.match(T.LBRACE)) { this.err('attendu `{`'); this.sync(); return null; }
        while (this.check(T.IDENT)) {
            const e = this.advance();
            enumEntries.push({ name: e.text, loc: this.locOf(e) });
            if (this.match(T.ASSIGN)) this.parseExpression();
            if (!this.match(T.COMMA)) break;
        }
        this.match(T.SEMI);
        while (!this.check(T.RBRACE) && !this.check(T.EOF)) {
            const start = this.pos;
            this.parseClassMember(fields, methods, body);
            if (this.pos === start) this.advance();
        }
        this.expect(T.RBRACE, '`}`');
        return { kind: 'class', name: id.text, isInterface: false, isAbstract: false, isEnum: true, typeParams: [], bases: [], interfaces: [], fields, methods, enumEntries, loc: this.locOf(id), body };
    }

    private parseNamespace(): NamespaceDecl | null {
        this.advance();
        const id = this.expect(T.IDENT, "un nom d'espace de noms");
        if (!id) { this.sync(); return null; }
        const body = this.parseBody();
        return { kind: 'namespace', name: id.text, body, loc: this.locOf(id) };
    }

    private parseImport(): ImportDecl | null {
        // Deux formes:
        //   import "chemin"          (expression → l'utilisateur écrit `let m = import "chemin"`)
        //   const m = import "chemin" (déclaration)
        // La forme déclaration est gérée par parseLet → ici on ne gère que la partie "expression" du import.
        // Donc `import` est traité comme un mot-clé d'expression, pas de statement autonome.
        // Ce cas ne devrait pas arriver si le code appelant respecte la grammaire.
        this.advance();
        const s = this.peek();
        if (s.kind === T.STRING) {
            this.advance();
            return { kind: 'import', name: '', path: this.decodeString(s.text), loc: this.locOf(s) };
        }
        this.err('attendu une chaîne après `import`');
        return null;
    }

    private decodeString(raw: string): string {
        // Retire les guillemets, résout les échappements basiques
        if (raw.length < 2) return raw;
        const body = raw.slice(1, -1);
        return body.replace(/\\(.)/g, (_m, c) => {
            switch (c) {
                case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r';
                case '0': return '\0'; case '\\': return '\\';
                case '"': return '"'; case "'": return "'";
                default: return '\\' + c;
            }
        });
    }

    private parseIf(): Stmt {
        const kw = this.advance();
        const cond = this.parseControlExpr();
        const then = this.parseStatement() ?? { kind: 'expr', expr: { kind: 'noop', loc: this.locOf(kw) }, loc: this.locOf(kw) };
        let els: Stmt | undefined;
        if (this.match(T.KW_ELSE)) els = this.parseStatement() ?? undefined;
        return { kind: 'if', cond, then, else: els, loc: this.locOf(kw) };
    }

    private parseWhile(): Stmt {
        const kw = this.advance();
        const cond = this.parseControlExpr();
        const body = this.parseStatement() ?? { kind: 'expr', expr: { kind: 'noop', loc: this.locOf(kw) }, loc: this.locOf(kw) };
        return { kind: 'while', cond, body, loc: this.locOf(kw) };
    }

    private parseDoWhile(): Stmt {
        const kw = this.advance();
        const body = this.parseStatement() ?? { kind: 'expr', expr: { kind: 'noop', loc: this.locOf(kw) }, loc: this.locOf(kw) };
        this.expect(T.KW_WHILE, '`while`');
        const cond = this.parseControlExpr();
        return { kind: 'while', cond, body, loc: this.locOf(kw) };
    }

    private parseControlExpr(): Expr {
        const saved = this.allowMapLiteral;
        this.allowMapLiteral = true;
        this.expect(T.LPAREN, '`(`');
        const e = this.parseExpression();
        this.expect(T.RPAREN, '`)`');
        this.allowMapLiteral = saved;
        return e;
    }

    private parseFor(): Stmt {
        const kw = this.advance();
        this.expect(T.LPAREN, '`(`');
        const id = this.expect(T.IDENT, 'un nom de variable de boucle');
        this.parseType(); // optional :type
        this.expect(T.KW_IN, '`in`');
        const iterable = this.parseExpression();
        this.expect(T.RPAREN, '`)`');
        const body = this.parseStatement() ?? { kind: 'expr', expr: { kind: 'noop', loc: this.locOf(kw) }, loc: this.locOf(kw) };
        return { kind: 'for', varName: id?.text ?? '_', varLoc: id ? this.locOf(id) : this.locOf(kw), iterable, body, loc: this.locOf(kw) };
    }

    private parseReturn(): Stmt {
        const kw = this.advance();
        let value: Expr | undefined;
        if (!this.check(T.RBRACE) && !this.check(T.SEMI) && !this.check(T.EOF)) value = this.parseExpression();
        this.match(T.SEMI);
        return { kind: 'return', value, loc: this.locOf(kw) };
    }

    private parseExprStmt(): Stmt {
        const start = this.peek();
        const lhs = this.parseExpression();
        // assign?
        if (this.check(T.ASSIGN) || this.check(T.PLUS_ASSIGN) || this.check(T.MINUS_ASSIGN) ||
            this.check(T.STAR_ASSIGN) || this.check(T.SLASH_ASSIGN)) {
            this.advance();
            const value = this.parseExpression();
            this.match(T.SEMI);
            return { kind: 'assign', target: lhs, value, loc: this.locOf(start) };
        }
        this.match(T.SEMI);
        return { kind: 'expr', expr: lhs, loc: this.locOf(start) };
    }

    // ── Expressions (récursives, précédence simplifiée mais suffisante pour la navigation) ──
    private parseExpression(): Expr { return this.parseFlow(); }

    private parseFlow(): Expr {
        let left = this.parseOr();
        for (;;) {
            if (this.check(T.ARROW_R) || this.check(T.ARROW_L) || this.check(T.ARROW_BOTH)) {
                const op = this.advance();
                const right = this.parseOr();
                left = { kind: 'binary', op: op.text, left, right, loc: this.locOf(op) };
            } else return left;
        }
    }
    private parseOr(): Expr {
        let left = this.parseAnd();
        while (this.match(T.KW_OR)) {
            const right = this.parseAnd();
            left = { kind: 'binary', op: 'or', left, right, loc: left.loc };
        }
        return left;
    }
    private parseAnd(): Expr {
        let left = this.parseEquality();
        while (this.match(T.KW_AND)) {
            const right = this.parseEquality();
            left = { kind: 'binary', op: 'and', left, right, loc: left.loc };
        }
        return left;
    }
    private parseEquality(): Expr {
        let left = this.parseComparison();
        while (this.check(T.EQ) || this.check(T.NEQ)) {
            const op = this.advance();
            const right = this.parseComparison();
            left = { kind: 'binary', op: op.text, left, right, loc: left.loc };
        }
        return left;
    }
    private parseComparison(): Expr {
        let left = this.parseConcat();
        while (this.check(T.LT) || this.check(T.LTE) || this.check(T.GT) || this.check(T.GTE) ||
               this.check(T.KW_IS) || this.check(T.KW_AS)) {
            const op = this.advance();
            const right = this.parseConcat();
            left = { kind: 'binary', op: op.text, left, right, loc: left.loc };
        }
        return left;
    }
    private parseConcat(): Expr {
        let left = this.parseAdditive();
        while (this.check(T.CONCAT)) {
            const op = this.advance();
            const right = this.parseAdditive();
            left = { kind: 'binary', op: op.text, left, right, loc: left.loc };
        }
        return left;
    }
    private parseAdditive(): Expr {
        let left = this.parseMultiplicative();
        while (this.check(T.PLUS) || this.check(T.MINUS)) {
            const op = this.advance();
            const right = this.parseMultiplicative();
            left = { kind: 'binary', op: op.text, left, right, loc: left.loc };
        }
        return left;
    }
    private parseMultiplicative(): Expr {
        let left = this.parseUnary();
        while (this.check(T.STAR) || this.check(T.SLASH) || this.check(T.PERCENT)) {
            const op = this.advance();
            const right = this.parseUnary();
            left = { kind: 'binary', op: op.text, left, right, loc: left.loc };
        }
        return left;
    }
    private parseUnary(): Expr {
        if (this.check(T.MINUS) || this.check(T.KW_NOT) || this.check(T.KW_AWAIT)) {
            const op = this.advance();
            const operand = this.parseUnary();
            return { kind: 'unary', op: op.text, operand, loc: this.locOf(op) };
        }
        if (this.check(T.KW_IMPORT)) {
            const kw = this.advance();
            const s = this.peek();
            if (s.kind === T.STRING) {
                this.advance();
                return { kind: 'import', path: this.decodeString(s.text), loc: this.locOf(kw) };
            }
            this.err('attendu une chaîne après `import`');
            return { kind: 'literal', value: null, loc: this.locOf(kw) };
        }
        return this.parsePostfix();
    }

    private parsePostfix(): Expr {
        let e = this.check(T.KW_NEW) ? this.parseNew() : this.parsePrimary();
        for (;;) {
            if (this.check(T.LPAREN)) {
                const open = this.advance();
                const args: Expr[] = [];
                if (!this.check(T.RPAREN)) {
                    const saved = this.allowMapLiteral;
                    this.allowMapLiteral = true;
                    do { args.push(this.parseExpression()); } while (this.match(T.COMMA));
                    this.allowMapLiteral = saved;
                }
                this.expect(T.RPAREN, '`)`');
                e = { kind: 'call', callee: e, args, loc: this.locOf(open) };
            } else if (this.check(T.LBRACK)) {
                this.advance();
                const idx = this.parseExpression();
                this.expect(T.RBRACK, '`]`');
                e = { kind: 'index', object: e, index: idx, loc: e.loc };
            } else if (this.check(T.DOT)) {
                const dot = this.advance();
                const id = this.advance();
                e = { kind: 'member', object: e, name: id.text, nameLoc: this.locOf(id), loc: this.locOf(dot) };
            } else break;
        }
        return e;
    }

    private parseNew(): Expr {
        const kw = this.advance();
        const path: string[] = [];
        const first = this.expect(T.IDENT, 'un nom de classe');
        if (first) path.push(first.text);
        while (this.match(T.DOT)) { const n = this.expect(T.IDENT, ''); if (n) path.push(n.text); }
        if (this.check(T.LT)) { this.advance(); do { this.parseTypeExpr(); } while (this.match(T.COMMA)); this.expect(T.GT, '`>`'); }
        let callee: Expr = { kind: 'ident', name: path[0] ?? '?', loc: this.locOf(kw) };
        for (let k = 1; k < path.length; k++) callee = { kind: 'member', object: callee, name: path[k], nameLoc: this.locOf(first!), loc: this.locOf(first!) };
        if (this.match(T.LPAREN)) {
            const args: Expr[] = [];
            if (!this.check(T.RPAREN)) { do { args.push(this.parseExpression()); } while (this.match(T.COMMA)); }
            this.expect(T.RPAREN, '`)`');
            return { kind: 'call', callee, args, loc: this.locOf(kw) };
        }
        return callee;
    }

    private parsePrimary(): Expr {
        const t = this.peek();
        switch (t.kind) {
            case T.NUMBER: this.advance(); return { kind: 'literal', value: t.text, loc: this.locOf(t) };
            case T.STRING: this.advance(); return { kind: 'literal', value: this.decodeString(t.text), loc: this.locOf(t) };
            case T.KW_TRUE: case T.KW_FALSE: this.advance(); return { kind: 'literal', value: t.kind === T.KW_TRUE, loc: this.locOf(t) };
            case T.KW_NIL: this.advance(); return { kind: 'literal', value: null, loc: this.locOf(t) };
            case T.IDENT: this.advance(); return { kind: 'ident', name: t.text, loc: this.locOf(t) };
            case T.KW_THIS: this.advance(); return { kind: 'this', loc: this.locOf(t) };
            case T.KW_SUPER: {
                this.advance();
                if (this.match(T.DOT)) {
                    const m = this.expect(T.IDENT, 'un nom de méthode');
                    return { kind: 'super', method: m?.text ?? '', loc: this.locOf(t) };
                }
                return { kind: 'super', method: 'init', loc: this.locOf(t) };
            }
            case T.KW_FN: {
                this.advance();
                const typeParams = this.parseTypeParams();
                const params = this.parseParams();
                const returnType = this.parseType();
                const body = this.parseBody();
                return { kind: 'lambda', params, returnType, typeParams, body, loc: this.locOf(t) };
            }
            case T.KW_ASYNC: {
                this.advance();
                if (!this.check(T.KW_FN)) { this.err('attendu `fn` après `async`'); return { kind: 'literal', value: null, loc: this.locOf(t) }; }
                this.advance();
                const typeParams = this.parseTypeParams();
                const params = this.parseParams();
                const returnType = this.parseType();
                const body = this.parseBody();
                return { kind: 'lambda', params, returnType, typeParams, body, isAsync: true, loc: this.locOf(t) };
            }
            case T.LPAREN: {
                this.advance();
                const saved = this.allowMapLiteral; this.allowMapLiteral = true;
                const inner = this.parseExpression();
                this.allowMapLiteral = saved;
                this.expect(T.RPAREN, '`)`');
                return inner;
            }
            case T.LBRACK: {
                this.advance();
                const items: Expr[] = [];
                const saved = this.allowMapLiteral; this.allowMapLiteral = true;
                if (!this.check(T.RBRACK)) {
                    do { items.push(this.parseExpression()); if (this.check(T.RBRACK)) break; } while (this.match(T.COMMA));
                }
                this.allowMapLiteral = saved;
                this.expect(T.RBRACK, '`]`');
                return { kind: 'list', items, loc: this.locOf(t) };
            }
            case T.LBRACE: {
                if (!this.allowMapLiteral) { this.err('`{` ouvre un bloc ici — entourez la table de parenthèses'); return { kind: 'literal', value: null, loc: this.locOf(t) }; }
                this.advance();
                const saved = this.allowMapLiteral; this.allowMapLiteral = true;
                const entries: { key: Expr; value: Expr }[] = [];
                if (!this.check(T.RBRACE)) {
                    do {
                        let key: Expr;
                        if (this.check(T.IDENT) && this.peek(1).kind === T.COLON) {
                            const id = this.advance();
                            key = { kind: 'literal', value: id.text, loc: this.locOf(id) };
                        } else key = this.parseExpression();
                        this.expect(T.COLON, '`:`');
                        const value = this.parseExpression();
                        entries.push({ key, value });
                        if (this.check(T.RBRACE)) break;
                    } while (this.match(T.COMMA));
                }
                this.allowMapLiteral = saved;
                this.expect(T.RBRACE, '`}`');
                return { kind: 'map', entries, loc: this.locOf(t) };
            }
            default: {
                this.err(`expression attendue, trouvé \`${t.text || 'EOF'}\``);
                this.advance();
                return { kind: 'literal', value: null, loc: this.locOf(t) };
            }
        }
    }
}