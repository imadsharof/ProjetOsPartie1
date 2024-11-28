// SharedMemory.hpp
#ifndef SHAREDMEMORY_HPP
#define SHAREDMEMORY_HPP

#include <string>

class SharedMemory {
public:
    // Constante
    static constexpr size_t SHM_SIZE = 4096; // Taille de la mémoire partagée

    // Variables membres
    char* shm_ptr = nullptr;         // Pointeur vers la mémoire partagée
    size_t* shm_offset_ptr = nullptr;// Pointeur vers l'offset dans la mémoire partagée
    std::string SHM_NAME;            // Nom de la mémoire partagée

    // Constructeur et destructeur
    SharedMemory(const std::string& shm_name);
    ~SharedMemory();

    // Fonctions
    void initialize_shared_memory(bool create);
    void release_shared_memory(bool isParent);
    void output_shared_memory();
    void write_to_shared_memory(const std::string& message);
};

#endif // SHAREDMEMORY_HPP
