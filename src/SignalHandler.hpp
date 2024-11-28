// SignalHandler.hpp
#ifndef SIGNALHANDLER_HPP
#define SIGNALHANDLER_HPP

#include <csignal>
#include "SharedMemory.hpp"
#include "Pipes.hpp"

class SignalHandler {
public:
    // Gestionnaires de signaux
    static void handleSIGINT(int signal);
    static void handleSIGPIPE(int signal);
    static void handleSIGUSR1(int signal);
    static void handleSIGTERM(int signal);
    static void handleSIGUSR2(int signal);

    // Méthode pour initialiser les pointeurs
    static void init(SharedMemory* sharedMemoryPtr, Pipes* pipesPtr);

private:
    static SharedMemory* sharedMemory;
    static Pipes* pipes;
};

#endif // SIGNALHANDLER_HPP
