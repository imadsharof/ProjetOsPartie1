#include<iostream>
#include<string>
#include<vector>
#include<algorithm>
#include<sys/stat.h>
#include<fcntl.h>
#include<cstring>
#include<unistd.h>
#include<sys/wait.h>

using namespace std;
// g++ -std=gnu++17 -Wall -Wextra -O2 -Wpedantic main.cpp -o chat


bool containsChar(const string& str, char ch){
    // Fonction qui sert à vérifier si le caractère passé en paramètre est dans le string passé en paramètre
    return find(str.begin(), str.end(), ch) != str.end();
}


void checkParams(int argc, char* argv[]){
    // Récupère les paramètres du programme et vérifie que les paramètres ne posent pas problème
    // Vérifie que les pseudos soient bien présents dans les paramètres
    if (argc < 3) {
        fprintf(stderr, "chat pseudo_utilisateur pseudo_destinataire [--bot] [--manuel]\n");
        exit(1);
    }
    
    string pseudo_utilisateur = argv[1];
    string pseudo_destinataire = argv[2];

    // Vérifie que les pseudos fassent bien moins de 30 caractères
    if (pseudo_utilisateur.size() > 30 || pseudo_destinataire.size() > 30) {
        fprintf(stderr, "Erreur : Les pseudonymes ne doivent pas dépasser 30 caractères.\n");
        exit(2);
    }
    
    // Vérifie que les pseudos ne contiennent pas de caractères interdits
    // Vérifie si les pseudonymes sont "." ou ".."
    if (pseudo_utilisateur == "." || pseudo_utilisateur == ".." || pseudo_destinataire == "." || pseudo_destinataire == ".."){
        fprintf(stderr, "Erreur : Les pseudonymes ne peuvent pas être '.' ou '..'.\n");
        exit(3);
    }
    // Vérifie si les pseudonymes contiennent les caractères interdits ou non
    vector<char> caracteres_interdits = {'/', '[', ']', '-'};
    for (char caractere : caracteres_interdits){
        if (containsChar(pseudo_utilisateur, caractere) || containsChar(pseudo_destinataire, caractere)){
            fprintf(stderr, "Erreur : Les pseudonymes contiennent des caractères interdits (/, -, [, ]).\n");
            exit(3);
        }
    }
}


void createPipe(const string& pipePath) {
    // Crée les pipes en fonction du chemin qu'on donne via un string
    if (access(pipePath.c_str(), F_OK) == -1) {  // Vérifie si le pipe n'existe pas déjà
        if (mkfifo(pipePath.c_str(), 0666) == -1){ // Vérifie qu'il n'y ait pas eu d'erreur lors de la création du pipe
            perror("Erreur lors de la création du pipe");
            exit(4);
        }
    }
}


int main(int argc, char* argv[]){
    checkParams(argc, argv);

    string pseudo_utilisateur = argv[1];
    string pseudo_destinataire = argv[2];

    bool isBotMode = false; 
    bool isManuelMode = false; 

    for (int i = 3; i < argc; ++i){
        if (string(argv[i]) == "--bot") isBotMode = true;
        if (string(argv[i]) == "--manuel") isManuelMode = true;
    }

    string sendPipe = "/tmp/" + pseudo_utilisateur + "-" + pseudo_destinataire + ".chat";
    string receivePipe = "/tmp/" + pseudo_destinataire + "-" + pseudo_utilisateur + ".chat";

    createPipe(sendPipe);
    createPipe(receivePipe);

    pid_t pid = fork();

    if (pid < 0) {
        perror("Erreur lors de la création du processus");
        exit(1);
    }
    else if (pid == 0) {
        // Processus enfant : lit les messages reçus
        int fd_receive = open(receivePipe.c_str(), O_RDONLY);
        if (fd_receive < 0) {
            perror("Erreur lors de l'ouverture du pipe de réception");
            exit(1);
        }

        char buffer[256];
        while (read(fd_receive, buffer, sizeof(buffer)) > 0) {
            if (isBotMode) {
                printf("%s", buffer);
            } else {
                printf("\x1B[4m%s\x1B[0m\n%s", pseudo_destinataire.c_str(), buffer);
            }
            fflush(stdout);  // Forcer l'affichage immédiat
        }

        close(fd_receive);
        exit(0);  // Fin du processus enfant
    }
    else {
        // Processus parent : envoie les messages
        int fd_send = open(sendPipe.c_str(), O_WRONLY); // Ouvre le pipe d'envoi en mode write_only
        if (fd_send < 0) {
            perror("Erreur lors de l'ouverture du pipe d'envoi");
            exit(1);
        }

        char buffer[256];
        while (fgets(buffer, sizeof(buffer), stdin) != NULL) {
            write(fd_send, buffer, strlen(buffer) + 1);
            if (!isBotMode){ // N'écris les messages que si le bot mode n'est pas activé
                printf("\x1B[4m%s\x1B[0m\n%s", pseudo_utilisateur.c_str(), buffer);
            }
            fflush(stdout);
        }

        close(fd_send);
        wait(nullptr);  // Attendre la fin du processus enfant
    }

    unlink(sendPipe.c_str());
    unlink(receivePipe.c_str());

    return 0;
}