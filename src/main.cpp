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

bool pipesOuverts = false;
bool isManuelMode = false;
bool isJoliMode = false;

char* shared_memory = nullptr;
sem_t* semaphore = nullptr;
int shm_fd = -1;

string pseudo_utilisateur;
string pseudo_destinataire;

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
        shm_fd = shm_open("/chat_shm", O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) {
            perror("Erreur lors de la configuration de la mémoire partagée");
            exit(1);
        }
        if (ftruncate(shm_fd, 4096) == -1) {
            perror("Erreur lors de la configuration de la taille de la mémoire partagée");
            exit(1);
        }
        shared_memory = (char*)mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shared_memory == MAP_FAILED) {
            perror("Erreur lors du mapping de la mémoire partagée");
            exit(1);
        }
        memset(shared_memory, 0, 4096);

        semaphore = sem_open("/chat_sem", O_CREAT, 0666, 0);
        if (semaphore == SEM_FAILED) {
            perror("Erreur lors de l'ouverture du sémaphore");
            exit(1);
        }
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
                    cout << '\a';
                    if (strlen(shared_memory) + bytesRead < 4096) {
                        strcat(shared_memory, buffer); 
                        sem_post(semaphore); 
                    } else {
                        cerr << "Mémoire partagée saturée, message ignoré." << endl;
                    }
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

            if (write(fd_send, buffer, strlen(buffer) + 1) == -1) {
                perror("Erreur lors de l'écriture dans le pipe");
            }
            printf("[%s] %s", pseudo_utilisateur.c_str(), buffer);
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
        munmap(shared_memory, 4096);
        close(shm_fd);
        shm_unlink("/chat_shm");
        sem_close(semaphore);
        sem_unlink("/chat_sem");
    }

    cout << "Session terminée. Au revoir, " << pseudo_utilisateur << "!" << endl;
    return 0;
}

void affichage_manuel() {
    while (sem_trywait(semaphore) == 0) {
        if (strlen(shared_memory) > 0) {
            printf("[%s] %s", pseudo_destinataire.c_str(), shared_memory);
            memset(shared_memory, 0, 4096); 
        }
    }
    fflush(stdout);
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
    if (access(pipePath.c_str(), F_OK) == -1) {
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
    if (signal == SIGPIPE) {
        cout << "Connexion terminée par l'autre utilisateur." << endl;
        exit(5);
    }
}
