export enum T {
    EOF, IDENT, NUMBER, STRING,
    // keywords
    KW_LET, KW_VAR, KW_CONST, KW_STATIC,
    KW_NAMESPACE, KW_FN, KW_IF, KW_ELSE, KW_WHILE, KW_DO, KW_FOR, KW_IN,
    KW_RETURN, KW_BREAK, KW_CONTINUE, KW_TRUE, KW_FALSE, KW_NIL,
    KW_AND, KW_OR, KW_NOT,
    KW_CLASS, KW_INTERFACE, KW_ABSTRACT, KW_FACTORY, KW_EXTENDS,
    KW_IMPLEMENTS, KW_OVERRIDE, KW_NEW, KW_THIS, KW_SUPER, KW_IS, KW_AS,
    KW_ASYNC, KW_AWAIT, KW_ENUM, KW_OPERATOR, KW_IMPORT,
    // punctuation
    LPAREN, RPAREN, LBRACE, RBRACE, LBRACK, RBRACK,
    COMMA, COLON, SEMI, DOT, QUESTION,
    // operators
    PLUS, MINUS, STAR, SLASH, PERCENT,
    ASSIGN, PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN, SLASH_ASSIGN,
    EQ, NEQ, LT, LTE, GT, GTE, CONCAT,
    ARROW_R, ARROW_L, ARROW_BOTH
}

export interface Tok {
    kind: T;
    text: string;
    line: number;   // 1-based
    col: number;    // 1-based
    offset: number; // 0-based
    end: number;    // 0-based exclusive
}

const KEYWORDS: Record<string, T> = {
    'let': T.KW_LET, 'var': T.KW_VAR, 'const': T.KW_CONST, 'static': T.KW_STATIC,
    'namespace': T.KW_NAMESPACE, 'fn': T.KW_FN, 'func': T.KW_FN,
    'if': T.KW_IF, 'else': T.KW_ELSE, 'while': T.KW_WHILE, 'do': T.KW_DO,
    'for': T.KW_FOR, 'in': T.KW_IN,
    'return': T.KW_RETURN, 'break': T.KW_BREAK, 'continue': T.KW_CONTINUE,
    'true': T.KW_TRUE, 'false': T.KW_FALSE, 'nil': T.KW_NIL, 'null': T.KW_NIL,
    'and': T.KW_AND, 'or': T.KW_OR, 'not': T.KW_NOT,
    'class': T.KW_CLASS, 'interface': T.KW_INTERFACE,
    'abstract': T.KW_ABSTRACT, 'factory': T.KW_FACTORY,
    'extends': T.KW_EXTENDS, 'implements': T.KW_IMPLEMENTS, 'override': T.KW_OVERRIDE,
    'new': T.KW_NEW, 'this': T.KW_THIS, 'super': T.KW_SUPER,
    'is': T.KW_IS, 'as': T.KW_AS,
    'async': T.KW_ASYNC, 'await': T.KW_AWAIT,
    'enum': T.KW_ENUM, 'operator': T.KW_OPERATOR,
    'import': T.KW_IMPORT
};

export interface LexError {
    message: string;
    line: number;
    col: number;
}

export function lex(src: string): { tokens: Tok[]; errors: LexError[] } {
    const tokens: Tok[] = [];
    const errors: LexError[] = [];
    let i = 0, line = 1, col = 1;

    const adv = (n = 1) => {
        for (let k = 0; k < n; k++) {
            if (src[i] === '\n') { line++; col = 1; } else { col++; }
            i++;
        }
    };
    const push = (kind: T, text: string, startI: number, startL: number, startC: number) => {
        tokens.push({ kind, text, line: startL, col: startC, offset: startI, end: i });
    };

    while (i < src.length) {
        const c = src[i];

        // Whitespace
        if (c === ' ' || c === '\t' || c === '\r' || c === '\n') { adv(); continue; }

        // Comments
        if (c === '#') { while (i < src.length && src[i] !== '\n') adv(); continue; }
        if (c === '/' && src[i + 1] === '/') { while (i < src.length && src[i] !== '\n') adv(); continue; }
        if (c === '/' && src[i + 1] === '*') {
            const sl = line, sc = col;
            adv(2);
            while (i < src.length && !(src[i] === '*' && src[i + 1] === '/')) adv();
            if (i >= src.length) { errors.push({ message: 'commentaire de bloc non terminé', line: sl, col: sc }); break; }
            adv(2);
            continue;
        }

        const sL = line, sC = col, sI = i;

        // Number
        if (c >= '0' && c <= '9') {
            while (i < src.length && /[0-9]/.test(src[i])) adv();
            if (src[i] === '.' && /[0-9]/.test(src[i + 1] || '')) {
                adv();
                while (i < src.length && /[0-9]/.test(src[i])) adv();
            }
            if (src[i] === 'e' || src[i] === 'E') {
                const save = i, saveL = line, saveC = col;
                adv();
                if (src[i] === '+' || src[i] === '-') adv();
                if (/[0-9]/.test(src[i] || '')) {
                    while (i < src.length && /[0-9]/.test(src[i])) adv();
                } else {
                    i = save; line = saveL; col = saveC;
                }
            }
            push(T.NUMBER, src.substring(sI, i), sI, sL, sC);
            continue;
        }

        // Identifier (UTF-8 friendly: accept any non-ASCII byte)
        if (/[a-zA-Z_]/.test(c) || c.charCodeAt(0) >= 0x80) {
            while (i < src.length && (/[a-zA-Z0-9_]/.test(src[i]) || src[i].charCodeAt(0) >= 0x80)) adv();
            const word = src.substring(sI, i);
            push(KEYWORDS[word] ?? T.IDENT, word, sI, sL, sC);
            continue;
        }

        // String
        if (c === '"' || c === "'") {
            const q = c; adv();
            let closed = false;
            while (i < src.length) {
                if (src[i] === '\\') { adv(2); continue; }
                if (src[i] === q) { adv(); closed = true; break; }
                adv();
            }
            if (!closed) errors.push({ message: 'chaîne non terminée', line: sL, col: sC });
            push(T.STRING, src.substring(sI, i), sI, sL, sC);
            continue;
        }

        // Operators / punctuation
        const two = src.substring(i, i + 2);
        const three = src.substring(i, i + 3);
        if (three === '<->') { adv(3); push(T.ARROW_BOTH, three, sI, sL, sC); continue; }
        if (two === '->') { adv(2); push(T.ARROW_R, two, sI, sL, sC); continue; }
        if (two === '<-') { adv(2); push(T.ARROW_L, two, sI, sL, sC); continue; }
        if (two === '..') { adv(2); push(T.CONCAT, two, sI, sL, sC); continue; }
        if (two === '==') { adv(2); push(T.EQ, two, sI, sL, sC); continue; }
        if (two === '!=') { adv(2); push(T.NEQ, two, sI, sL, sC); continue; }
        if (two === '<=') { adv(2); push(T.LTE, two, sI, sL, sC); continue; }
        if (two === '>=') { adv(2); push(T.GTE, two, sI, sL, sC); continue; }
        if (two === '+=') { adv(2); push(T.PLUS_ASSIGN, two, sI, sL, sC); continue; }
        if (two === '-=') { adv(2); push(T.MINUS_ASSIGN, two, sI, sL, sC); continue; }
        if (two === '*=') { adv(2); push(T.STAR_ASSIGN, two, sI, sL, sC); continue; }
        if (two === '/=') { adv(2); push(T.SLASH_ASSIGN, two, sI, sL, sC); continue; }

        const singles: Record<string, T> = {
            '(': T.LPAREN, ')': T.RPAREN, '{': T.LBRACE, '}': T.RBRACE,
            '[': T.LBRACK, ']': T.RBRACK,
            ',': T.COMMA, ':': T.COLON, ';': T.SEMI, '.': T.DOT, '?': T.QUESTION,
            '+': T.PLUS, '-': T.MINUS, '*': T.STAR, '/': T.SLASH, '%': T.PERCENT,
            '=': T.ASSIGN, '<': T.LT, '>': T.GT
        };
        if (singles[c]) { adv(); push(singles[c], c, sI, sL, sC); continue; }

        errors.push({ message: `caractère inattendu '${c}'`, line: sL, col: sC });
        adv();
    }

    tokens.push({ kind: T.EOF, text: '', line, col, offset: i, end: i });
    return { tokens, errors };
}