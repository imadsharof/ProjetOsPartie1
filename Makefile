# Définition des répertoires et des fichiers
SRCDIR  := ./src
BINDIR  := ./
EXE := $(BINDIR)chat

# Compilation avec g++
CC      := g++
CFLAGS  := -Wall -Wextra -O3 -std=c++20 -g

# Fichiers sources et objets
SOURCES := $(SRCDIR)/main.cpp
OBJECTS := $(SOURCES:.cpp=.o)

# Cible par défaut
.PHONY: all clean

all: $(EXE)

$(EXE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(EXE)

# Règle pour compiler chaque fichier source en objet
$(SRCDIR)/%.o: $(SRCDIR)/%.cpp
	$(CC) $(CFLAGS) -c $< -o $@

# Nettoyage des fichiers objets et de l'exécutable
clean:
	@rm -f $(OBJECTS) $(EXE)

