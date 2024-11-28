// SignalHandler.cpp
#include "SignalHandler.hpp"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <iostream>

// Définition des variables statiques
SharedMemory* SignalHandler::sharedMemory = nullptr;
Pipes* SignalHandler::pipes = nullptr;

// Variables globales externes
extern bool pipesOuverts;
extern bool isManuelMode;
extern bool isBotMode;
extern int fd_send;
extern pid_t pid;
extern volatile sig_atomic_t should_exit;
extern std::string pseudo_destinataire;
extern std::string texte_a_print(std::string pseudo);

/**
 * @brief Initialise les pointeurs vers SharedMemory et Pipes
 * @param sharedMemoryPtr Pointeur vers l'instance de SharedMemory
 * @param pipesPtr Pointeur vers l'instance de Pipes
 */
void SignalHandler::init(SharedMemory* sharedMemoryPtr, Pipes* pipesPtr) {
    sharedMemory = sharedMemoryPtr;
    pipes = pipesPtr;
}

/**
 * @brief Gestionnaire pour SIGINT dans le processus parent
 * @param signal Le numéro du signal reçu
 */
void SignalHandler::handleSIGINT(int signal) {
    if (signal == SIGINT) {
        if (!pipesOuverts) {
            // Les pipes n'ont pas été ouverts, terminer avec le code de retour 4
            exit(4);
        } else if (isManuelMode && sharedMemory) {
            sharedMemory->output_shared_memory(); // Afficher les messages en attente
        } else {
            // Terminer le programme proprement avec un message
            fprintf(stderr, "\n\033[33mWARNING\033[0m Utilisateur déconnecté.\n");
            // Fermer les descripteurs de fichiers et envoyer un signal au processus enfant
            if (fd_send != -1) close(fd_send);
            kill(pid, SIGTERM); // Envoyer SIGTERM au processus enfant
            // Attendre la fin du processus enfant
            wait(nullptr);
            // Suppression des pipes nommés
            if (pipes) {
                pipes->unlink_pipes();
            }
            if (isManuelMode && sharedMemory) {
                sharedMemory->release_shared_memory(true); // Libération de la mémoire partagée
                delete sharedMemory;
                sharedMemory = nullptr;
            }
            exit(0);
        }
    }
}

/**
 * @brief Gestionnaire pour SIGPIPE dans le processus parent
 * @param signal Le numéro du signal reçu
 */
void SignalHandler::handleSIGPIPE(int signal) {
    if (signal == SIGPIPE && !isManuelMode) {
        std::cout << "Connexion terminée par l'autre utilisateur." << std::endl;
        exit(5);
    }
}

/**
 * @brief Gestionnaire pour SIGUSR1 dans le processus parent
 * @param signal Le numéro du signal reçu
 */
void SignalHandler::handleSIGUSR1(int) {
    if (isManuelMode && sharedMemory) {
        sharedMemory->output_shared_memory(); // Afficher les messages en attente
    }
}

/**
 * @brief Gestionnaire pour SIGTERM dans le processus enfant
 * @param signal Le numéro du signal reçu
 */
void SignalHandler::handleSIGTERM(int /*signal*/) {
    should_exit = 1; // Indique au processus enfant de se terminer
}

/**
 * @brief Gestionnaire pour SIGUSR2 dans le processus parent
 * @param signal Le numéro du signal reçu
 */
void SignalHandler::handleSIGUSR2(int signal) {
    if (signal == SIGUSR2) {
        // L'autre utilisateur s'est déconnecté
        if (!isManuelMode) {
            // En mode normal, terminer le programme proprement
            if (fd_send != -1) close(fd_send);
            if (pid > 0) {
                kill(pid, SIGTERM);
                wait(nullptr);
            }
            if (pipes) {
                pipes->unlink_pipes();
            }
            if (isManuelMode && sharedMemory) {
                sharedMemory->release_shared_memory(true);
                delete sharedMemory;
                sharedMemory = nullptr;
            }
            exit(0);
        } else {
            // En mode manuel, ne pas terminer le programme
        }
    }
}
