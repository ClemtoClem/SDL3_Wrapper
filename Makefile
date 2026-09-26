# ============================================================================
# Makefile - C++23
# ============================================================================

# Force Make à utiliser tous les cœurs par défaut pour toutes les cibles
MAKEFLAGS += -j$(shell nproc)

# ============================================================================
# Configuration de Compilation (Debug / Release)
# ============================================================================

# Mode de compilation (debug par défaut)
MODE ?= debug

BUILDDIR := build

ifeq ($(MODE),debug)
	OPT_FLAGS := -O0 -g
	SAN_FLAGS := -fsanitize=undefined,address
	BINDIR    := $(BUILDDIR)/debug
else ifeq ($(MODE),release)
	OPT_FLAGS := -O3 -DNDEBUG
	SAN_FLAGS :=
	BINDIR    := $(BUILDDIR)/release
else
	$(error "MODE doit être 'debug' ou 'release'")
endif

# Les objets sont séparés selon le mode pour éviter les conflits
OBJDIR := $(BUILDDIR)/obj/$(MODE)

# ============================================================================
# Compilateur et Chemins
# ============================================================================

CXX        := g++
CXX_VERSION:= c++23

SRCDIR     := lib/src
INCDIR     := lib/include
TESTDIR    := tests
EXAMPLEDIR := examples

# Dépendances SDL3
SDL_PKGS   := sdl3 sdl3-image sdl3-ttf sdl3-mixer sdl3-net
SDL_CFLAGS := $(shell pkg-config --cflags $(SDL_PKGS) 2>/dev/null)
SDL_LIBS   := $(shell pkg-config --libs $(SDL_PKGS) 2>/dev/null)

# Dépendances Shaderc & SPIRV-Cross
SHADERC_CFLAGS := $(shell pkg-config --cflags shaderc 2>/dev/null)
SHADERC_LIBS   := $(shell pkg-config --libs shaderc 2>/dev/null)
SPIRV_CROSS_LIBS := -lspirv-cross-msl -lspirv-cross-glsl -lspirv-cross-core -lspirv-cross-util

CXXFLAGS := $(OPT_FLAGS) -std=$(CXX_VERSION) -Wextra -Werror -Wsign-compare $(SAN_FLAGS) -I $(INCDIR) -I $(SRCDIR) -MMD -MP $(SDL_CFLAGS) $(SHADERC_CFLAGS)
LDFLAGS  := $(SAN_FLAGS)
LDLIBS   := $(SDL_LIBS) $(SHADERC_LIBS) $(SPIRV_CROSS_LIBS)
TEST_LDLIBS :=

export LSAN_OPTIONS := suppressions=$(CURDIR)/tests/lsan_suppressions.txt

# ============================================================================
# Sources et Cibles
# ============================================================================

# Bibliothèque (statique)
LIB       := $(OBJDIR)/ui.a
SRCS      := $(shell find $(SRCDIR) -name '*.cpp' 2>/dev/null)
OBJS      := $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/lib/%.o,$(SRCS))

# Tests
TEST_SRCS := $(shell find $(TESTDIR) -name '*.cpp' 2>/dev/null)
TEST_OBJS := $(patsubst $(TESTDIR)/%.cpp,$(OBJDIR)/tests/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst $(TESTDIR)/%.cpp,$(BUILDDIR)/tests/%,$(TEST_SRCS))

# Exemples
EXAMPLE_SRCS := $(shell find $(EXAMPLEDIR) -maxdepth 1 -name '*.cpp' 2>/dev/null)
EXAMPLES     := $(basename $(notdir $(EXAMPLE_SRCS)))
EXAMPLE_OBJS := $(patsubst $(EXAMPLEDIR)/%.cpp,$(OBJDIR)/examples/%.o,$(EXAMPLE_SRCS))
EXAMPLE_BINS := $(patsubst $(EXAMPLEDIR)/%.cpp,$(BINDIR)/%,$(EXAMPLE_SRCS))

# Émulateur (Cas particulier multi-fichiers)
EMULATOR_DIR  := $(EXAMPLEDIR)/emulator_demo
EMULATOR_SRCS := $(shell find $(EMULATOR_DIR) -name '*.cpp' 2>/dev/null)
EMULATOR_OBJS := $(patsubst $(EXAMPLEDIR)/%.cpp,$(OBJDIR)/examples/%.o,$(EMULATOR_SRCS))
# Optimisation forcée pour l'émulateur même en mode debug pour conserver le temps réel
ifeq ($(MODE),debug)
	EMULATOR_OPT ?= -O2
else
	EMULATOR_OPT ?= -O3
endif

DEPS := $(OBJS:.o=.d) $(TEST_OBJS:.o=.d) $(EXAMPLE_OBJS:.o=.d) $(EMULATOR_OBJS:.o=.d)

# ============================================================================
# Outils qualité / conventions
# ============================================================================

CLANG_TIDY   := clang-tidy
CLANG_FORMAT := clang-format
BEAR         := bear

CLANG_TIDY_CONFIG   := .clang-tidy.yml
CLANG_FORMAT_CONFIG := .clang-format.yml
COMPILE_DB := compile_commands.json

TIDY_SRCS := $(TEST_SRCS) $(EXAMPLE_SRCS)
TIDY_ARGS := --config-file=$(CLANG_TIDY_CONFIG) --header-filter='^$(LIBDIR)/.*\.(h|hpp)$$' -p=.

FORMAT_SRCS := $(shell find $(LIBDIR) $(TESTDIR) $(EXAMPLEDIR) -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) 2>/dev/null)
FORMAT_ARGS := -style=file:$(CLANG_FORMAT_CONFIG)

YELLOW    := \033[33m
GREEN     := \033[32m
CYAN      := \033[36m
RED       := \033[31m
RESET     := \033[0m

# ============================================================================
# Cibles Principales
# ============================================================================

.PHONY: all debug release lib clean re tests examples examples-debug examples-release check help run list-examples run-tests run-example shaders \
	lint rename format format-check fix compile-commands

all: lib examples tests shaders ## Compile tout dans le mode actif (debug par defaut)

debug: ## Force la compilation globale en mode Debug (O0 + fsanitize)
	@$(MAKE) --no-print-directory MODE=debug all

release: ## Force la compilation globale en mode Release (O3)
	@$(MAKE) --no-print-directory MODE=release all

lib: $(LIB) ## Compile uniquement la bibliothèque

shaders: ## Compile les shaders GLSL
	@if [ -d assets/shaders/src ]; then \
		assets/shaders/compiler.sh; \
	else \
		echo "$(YELLOW)Aucun shader source (assets/shaders/src absent) - etape ignoree$(RESET)"; \
	fi

$(LIB): $(OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^
	@echo "=== Bibliotheque compilee ($(MODE)) : $(LIB) ==="

$(OBJDIR)/lib/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# ============================================================================
# Tests
# ============================================================================

$(OBJDIR)/tests/%.o: $(TESTDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TEST_BINS): $(BUILDDIR)/tests/%: $(OBJDIR)/tests/%.o $(LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $< $(LIB) $(LDLIBS) $(TEST_LDLIBS) -o $@

tests: $(TEST_BINS) ## Compile tous les tests (dans build/tests/)
run-tests: check ## Compile et execute la suite de tests

check: shaders $(TEST_BINS)
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

$(OBJDIR)/examples/%.o: $(EXAMPLEDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/examples/emulator_demo.o $(EMULATOR_OBJS): CXXFLAGS += $(EMULATOR_OPT) -I $(EMULATOR_DIR)

$(EXAMPLE_BINS): $(BINDIR)/%: $(OBJDIR)/examples/%.o $(LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $(filter %.o,$^) $(LIB) $(LDLIBS) -o $@

$(BINDIR)/emulator_demo: $(EMULATOR_OBJS)

examples: $(EXAMPLE_BINS) ## Compile tous les exemples (mode actif)

examples-debug: ## Compile tous les exemples en mode debug
	@$(MAKE) --no-print-directory MODE=debug examples

examples-release: ## Compile tous les exemples en mode release
	@$(MAKE) --no-print-directory MODE=release examples

# Generation automatique des cibles spécifiques pour chaque exemple
define EXAMPLE_RULES
.PHONY: $(1) $(1)-debug $(1)-release run-$(1) run-$(1)-debug run-$(1)-release

$(1): $$(BINDIR)/$(1) ## Compile l'exemple $(1) dans le mode actif

$(1)-debug: ## Compile l'exemple $(1) en mode debug
	@$$(MAKE) --no-print-directory MODE=debug $(1)

$(1)-release: ## Compile l'exemple $(1) en mode release
	@$$(MAKE) --no-print-directory MODE=release $(1)

run-$(1): $(1) ## Compile et lance l'exemple $(1) (mode actif)
	@./$$(BINDIR)/$(1)

run-$(1)-debug: ## Compile et lance l'exemple $(1) en mode debug
	@$$(MAKE) --no-print-directory MODE=debug run-$(1)

run-$(1)-release: ## Compile et lance l'exemple $(1) en mode release
	@$$(MAKE) --no-print-directory MODE=release run-$(1)
endef

$(foreach ex,$(EXAMPLES),$(eval $(call EXAMPLE_RULES,$(ex))))

list-examples:
	@printf "$(YELLOW)Exemples disponibles :$(RESET)\n"
	@if [ -z "$(EXAMPLES)" ]; then \
		printf "  $(RED)Aucun exemple trouve dans $(EXAMPLEDIR)/$(RESET)\n"; \
	else \
		for e in $(EXAMPLES); do printf "  $(CYAN)$$e$(RESET)\n"; done; \
	fi

EXAMPLE ?=
DEFAULT_EXAMPLE := $(firstword $(EXAMPLES))

run-example: ## Lance un exemple spécifique (ex: make run-example EXAMPLE=nom MODE=release)
ifneq ($(EXAMPLE),)
	@$(MAKE) --no-print-directory MODE=$(MODE) run-$(EXAMPLE)
else
	@echo "$(RED)Erreur : veuillez specifier un exemple avec EXAMPLE=nom$(RESET)"
	@$(MAKE) --no-print-directory list-examples
endif

run: ## Lance l'exemple par défaut
ifneq ($(DEFAULT_EXAMPLE),)
	@$(MAKE) --no-print-directory MODE=$(MODE) run-$(DEFAULT_EXAMPLE)
else
	@echo "$(RED)Aucun exemple trouve sous $(EXAMPLEDIR)/$(RESET)"
endif

# ============================================================================
# Nettoyage et Utilitaires
# ============================================================================

clean: ## Supprime l’intégralité du dossier build
	rm -rf $(BUILDDIR)
	@echo "=== Repertoire build supprime ==="

re: clean all ## Reconstruit entièrement le projet

-include $(DEPS)

# Qualité du code
COMPILE_DB_INPUTS := $(shell find $(LIBDIR) $(TESTDIR) $(EXAMPLEDIR) -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) 2>/dev/null)

$(COMPILE_DB): $(COMPILE_DB_INPUTS)
	@command -v $(BEAR) >/dev/null 2>&1 || { echo "$(RED)bear n'est pas installe$(RESET)"; exit 1; }
	@echo "$(CYAN)== Generation de $(COMPILE_DB) (bear) ==$(RESET)"
	-@$(BEAR) --output $(COMPILE_DB) -- $(MAKE) --no-print-directory -k -B tests examples
	@echo "$(GREEN)== $(COMPILE_DB) genere ==$(RESET)"

compile-commands: $(COMPILE_DB)
lint: $(COMPILE_DB)
	@echo "$(CYAN)== Vérification des conventions ==$(RESET)"
	@$(CLANG_TIDY) $(TIDY_SRCS) $(TIDY_ARGS)

rename: $(COMPILE_DB)
	@echo "$(YELLOW)== Application des conventions ==$(RESET)"
	@$(CLANG_TIDY) $(TIDY_SRCS) $(TIDY_ARGS) --fix
	@echo "$(GREEN)== Renommage termine ==$(RESET)"

format:
	@echo "$(CYAN)== Formatage du code ==$(RESET)"
	@if command -v $(CLANG_FORMAT) >/dev/null 2>&1; then \
		$(CLANG_FORMAT) $(FORMAT_ARGS) -i $(FORMAT_SRCS); \
	else \
		echo "$(RED)clang-format n'est pas installe$(RESET)"; exit 1; \
	fi

format-check:
	@echo "$(CYAN)== Vérification du formatage ==$(RESET)"
	@$(CLANG_FORMAT) $(FORMAT_ARGS) --dry-run --Werror $(FORMAT_SRCS)

fix: rename format
	@echo "$(GREEN)== Conventions et formatage appliques ==$(RESET)"

help: ## Affiche ce message d'aide
	@printf "$(YELLOW)Compilation globale :$(RESET)\n"
	@printf "  $(CYAN)make debug$(RESET)               compile tout en mode debug (-O0 + fsanitize)\n"
	@printf "  $(CYAN)make release$(RESET)             compile tout en mode release (-O3)\n\n"
	@printf "$(YELLOW)Compilation des exemples :$(RESET)\n"
	@printf "  $(CYAN)make examples-debug$(RESET)      compile tous les exemples en mode debug\n"
	@printf "  $(CYAN)make examples-release$(RESET)    compile tous les exemples en mode release\n"
	@printf "  $(CYAN)make <nom>-debug$(RESET)         compile l'exemple <nom> en mode debug\n"
	@printf "  $(CYAN)make <nom>-release$(RESET)       compile l'exemple <nom> en mode release\n\n"
	@printf "$(YELLOW)Exécution des exemples :$(RESET)\n"
	@printf "  $(CYAN)make run-<nom>-debug$(RESET)     compile et lance l'exemple <nom> en debug\n"
	@printf "  $(CYAN)make run-<nom>-release$(RESET)   compile et lance l'exemple <nom> en release\n"
	@printf "  $(CYAN)make run-example EXAMPLE=<nom> MODE=<mode>$(RESET)\n\n"
	@printf "$(YELLOW)Toutes les commandes disponibles :$(RESET)\n"
	@awk 'BEGIN {FS=":[[:space:]]*.*## "} /^[[:alnum:]_.-]+[[:space:]]*:/ && /##/ { printf "  $(CYAN)%-22s$(RESET) %s\n", $$1, $$2 }' $(MAKEFILE_LIST)