// SharedMemory.cpp
#include "SharedMemory.hpp"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Déclaration des variables globales utilisées
extern bool isBotMode;
extern std::string pseudo_destinataire;
extern std::string texte_a_print(std::string pseudo);

/**
 * @brief Constructeur de la classe SharedMemory
 * @param shm_name Nom de la mémoire partagée
 */
SharedMemory::SharedMemory(const std::string& shm_name)
    : SHM_NAME(shm_name) {
    // Initialisation du pointeur à nullptr
    shm_ptr = nullptr;
    shm_offset_ptr = nullptr;
}

/**
 * @brief Destructeur de la classe SharedMemory
 */
SharedMemory::~SharedMemory() {
    // Rien à faire ici, la libération se fait explicitement
}

/**
 * @brief Initialise la mémoire partagée
 * @param create Indique si la mémoire partagée doit être créée
 */
void SharedMemory::initialize_shared_memory(bool create) {
    int shm_fd;
    if (create) {
        shm_fd = shm_open(SHM_NAME.c_str(), O_CREAT | O_RDWR, 0666);
    } else {
        shm_fd = shm_open(SHM_NAME.c_str(), O_RDWR, 0666);
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
            shm_unlink(SHM_NAME.c_str());
            exit(1);
        }
    }

    // Mapping de la mémoire partagée
    shm_ptr = static_cast<char*>(mmap(nullptr, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
    if (shm_ptr == MAP_FAILED) {
        perror("Erreur lors du mapping de la mémoire partagée");
        close(shm_fd);
        if (create) {
            shm_unlink(SHM_NAME.c_str());
        }
        exit(1);
    }

    shm_offset_ptr = reinterpret_cast<size_t*>(shm_ptr);
    if (create) {
        *shm_offset_ptr = 0; // Initialisation de l'offset
    }

    if (close(shm_fd) == -1) {
        perror("Erreur lors de la fermeture du descripteur de la mémoire partagée");
    }
}

/**
 * @brief Libère la mémoire partagée
 * @param isParent Indique si l'appelant est le processus parent
 */
void SharedMemory::release_shared_memory(bool isParent) {
    if (shm_ptr) {
        if (munmap(shm_ptr, SHM_SIZE) == -1) {
            perror("Erreur lors du détachement de la mémoire partagée");
        }
        if (isParent) {
            if (shm_unlink(SHM_NAME.c_str()) == -1) {
                perror("Erreur lors de la suppression de la mémoire partagée");
            }
        }
    }
}

/**
 * @brief Écrit un message dans la mémoire partagée
 * @param message Le message à écrire
 */
void SharedMemory::write_to_shared_memory(const std::string& message) {
    size_t message_length = message.size() + 1; // Taille du message avec le caractère nul
    if (*shm_offset_ptr + message_length > SHM_SIZE - sizeof(size_t)) {
        // Mémoire pleine, réinitialiser
        *shm_offset_ptr = 0;
    }

    char* shm_data = shm_ptr + sizeof(size_t) + *shm_offset_ptr;
    memcpy(shm_data, message.c_str(), message_length); // Copie du message dans la mémoire partagée
    *shm_offset_ptr += message_length; // Mise à jour de l'offset
}

/**
 * @brief Affiche les messages en attente depuis la mémoire partagée
 */
void SharedMemory::output_shared_memory() {
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
