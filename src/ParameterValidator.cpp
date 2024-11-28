// ParameterValidator.cpp
#include "ParameterValidator.hpp"
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

// Déclaration des variables globales utilisées
extern std::string pseudo_utilisateur;
extern std::string pseudo_destinataire;
extern bool isBotMode;
extern bool isManuelMode;
extern bool isJoliMode;

// Fonction utilisée
extern bool containsChar(const std::string& str, char ch);

/**
 * @brief Vérifie les paramètres du programme
 * @param argc Nombre d'arguments
 * @param argv Tableau des arguments
 */
void ParameterValidator::checkParams(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "chat pseudo_utilisateur pseudo_destinataire [--bot] [--manuel]\n");
        exit(1);
    }

    pseudo_utilisateur = argv[1];
    pseudo_destinataire = argv[2];

    // Vérification de la longueur des pseudonymes
    if (pseudo_utilisateur.size() > 30 || pseudo_destinataire.size() > 30) {
        fprintf(stderr, "Erreur : Les pseudonymes ne doivent pas dépasser 30 caractères.\n");
        exit(2);
    }

    // Vérification des pseudonymes interdits
    if (pseudo_utilisateur == "." || pseudo_utilisateur == ".." ||
        pseudo_destinataire == "." || pseudo_destinataire == "..") {
        fprintf(stderr, "Erreur : Les pseudonymes ne peuvent pas être '.' ou '..'.\n");
        exit(3);
    }

    // Vérification des caractères interdits dans les pseudonymes
    std::vector<char> caracteres_interdits = {'/', '[', ']', '-'};
    for (char caractere : caracteres_interdits) {
        if (containsChar(pseudo_utilisateur, caractere) || containsChar(pseudo_destinataire, caractere)) {
            fprintf(stderr, "Erreur : Les pseudonymes contiennent des caractères interdits (/, -, [, ]).\n");
            exit(3);
        }
    }

    // Traitement des options
    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "--bot") isBotMode = true;
        if (std::string(argv[i]) == "--manuel") isManuelMode = true;
        if (std::string(argv[i]) == "--joli") isJoliMode = true;
    }
}
