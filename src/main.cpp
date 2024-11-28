// main.cpp

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <sys/stat.h>
#include <fcntl.h>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <sys/mman.h>
#include <errno.h>
#include <termios.h> // Pour --joli

#include "SignalHandler.hpp"
#include "SharedMemory.hpp"
#include "Pipes.hpp"
#include "ParameterValidator.hpp"

using namespace std;

// Variables globales
bool pipesOuverts = false;       // Indique si les pipes ont été ouverts
bool isManuelMode = false;       // Mode manuel activé ou non
bool isBotMode = false;          // Mode bot activé ou non
bool isJoliMode = false;         // Mode joli activé ou non

int fd_receive = -1;             // Descripteur du pipe de réception
int fd_send = -1;                // Descripteur du pipe d'envoi
string sendPipe;                 // Nom du pipe d'envoi
string receivePipe;              // Nom du pipe de réception

char* shm_ptr = nullptr;         // Pointeur vers la mémoire partagée
size_t* shm_offset_ptr = nullptr;// Pointeur vers l'offset dans la mémoire partagée

string pseudo_utilisateur;       // Pseudonyme de l'utilisateur
string pseudo_destinataire;      // Pseudonyme du destinataire
string SHM_NAME;                 // Nom de la mémoire partagée

volatile sig_atomic_t should_exit = 0; // Indique si le processus enfant doit se terminer

pid_t pid; // PID du processus enfant

// Liste des codes de couleur ANSI
const vector<string> color_codes = {
    "\033[31m", // Rouge
    "\033[32m", // Vert
    "\033[33m", // Jaune
    "\033[34m", // Bleu
    "\033[35m", // Magenta (Violet)
    "\033[36m", // Cyan (Turquoise)
    "\033[91m", // Rouge clair
    "\033[92m", // Vert clair
    "\033[93m", // Jaune clair
    "\033[94m", // Bleu clair
    "\033[95m", // Magenta clair
    "\033[96m", // Cyan clair
};

// Prototypes des fonctions restantes
bool containsChar(const string& str, char ch);
string texte_a_print(string pseudo);
string getColorCode(const string& pseudo);
ssize_t safeWrite(int fd, const void* buf, size_t count);
ssize_t safeReadMessage(int fd, char* buffer, size_t max_size);

int main(int argc, char* argv[]) {
    // Création des instances des classes
    ParameterValidator paramValidator;
    Pipes pipes("", "");
    SharedMemory* sharedMemory = nullptr;

    // Vérification des paramètres du programme
    paramValidator.checkParams(argc, argv);

    // Construction des noms des pipes
    sendPipe = "/tmp/" + pseudo_utilisateur + "-" + pseudo_destinataire + ".chat";
    receivePipe = "/tmp/" + pseudo_destinataire + "-" + pseudo_utilisateur + ".chat";

    // Définition du nom de la mémoire partagée
    SHM_NAME = "/chat_shm_" + pseudo_utilisateur + "_" + pseudo_destinataire;

    // Mise à jour des noms des pipes dans l'instance de Pipes
    pipes = Pipes(pseudo_utilisateur, pseudo_destinataire);

    // Création des pipes nommés
    pipes.createPipe(sendPipe);
    pipes.createPipe(receivePipe);

    // Initialisation de la mémoire partagée avant le fork
    if (isManuelMode) {
        sharedMemory = new SharedMemory(SHM_NAME);
        sharedMemory->initialize_shared_memory(true); // Crée la mémoire partagée
        shm_ptr = sharedMemory->shm_ptr;
        shm_offset_ptr = sharedMemory->shm_offset_ptr;
    }

    // Initialiser SignalHandler avec les instances
    SignalHandler::init(sharedMemory, &pipes);

    pid = fork(); // Création du processus enfant
    if (pid < 0) {
        perror("Erreur lors de la création du processus");
        if (isManuelMode) {
            sharedMemory->release_shared_memory(true);
            delete sharedMemory;
            sharedMemory = nullptr;
        }
        exit(1);
    } else if (pid == 0) {
        // === Processus enfant ===

        // Ouverture de la mémoire partagée existante en mode manuel
        if (isManuelMode) {
            sharedMemory->initialize_shared_memory(false);
            shm_ptr = sharedMemory->shm_ptr;
            shm_offset_ptr = sharedMemory->shm_offset_ptr;
        }

        signal(SIGINT, SIG_IGN); // Ignorer SIGINT dans le processus enfant
        signal(SIGTERM, SignalHandler::handleSIGTERM); // Gestionnaire pour SIGTERM
        signal(SIGUSR1, SignalHandler::handleSIGUSR1); // Gestionnaire pour SIGUSR1

        // Ouverture du pipe de réception
        fd_receive = open(receivePipe.c_str(), O_RDONLY);
        if (fd_receive < 0) {
            perror("Erreur lors de l'ouverture du pipe de réception");
            exit(1);
        }
        pipesOuverts = true;

        char buffer[256]; // Buffer pour la lecture des messages
        while (!should_exit) {
            ssize_t bytesRead = safeReadMessage(fd_receive, buffer, sizeof(buffer));
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0'; // Ajout du terminateur de chaîne
                if (isManuelMode) {
                    sharedMemory->write_to_shared_memory(string(buffer)); // Écriture dans la mémoire partagée
                    // Émettre un bip sonore pour notifier l'arrivée d'un message
                    printf("\a");
                    fflush(stdout);
                    // Vérifier si plus de 4096 octets sont en attente
                    if (*shm_offset_ptr >= SharedMemory::SHM_SIZE - sizeof(size_t)) {
                        // Envoyer SIGUSR1 au processus parent pour afficher les messages
                        kill(getppid(), SIGUSR1);
                    }
                } else {
                    // Affichage immédiat du message
                    printf(texte_a_print(pseudo_destinataire).c_str(),
                           pseudo_destinataire.c_str(), buffer);
                    fflush(stdout);
                }
            } else if (bytesRead == 0) {
                // Pipe fermé, l'autre utilisateur a quitté
                if (!isManuelMode) {
                    // Informer le processus parent en mode normal
                    kill(getppid(), SIGUSR2);
                }
                break;
            } else {
                // Erreur de lecture
                if (errno == EINTR) {
                    continue; // Continuer la lecture
                } else {
                    perror("Erreur lors de la lecture du pipe de réception");
                    break;
                }
            }
        }
        close(fd_receive); // Fermeture du pipe de réception
        if (isManuelMode) {
            sharedMemory->release_shared_memory(false); // Libération de la mémoire partagée
            delete sharedMemory;
            sharedMemory = nullptr;
        }
        exit(0);
    } else {
        // === Processus parent ===

        signal(SIGINT, SignalHandler::handleSIGINT);   // Gestionnaire pour SIGINT
        signal(SIGPIPE, SignalHandler::handleSIGPIPE); // Gestionnaire pour SIGPIPE
        signal(SIGUSR1, SignalHandler::handleSIGUSR1); // Gestionnaire pour SIGUSR1
        signal(SIGUSR2, SignalHandler::handleSIGUSR2); // Gestionnaire pour SIGUSR2

        // Configuration du terminal pour le mode joli
        struct termios oldt, newt;
        if (isJoliMode) {
            // Obtenir les attributs du terminal
            tcgetattr(STDIN_FILENO, &oldt);
            newt = oldt;

            // Désactiver l'écho des caractères de contrôle
            newt.c_lflag &= ~ECHOCTL;

            // Appliquer les nouveaux attributs
            tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        }

        // Ouverture du pipe d'envoi
        fd_send = open(sendPipe.c_str(), O_WRONLY);
        if (fd_send < 0) {
            perror("Erreur lors de l'ouverture du pipe d'envoi");
            // Envoyer SIGTERM au processus enfant pour qu'il se termine
            kill(pid, SIGTERM);
            if (isManuelMode) {
                sharedMemory->release_shared_memory(true);
                delete sharedMemory;
                sharedMemory = nullptr;
            }
            exit(1);
        }
        pipesOuverts = true;

        char buffer[256]; // Buffer pour la lecture des messages de l'utilisateur
        while (true) {
            if (isJoliMode) {
                // Afficher une phrase avant la saisie
                printf("\n⭐✨ Veuillez entrer votre message ✨⭐ : ");
                fflush(stdout);
            }

            if (!fgets(buffer, sizeof(buffer), stdin)) {
                // Fin de stdin (Ctrl+D)
                if (isManuelMode) {
                    sharedMemory->output_shared_memory(); // Afficher les messages en attente
                }
                // Envoyer SIGTERM au processus enfant pour qu'il se termine
                kill(pid, SIGTERM);
                break;
            }

            if (strcmp(buffer, "exit\n") == 0) {
                // Commande 'exit' reçue, terminer le chat
                // Envoyer SIGTERM au processus enfant pour qu'il se termine
                kill(pid, SIGTERM);
                break;
            }

            size_t message_length = strlen(buffer) + 1;
            if (safeWrite(fd_send, buffer, message_length) == -1) {
                // Erreur lors de l'écriture, déjà affichée dans safeWrite
                break;
            }

            if (!isBotMode) {
                // Affichage du message envoyé par l'utilisateur
                printf(texte_a_print(pseudo_utilisateur).c_str(), pseudo_utilisateur.c_str(), buffer);
                fflush(stdout);
            }

            if (isManuelMode) {
                sharedMemory->output_shared_memory(); // Afficher les messages en attente
            }
        }

        // Rétablir les anciens attributs du terminal
        if (isJoliMode) {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        }

        close(fd_send); // Fermeture du pipe d'envoi

        // Attendre la fin du processus enfant
        wait(nullptr);

        // Suppression des pipes nommés
        pipes.unlink_pipes();

        if (isManuelMode) {
            sharedMemory->release_shared_memory(true); // Libération de la mémoire partagée
            delete sharedMemory;
            sharedMemory = nullptr;
        }

        return 0; // Fin du programme
    }
}

// === Fonctions auxiliaires ===

bool containsChar(const string& str, char ch) {
    return find(str.begin(), str.end(), ch) != str.end();
}

// Fonction pour obtenir le code couleur en fonction du pseudonyme
string getColorCode(const string& pseudo) {
    // Calculer un hachage simple du pseudonyme
    size_t hash = 0;
    for (char c : pseudo) {
        hash = hash * 36 + c;
    }
    // Mapper le hachage sur le nombre de codes couleur disponibles
    size_t color_index = hash % color_codes.size();
    return color_codes[color_index];
}

string texte_a_print(string pseudo) {
    string texte;
    if (isBotMode) {
        // En mode bot, pas de soulignement ni de couleur
        texte = "[%s] %s";
    } else if (isJoliMode) {
        // En mode joli, ajout de couleurs pour les pseudonymes
        string color_code = getColorCode(pseudo);
        texte = "[" + color_code + "%s\033[0m] %s";
    } else {
        // Format par défaut avec pseudonyme souligné
        texte = "[\x1B[4m%s\x1B[0m] %s";
    }
    return texte;
}

ssize_t safeWrite(int fd, const void* buf, size_t count) {
    size_t total_written = 0;
    const char* buffer = static_cast<const char*>(buf);

    while (total_written < count) {
        ssize_t bytes_written = write(fd, buffer + total_written, count - total_written);
        if (bytes_written == -1) {
            if (errno == EINTR) {
                continue; // Interruption par un signal, on réessaie
            } else {
                // Erreur critique
                perror("Erreur lors de l'écriture dans le pipe");
                return -1;
            }
        }
        total_written += bytes_written;
    }
    return total_written;
}

ssize_t safeReadMessage(int fd, char* buffer, size_t max_size) {
    size_t total_read = 0;
    while (total_read < max_size - 1) { // On laisse de la place pour le '\0'
        ssize_t bytes_read = read(fd, buffer + total_read, 1);
        if (bytes_read == -1) {
            if (errno == EINTR) {
                continue; // Interruption par un signal, on réessaie
            } else {
                // Erreur critique
                perror("Erreur lors de la lecture du pipe de réception");
                return -1;
            }
        } else if (bytes_read == 0) {
            // Fin du flux
            if (total_read == 0) {
                // Pas de données lues
                return 0;
            } else {
                // Données partielles lues
                buffer[total_read] = '\0'; // On termine la chaîne
                return total_read;
            }
        } else {
            // Un octet lu
            if (buffer[total_read] == '\0') {
                // Fin du message
                return total_read + 1;
            }
            total_read += bytes_read;
        }
    }
    // Si on arrive ici, le buffer est plein
    buffer[total_read] = '\0'; // On termine la chaîne
    return total_read;
}
