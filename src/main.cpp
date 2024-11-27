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

using namespace std;

// Constantes
constexpr size_t SHM_SIZE = 4096; // Taille de la mémoire partagée

// Variables globales
bool pipesOuverts = false;       // Indique si les pipes ont été ouverts
bool isManuelMode = false;       // Mode manuel activé ou non
bool isJoliMode = false;         // Mode joli activé ou non
bool isBotMode = false;          // Mode bot activé ou non

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

// Prototypes des fonctions
void initialize_shared_memory(bool create, const char* shm_name);
void release_shared_memory(bool isParent, const char* shm_name);
void output_shared_memory();
void write_to_shared_memory(const string& message);
bool containsChar(const string& str, char ch);
void checkParams(int argc, char* argv[]);
void createPipe(const string& pipePath);
void Reset_Ligne();
string texte_a_print(string pseudo);
void handleSIGINT(int signal);
void handleSIGPIPE(int signal);
void handleSIGUSR1(int signal);
void handleSIGTERM(int signal);

int main(int argc, char* argv[]) {
    checkParams(argc, argv); // Vérifie les paramètres du programme

    // Construction des noms des pipes
    sendPipe = "/tmp/" + pseudo_utilisateur + "-" + pseudo_destinataire + ".chat";
    receivePipe = "/tmp/" + pseudo_destinataire + "-" + pseudo_utilisateur + ".chat";

    // Définition du nom de la mémoire partagée
    SHM_NAME = "/chat_shm_" + pseudo_utilisateur + "_" + pseudo_destinataire;

    createPipe(sendPipe);      // Crée le pipe d'envoi si nécessaire
    createPipe(receivePipe);   // Crée le pipe de réception si nécessaire

    // Initialisation de la mémoire partagée avant le fork
    if (isManuelMode) {
        initialize_shared_memory(true, SHM_NAME.c_str()); // Crée la mémoire partagée
    }

    pid = fork(); // Création du processus enfant
    if (pid < 0) {
        perror("Erreur lors de la création du processus");
        if (isManuelMode) {
            release_shared_memory(true, SHM_NAME.c_str());
        }
        exit(1);
    } else if (pid == 0) {
        // === Processus enfant ===

        // Ouverture de la mémoire partagée existante en mode manuel
        if (isManuelMode) {
            initialize_shared_memory(false, SHM_NAME.c_str());
        }

        signal(SIGINT, SIG_IGN);          // Ignorer SIGINT dans le processus enfant
        signal(SIGTERM, handleSIGTERM);   // Gestionnaire pour SIGTERM
        signal(SIGUSR1, handleSIGUSR1);   // Gestionnaire pour SIGUSR1

        // Ouverture du pipe de réception
        fd_receive = open(receivePipe.c_str(), O_RDONLY);
        if (fd_receive < 0) {
            perror("Erreur lors de l'ouverture du pipe de réception");
            exit(1);
        }
        pipesOuverts = true;

        char buffer[256]; // Buffer pour la lecture des messages
        while (true) {
            ssize_t bytesRead = read(fd_receive, buffer, sizeof(buffer) - 1);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0'; // Ajout du terminateur de chaîne
                if (isManuelMode) {
                    write_to_shared_memory(string(buffer)); // Écriture dans la mémoire partagée
                    // Émettre un bip sonore pour notifier l'arrivée d'un message
                    printf("\a");
                    fflush(stdout);
                    // Vérifier si plus de 4096 octets sont en attente
                    if (*shm_offset_ptr >= SHM_SIZE - sizeof(size_t)) {
                        // Envoyer SIGUSR1 au processus parent pour afficher les messages
                        kill(getppid(), SIGUSR1);
                    }
                } else {
                    // Affichage immédiat du message en mode normal
                    printf(isBotMode ? "[%s] %s" : texte_a_print(pseudo_destinataire).c_str(),
                           pseudo_destinataire.c_str(), buffer);
                    if (isJoliMode) {
                        printf("%s, entrez votre message (tapez 'exit' pour quitter) : ", pseudo_utilisateur.c_str());
                    }
                    fflush(stdout);
                }
            } else if (bytesRead == 0) {
                // Pipe fermé, l'autre utilisateur a quitté
                break;
            } else {
                // Erreur de lecture
                if (errno == EINTR) {
                    if (should_exit) {
                        // Le parent a demandé la terminaison
                        break;
                    } else {
                        continue; // Continuer la lecture
                    }
                } else {
                    perror("Erreur lors de la lecture du pipe de réception");
                    break;
                }
            }
        }
        close(fd_receive); // Fermeture du pipe de réception
        if (isManuelMode) {
            release_shared_memory(false, SHM_NAME.c_str()); // Libération de la mémoire partagée
        }
        exit(0);
    } else {
        // === Processus parent ===

        signal(SIGINT, handleSIGINT);     // Gestionnaire pour SIGINT
        signal(SIGPIPE, handleSIGPIPE);   // Gestionnaire pour SIGPIPE
        signal(SIGUSR1, handleSIGUSR1);   // Gestionnaire pour SIGUSR1

        // Ouverture du pipe d'envoi
        fd_send = open(sendPipe.c_str(), O_WRONLY);
        if (fd_send < 0) {
            perror("Erreur lors de l'ouverture du pipe d'envoi");
            // Envoyer SIGTERM au processus enfant pour qu'il se termine
            kill(pid, SIGTERM);
            if (isManuelMode) {
                release_shared_memory(true, SHM_NAME.c_str());
            }
            exit(1);
        }
        pipesOuverts = true;

        char buffer[256]; // Buffer pour la lecture des messages de l'utilisateur
        while (true) {
            if (isJoliMode) {
                printf("%s, entrez votre message (tapez 'exit' pour quitter) : ", pseudo_utilisateur.c_str());
                fflush(stdout);
            }
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                // Fin de stdin (Ctrl+D)
                if (isManuelMode) {
                    output_shared_memory(); // Afficher les messages en attente
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
            ssize_t bytes_written = write(fd_send, buffer, strlen(buffer) + 1);
            if (bytes_written == -1) {
                perror("Erreur lors de l'écriture dans le pipe");
                break;
            }
            if (!isBotMode) {
                // Affichage du message envoyé par l'utilisateur
                printf(texte_a_print(pseudo_utilisateur).c_str(), pseudo_utilisateur.c_str(), buffer);
            }
            if (isManuelMode) {
                // Afficher les messages en attente si en mode manuel
                output_shared_memory();
            }
            fflush(stdout);
        }

        close(fd_send); // Fermeture du pipe d'envoi

        // Attendre la fin du processus enfant
        wait(nullptr);

        // Suppression des pipes nommés
        unlink(sendPipe.c_str());
        unlink(receivePipe.c_str());

        if (isManuelMode) {
            release_shared_memory(true, SHM_NAME.c_str()); // Libération de la mémoire partagée
        }

        return 0; // Fin du programme
    }
}

// === Gestionnaires de signaux ===

// Gestionnaire pour SIGTERM dans le processus enfant
void handleSIGTERM(int /*signal*/) {
    should_exit = 1; // Indique au processus enfant de se terminer
}

// Gestionnaire pour SIGINT dans le processus parent
void handleSIGINT(int signal) {
    if (signal == SIGINT) {
        if (!pipesOuverts) {
            // Les pipes n'ont pas été ouverts, terminer avec le code de retour 4
            exit(4);
        } else if (isManuelMode) {
            // Afficher les messages en attente en mode manuel
            output_shared_memory();
        } else {
            // Ignorer SIGINT lorsque les pipes sont ouverts et que --manuel n'est pas activé
        }
    }
}

// Gestionnaire pour SIGPIPE dans le processus parent
void handleSIGPIPE(int signal) {
    if (signal == SIGPIPE && !isManuelMode) {
        cout << "Connexion terminée par l'autre utilisateur." << endl;
        exit(5);
    }
}

// Gestionnaire pour SIGUSR1 dans le processus parent
void handleSIGUSR1(int) {
    if (isManuelMode) {
        output_shared_memory(); // Afficher les messages en attente
    }
}

// === Fonctions auxiliaires ===

// Vérifie si une chaîne contient un caractère spécifique
bool containsChar(const string& str, char ch) {
    return find(str.begin(), str.end(), ch) != str.end();
}

// Vérifie les paramètres du programme
void checkParams(int argc, char* argv[]) {
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
    vector<char> caracteres_interdits = {'/', '[', ']', '-'};
    for (char caractere : caracteres_interdits) {
        if (containsChar(pseudo_utilisateur, caractere) || containsChar(pseudo_destinataire, caractere)) {
            fprintf(stderr, "Erreur : Les pseudonymes contiennent des caractères interdits (/, -, [, ]).\n");
            exit(3);
        }
    }

    // Traitement des options
    for (int i = 3; i < argc; ++i) {
        if (string(argv[i]) == "--bot") isBotMode = true;
        if (string(argv[i]) == "--manuel") isManuelMode = true;
        if (string(argv[i]) == "--joli") isJoliMode = true; // Option supplémentaire pour un affichage amélioré
    }
}

// Crée un pipe nommé si celui-ci n'existe pas déjà
void createPipe(const string& pipePath) {
    if (access(pipePath.c_str(), F_OK) != 0) {
        if (mkfifo(pipePath.c_str(), 0666) == -1) {
            perror("Erreur lors de la création du pipe");
            exit(1);
        }
    }
}

// Réinitialise la ligne du terminal (utilisé en mode joli)
void Reset_Ligne() {
    cout << "\033[2K\r";
}

// Génère le format du texte à afficher en fonction des options
string texte_a_print(string pseudo) {
    string texte = "[\x1B[4m%s\x1B[0m] %s"; // Format par défaut avec pseudonyme souligné
    if (isJoliMode) {
        if (pseudo == pseudo_destinataire) {
            // Couleur jaune pour le destinataire
            texte = "\033[93m[\x1B[4m%s\x1B[24m]\033[0m %s";
            Reset_Ligne();
        } else if (pseudo == pseudo_utilisateur) {
            // Couleur cyan pour l'utilisateur
            texte = "\033[96m[\x1B[4m%s\x1B[24m]\033[0m %s";
            printf("\033[A");
            Reset_Ligne();
        }
    }
    return texte;
}

// Initialise la mémoire partagée
void initialize_shared_memory(bool create, const char* shm_name) {
    int shm_fd;
    if (create) {
        shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    } else {
        shm_fd = shm_open(shm_name, O_RDWR, 0666);
    }

    if (shm_fd == -1) {
        perror("Erreur lors de l'ouverture de la mémoire partagée");
        exit(1);
    }

    if (create) {
        // Définir la taille de la mémoire partagée
        if (ftruncate(shm_fd, SHM_SIZE) == -1) {
            perror("Erreur lors de la configuration de la taille de la mémoire partagée");
            close(shm_fd);
            shm_unlink(shm_name);
            exit(1);
        }
    }

    // Mapping de la mémoire partagée
    shm_ptr = static_cast<char*>(mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    if (shm_ptr == MAP_FAILED) {
        perror("Erreur lors du mapping de la mémoire partagée");
        close(shm_fd);
        if (create) {
            shm_unlink(shm_name);
        }
        exit(1);
    }

    shm_offset_ptr = reinterpret_cast<size_t*>(shm_ptr);
    if (create) {
        *shm_offset_ptr = 0; // Initialisation de l'offset
    }

    close(shm_fd); // Fermeture du descripteur de la mémoire partagée
}

// Libère la mémoire partagée
void release_shared_memory(bool isParent, const char* shm_name) {
    if (shm_ptr) {
        munmap(shm_ptr, SHM_SIZE); // Détache le segment de mémoire partagée
        if (isParent) {
            shm_unlink(shm_name); // Supprime la mémoire partagée
        }
    }
}

// Écrit un message dans la mémoire partagée
void write_to_shared_memory(const string& message) {
    size_t message_length = message.size() + 1; // Taille du message avec le caractère nul
    if (*shm_offset_ptr + message_length > SHM_SIZE - sizeof(size_t)) {
        // Mémoire pleine, réinitialiser
        *shm_offset_ptr = 0;
    }

    char* shm_data = shm_ptr + sizeof(size_t) + *shm_offset_ptr;
    memcpy(shm_data, message.c_str(), message_length); // Copie du message dans la mémoire partagée
    *shm_offset_ptr += message_length; // Mise à jour de l'offset
}

// Affiche les messages en attente depuis la mémoire partagée
void output_shared_memory() {
    size_t offset = 0;
    char* shm_data = shm_ptr + sizeof(size_t);
    while (offset < *shm_offset_ptr) {
        // Affichage des messages
        printf(isBotMode ? "[%s] %s" : texte_a_print(pseudo_destinataire).c_str(),
               pseudo_destinataire.c_str(), (shm_data + offset));
        offset += strlen(shm_data + offset) + 1;
    }
    // Réinitialisation de la mémoire partagée
    memset(shm_ptr + sizeof(size_t), 0, SHM_SIZE - sizeof(size_t));
    *shm_offset_ptr = 0;
}
