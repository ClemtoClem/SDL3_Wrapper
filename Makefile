# ============================================================================
# Makefile - ECS / C++23
# ============================================================================

# Compilateur et options C++
CXX        := g++
CXX_VERSION:=c++23

# Répertoires — lib/ est 100% header-only (aucun .cpp aujourd'hui) : LIBDIR
# sert à la fois de racine d'include et de racine de recherche d'éventuelles
# sources .cpp futures. SRCS/OBJS/LIB restent définis pour ce cas (no-op
# inoffensif tant que lib/ ne contient aucun .cpp).
SRCDIR     := lib/src
INCDIR     := lib/include
BUILDDIR   := build
TESTDIR    := tests
EXAMPLEDIR := examples

# Dépendances SDL3 (résolues via pkg-config, installation système)
SDL_PKGS   := sdl3 sdl3-image sdl3-ttf sdl3-mixer sdl3-net
SDL_CFLAGS := $(shell pkg-config --cflags $(SDL_PKGS) 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs $(SDL_PKGS) 2>/dev/null)

# Compilateur GLSL embarqué (render3d::ShaderBuilder) : shaderc a un .pc
# (GLSL -> SPIR-V) ; spirv-cross n'expose son API C++ (spirv_msl.hpp, utilisée
# pour SPIR-V -> MSL) que via des .a statiques sans .pc dédié — liés en dur.
SHADERC_CFLAGS := $(shell pkg-config --cflags shaderc 2>/dev/null)
SHADERC_LIBS   := $(shell pkg-config --libs shaderc 2>/dev/null)
SPIRV_CROSS_LIBS := -lspirv-cross-msl -lspirv-cross-glsl -lspirv-cross-core -lspirv-cross-util

CXXFLAGS := -O0 -g -std=$(CXX_VERSION) -Wextra -Werror -Wsign-compare -fsanitize=undefined,address -I $(INCDIR) -I $(SRCDIR) -MMD -MP -g $(SDL_CFLAGS) $(SHADERC_CFLAGS)
LDFLAGS  := -fsanitize=undefined,address
LDLIBS   := $(SDL_LIBS) $(SHADERC_LIBS) $(SPIRV_CROSS_LIBS)
TEST_LDLIBS :=

# Suppressions LeakSanitizer (voir tests/lsan_suppressions.txt) : fuites
# internes au backend X11 de SDL3 lui-même, pas de ce dépôt — exportée pour
# que tout binaire lancé depuis ce Makefile (check/run-tests, run-<exemple>)
# en hérite automatiquement.
export LSAN_OPTIONS := suppressions=$(CURDIR)/tests/lsan_suppressions.txt

# Bibliothèque statique (no-op aujourd'hui : lib/ est header-only)
LIB       := $(BUILDDIR)/ui.a
SRCS      := $(shell find $(SRCDIR) -name '*.cpp' 2>/dev/null)
OBJS      := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/lib/%.o,$(SRCS))

# Sources, objets et exécutables des tests (tests/*.cpp à plat)
TEST_SRCS := $(shell find $(TESTDIR) -name '*.cpp' 2>/dev/null)
TEST_OBJS := $(patsubst $(TESTDIR)/%.cpp,$(BUILDDIR)/tests/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst $(TESTDIR)/%.cpp,$(BUILDDIR)/bin/%,$(TEST_SRCS))

# Sources, objets et exécutables des exemples (examples/*.cpp à plat, un
# binaire par fichier — pas de sous-dossier par exemple)
EXAMPLE_SRCS := $(shell find $(EXAMPLEDIR) -maxdepth 1 -name '*.cpp' 2>/dev/null)
EXAMPLES     := $(basename $(notdir $(EXAMPLE_SRCS)))
EXAMPLE_OBJS := $(patsubst $(EXAMPLEDIR)/%.cpp,$(BUILDDIR)/examples/%.o,$(EXAMPLE_SRCS))
EXAMPLE_BINS := $(patsubst $(EXAMPLEDIR)/%.cpp,$(BUILDDIR)/bin/%,$(EXAMPLE_SRCS))

# Exemple multi-fichiers : emulator_demo. Son point d'entrée reste
# examples/emulator_demo.cpp (donc listé ci-dessus comme les autres), mais il
# embarque en plus ~50 unités de traduction sous examples/emulator_demo/
# (cœur NDS/GBA/GBC + coquille applicative), compilées séparément pour
# profiter de make -j et liées au binaire.
EMULATOR_DIR  := $(EXAMPLEDIR)/emulator_demo
EMULATOR_SRCS := $(shell find $(EMULATOR_DIR) -name '*.cpp' 2>/dev/null)
EMULATOR_OBJS := $(patsubst $(EXAMPLEDIR)/%.cpp,$(BUILDDIR)/examples/%.o,$(EMULATOR_SRCS))
# Un interpréteur ARM à -O0 sous ASan n'atteint pas le temps réel : le cœur
# est optimisé (les sanitizers restent actifs). Surchargeable :
# make emulator_demo EMULATOR_OPT=-O0
EMULATOR_OPT  ?= -O2

# Fichiers de dépendances (.d), inclus plus bas (-MMD -MP)
DEPS      := $(OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(EXAMPLE_OBJS:.o=.d) $(EMULATOR_OBJS:.o=.d)

# ============================================================================
# Outils qualité / conventions (clang-format, clang-tidy, bear)
# ============================================================================

CLANG_TIDY   := clang-tidy
CLANG_FORMAT := clang-format
BEAR         := bear

# Configs nommées explicitement en .yml (pas les noms par défaut .clang-tidy/
# .clang-format) : chargées via --config-file / -style=file: plutôt que par
# la recherche automatique habituelle de ces outils.
CLANG_TIDY_CONFIG   := .clang-tidy.yml
CLANG_FORMAT_CONFIG := .clang-format.yml

# compile_commands.json à la racine du projet (pas dans build/) : c'est
# l'emplacement que clangd et la plupart des éditeurs/IDE cherchent par
# défaut, en plus d'être ce que lit clang-tidy via -p=. ci-dessous.
COMPILE_DB := compile_commands.json

# Fichiers analysés par clang-tidy : les .cpp réels (tests/exemples) — ce
# sont les seules unités de traduction qui apparaissent dans
# compile_commands.json (lib/ est header-only, jamais compilé seul). Les
# en-têtes de lib/ sont couverts par transitivité via --header-filter : tout
# diagnostic trouvé dans un en-tête inclus par une des TU ci-dessous est
# remonté, ce qui couvre la bibliothèque en entier tant que chaque en-tête
# est inclus par au moins un test ou un exemple.
TIDY_SRCS := $(TEST_SRCS) $(EXAMPLE_SRCS)

TIDY_ARGS := \
	--config-file=$(CLANG_TIDY_CONFIG) \
	--header-filter='^$(LIBDIR)/.*\.(h|hpp)$$' \
	-p=.

# Fichiers à formater
FORMAT_SRCS := \
	$(shell find $(LIBDIR) $(TESTDIR) $(EXAMPLEDIR) \
		-type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) \
		2>/dev/null)

FORMAT_ARGS := -style=file:$(CLANG_FORMAT_CONFIG)

# Couleurs pour la cible help
YELLOW    := \033[33m
GREEN     := \033[32m
CYAN      := \033[36m
RED       := \033[31m
RESET     := \033[0m

# ============================================================================
# Bibliothèque
# ============================================================================

.PHONY: all lib clean re tests examples check help run list-examples run-tests run-example shaders \
	lint rename format format-check fix compile-commands

all: lib examples tests shaders ## Compile la bibliotheque, les exemples, les tests et les shaders

lib: $(LIB) ## Alias explicite pour la bibliotheque

shaders: ## Compile les shaders GLSL (assets/shaders/src -> bin, no-op tant que src/ n'existe pas)
	@if [ -d assets/shaders/src ]; then \
		assets/shaders/compiler.sh; \
	else \
		echo "$(YELLOW)Aucun shader source (assets/shaders/src absent) - etape ignoree$(RESET)"; \
	fi

$(LIB): $(OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^
	@echo "=== Bibliotheque compilee : $(LIB) ==="

# Compilation des sources du projet (.cpp -> .o dans build/lib/) — no-op
# aujourd'hui, aucun .cpp sous lib/ (header-only), prêt si ça change un jour.
$(BUILDDIR)/lib/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

help: ## Affiche ce message d'aide
	@printf "$(YELLOW)Commandes disponibles :$(RESET)\n"
	@awk 'BEGIN {FS=":[[:space:]]*.*## "} \
		/^[[:alnum:]_.-]+[[:space:]]*:/ && /##/ { \
			printf "  $(CYAN)%-18s$(RESET) %s\n", $$1, $$2 \
		}' $(MAKEFILE_LIST)
	@printf "\n$(YELLOW)Raccourcis utiles :$(RESET)\n"
	@printf "  $(CYAN)make run-tests$(RESET)             compile et lance la suite de tests\n"
	@printf "  $(CYAN)make run-example EXAMPLE=X$(RESET) compile et lance l'exemple nomme X\n"
	@printf "  $(CYAN)make list-examples$(RESET)         liste les exemples disponibles\n"
	@printf "  $(CYAN)make lint$(RESET)                  verifie les conventions de nommage (clang-tidy)\n"
	@printf "  $(CYAN)make format$(RESET)                applique le formatage (clang-format)\n"
	@printf "  $(CYAN)make fix$(RESET)                   applique conventions + formatage d'un coup\n"

# ============================================================================
# Tests
# ============================================================================

# Compilation des sources de test (Static Pattern Rule)
$(TEST_OBJS): $(BUILDDIR)/tests/%.o: $(TESTDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Édition des liens pour chaque test (Static Pattern Rule)
# La syntaxe "$(TEST_BINS): cible: dépendances" empêche Make d'effacer les exécutables
$(TEST_BINS): $(BUILDDIR)/bin/%: $(BUILDDIR)/tests/%.o $(LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $< $(LIB) $(LDLIBS) $(TEST_LDLIBS) -o $@

tests: $(TEST_BINS) ## Compile tous les tests (sans les executer)
run-tests: check ## Compile et execute la suite de tests

check: shaders $(TEST_BINS) ## Compile et exécute automatiquement tous les tests
	@echo "$(CYAN)== Lancement des tests ==$(RESET)"
	@erreurs=0; \
	for t in $(TEST_BINS); do \
		echo "$(YELLOW)[RUN] $$t$(RESET)"; \
		$$t; \
		if [ $$? -ne 0 ]; then \
			echo "$(RED)[FAIL] Le test $$t a échoué !$(RESET)"; \
			erreurs=1; \
		fi; \
	done; \
	if [ $$erreurs -eq 1 ]; then \
		echo "$(RED)==> Certains tests ont échoué !$(RESET)"; \
		exit 1; \
	else \
		echo "$(GREEN)==> Tous les tests ont réussi !$(RESET)"; \
	fi

# ============================================================================
# Exemples
# ============================================================================

# Compilation des sources d'exemple (Static Pattern Rule) — un .cpp à plat
# par exemple sous examples/, pas de sous-dossier.
$(EXAMPLE_OBJS): $(BUILDDIR)/examples/%.o: $(EXAMPLEDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Sources des sous-dossiers d'exemple (emulator_demo/**/*.cpp)
$(EMULATOR_OBJS): $(BUILDDIR)/examples/%.o: $(EXAMPLEDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Les includes internes de l'émulateur sont relatifs à examples/emulator_demo/
$(BUILDDIR)/examples/emulator_demo.o $(EMULATOR_OBJS): CXXFLAGS += $(EMULATOR_OPT) -I $(EMULATOR_DIR)

# Édition des liens pour chaque exemple (Static Pattern Rule) — tous les .o
# prérequis sont liés, ce qui permet à un exemple d'en ajouter (ci-dessous).
$(EXAMPLE_BINS): $(BUILDDIR)/bin/%: $(BUILDDIR)/examples/%.o $(LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $(filter %.o,$^) $(LIB) $(LDLIBS) -o $@

$(BUILDDIR)/bin/emulator_demo: $(EMULATOR_OBJS)

examples: $(EXAMPLE_BINS) ## Compile tous les exemples

# Cibles de convenance par exemple : "make NOM" / "make run-NOM"
define EXAMPLE_RULES
.PHONY: $(1) run-$(1)
$(1): $$(BUILDDIR)/bin/$(1) ## Compile l'exemple $(1)
run-$(1): $(1) ## Compile et lance l'exemple $(1)
	@./$$(BUILDDIR)/bin/$(1)
endef

$(foreach ex,$(EXAMPLES),$(eval $(call EXAMPLE_RULES,$(ex))))

list-examples: ## Liste les exemples disponibles
	@printf "$(YELLOW)Exemples disponibles :$(RESET)\n"
	@if [ -z "$(EXAMPLES)" ]; then \
		printf "  $(RED)Aucun exemple trouve dans $(EXAMPLEDIR)/$(RESET)\n"; \
	else \
		for e in $(EXAMPLES); do printf "  $(CYAN)$$e$(RESET)\n"; done; \
	fi

EXAMPLE ?=
DEFAULT_EXAMPLE := $(firstword $(EXAMPLES))

run-example: ## Lance un exemple specifique (ex: make run-example EXAMPLE=nom)
ifneq ($(EXAMPLE),)
	@$(MAKE) --no-print-directory run-$(EXAMPLE)
else
	@echo "$(RED)Erreur : veuillez specifier un exemple avec EXAMPLE=nom$(RESET)"
	@$(MAKE) --no-print-directory list-examples
endif

run: ## Lance l'exemple par defaut
ifneq ($(DEFAULT_EXAMPLE),)
	@$(MAKE) --no-print-directory run-$(DEFAULT_EXAMPLE)
else
	@echo "$(RED)Aucun exemple trouve sous $(EXAMPLEDIR)/$(RESET)"
endif

# Nettoyage
clean: ## Supprime les dossiers de build
	rm -rf $(BUILDDIR)
	@echo "=== Repertoire build supprime ==="

# Reconstruction complète
re: clean all ## Reconstruit entièrement le projet

# Inclusion automatique des dépendances générées par GCC (-MMD -MP)
-include $(DEPS)

# ============================================================================
# Qualité du code
# ============================================================================

# compile_commands.json — généré via bear en interceptant une VRAIE
# recompilation complète de tous les tests + exemples (seules unités de
# traduction du projet, lib/ étant header-only) : "-B" force make à tout
# recompiler même si les .o sont déjà à jour, sinon bear n'intercepterait
# aucune commande pour les fichiers déjà compilés. Redéclenché automatique-
# ment par make si un .cpp/.hpp du projet a changé depuis la dernière
# génération (dépendances ci-dessous), pas seulement à la demande.
#
# "-k" (keep going) + le "-" en tête de la ligne de recette : un test cassé
# préexistant (tests/ecs_smoke_test.cpp, sans rapport avec ce Makefile, cf.
# memory/) ne doit ni interrompre la capture des AUTRES unités de traduction
# par bear, ni faire échouer la cible tout entière à chaque régénération —
# bear écrit compile_commands.json avec ce qu'il a intercepté même si "make"
# se termine en erreur.
COMPILE_DB_INPUTS := $(shell find $(LIBDIR) $(TESTDIR) $(EXAMPLEDIR) \
	-type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) 2>/dev/null)

$(COMPILE_DB): $(COMPILE_DB_INPUTS)
	@command -v $(BEAR) >/dev/null 2>&1 || { echo "$(RED)bear n'est pas installe$(RESET)"; exit 1; }
	@echo "$(CYAN)== Generation de $(COMPILE_DB) (bear) ==$(RESET)"
	-@$(BEAR) --output $(COMPILE_DB) -- $(MAKE) --no-print-directory -k -B tests examples
	@echo "$(GREEN)== $(COMPILE_DB) genere ==$(RESET)"

compile-commands: $(COMPILE_DB) ## (Re)genere compile_commands.json via bear (tests+exemples)

lint: $(COMPILE_DB) ## Verifie les conventions de nommage (clang-tidy, lecture seule)
	@echo "$(CYAN)== Vérification des conventions de nommage ==$(RESET)"
	@$(CLANG_TIDY) $(TIDY_SRCS) $(TIDY_ARGS)

rename: $(COMPILE_DB) ## Applique les conventions de nommage (clang-tidy --fix)
	@echo "$(YELLOW)== Application des conventions de nommage ==$(RESET)"
	@$(CLANG_TIDY) $(TIDY_SRCS) $(TIDY_ARGS) --fix
	@echo "$(GREEN)== Renommage terminé ==$(RESET)"

format: ## Applique le formatage du code (clang-format -i)
	@echo "$(CYAN)== Formatage du code ==$(RESET)"
	@if command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
		$(CLANG_FORMAT) $(FORMAT_ARGS) -i $(FORMAT_SRCS); \
	else \
		echo "$(RED)clang-format n'est pas installé$(RESET)"; \
		exit 1; \
	fi

format-check: ## Verifie le formatage sans modifier les fichiers (echoue si non conforme)
	@echo "$(CYAN)== Vérification du formatage ==$(RESET)"
	@$(CLANG_FORMAT) $(FORMAT_ARGS) --dry-run --Werror $(FORMAT_SRCS)

fix: rename format ## Applique conventions de nommage + formatage
	@echo "$(GREEN)== Conventions et formatage appliqués ==$(RESET)"
