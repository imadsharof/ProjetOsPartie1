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
#include <semaphore.h>

using namespace std;

// Constantes
constexpr size_t SHM_SIZE = 4096;
const char* SHM_NAME = "/chat_shm";
const char* SEM_NAME = "/chat_semaphore";

//Booléens
bool pipesOuverts = false;
bool isManuelMode = false;
bool isJoliMode = false;

// Variables globales
char* shm_ptr = nullptr;
sem_t* semaphore = nullptr;
size_t* shm_offset_ptr = nullptr;

string pseudo_utilisateur;
string pseudo_destinataire;

//Prototypes
void initialize_shared_memory();
void release_shared_memory();
void output_shared_memory();
void write_to_shared_memory(const string& message);
void affichage_manuel(); 
bool containsChar(const string& str, char ch); 
void checkParams(int argc, char* argv[], bool& isBotMode);
void createPipe(const string& pipePath);
void Reset_Ligne();
void handleSIGINT(int signal);
void handleSIGPIPE(int signal);


int main(int argc, char* argv[]) {
    bool isBotMode = false;

    checkParams(argc, argv, isBotMode);

    string sendPipe = "/tmp/" + pseudo_utilisateur + "-" + pseudo_destinataire + ".chat";
    string receivePipe = "/tmp/" + pseudo_destinataire + "-" + pseudo_utilisateur + ".chat";

    createPipe(sendPipe);
    createPipe(receivePipe);

    if (isManuelMode) {
        initialize_shared_memory();
    }

    pid_t pid = fork(); 
    if (pid < 0) {
        perror("Erreur lors de la création du processus");
        exit(1);
    } 
    else if (pid == 0) {
        int fd_receive = open(receivePipe.c_str(), O_RDONLY);
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
                    write_to_shared_memory(buffer); 
                } else {
                    printf("[%s] %s", pseudo_destinataire.c_str(), buffer);
                }
                fflush(stdout);
            } else {
                break;
            }
        }
        close(fd_receive);
        exit(0);
    } 
    else {
        signal(SIGINT, handleSIGINT);
        signal(SIGPIPE, handleSIGPIPE);

        int fd_send = open(sendPipe.c_str(), O_WRONLY);
        if (fd_send < 0) {
            perror("Erreur lors de l'ouverture du pipe d'envoi");
            exit(1);
        }
        pipesOuverts = true;
        char buffer[256];
        while (true) {
            if(isJoliMode) {
                printf("%s, entrez votre message (tapez 'exit' pour quitter) : ", pseudo_utilisateur.c_str());
            }
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                perror("Erreur lors de la lecture de l'entrée");
                break;
            }
            if (string(buffer) == "exit\n") exit(0);
            ssize_t bytes_written = write(fd_send, buffer, strlen(buffer) + 1);
            if (bytes_written == -1) {
                perror("Erreur lors de l'écriture dans le pipe");
            }
            if(!isBotMode){
                printf("[%s] %s", pseudo_utilisateur.c_str(), buffer);
            }
            if (isManuelMode) {
                affichage_manuel();
            }
            fflush(stdout);
        }

        close(fd_send);
        wait(nullptr);
    }

    unlink(sendPipe.c_str());
    unlink(receivePipe.c_str());

    if (isManuelMode) {
        release_shared_memory();
    }

    return 0;
}

// Initialisation de la mémoire partagée
void initialize_shared_memory() {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("Erreur lors de l'ouverture de la mémoire partagée");
        exit(1);
    }

    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("Erreur lors de la configuration de la taille de la mémoire partagée");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(1);
    }

    shm_ptr = static_cast<char*>(mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    if (shm_ptr == MAP_FAILED) {
        perror("Erreur lors du mapping de la mémoire partagée");
        close(shm_fd);
        shm_unlink(SHM_NAME);
        exit(1);
    }

    shm_offset_ptr = reinterpret_cast<size_t*>(shm_ptr);
    *shm_offset_ptr = 0;

    semaphore = sem_open(SEM_NAME, O_CREAT, 0666, 1);
    if (semaphore == SEM_FAILED) {
        perror("Erreur lors de l'ouverture du sémaphore");
        munmap(shm_ptr, SHM_SIZE);
        shm_unlink(SHM_NAME);
        exit(1);
    }

    close(shm_fd);
}

// Libération de la mémoire partagée
void release_shared_memory() {
    if (shm_ptr) {
        munmap(shm_ptr, SHM_SIZE);
        shm_unlink(SHM_NAME);
    }
    if (semaphore) {
        sem_close(semaphore);
        sem_unlink(SEM_NAME);
    }
}

// Écriture dans la mémoire partagée
void write_to_shared_memory(const string& message) {
    sem_wait(semaphore);

    size_t message_length = message.size() + 1;
    if (*shm_offset_ptr + message_length > SHM_SIZE) {
        cerr << "Mémoire partagée pleine. Vidage..." << endl;
        *shm_offset_ptr = 0;
    }

    char* shm_data = shm_ptr + sizeof(size_t) + *shm_offset_ptr;
    memcpy(shm_data, message.c_str(), message_length);
    *shm_offset_ptr += message_length;

    sem_post(semaphore);
}

// Lecture depuis la mémoire partagée
void output_shared_memory() {
    sem_wait(semaphore);

    size_t offset = 0;
    char* shm_data = shm_ptr + sizeof(size_t);
    while (offset < *shm_offset_ptr) {
        cout << "[" << pseudo_destinataire << "] " << (shm_data + offset);
        offset += strlen(shm_data + offset) + 1;
    }

    memset(shm_ptr, 0, SHM_SIZE);
    sem_post(semaphore);
}


void affichage_manuel() {
    output_shared_memory();
}

bool containsChar(const string& str, char ch) {
    return find(str.begin(), str.end(), ch) != str.end();
}

void checkParams(int argc, char* argv[], bool& isBotMode) {
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

void handleSIGINT(int signal) {
    Reset_Ligne();
    if (signal == SIGINT) {
        if (pipesOuverts && isManuelMode) {
            affichage_manuel();
        } else {
            cout << "Fermeture du programme suite à SIGINT." << endl;
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
