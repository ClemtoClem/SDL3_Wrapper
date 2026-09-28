#!/usr/bin/env python3
"""
splitter.py — sépare un en-tête C++ « header-only » en une paire
en-tête (déclarations) / source (définitions).

Principes
---------
* L'en-tête produit est l'ORIGINAL, octet pour octet, où seuls les corps des
  fonctions déplacées sont remplacés par `;` : commentaires, mise en forme et
  style (tabulations ou espaces) sont intacts.
* Le code des fonctions n'est jamais modifié : le corps est déplacé tel quel,
  seulement désindenté (il quitte la classe qui l'entourait). Seule la
  signature est réécrite, comme l'exige le C++ hors de la classe : nom
  qualifié (`Classe::Fonction`), arguments par défaut retirés, `static`,
  `virtual`, `explicit`, `inline`, `override`, `final` et attributs `[[...]]`
  retirés, types imbriqués du type de retour qualifiés (`Classe::Noeud *`).
* Reste dans l'en-tête ce qui DOIT y rester ou gagne à y rester : templates,
  fonctions `constexpr`/`consteval`, type de retour déduit (`auto` sans `->`),
  paramètres `auto` (templates abrégés), opérateurs, fonctions `friend`,
  fonctions à liaison interne (`static` hors classe, espace de noms anonyme),
  membres de classes sans nom, fonctions tenant sur une ligne (accesseurs :
  on garde leur inlining) et fonctions qui utilisent une macro que l'en-tête
  `#undef` plus loin.
* Les directives `#if`/`#ifdef`/`#elif`/`#else`/`#endif` et
  `#pragma GCC diagnostic` qui entourent une fonction déplacée
  l'accompagnent dans le source ; les groupes restés vides disparaissent.

Utilisation
-----------
    splitter.py ENTETE.hpp --include-root DIR --src-root DIR [--dry-run]

Réécrit l'en-tête sur place et crée `<src-root>/<chemin relatif>.cpp` si au
moins une fonction a été déplacée (sinon rien n'est écrit). Affiche une ligne
de bilan : `<chemin> : N déplacées, M gardées en ligne`.
Code de retour : 0 succès, 1 erreur d'analyse (fichier laissé intact).
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from typing import Optional

# ============================================================================
# Lexique
# ============================================================================

WS, COMMENT, PP, IDENT, NUMBER, STRING, CHAR, PUNCT = range(8)


@dataclass
class Token:
	kind: int
	text: str
	start: int  # position du premier caractère dans le texte source
	end: int  # position après le dernier caractère

	def is_trivia(self) -> bool:
		return self.kind in (WS, COMMENT)


# Ponctuations de plusieurs caractères, les plus longues d'abord. `<` et `>`
# restent toujours isolés : `>>` peut fermer deux listes de template.
PUNCTUATORS = ["->*", "...", "::", "->", ".*", "##", "==", "!=", "&&", "||", "++", "--",
			   "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^="]

RAW_STRING = re.compile(r'(?:u8|u|U|L)?R"([^()\\ \t\n]{0,16})\(')
QUOTED = re.compile(r'(?:u8|u|U|L)?["\']')
NUMBER_RE = re.compile(r"\.?[0-9](?:[eEpP][+-]|[0-9A-Za-z_.']|)*")
IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


class SplitError(Exception):
	"""Texte que l'analyseur ne sait pas découper sans risque."""


def tokenize(text: str) -> list[Token]:
	tokens: list[Token] = []
	i, n = 0, len(text)
	at_line_start = True  # seuls des blancs depuis le début de la ligne

	def add(kind: int, end: int) -> None:
		nonlocal i
		tokens.append(Token(kind, text[i:end], i, end))
		i = end

	while i < n:
		c = text[i]
		if c in " \t\r\n\f\v":
			j = i
			while j < n and text[j] in " \t\r\n\f\v":
				at_line_start = at_line_start or text[j] == "\n"
				j += 1
			add(WS, j)
			continue
		if text.startswith("//", i):
			j = text.find("\n", i)
			add(COMMENT, n if j == -1 else j)
			continue
		if text.startswith("/*", i):
			j = text.find("*/", i + 2)
			if j == -1:
				raise SplitError("commentaire /* non fermé")
			add(COMMENT, j + 2)
			continue
		if c == "#" and at_line_start:
			add(PP, _directive_end(text, i))
			continue
		at_line_start = False
		m = RAW_STRING.match(text, i)
		if m:
			close = text.find(")" + m.group(1) + '"', m.end())
			if close == -1:
				raise SplitError("chaîne brute non fermée")
			add(STRING, close + len(m.group(1)) + 2)
			continue
		m = QUOTED.match(text, i)
		if m:
			quote = text[m.end() - 1]
			j = m.end()
			while j < n and text[j] != quote:
				if text[j] == "\n":
					raise SplitError(f"chaîne non fermée à l'octet {i}")
				j += 2 if text[j] == "\\" else 1
			add(STRING if quote == '"' else CHAR, j + 1)
			continue
		m = IDENT_RE.match(text, i)
		if m:
			add(IDENT, m.end())
			continue
		if c.isdigit() or (c == "." and text[i + 1:i + 2].isdigit()):
			add(NUMBER, NUMBER_RE.match(text, i).end())
			continue
		for p in PUNCTUATORS:
			if text.startswith(p, i):
				add(PUNCT, i + len(p))
				break
		else:
			add(PUNCT, i + 1)
	return tokens


def _directive_end(text: str, i: int) -> int:
	"""Fin d'une directive : la ligne logique (suites `\\` comprises)."""
	n = len(text)
	j = i
	while j < n:
		if text.startswith("/*", j):  # commentaire bloc au milieu d'une directive
			k = text.find("*/", j + 2)
			j = n if k == -1 else k + 2
			continue
		if text[j] == "\n":
			k = j - 1
			while k > i and text[k] in " \t\r":
				k -= 1
			if text[k] != "\\":
				return j
		j += 1
	return n


# ============================================================================
# Parcours des jetons
# ============================================================================

CLASS_KEYS = {"class", "struct", "union"}
ACCESS = {"public", "private", "protected"}
# Mots qui, suivis de `(`, ne sont pas le nom d'une fonction déclarée.
NOT_A_NAME = {
	"alignas", "alignof", "decltype", "noexcept", "sizeof", "static_assert", "requires",
	"__attribute__", "__declspec", "throw", "typeid", "if", "while", "for", "switch", "return",
	"void", "bool", "char", "short", "int", "long", "float", "double", "signed", "unsigned",
	"const", "volatile", "auto", "wchar_t", "char8_t", "char16_t", "char32_t",
}
SPEC_ATTRIBUTES = {"alignas", "__attribute__", "__declspec"}
DROPPED_IN_DEFINITION = {"inline", "static", "virtual", "explicit"}
KEEP_INLINE_SPECIFIERS = {"constexpr", "consteval", "friend"}
MACRO_NAME = re.compile(r"^[A-Z][A-Z0-9]*_[A-Z0-9_]+$")
FORCE_INLINE_HINT = re.compile(r"always_inline|FORCE_INLINE|forceinline", re.IGNORECASE)
OPENERS = {"(": ")", "[": "]", "{": "}"}
CLOSERS = {")", "]", "}"}


class Cursor:
	"""Accès aux jetons, en sautant blancs et commentaires."""

	def __init__(self, tokens: list[Token]):
		self.t = tokens
		self.n = len(tokens)

	def next_sig(self, i: int) -> int:
		while i < self.n and self.t[i].is_trivia():
			i += 1
		return i

	def prev_sig(self, i: int) -> int:
		i -= 1
		while i >= 0 and self.t[i].is_trivia():
			i -= 1
		return i

	def text(self, i: int) -> str:
		return self.t[i].text if 0 <= i < self.n else ""

	def kind(self, i: int) -> int:
		return self.t[i].kind if 0 <= i < self.n else -1

	def is_(self, i: int, text: str) -> bool:
		return 0 <= i < self.n and self.t[i].kind in (PUNCT, IDENT) and self.t[i].text == text

	def match(self, i: int) -> int:
		"""t[i] ouvre ( [ { : indice du fermant correspondant."""
		expected = [OPENERS[self.t[i].text]]
		for j in range(i + 1, self.n):
			tok = self.t[j]
			if tok.kind != PUNCT:
				continue
			if tok.text in OPENERS:
				expected.append(OPENERS[tok.text])
			elif tok.text in CLOSERS:
				if tok.text != expected[-1]:
					raise SplitError(f"`{tok.text}` inattendu à l'octet {tok.start} (attendu `{expected[-1]}`)")
				expected.pop()
				if not expected:
					return j
		raise SplitError(f"`{self.t[i].text}` non fermé (octet {self.t[i].start})")

	def match_angle(self, i: int) -> int:
		"""t[i] == '<' ouvrant des arguments de template : indice du '>' fermant.
		Les parenthèses protègent d'éventuelles comparaisons internes."""
		depth = 0
		j = i
		while j < self.n:
			tok = self.t[j]
			if tok.kind == PUNCT:
				if tok.text in OPENERS:
					j = self.match(j) + 1
					continue
				if tok.text == "<":
					nxt = self.t[j + 1] if j + 1 < self.n else None
					if depth > 0 and nxt is not None and nxt.text == "<" and nxt.start == tok.end:
						j += 2  # `<<` : décalage, pas un chevron
						continue
					depth += 1
				elif tok.text == ">":
					depth -= 1
					if depth == 0:
						return j
				elif tok.text in (";", "{", "}", ")", "]"):
					break
			j += 1
		raise SplitError(f"`<` non fermé (octet {self.t[i].start})")

	def skip_to_semicolon(self, i: int) -> int:
		"""Indice du `;` qui termine l'instruction commencée en i (accolades,
		lambdas et listes d'initialisation traversées)."""
		j = i
		while j < self.n:
			tok = self.t[j]
			if tok.kind == PUNCT:
				if tok.text in OPENERS:
					j = self.match(j) + 1
					continue
				if tok.text == ";":
					return j
				if tok.text in CLOSERS:
					break
			j += 1
		raise SplitError(f"`;` manquant après l'octet {self.t[i].start}")


# ============================================================================
# Arbre des déclarations
# ============================================================================


@dataclass
class Scope:
	"""Espace de noms, classe ou fichier."""
	kind: str  # "file" | "namespace" | "class" | "template"
	name: str = ""  # classe : nom, éventuellement qualifié (`A::B`)
	parent: Optional["Scope"] = None
	anonymous: bool = False  # espace de noms anonyme, classe sans nom
	types: set = field(default_factory=set)  # noms de types déclarés dans cette portée
	functions: set = field(default_factory=set)  # noms des fonctions déclarées (classe)

	def class_path(self) -> str:
		"""`Externe::Interne` pour une classe, vide hors de toute classe."""
		parts = []
		s = self
		while s is not None and s.kind == "class":
			parts.append(s.name)
			s = s.parent
		return "::".join(reversed(parts))

	def namespace_path(self) -> list[str]:
		parts = []
		s = self
		while s is not None:
			if s.kind == "namespace" and s.name:
				parts.append(s.name)
			s = s.parent
		return list(reversed(parts))

	def in_template_or_anonymous(self) -> bool:
		s = self
		while s is not None:
			if s.anonymous or s.kind == "template":
				return True
			s = s.parent
		return False

	def owner_of_type(self, name: str) -> Optional[str]:
		"""Chemin de la classe englobante qui déclare le type `name`."""
		s = self
		while s is not None and s.kind == "class":
			if name in s.types:
				return s.class_path()
			s = s.parent
		return None


@dataclass
class Item:
	start: int  # premier jeton (inclus)
	end: int  # dernier jeton + 1


@dataclass
class Verbatim(Item):
	"""Recopié tel quel dans l'en-tête."""


@dataclass
class Directive(Item):
	"""Directive du préprocesseur."""


@dataclass
class Block(Item):
	"""Espace de noms ou classe : en-tête et fin recopiés, corps analysé."""
	scope: Scope = None
	open_brace: int = 0
	close_brace: int = 0
	children: list = field(default_factory=list)


@dataclass
class Function(Item):
	scope: Scope = None
	name_start: int = 0  # premier jeton du nom déclaré (qualifié ou non)
	name_end: int = 0  # jeton qui suit le nom
	params_open: int = 0
	params_close: int = 0
	suffix_end: int = 0  # jeton qui suit les qualificatifs (const, noexcept, -> T…)
	init_start: Optional[int] = None  # `:` d'une liste d'initialisation
	body_open: Optional[int] = None  # None : simple déclaration
	body_close: Optional[int] = None
	qualified_name: str = ""
	extract: bool = False
	keep_reason: str = ""


# ============================================================================
# Analyse
# ============================================================================


class Parser:
	def __init__(self, text: str):
		self.text = text
		self.tokens = tokenize(text)
		self.c = Cursor(self.tokens)
		directives = [(k, *self._directive(tok.text)) for k, tok in enumerate(self.tokens) if tok.kind == PP]
		self.function_macros = {m.group(1) for _, word, rest in directives
								if word == "define" and (m := re.match(r"(\w+)\(", rest))}
		self.undefined_macros = {rest.split()[0] for _, word, rest in directives if word == "undef" and rest.split()}
		self.include_guard = self._include_guard(directives)

	@staticmethod
	def _directive(text: str) -> tuple[str, str]:
		m = re.match(r"#\s*(\w*)\s*(.*)", text, re.S)
		return (m.group(1), m.group(2)) if m else ("", "")

	def _include_guard(self, directives) -> set:
		"""Jetons d'une garde `#ifndef X / #define X / … / #endif` englobant
		tout le fichier : à ne pas recopier dans le source (X y est défini)."""
		c = self.c
		if len(directives) < 3:
			return set()
		(k0, w0, r0), (k1, w1, r1), (kn, wn, _) = directives[0], directives[1], directives[-1]
		first, last = c.next_sig(0), c.prev_sig(c.n)
		if (w0 == "ifndef" and w1 == "define" and wn == "endif" and k0 == first and kn == last
				and r0.split()[:1] == r1.split()[:1]):
			return {k0, k1, kn}
		return set()

	# ── portées ────────────────────────────────────────────────────────────

	def parse_file(self) -> Block:
		scope = Scope("file")
		root = Block(0, self.c.n, scope=scope, open_brace=-1, close_brace=self.c.n)
		root.children = self.parse_scope(0, self.c.n, scope)
		return root

	def parse_scope(self, i: int, end: int, scope: Scope) -> list:
		items = []
		while i < end:
			tok = self.tokens[i]
			if tok.is_trivia():
				j = i
				while j < end and self.tokens[j].is_trivia():
					j += 1
				items.append(Verbatim(i, j))
			elif tok.kind == PP:
				items.append(Directive(i, i + 1))
			else:
				items.append(self.parse_declaration(i, end, scope))
			i = items[-1].end
		return items

	def parse_declaration(self, i: int, end: int, scope: Scope) -> Item:
		c = self.c
		word = c.text(i)

		if c.is_(i, ";"):
			return Verbatim(i, i + 1)
		if word in ACCESS and c.is_(c.next_sig(i + 1), ":"):
			return Verbatim(i, c.next_sig(i + 1) + 1)

		# Invocation d'une macro « fonction » : définie dans le fichier, ou nom
		# EN_MAJUSCULES (convention des macros) — souvent sans `;` final.
		if (c.kind(i) == IDENT and (word in self.function_macros or MACRO_NAME.match(word))
				and c.is_(c.next_sig(i + 1), "(")):
			close = c.match(c.next_sig(i + 1))
			after = c.next_sig(close + 1)
			return Verbatim(i, after + 1 if c.is_(after, ";") else close + 1)

		# template <…> déclaration : tout reste dans l'en-tête.
		if word == "template":
			j = c.next_sig(i + 1)
			if c.is_(j, "<"):
				j = c.next_sig(c.match_angle(j) + 1)
			self._register_template_name(j, scope)
			inner = self.parse_declaration(j, end, Scope("template", parent=scope))
			return Verbatim(i, inner.end)

		# extern "C" { … }
		if word == "extern" and c.kind(c.next_sig(i + 1)) == STRING:
			k = c.next_sig(c.next_sig(i + 1) + 1)
			if c.is_(k, "{"):
				return Verbatim(i, c.match(k) + 1)

		if word == "namespace" or (word == "inline" and c.is_(c.next_sig(i + 1), "namespace")):
			return self.parse_namespace(i, end, scope)

		if word in ("using", "typedef", "static_assert"):
			semi = c.skip_to_semicolon(i)
			self._register_alias(i, semi, word, scope)
			return Verbatim(i, semi + 1)

		if word in CLASS_KEYS or word == "enum":
			block = self.try_parse_class(i, end, scope)
			if block is not None:
				return block

		return self.parse_function_or_variable(i, end, scope)

	def parse_namespace(self, i: int, end: int, scope: Scope) -> Item:
		c = self.c
		j = c.next_sig(i + 1)
		if c.text(i) == "inline":
			j = c.next_sig(j + 1)
		name = ""
		while j < end and (c.kind(j) == IDENT or c.is_(j, "::")):
			name += c.text(j)
			j = c.next_sig(j + 1)
		if not c.is_(j, "{"):  # namespace A = B::C;
			return Verbatim(i, c.skip_to_semicolon(i) + 1)
		close = c.match(j)
		inner = Scope("namespace", name, scope, anonymous=(name == ""))
		block = Block(i, close + 1, scope=inner, open_brace=j, close_brace=close)
		block.children = self.parse_scope(j + 1, close, inner)
		return block

	def try_parse_class(self, i: int, end: int, scope: Scope) -> Optional[Item]:
		"""`class/struct/union/enum … { … } [déclarateurs] ;` — None si ce
		n'est pas une définition (déclaration anticipée, type élaboré…)."""
		c = self.c
		is_enum = c.text(i) == "enum"
		j = c.next_sig(i + 1)
		if is_enum and c.text(j) in ("class", "struct"):
			j = c.next_sig(j + 1)
		segments: list[str] = []  # identifiants (qualifiés) rencontrés
		prev_was_scope_op = False
		while j < end and not c.is_(j, "{"):
			tok = self.tokens[j]
			if tok.kind == IDENT:
				if tok.text in SPEC_ATTRIBUTES:
					k = c.next_sig(j + 1)
					j = c.next_sig(c.match(k) + 1) if c.is_(k, "(") else k
					continue
				if tok.text != "final":
					if prev_was_scope_op and segments:
						segments[-1] += tok.text
					else:
						segments.append(tok.text)
				prev_was_scope_op = False
			elif c.is_(j, "::"):
				if segments:
					segments[-1] += "::"
				prev_was_scope_op = True
			elif c.is_(j, "["):  # attribut [[…]]
				j = c.next_sig(c.match(j) + 1)
				continue
			elif c.is_(j, "<"):  # spécialisation (forcément dans un template)
				j = c.next_sig(c.match_angle(j) + 1)
				continue
			elif c.is_(j, ":"):  # base(s) ou type sous-jacent : jusqu'à `{`
				k = j + 1
				while k < end and not c.is_(k, "{"):
					if c.is_(k, "<"):
						k = c.match_angle(k)
					elif c.is_(k, "(") or c.is_(k, "["):
						k = c.match(k)
					elif c.is_(k, ";"):
						return None
					k += 1
				j = k
				break
			else:
				return None  # `(`, `;`, `*`, `=`… : pas une définition
			j = c.next_sig(j + 1)
		if not c.is_(j, "{"):
			return None
		close = c.match(j)
		semi = c.skip_to_semicolon(close + 1)
		name = segments[-1] if segments else ""
		if name:
			scope.types.add(name.split("::")[-1])
		if is_enum:
			return Verbatim(i, semi + 1)
		inner = Scope("class", name, scope, anonymous=(name == ""))
		block = Block(i, semi + 1, scope=inner, open_brace=j, close_brace=close)
		block.children = self.parse_scope(j + 1, close, inner)
		return block

	def _register_alias(self, i: int, semi: int, word: str, scope: Scope) -> None:
		c = self.c
		if word == "using":
			j = c.next_sig(i + 1)
			if c.kind(j) == IDENT and c.is_(c.next_sig(j + 1), "="):
				scope.types.add(c.text(j))
		elif word == "typedef":
			for k in range(i, semi):  # typedef R (*Nom)(…);
				if c.is_(k, "(") and c.is_(c.next_sig(k + 1), "*"):
					scope.types.add(c.text(c.next_sig(c.next_sig(k + 1) + 1)))
					return
			idents = [k for k in range(i + 1, semi) if c.kind(k) == IDENT]
			if idents:
				scope.types.add(c.text(idents[-1]))

	def _register_template_name(self, j: int, scope: Scope) -> None:
		"""`template <…> class Nom` ou `template <…> using Nom = …` : Nom est
		un type de la portée."""
		c = self.c
		if c.text(j) in CLASS_KEYS or c.text(j) == "using":
			k = c.next_sig(j + 1)
			if c.kind(k) == IDENT:
				scope.types.add(c.text(k))

	# ── fonctions et variables ─────────────────────────────────────────────

	def parse_function_or_variable(self, i: int, end: int, scope: Scope) -> Item:
		"""Cherche un déclarateur de fonction (`nom (…)` au premier niveau,
		suivi de qualificatifs puis de `;`, `= default ;`, d'une liste
		d'initialisation ou d'un corps). À défaut : variable ou autre
		déclaration, recopiée jusqu'à son `;`."""
		c = self.c
		saw_parens = False
		j = i
		while j < end:
			tok = self.tokens[j]
			if tok.is_trivia() or tok.kind == PP:
				j += 1
				continue
			if tok.kind == IDENT and tok.text == "operator":
				fn = self._try_operator(i, j, end, scope)
				if fn is not None:
					return fn
			if tok.kind == PUNCT:
				if tok.text == "<":
					j = c.match_angle(j) + 1
					continue
				if tok.text == "[":
					j = c.match(j) + 1
					continue
				if tok.text == "(":
					name_start = self._name_before(j, i)
					if name_start is not None:
						fn = self._finish_function(i, name_start, j, end, scope)
						if fn is not None:
							return fn
					saw_parens = True
					j = c.match(j) + 1
					continue
				if tok.text == "{" and saw_parens:
					# Corps d'une signature non reconnue (macro…) : gardé tel quel.
					close = c.match(j)
					after = c.next_sig(close + 1)
					return Verbatim(i, after + 1 if c.is_(after, ";") else close + 1)
				if tok.text in ("=", "{", ";"):
					break
				if tok.text in CLOSERS:
					raise SplitError(f"`{tok.text}` inattendu (octet {tok.start})")
			j += 1
		return Verbatim(i, c.skip_to_semicolon(i) + 1)

	def _name_before(self, paren: int, first: int) -> Optional[int]:
		"""Début du nom (qualifié, `~` compris) juste avant `paren`, ou None
		si ce qui précède n'est pas un nom de fonction."""
		c = self.c
		k = c.prev_sig(paren)
		if k < first or c.kind(k) != IDENT or c.text(k) in NOT_A_NAME:
			return None
		start = k
		while True:
			p = c.prev_sig(start)
			if p >= first and c.is_(p, "~"):
				start, p = p, c.prev_sig(p)
			if p >= first and c.is_(p, "::"):
				q = c.prev_sig(p)
				if q >= first and c.kind(q) == IDENT and c.text(q) not in NOT_A_NAME:
					start = q
					continue
				start = p  # ::nom
			return start

	def _try_operator(self, first: int, op: int, end: int, scope: Scope) -> Optional[Function]:
		c = self.c
		j = c.next_sig(op + 1)
		if c.is_(j, "("):  # operator()
			j = c.next_sig(c.match(j) + 1)
		while j < end and not c.is_(j, "("):
			if c.is_(j, ";") or c.is_(j, "{"):
				return None
			if c.is_(j, "[") and c.is_(c.next_sig(j + 1), "]"):  # operator[] / new[]
				j = c.next_sig(j + 1)
			j = c.next_sig(j + 1)
		if not c.is_(j, "("):
			return None
		start = op
		p = c.prev_sig(op)
		while p >= first and c.is_(p, "::") and c.kind(c.prev_sig(p)) == IDENT:
			start = c.prev_sig(p)
			p = c.prev_sig(start)
		return self._finish_function(first, start, j, end, scope, is_operator=True)

	def _finish_function(self, first: int, name_start: int, paren: int, end: int, scope: Scope,
						 is_operator: bool = False) -> Optional[Function]:
		c = self.c
		close = c.match(paren)
		j = c.next_sig(close + 1)
		while j < end:  # qualificatifs
			w = c.text(j)
			if c.kind(j) not in (IDENT, PUNCT):
				break
			if w in ("const", "volatile", "&", "&&", "override", "final"):
				j = c.next_sig(j + 1)
			elif w in ("noexcept", "throw"):
				k = c.next_sig(j + 1)
				j = c.next_sig(c.match(k) + 1) if c.is_(k, "(") else k
			elif w == "[" and c.is_(c.next_sig(j + 1), "["):
				j = c.next_sig(c.match(j) + 1)
			elif w == "->":  # type de retour en fin de signature
				k = c.next_sig(j + 1)
				while k < end and not any(c.is_(k, s) for s in ("{", ";", "=")):
					if c.is_(k, "<"):
						k = c.match_angle(k)
					elif c.is_(k, "(") or c.is_(k, "["):
						k = c.match(k)
					k = c.next_sig(k + 1)
				j = k
			elif w == "requires":
				return None  # contrainte : fonction template, gardée par ailleurs
			else:
				break
		fn = Function(first, 0, scope=scope, name_start=name_start, name_end=c.prev_sig(paren) + 1,
					  params_open=paren, params_close=close, suffix_end=c.prev_sig(j) + 1)
		if c.is_(j, ";"):
			fn.end = j + 1
		elif c.is_(j, "="):
			k = c.next_sig(j + 1)
			if c.text(k) in ("default", "delete", "0") and c.is_(c.next_sig(k + 1), ";"):
				fn.end = c.next_sig(k + 1) + 1
			else:
				return None
		elif c.is_(j, ":"):
			body = self._skip_init_list(j, end)
			if body is None:
				return None
			fn.init_start = j
			fn.body_open, fn.body_close = body, c.match(body)
		elif c.is_(j, "{"):
			fn.body_open, fn.body_close = j, c.match(j)
		else:
			return None  # pas une signature de fonction (ex. `T x(1), y;`)
		if fn.body_close is not None:
			after = c.next_sig(fn.body_close + 1)
			fn.end = after + 1 if (c.is_(after, ";") and after < end) else fn.body_close + 1
		fn.qualified_name = self._qualified_name(fn)
		if scope.kind == "class":
			scope.functions.add(c.text(c.prev_sig(paren)))
		self._decide(fn, is_operator)
		return fn

	def _skip_init_list(self, colon: int, end: int) -> Optional[int]:
		"""`: a(1), b{2}, Base<T>(x) {` → indice de l'accolade du corps."""
		c = self.c
		j = c.next_sig(colon + 1)
		while j < end:
			while j < end and (c.kind(j) == IDENT or c.is_(j, "::")):
				j = c.next_sig(j + 1)
			if c.is_(j, "<"):
				j = c.next_sig(c.match_angle(j) + 1)
			if not (c.is_(j, "(") or c.is_(j, "{")):
				return None
			j = c.next_sig(c.match(j) + 1)
			if c.is_(j, "..."):
				j = c.next_sig(j + 1)
			if not c.is_(j, ","):
				return j if c.is_(j, "{") else None
			j = c.next_sig(j + 1)
		return None

	def _qualified_name(self, fn: Function) -> str:
		local = "".join(self.c.text(k) for k in range(fn.name_start, fn.name_end) if not self.tokens[k].is_trivia())
		parts = fn.scope.namespace_path()
		if fn.scope.class_path():
			parts.append(fn.scope.class_path())
		return "::".join(parts + [local.lstrip(":")])

	def parameters(self, fn: Function) -> list[tuple[int, int]]:
		"""Plages de jetons [début, fin) des paramètres."""
		c = self.c
		out = []
		start = k = fn.params_open + 1
		angle = 0
		while k < fn.params_close:
			tok = self.tokens[k]
			if tok.kind == PUNCT:
				if tok.text in OPENERS:
					k = c.match(k) + 1
					continue
				if tok.text == "<":
					nxt = self.tokens[k + 1]
					if nxt.text == "<" and nxt.start == tok.end:  # `<<` : décalage
						k += 2
						continue
					angle += 1
				elif tok.text == ">" and angle > 0:
					angle -= 1
				elif tok.text == "," and angle == 0:
					out.append((start, k))
					start = k + 1
			k += 1
		if any(not self.tokens[x].is_trivia() for x in range(start, fn.params_close)):
			out.append((start, fn.params_close))
		return out

	def default_value_start(self, a: int, b: int) -> int:
		"""Jeton `=` d'un argument par défaut dans le paramètre [a, b), ou b."""
		depth = 0
		for k in range(a, b):
			tok = self.tokens[k]
			if tok.kind != PUNCT:
				continue
			if tok.text in ("(", "[", "{", "<"):
				depth += 1
			elif tok.text in (")", "]", "}", ">"):
				depth -= 1
			elif tok.text == "=" and depth == 0:
				return k
		return b

	# ── déplacer ou garder en ligne ────────────────────────────────────────

	def _decide(self, fn: Function, is_operator: bool) -> None:
		if fn.body_open is None:
			return
		c = self.c
		words = {self.tokens[k].text for k in range(fn.start, fn.name_start) if c.kind(k) == IDENT}
		head = self.text[self.tokens[fn.start].start:self.tokens[fn.params_open].start]
		body = self.text[self.tokens[fn.body_open].start:self.tokens[fn.body_close].end]
		has_trailing_return = any(c.is_(k, "->") for k in range(fn.params_close, fn.suffix_end))

		reason = None
		if fn.scope.in_template_or_anonymous():
			reason = "template, classe sans nom ou espace de noms anonyme"
		elif is_operator:
			reason = "opérateur"
		elif words & KEEP_INLINE_SPECIFIERS:
			reason = "constexpr / consteval / friend"
		elif FORCE_INLINE_HINT.search(head):
			reason = "inlining forcé"
		elif fn.scope.kind != "class" and "static" in words:
			reason = "fonction static (liaison interne)"
		elif ("auto" in words or "decltype" in words) and not has_trailing_return:
			reason = "type de retour déduit"
		elif any(self._parameter_is_auto(a, b) for a, b in self.parameters(fn)):
			reason = "paramètre auto (template abrégé)"
		elif "\n" not in body:
			reason = "tient sur une ligne"
		elif {m.group(0) for m in IDENT_RE.finditer(body)} & self.undefined_macros:
			reason = "utilise une macro #undef plus loin"
		fn.extract = reason is None
		fn.keep_reason = reason or ""

	def _parameter_is_auto(self, a: int, b: int) -> bool:
		stop = self.default_value_start(a, b)
		depth = 0
		for k in range(a, stop):
			tok = self.tokens[k]
			if tok.kind == PUNCT and tok.text in ("(", "[", "{", "<"):
				depth += 1
			elif tok.kind == PUNCT and tok.text in (")", "]", "}", ">"):
				depth -= 1
			elif depth == 0 and tok.kind == IDENT and tok.text == "auto":
				return True
		return False


# ============================================================================
# Génération
# ============================================================================


def indentation_at(text: str, pos: int) -> str:
	"""Blancs de début de la ligne qui contient `pos`."""
	line_start = text.rfind("\n", 0, pos) + 1
	return re.match(r"[ \t]*", text[line_start:pos]).group(0)


def dedent(block: str, indent: str) -> str:
	"""Retire `indent` au début de chaque ligne (sauf la première)."""
	lines = block.split("\n")
	out = [lines[0]]
	for line in lines[1:]:
		if not line.strip():
			out.append("")
		elif indent and line.startswith(indent):
			out.append(line[len(indent):])
		else:
			out.append(line)
	return "\n".join(out)


def section_comment(title: str, width: int = 80) -> str:
	head = f"// ── {title} "
	return head + "─" * max(3, width - len(head))


@dataclass
class Node:
	"""Arbre du source généré : les directives et espaces de noms qui
	entourent les définitions, pour pouvoir élaguer les groupes vides."""
	kind: str  # "root" | "namespace" | "if" | "pragma" | "line" | "def"
	text: str = ""  # ligne d'ouverture, directive ou définition
	close: str = ""  # ligne de fermeture
	children: list = field(default_factory=list)
	branches: list = field(default_factory=list)  # "if" : [(directive, enfants)]
	class_path: str = ""

	def container(self) -> list:
		return self.branches[-1][1] if self.kind == "if" else self.children

	def has_definitions(self) -> bool:
		if self.kind == "def":
			return True
		if self.kind == "if":
			return any(child.has_definitions() for _, branch in self.branches for child in branch)
		return any(child.has_definitions() for child in self.children)


class Generator:
	def __init__(self, parser: Parser, root: Block):
		self.p = parser
		self.c = parser.c
		self.text = parser.text
		self.tokens = parser.tokens
		self.root = root
		self.moved = 0
		self.kept = 0
		self.kept_reasons: dict[str, int] = {}
		self.moved_names: set = set()
		self._collect(root.children)

	def _shadowed_type(self, fn: Function) -> bool:
		"""Un type des paramètres porte le nom d'une fonction membre
		(`Anchor(Anchor a)`) : hors de la classe, ce nom désignerait la
		fonction et non le type — la définition doit rester dans la classe."""
		members = fn.scope.functions
		if fn.scope.kind != "class" or not members:
			return False
		c = self.c
		for a, b in self.p.parameters(fn):
			for k in range(a, self.p.default_value_start(a, b)):
				if c.kind(k) == IDENT and c.text(k) in members:
					nxt = c.next_sig(k + 1)
					if c.kind(nxt) == IDENT or c.text(nxt) in ("&", "&&", "*", "<", "::"):
						return True
		return False

	def _collect(self, items: list) -> None:
		for item in items:
			if isinstance(item, Block):
				self._collect(item.children)
			elif isinstance(item, Function) and item.body_open is not None:
				if item.extract and self._shadowed_type(item):
					item.extract, item.keep_reason = False, "type de paramètre masqué par un membre"
				if item.extract:
					self.moved += 1
					self.moved_names.add(item.qualified_name)
				else:
					self.kept += 1
					self.kept_reasons[item.keep_reason] = self.kept_reasons.get(item.keep_reason, 0) + 1

	def slice(self, a: int, b: int) -> str:
		"""Texte source des jetons [a, b)."""
		return self.text[self.tokens[a].start:self.tokens[b - 1].end] if a < b else ""

	def _without_inline(self, a: int, b: int) -> str:
		"""Texte des jetons [a, b) sans le mot `inline` ni le blanc qui le suit."""
		out = []
		k = a
		while k < b:
			tok = self.tokens[k]
			if tok.kind == IDENT and tok.text == "inline":
				k += 2 if k + 1 < b and self.tokens[k + 1].kind == WS else 1
				continue
			out.append(tok.text)
			k += 1
		return "".join(out)

	# ── en-tête ────────────────────────────────────────────────────────────

	def header(self) -> str:
		return self._header_items(self.root.children)

	def _header_items(self, items: list) -> str:
		out = []
		for item in items:
			if isinstance(item, Block):
				out.append(self.slice(item.start, item.open_brace + 1))
				out.append(self._header_items(item.children))
				out.append(self.slice(item.close_brace, item.end))
			elif isinstance(item, Function):
				out.append(self._header_function(item))
			else:
				out.append(self.slice(item.start, item.end))
		return "".join(out)

	def _header_function(self, fn: Function) -> str:
		inline_here = any(self.c.text(k) == "inline" for k in range(fn.start, fn.name_start))
		if fn.body_open is None:
			# Déclaration seule dont la définition part au source : sans `inline`.
			if inline_here and fn.qualified_name in self.moved_names:
				return self._without_inline(fn.start, fn.end)
			return self.slice(fn.start, fn.end)
		if not fn.extract:
			return self.slice(fn.start, fn.end)
		if any(self.c.is_(k, "::") for k in range(fn.name_start, fn.name_end)):
			return ""  # définition hors classe d'un membre déjà déclaré
		return self._without_inline(fn.start, fn.suffix_end).rstrip() + ";"

	# ── source ─────────────────────────────────────────────────────────────

	def source_tree(self) -> Node:
		tree = Node("root")
		self._emit(self.root.children, [tree])
		return tree

	def _emit(self, items: list, stack: list) -> None:
		for item in items:
			if isinstance(item, Directive):
				if item.start not in self.p.include_guard:
					self._emit_directive(self.tokens[item.start].text.rstrip(), stack)
			elif isinstance(item, Block) and item.scope.kind == "namespace":
				head = re.sub(r"\s+", " ", self.slice(item.start, item.open_brace)).strip()
				label = item.scope.name or "(anonyme)"
				node = Node("namespace", head + " {", f"}} // namespace {label}")
				stack[-1].container().append(node)
				self._emit(item.children, [node])
			elif isinstance(item, Block):
				self._emit(item.children, stack)
			elif isinstance(item, Function) and item.extract:
				stack[-1].container().append(Node("def", self._definition(item), class_path=item.scope.class_path()))

	def _emit_directive(self, line: str, stack: list) -> None:
		word = Parser._directive(line)[0]
		top = stack[-1]
		if word in ("if", "ifdef", "ifndef"):
			node = Node("if", branches=[(line, [])])
			top.container().append(node)
			stack.append(node)
		elif word in ("elif", "else", "elifdef", "elifndef") and top.kind == "if":
			top.branches.append((line, []))
		elif word == "endif" and top.kind == "if":
			top.close = line
			stack.pop()
		elif word == "pragma" and re.search(r"\bdiagnostic\b", line):
			if re.search(r"diagnostic\s+push", line):
				node = Node("pragma", line)
				top.container().append(node)
				stack.append(node)
			elif re.search(r"diagnostic\s+pop", line):
				if top.kind == "pragma":
					top.close = line
					stack.pop()
			else:
				top.container().append(Node("line", line))
		# #include, #define, #undef, #pragma once… : propres à l'en-tête.

	def _definition(self, fn: Function) -> str:
		"""Définition hors classe : spécificateurs nettoyés, nom qualifié,
		arguments par défaut retirés, liste d'initialisation et corps intacts."""
		c, t = self.c, self.tokens
		scope = fn.scope

		# Spécificateurs et type de retour.
		prefix = []
		previous = ""
		k = fn.start
		while k < fn.name_start:
			tok = t[k]
			if c.is_(k, "[") and c.is_(c.next_sig(k + 1), "["):  # attribut [[…]]
				k = c.match(k) + 1
				continue
			if tok.is_trivia():
				prefix.append(" ")
			elif tok.kind == IDENT and tok.text in DROPPED_IN_DEFINITION:
				pass
			else:
				owner = scope.owner_of_type(tok.text) if tok.kind == IDENT and previous not in ("::", ".", "->") \
					else None
				prefix.append(f"{owner}::{tok.text}" if owner else tok.text)
				previous = tok.text
			k += 1
		prefix_text = re.sub(r"\s+", " ", "".join(prefix)).strip()

		# Nom qualifié.
		local = self.slice(fn.name_start, fn.name_end)
		path = scope.class_path()
		name = f"{path}::{local}" if path else local
		head = f"{prefix_text} {name}" if prefix_text else name

		# Paramètres, sans arguments par défaut.
		params = []
		for a, b in self.p.parameters(fn):
			stop = self.p.default_value_start(a, b)
			while stop > a and t[stop - 1].is_trivia():
				stop -= 1
			params.append(self.text[t[a].start:t[stop - 1].end] if stop > a else "")
		params_text = self._layout_parameters(head, ",".join(params))

		# Qualificatifs sans override / final.
		suffix = []
		for k in range(fn.params_close + 1, fn.suffix_end):
			tok = t[k]
			if tok.kind == IDENT and tok.text in ("override", "final"):
				while suffix and suffix[-1].isspace():
					suffix.pop()
				continue
			suffix.append(tok.text)
		suffix_text = "".join(suffix).rstrip()

		# Liste d'initialisation et corps, séparés comme dans l'original
		# s'ils étaient sur une autre ligne, sinon d'une espace.
		rest_start = fn.init_start if fn.init_start is not None else fn.body_open
		gap = self.text[t[fn.suffix_end - 1].end:t[rest_start].start]
		gap = gap if "\n" in gap else " "
		rest = self.text[t[rest_start].start:t[fn.body_close].end]

		signature = f"{head}({params_text}){suffix_text}"
		return signature + dedent(gap + rest, indentation_at(self.text, t[fn.start].start))

	@staticmethod
	def _layout_parameters(head: str, params: str, width: int = 120) -> str:
		"""Paramètres sur plusieurs lignes : réunis sur une seule s'ils
		tiennent dans `width` colonnes, sinon une ligne chacun avec une
		indentation de continuation (l'alignement d'origine suivait l'ancien
		nom, plus court)."""
		if "\n" not in params or "//" in params or "/*" in params:
			return params
		one_line = re.sub(r"\s*\n\s*", " ", params).strip()
		if len(f"{head}({one_line})".expandtabs(4)) <= width:
			return one_line
		lines = [line.strip() for line in params.split("\n")]
		return lines[0] + "".join("\n\t\t" + line for line in lines[1:] if line)

	def source(self, include: str, preludes: list[str]) -> str:
		out = [f"// Définitions de {include} — fichier généré par splitter.py : le code",
			   "// vient tel quel de l'en-tête (seules les signatures sont réécrites).",
			   ""]
		if preludes:
			out.append("// L'en-tête n'est pas autonome : il compte sur ce qu'inclut son module.")
			out += [f'#include "{prelude}"' for prelude in preludes]
		out.append(f'#include "{include}"')
		self._render(self.source_tree().children, out, [""])
		text = "\n".join(out)
		return re.sub(r"\n{3,}", "\n\n", text).rstrip() + "\n"

	def _render(self, nodes: list, out: list, current_class: list) -> None:
		for node in nodes:
			if node.kind == "line":
				out.append(node.text)
			elif not node.has_definitions():
				continue
			elif node.kind == "def":
				if node.class_path and node.class_path != current_class[0]:
					out += ["", section_comment(node.class_path)]
				current_class[0] = node.class_path
				out += ["", node.text]
			elif node.kind == "namespace":
				out += ["", node.text]
				self._render(node.children, out, [""])
				out += ["", node.close]
				current_class[0] = ""
			elif node.kind == "if":
				out.append("")
				for directive, branch in node.branches:
					out.append(directive)
					self._render(branch, out, current_class)
				out.append(node.close or "#endif")
			elif node.kind == "pragma":
				out += ["", node.text]
				self._render(node.children, out, current_class)
				out.append(node.close or "#pragma GCC diagnostic pop")


# ============================================================================
# Refusion d'une paire .hpp/.cpp (annule un découpage)
# ============================================================================


def _functions(items: list):
	for item in items:
		if isinstance(item, Block):
			yield from _functions(item.children)
		elif isinstance(item, Function):
			yield item


def _signature_key(parser: Parser, fn: Function) -> str:
	"""Paramètres (sans valeurs par défaut ni blancs) et qualificatifs :
	distingue les surcharges d'un même nom."""
	t = parser.tokens
	params = []
	for a, b in parser.parameters(fn):
		stop = parser.default_value_start(a, b)
		params.append("".join(t[k].text for k in range(a, stop) if not t[k].is_trivia()))
	quals = "".join(t[k].text for k in range(fn.params_close + 1, fn.suffix_end)
					if not t[k].is_trivia() and t[k].text not in ("override", "final"))
	return ",".join(params) + "|" + quals


def join_pair(header_text: str, source_text: str) -> tuple[str, list[str]]:
	"""Remet dans l'en-tête le corps de chaque fonction définie dans le
	source (celui d'un découpage précédent). Rend le nouvel en-tête et la
	liste de ce qui n'a pas pu être replacé (vide si tout est rentré)."""
	hp, sp = Parser(header_text), Parser(source_text)
	hroot, sroot = hp.parse_file(), sp.parse_file()

	declarations: dict[str, list[Function]] = {}
	for fn in _functions(hroot.children):
		if fn.body_open is None and not any(hp.c.is_(k, "=") for k in range(fn.suffix_end, fn.end)):
			declarations.setdefault(fn.qualified_name, []).append(fn)

	problems: list[str] = []
	replacements: list[tuple[Function, Function]] = []
	for item in sroot.children:
		if isinstance(item, Verbatim) and any(not sp.tokens[k].is_trivia() for k in range(item.start, item.end)):
			problems.append(f"contenu hors fonction : {sp.text[sp.tokens[item.start].start:][:60]!r}")
	for definition in _functions(sroot.children):
		if definition.body_open is None:
			continue
		key = _signature_key(sp, definition)
		candidates = [d for d in declarations.get(definition.qualified_name, []) if _signature_key(hp, d) == key]
		# La définition d'origine est la DERNIÈRE déclaration du nom (les
		# précédentes sont des déclarations anticipées), et une définition hors
		# classe redéclarée par l'ancien script passe avant la déclaration de
		# la classe.
		candidates.sort(key=lambda d: (not any(hp.c.is_(k, "::") for k in range(d.name_start, d.name_end)),
									   -d.start))
		if not candidates:
			problems.append(f"aucune déclaration pour {definition.qualified_name}({key})")
			continue
		replacements.append((candidates[0], definition))
		declarations[definition.qualified_name].remove(candidates[0])

	text = header_text
	for declaration, definition in sorted(replacements, key=lambda r: -r[0].start):
		t = hp.tokens
		signature = text[t[declaration.start].start:t[declaration.suffix_end - 1].end]
		words = {t[k].text for k in range(declaration.start, declaration.name_start) if t[k].kind == IDENT}
		if declaration.scope.kind != "class" and not words & {"inline", "constexpr", "consteval", "static"}:
			# Hors classe, une définition d'en-tête doit être `inline`.
			k = declaration.start
			while hp.c.is_(k, "[") and hp.c.is_(hp.c.next_sig(k + 1), "["):
				k = hp.c.next_sig(hp.c.match(k) + 1)
			cut = t[k].start - t[declaration.start].start
			signature = signature[:cut] + "inline " + signature[cut:]
		st = sp.tokens
		rest_start = definition.init_start if definition.init_start is not None else definition.body_open
		body = sp.text[st[rest_start].start:st[definition.body_close].end]
		text = text[:t[declaration.start].start] + signature + " " + body + text[t[declaration.end - 1].end:]
	return text, problems


# ============================================================================
# Point d'entrée
# ============================================================================


@dataclass
class Outcome:
	moved: int
	kept: int
	kept_reasons: dict
	cpp_path: str  # vide si rien n'a été déplacé


def split_header(header_path: str, include_root: str, src_root: str, preludes: list[str] = (),
				 dry_run: bool = False) -> Outcome:
	with open(header_path, encoding="utf-8") as f:
		text = f.read()
	parser = Parser(text)
	root = parser.parse_file()
	gen = Generator(parser, root)
	outcome = Outcome(gen.moved, gen.kept, gen.kept_reasons, "")
	if gen.moved == 0:
		return outcome

	rel = os.path.relpath(header_path, include_root).replace(os.sep, "/")
	header_text = gen.header()
	source_text = gen.source(rel, list(preludes))
	outcome.cpp_path = os.path.join(src_root, os.path.splitext(rel)[0] + ".cpp")
	if not dry_run:
		os.makedirs(os.path.dirname(outcome.cpp_path), exist_ok=True)
		with open(outcome.cpp_path, "w", encoding="utf-8") as f:
			f.write(source_text)
		with open(header_path, "w", encoding="utf-8") as f:
			f.write(header_text)
	return outcome


def main() -> int:
	ap = argparse.ArgumentParser(description="Sépare un en-tête header-only en .hpp + .cpp "
											 "(ou, avec --join, refusionne une paire déjà séparée).")
	ap.add_argument("header", help="en-tête à traiter (réécrit sur place)")
	ap.add_argument("--include-root", help="racine des en-têtes (chemin du #include du .cpp)")
	ap.add_argument("--src-root", help="racine des sources générés")
	ap.add_argument("--prelude", action="append", default=[], metavar="ENTETE",
					help="en-tête à inclure avant le sien dans le .cpp (en-tête non autonome)")
	ap.add_argument("--join", metavar="SOURCE.cpp",
					help="remet les corps de SOURCE.cpp dans l'en-tête, puis supprime SOURCE.cpp")
	ap.add_argument("--dry-run", action="store_true", help="analyse seulement, n'écrit rien")
	ap.add_argument("--verbose", action="store_true", help="détaille pourquoi des fonctions restent en ligne")
	args = ap.parse_args()

	if args.join:
		try:
			with open(args.header, encoding="utf-8") as f:
				header_text = f.read()
			with open(args.join, encoding="utf-8") as f:
				source_text = f.read()
			joined, problems = join_pair(header_text, source_text)
		except SplitError as e:
			print(f"{args.header} : ERREUR d'analyse — {e}", file=sys.stderr)
			return 1
		if problems:
			print(f"{args.header} : refusion impossible, fichiers laissés intacts :", file=sys.stderr)
			for problem in problems:
				print(f"    {problem}", file=sys.stderr)
			return 1
		if not args.dry_run:
			with open(args.header, "w", encoding="utf-8") as f:
				f.write(joined)
			os.remove(args.join)
		print(f"{args.header} : refusionné avec {args.join}")
		return 0

	if not args.include_root or not args.src_root:
		ap.error("--include-root et --src-root sont requis pour séparer")
	rel = os.path.relpath(args.header, args.include_root)
	try:
		outcome = split_header(args.header, args.include_root, args.src_root, args.prelude, args.dry_run)
	except SplitError as e:
		print(f"{rel} : ERREUR d'analyse, fichier laissé intact — {e}", file=sys.stderr)
		return 1
	line = f"{rel} : {outcome.moved} déplacées, {outcome.kept} gardées en ligne"
	if outcome.cpp_path:
		line += f" -> {os.path.relpath(outcome.cpp_path, args.src_root)}"
	print(line)
	if args.verbose:
		for reason, count in sorted(outcome.kept_reasons.items(), key=lambda x: -x[1]):
			print(f"    {count:4d} × {reason}")
	return 0


if __name__ == "__main__":
	sys.exit(main())
