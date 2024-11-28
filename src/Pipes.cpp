#include "Pipes.hpp"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <errno.h>

/**
 * @brief Constructeur de la classe Pipes
 * @param pseudo_utilisateur Pseudonyme de l'utilisateur
 * @param pseudo_destinataire Pseudonyme du destinataire
 */
Pipes::Pipes(const std::string& pseudo_utilisateur, const std::string& pseudo_destinataire) {
    sendPipe = "/tmp/" + pseudo_utilisateur + "-" + pseudo_destinataire + ".chat";
    receivePipe = "/tmp/" + pseudo_destinataire + "-" + pseudo_utilisateur + ".chat";
}

/**
 * @brief Crée un pipe nommé si celui-ci n'existe pas déjà
 * @param pipePath Chemin du pipe à créer
 */
void Pipes::createPipe(const std::string& pipePath) {
    if (mkfifo(pipePath.c_str(), 0666) == -1) {
        if (errno != EEXIST) {
            perror("Erreur lors de la création du pipe");
            exit(1);
        }
        // Si le pipe existe déjà, on ne fait rien
    }
}

/**
 * @brief Supprime les pipes nommés
 */
void Pipes::unlink_pipes() {
    if (unlink(sendPipe.c_str()) == -1) {
        if (errno != ENOENT) {
            perror("Erreur lors de la suppression du pipe d'envoi");
        }
    }
    if (unlink(receivePipe.c_str()) == -1) {
        if (errno != ENOENT) {
            perror("Erreur lors de la suppression du pipe de réception");
        }
    }
}
