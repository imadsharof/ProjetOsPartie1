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
#include <sys/shm.h>
#include <semaphore.h>

using namespace std;

// g++ -std=gnu++17 -Wall -Wextra -O2 -Wpedantic main.cpp -o chat

bool affichageManuel = false;
bool pipesOuverts = false;

// Fonction pour vérifier si une chaîne de caractères contient un caractère spécifique
bool containsChar(const string& str, char ch) {
    return find(str.begin(), str.end(), ch) != str.end();
}

// Vérifie les paramètres fournis par l'utilisateur
void checkParams(int argc, char* argv[], string& pseudo_utilisateur, string& pseudo_destinataire, bool& isBotMode, bool& isManuelMode, bool& isJoliMode, bool& isTheBot) {
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
        if (string(argv[i]) == "--isTheBot") isTheBot = true;
    }
}

// Crée un pipe nommé si celui-ci n'existe pas
void createPipe(const string& pipePath) {
    if (access(pipePath.c_str(), F_OK) == -1) {
        if (mkfifo(pipePath.c_str(), 0666) == -1) {
            perror("Erreur lors de la création du pipe");
            exit(4);
        }
    }
}

// Efface tout ce qui est sur la ligne ou est le curseur et remet le curseur au début de la ligne
void Reset_Ligne(){
    cout << "\033[2K\r";
}

// Gestion des signaux SIGINT et SIGPIPE
void handleSIGINT(int signal) {
    if (signal == SIGINT){; // Vérifier que le signal soit bien SIGINT
        if (pipesOuverts) {
            affichageManuel = true;
        } else {
            cout << "\033[2K\rFermeture du programme suite à SIGINT." << endl;
            exit(4);
        }
    }
}

void handleSIGPIPE(int signal) {
    if (signal == SIGPIPE){ // Vérifier que le signal soit bien SIGPIPE
        cout << "Connexion terminée." << endl;
        exit(5);
    }
}

int main(int argc, char* argv[]) {
    string pseudo_utilisateur, pseudo_destinataire;
    bool isBotMode = false;
    bool isManuelMode = false;
    bool isJoliMode = false;
    bool isTheBot = false;

    checkParams(argc, argv, pseudo_utilisateur, pseudo_destinataire, isBotMode, isManuelMode, isJoliMode, isTheBot);

    string sendPipe = "/tmp/" + pseudo_utilisateur + "-" + pseudo_destinataire + ".chat";
    string receivePipe = "/tmp/" + pseudo_destinataire + "-" + pseudo_utilisateur + ".chat";

    createPipe(sendPipe);
    createPipe(receivePipe);


    int shm_fd = -1;
    char* shared_memory = nullptr;
    sem_t* semaphore = nullptr;

    if (isManuelMode) {
        shm_fd = shm_open("/chat_shm", O_CREAT | O_RDWR, 0666);
        if (ftruncate(shm_fd, 4096) == -1) {
            perror("Erreur lors de la configuration de la mémoire partagée");
            exit(1);
        }
        shared_memory = (char*)mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        semaphore = sem_open("/chat_sem", O_CREAT, 0666, 0);
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
        if(isManuelMode){
        pipesOuverts = true;
        }
        char buffer[256];
        while (true) {
            ssize_t bytesRead = read(fd_receive, buffer, sizeof(buffer) - 1);
            if (bytesRead > 0) {
                buffer[bytesRead] = '\0';
                if (isManuelMode) {
                    cout << '\a';
                    strncpy(shared_memory, buffer, 4096);
                    sem_post(semaphore);
                } else {
                    if(isJoliMode){
                        Reset_Ligne();
                    }
                    printf(isBotMode ? "[%s] %s" : "[\x1B[4m%s\x1B[0m] %s", pseudo_destinataire.c_str(), buffer);
                    if(isJoliMode){
                        printf("[%s, entrez votre message (tapez 'exit' pour quitter) : ", pseudo_utilisateur.c_str());
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
        signal(SIGINT, handleSIGINT);
        signal(SIGPIPE, handleSIGPIPE);

        int fd_send = open(sendPipe.c_str(), O_WRONLY);
        if (fd_send < 0) {
            perror("Erreur lors de l'ouverture du pipe d'envoi");
            exit(1);
        }
        if(isManuelMode){
            pipesOuverts = true;
        }
        char buffer[256];
        while (true) {
            if(isJoliMode){
                printf("%s, entrez votre message (tapez 'exit' pour quitter) : ", pseudo_utilisateur.c_str());
            }
            if (!fgets(buffer, sizeof(buffer), stdin)) {
                perror("Erreur lors de la lecture de l'entrée");
                break;
            }
            if (string(buffer) == "exit\n") exit(0);

            if (write(fd_send, buffer, strlen(buffer) + 1) == -1) {
                perror("Erreur lors de l'écriture dans le pipe");
            }
            if (!isBotMode) {
                if(isJoliMode){
                    cout << "\033[F"; // Revient une ligne plus haut
                    Reset_Ligne();
                }
                printf("[\x1B[4m%s\x1B[0m] %s", pseudo_utilisateur.c_str(), buffer);
            }

            if (isManuelMode) {
                if (affichageManuel || (strlen(shared_memory) >= 4096)) {
                    affichageManuel = false;
                    sem_wait(semaphore);
                    printf("[%s] %s\n", pseudo_destinataire.c_str(), shared_memory);
                    memset(shared_memory, 0, 4096);
                }
            }
            fflush(stdout);
        }

        close(fd_send);
        wait(nullptr);
    }

    unlink(sendPipe.c_str());
    unlink(receivePipe.c_str());

    if (isManuelMode) {
        munmap(shared_memory, 4096);
        close(shm_fd);
        shm_unlink("/chat_shm");
        sem_close(semaphore);
        sem_unlink("/chat_sem");
    }

    cout << "Session terminée. Au revoir, " << pseudo_utilisateur << "!" << endl;
    return 0;
}
