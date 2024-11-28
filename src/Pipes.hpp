#ifndef PIPES_HPP
#define PIPES_HPP

#include <string>

class Pipes {
public:
    // Variables membres
    bool pipesOuverts = false;       // Indique si les pipes ont été ouverts

    int fd_receive = -1;             // Descripteur du pipe de réception
    int fd_send = -1;                // Descripteur du pipe d'envoi
    std::string sendPipe;            // Nom du pipe d'envoi
    std::string receivePipe;         // Nom du pipe de réception

    // Constructeur
    Pipes(const std::string& pseudo_utilisateur, const std::string& pseudo_destinataire);

    // Fonctions
    void createPipe(const std::string& pipePath);
    void unlink_pipes(); // Ajout de cette méthode
};

#endif // PIPES_HPP
