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
bool isBotMode = false;

// Variables globales
char* shm_ptr = nullptr;
sem_t* semaphore = nullptr;
size_t* shm_offset_ptr = nullptr;

string pseudo_utilisateur;
string pseudo_destinataire;

//Prototypes
// Initialise la mémoire partagée
void initialize_shared_memory();

// Libère la mémoire partagée à la fin du code
void release_shared_memory();

// Print ce qu'il y a dans la mémoire partagée et la réinitialise
void output_shared_memory();

// Ecrit dans la mémoire partagée
void write_to_shared_memory(const string& message);

// Vérifie si un caractère spécirique est dans la chaine de caractères
bool containsChar(const string& str, char ch); 

// Check les paramètres
void checkParams(int argc, char* argv[]);

// Crée les pipes
void createPipe(const string& pipePath);

// Supprime la ligne et remet le curseur au début de la ligne
void Reset_Ligne();

// Aide à définir la manière dont le pseudo est écrit en fonction du mode joli et du pseudo
string texte_a_print(string pseudo);

// S'occupe du signal SIGINT
void handleSIGINT(int signal);

// S'occupe du signal SIGPIPE
void handleSIGPIPE(int signal);


int main(int argc, char* argv[]) {
    bool isBotMode = false;

    checkParams(argc, argv);

    string sendPipe = "/tmp/" + pseudo_utilisateur + "-" + pseudo_destinataire + ".chat";
    string receivePipe = "/tmp/" + pseudo_destinataire + "-" + pseudo_utilisateur + ".chat";

    createPipe(sendPipe);
    createPipe(receivePipe);

    if (isManuelMode) {
        initialize_shared_memory();
    }

    // Crée les processus
    pid_t pid = fork(); 
    if (pid < 0) {
        perror("Erreur lors de la création du processus");
        exit(1);
    } 
    else if (pid == 0) {
        signal(SIGINT, SIG_IGN);
        // Processus fils, reçoit les messages
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
                    write_to_shared_memory(string(buffer)); 
                } 
                else {
                    printf(isBotMode ? "[%s] %s" : texte_a_print(pseudo_destinataire).c_str(), pseudo_destinataire.c_str(), buffer);
                    if(isJoliMode){
                        printf("%s, entrez votre message (tapez 'exit' pour quitter) : ", pseudo_utilisateur.c_str());
                    }
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
        // Processus père, envoie les messages
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
                printf(texte_a_print(pseudo_utilisateur).c_str(), pseudo_utilisateur.c_str(), buffer);
            }
            if (isManuelMode) {
                output_shared_memory();
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
        output_shared_memory();
        printf(isBotMode ? "[%s] %s" : texte_a_print(pseudo_destinataire).c_str(), pseudo_destinataire.c_str(), message.c_str());
        if(isJoliMode){
            printf("%s, entrez votre message (tapez 'exit' pour quitter) : ", pseudo_utilisateur.c_str());
        }
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
        printf(isBotMode ? "[%s] %s" : texte_a_print(pseudo_destinataire).c_str(), pseudo_destinataire.c_str(), (shm_data + offset));
        offset += strlen(shm_data + offset) + 1;
    }
    if(isJoliMode){
        printf("%s, entrez votre message (tapez 'exit' pour quitter) : ", pseudo_utilisateur.c_str());
    }

    memset(shm_ptr, 0, SHM_SIZE);
    sem_post(semaphore);
}

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

string texte_a_print(string pseudo){
    string texte = "[\x1B[4m%s\x1B[0m] %s";
    if(isJoliMode){
        if (pseudo == pseudo_destinataire){
                texte = "\033[93m[\x1B[4m%s\x1B[24m]\033[0m %s";
                Reset_Ligne();
        }
        else if(pseudo == pseudo_utilisateur){
                texte = "\033[96m[\x1B[4m%s\x1B[24m]\033[0m %s";
                printf("\033[A");
                Reset_Ligne();
        }
    }
    return texte;
}

void handleSIGINT(int signal) {
    if(isJoliMode){
        Reset_Ligne();
    }
    if (signal == SIGINT) {
        if (pipesOuverts && isManuelMode) {
            output_shared_memory();
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
