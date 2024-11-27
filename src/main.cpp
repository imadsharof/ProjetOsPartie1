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

using namespace std;

// Constantes
constexpr size_t SHM_SIZE = 4096;

// Booléens
bool pipesOuverts = false;
bool isManuelMode = false;
bool isJoliMode = false;
bool isBotMode = false;

// Pipes
int fd_receive;
int fd_send;
string sendPipe;
string receivePipe;

// Variables globales
char* shm_ptr = nullptr;
size_t* shm_offset_ptr = nullptr;

string pseudo_utilisateur;
string pseudo_destinataire;
string SHM_NAME;

// Prototypes
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

int main(int argc, char* argv[]) {
    checkParams(argc, argv);

    sendPipe = "/tmp/" + pseudo_utilisateur + "-" + pseudo_destinataire + ".chat";
    receivePipe = "/tmp/" + pseudo_destinataire + "-" + pseudo_utilisateur + ".chat";

    // Définir SHM_NAME après avoir initialisé les pseudonymes
    SHM_NAME = "/chat_shm_" + pseudo_utilisateur + "_" + pseudo_destinataire;

    createPipe(sendPipe);
    createPipe(receivePipe);

    pid_t pid = fork();
    if (pid < 0) {
        perror("Erreur lors de la création du processus");
        exit(1);
    } else if (pid == 0) {
        // Processus enfant
        if (isManuelMode) {
            initialize_shared_memory(false, SHM_NAME.c_str()); // Ouvre la mémoire partagée existante
        }
        signal(SIGINT, SIG_IGN);
        fd_receive = open(receivePipe.c_str(), O_RDONLY);
        if (fd_receive < 0) {
            perror("Erreur lors de l'ouverture du pipe de réception");
            exit(1);
        }
        pipesOuverts = true;
        char buffer[256];
        while (true) {
            ssize_t bytesRead = read(fd_receive, buffer, sizeof(buffer) - 1);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                if (isManuelMode) {
                    write_to_shared_memory(string(buffer));
                    // Émettre un bip sonore
                    printf("\a");
                    fflush(stdout);
                    // Vérifier si plus de 4096 octets sont en attente
                    if (*shm_offset_ptr >= SHM_SIZE - sizeof(size_t)) {
                        // Envoyer SIGUSR1 au processus parent pour afficher les messages
                        kill(getppid(), SIGUSR1);
                    }
                } else {
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
                perror("Erreur lors de la lecture du pipe de réception");
                break;
            }
        }
        close(fd_receive);
        if (isManuelMode) {
            release_shared_memory(false, SHM_NAME.c_str());
        }
        exit(0);
    } else {
        // Processus parent
        if (isManuelMode) {
            initialize_shared_memory(true, SHM_NAME.c_str()); // Crée la mémoire partagée
        }
        signal(SIGINT, handleSIGINT);
        signal(SIGPIPE, handleSIGPIPE);
        signal(SIGUSR1, handleSIGUSR1);

        fd_send = open(sendPipe.c_str(), O_WRONLY);
        if (fd_send < 0) {
            perror("Erreur lors de l'ouverture du pipe d'envoi");
            exit(1);
        }
        pipesOuverts = true;
        char buffer[256];
        while (true) {
            if (isJoliMode) {
                printf("%s, entrez votre message (tapez 'exit' pour quitter) : ", pseudo_utilisateur.c_str());
                fflush(stdout);
            }
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                // Fin de stdin
                break;
            }
            if (strcmp(buffer, "exit\n") == 0) {
                break;
            }
            ssize_t bytes_written = write(fd_send, buffer, strlen(buffer) + 1);
            if (bytes_written == -1) {
                perror("Erreur lors de l'écriture dans le pipe");
                break;
            }
            if (!isBotMode) {
                printf(texte_a_print(pseudo_utilisateur).c_str(), pseudo_utilisateur.c_str(), buffer);
            }
            if (isManuelMode) {
                output_shared_memory();
            }
            fflush(stdout);
        }

        close(fd_send);
        wait(nullptr);

        unlink(sendPipe.c_str());
        unlink(receivePipe.c_str());

        if (isManuelMode) {
            release_shared_memory(true, SHM_NAME.c_str());
        }

        return 0;
    }
}

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
        if (ftruncate(shm_fd, SHM_SIZE) == -1) {
            perror("Erreur lors de la configuration de la taille de la mémoire partagée");
            close(shm_fd);
            shm_unlink(shm_name);
            exit(1);
        }
    }

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
        *shm_offset_ptr = 0;
    }

    close(shm_fd);
}

void release_shared_memory(bool isParent, const char* shm_name) {
    if (shm_ptr) {
        munmap(shm_ptr, SHM_SIZE);
        if (isParent) {
            shm_unlink(shm_name);
        }
    }
}

void write_to_shared_memory(const string& message) {
    size_t message_length = message.size() + 1;
    if (*shm_offset_ptr + message_length > SHM_SIZE - sizeof(size_t)) {
        // Mémoire pleine, réinitialiser
        *shm_offset_ptr = 0;
    }

    char* shm_data = shm_ptr + sizeof(size_t) + *shm_offset_ptr;
    memcpy(shm_data, message.c_str(), message_length);
    *shm_offset_ptr += message_length;
}

void output_shared_memory() {
    size_t offset = 0;
    char* shm_data = shm_ptr + sizeof(size_t);
    while (offset < *shm_offset_ptr) {
        printf(isBotMode ? "[%s] %s" : texte_a_print(pseudo_destinataire).c_str(),
               pseudo_destinataire.c_str(), (shm_data + offset));
        offset += strlen(shm_data + offset) + 1;
    }
    // Réinitialiser la mémoire partagée
    memset(shm_ptr + sizeof(size_t), 0, SHM_SIZE - sizeof(size_t));
    *shm_offset_ptr = 0;
}

void handleSIGINT(int signal) {
    if (isJoliMode) {
        Reset_Ligne();
    }
    if (signal == SIGINT) {
        if (pipesOuverts && isManuelMode) {
            output_shared_memory();
        } else {
            cout << "Fermeture du programme suite à SIGINT." << endl;
            if (fd_receive > 0) close(fd_receive);
            if (fd_send > 0) close(fd_send);

            unlink(sendPipe.c_str());
            unlink(receivePipe.c_str());

            exit(4);
        }
    }
}

void handleSIGPIPE(int signal) {
    if (signal == SIGPIPE && !isManuelMode) {
        cout << "Connexion terminée par l'autre utilisateur." << endl;
        exit(5);
    }
}

void handleSIGUSR1(int ) {
    if (isManuelMode) {
        output_shared_memory();
    }
}

// Les autres fonctions (checkParams, createPipe, etc.) restent inchangées


bool containsChar(const string& str, char ch) {
    return find(str.begin(), str.end(), ch) != str.end();
}

void checkParams(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "chat pseudo_utilisateur pseudo_destinataire [--bot] [--manuel]\n");
        exit(1);
    }

    pseudo_utilisateur = argv[1];
    pseudo_destinataire = argv[2];

    if (pseudo_utilisateur.size() > 30 || pseudo_destinataire.size() > 30) {
        fprintf(stderr, "Erreur : Les pseudonymes ne doivent pas dépasser 30 caractères.\n");
        exit(2);
    }

    if (pseudo_utilisateur == "." || pseudo_utilisateur == ".." || pseudo_destinataire == "." || pseudo_destinataire == "..") {
        fprintf(stderr, "Erreur : Les pseudonymes ne peuvent pas être '.' ou '..'.\n");
        exit(3);
    }

    vector<char> caracteres_interdits = {'/', '[', ']', '-'};
    for (char caractere : caracteres_interdits) {
        if (containsChar(pseudo_utilisateur, caractere) || containsChar(pseudo_destinataire, caractere)) {
            fprintf(stderr, "Erreur : Les pseudonymes contiennent des caractères interdits (/, -, [, ]).\n");
            exit(3);
        }
    }

    for (int i = 3; i < argc; ++i) {
        if (string(argv[i]) == "--bot") isBotMode = true;
        if (string(argv[i]) == "--manuel") isManuelMode = true;
        if (string(argv[i]) == "--joli") isJoliMode = true;
    }
}

void createPipe(const string& pipePath) {
    if (access(pipePath.c_str(), F_OK) != 0) {
        if (mkfifo(pipePath.c_str(), 0666) == -1) {
            perror("Erreur lors de la création du pipe");
            exit(1);
        }
    }
}

void Reset_Ligne() {
    cout << "\033[2K\r";
}

string texte_a_print(string pseudo) {
    string texte = "[\x1B[4m%s\x1B[0m] %s";
    if (isJoliMode) {
        if (pseudo == pseudo_destinataire) {
            texte = "\033[93m[\x1B[4m%s\x1B[24m]\033[0m %s";
            Reset_Ligne();
        } else if (pseudo == pseudo_utilisateur) {
            texte = "\033[96m[\x1B[4m%s\x1B[24m]\033[0m %s";
            printf("\033[A");
            Reset_Ligne();
        }
    }
    return texte;
}


