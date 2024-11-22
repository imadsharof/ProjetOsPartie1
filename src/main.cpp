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

bool pipesOuverts = false;
bool isManuelMode = false;
bool isJoliMode = false;

char* shared_memory = nullptr;
sem_t* semaphore = nullptr;
int shm_fd = -1;

string pseudo_utilisateur;
string pseudo_destinataire;

// Se charge d'afficher les messages quand un ctrl+c est effectué lorsque le mode manuel est actif
void affichage_manuel(); 
// Vérifie si une chaîne de caractères contient un caractère spécifique
bool containsChar(const string& str, char ch); 
// Vérifie les paramètres fournis par l'utilisateur
void checkParams(int argc, char* argv[], bool& isBotMode);
// Crée un pipe si celui-ci n'existe pas déjà
void createPipe(const string& pipePath);
// Efface tout ce qui est sur la ligne ou est le curseur et remet le curseur au début de la ligne
void Reset_Ligne();
// Gestion du signal SIGINT
void handleSIGINT(int signal);
// Gestion du signal SIGPIPE
void handleSIGPIPE(int signal);

int main(int argc, char* argv[]) {
    bool isBotMode = false;

    checkParams(argc, argv, isBotMode);

    string sendPipe = "/tmp/" + pseudo_utilisateur + "-" + pseudo_destinataire + ".chat";
    string receivePipe = "/tmp/" + pseudo_destinataire + "-" + pseudo_utilisateur + ".chat";

    createPipe(sendPipe);
    createPipe(receivePipe);

    if (isManuelMode) {
        // Initialise la mémoire partagée si le mode manuel est actif
        shm_fd = shm_open("/chat_shm", O_CREAT | O_RDWR, 0666);
        if (ftruncate(shm_fd, 4096) == -1) { // Cherche les erreurs dans la création de mémoire partagée
            perror("Erreur lors de la configuration de la mémoire partagée");
            exit(1);
        }
        shared_memory = (char*)mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        semaphore = sem_open("/chat_sem", O_CREAT, 0666, 0);
    }

    pid_t pid = fork(); // Initialise les processus 
    if (pid < 0) {
        // Cherche les erreurs dans la création des processus
        perror("Erreur lors de la création du processus");
        exit(1);
    } 
    else if (pid == 0) {
        // Processus fils, reçoit les messages envoyés par l'autre instance du programme 
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
                    strncat(shared_memory, buffer, 4096 - strlen(shared_memory)); // Ajouter à la mémoire partagée.
                    // Signaler que des messages sont disponibles.
                    if(strlen(shared_memory) >= 4096){ // Si la mémoire partagée est au dessus de 4096 octets, affiche les messages
                        affichage_manuel();
                    }
                } 
                else {
                    string texte_a_print = "[\x1B[4m%s\x1B[0m] %s"; // Le texte ne sera pas coloré
                    if(isJoliMode){
                        texte_a_print = "\033[96m[\x1B[4m%s\x1B[24m]\033[0m %s"; // Le texte sera coloré
                        Reset_Ligne(); // La ligne sera effacée pour laisser sa place au message à afficher
                    }
                    printf(isBotMode ? "[%s] %s" : texte_a_print.c_str(), pseudo_destinataire.c_str(), buffer);
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
        // Processus père, envoie les messages reçus sur stdin à l'autre instance
        // Gère les signaux 
        signal(SIGINT, handleSIGINT);
        signal(SIGPIPE, handleSIGPIPE);

        int fd_send = open(sendPipe.c_str(), O_WRONLY);
        if (fd_send < 0) {
            // Vérifie qu'il n'y ait pas d'erreur lors de l'ouverture du pipe
            perror("Erreur lors de l'ouverture du pipe d'envoi");
            exit(1);
        }
        pipesOuverts = true;
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
                string texte_a_print = "[\x1B[4m%s\x1B[0m] %s";
                if(isJoliMode){
                    cout << "\033[F"; // Revient une ligne plus haut
                    Reset_Ligne(); // Efface la ligne écrite par l'utilisateur pour la remplacer par la ligne à print
                    texte_a_print = "\033[93m[\x1B[4m%s\x1B[24m]\033[0m %s";
                }
                printf(texte_a_print.c_str(), pseudo_utilisateur.c_str(), buffer);
                fflush(stdout); // S'assurer que le message est affiché immédiatement
			}
                if (isManuelMode){
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
    while (sem_trywait(semaphore) == 0) { // Tant qu'il y a des messages en attente
        if (strlen(shared_memory) > 0) { // Si la mémoire partagée contient des messages
            string texte_a_print = "[\x1B[4m%s\x1B[0m] %s";
            if (isJoliMode) {
                texte_a_print = "\033[93m[\x1B[4m%s\x1B[24m]\033[0m %s";
            }
            printf(texte_a_print.c_str(), pseudo_destinataire.c_str(), shared_memory);
            memset(shared_memory, 0, 4096); // Effacer le contenu après affichage
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
        if (mkfifo(pipePath.c_str(), 0666) == -1) { // Cherche une erreur éventuelle
            perror("Erreur lors de la création du pipe");
            exit(1);
        }
    }
}

void Reset_Ligne(){
    // Suite de commandes ANSI qui efface toute la ligne et remet le curseur au début de la ligne
    cout << "\033[2K\r"; 
}

void handleSIGINT(int signal) {
    if (signal == SIGINT) {
        if (pipesOuverts && isManuelMode) {
            affichage_manuel(); // Affiche tous les messages en attente
        } else {
            cout << "Fermeture du programme suite à SIGINT." << endl;
            exit(4); // Ferme proprement si les pipes ne sont pas ouverts
        }
    }
}
void handleSIGPIPE(int signal) {
    if (signal == SIGPIPE){ // Vérifier que le signal soit bien SIGPIPE
        cout << "Connexion terminée par l'autre utilisateur." << endl;
        exit(5);
    }
}
