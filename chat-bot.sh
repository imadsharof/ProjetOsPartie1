#!/usr/bin/env bash

pseudo_destinataire=$1
pseudo_utilisateur=${2:-bot}
liste_bot="liste-bot.txt"

coproc CHAT_PIPES { ./chat "$pseudo_utilisateur" "$pseudo_destinataire" --bot; } 

function reponse_liste_bot(){
    local command="$1" 
    local response=$(grep -m 1 "^${command} " $liste_bot | cut -d' ' -f2-)

    if [ -n "$response" ]; then
        echo "$response" >&"${CHAT_PIPES[1]}"
    else
        echo "Commande non reconnue." >&"${CHAT_PIPES[1]}"
    fi
}

function reponse(){
    local input=$(echo "$1" | cut -d' ' -f2-)
    case $input in
        "liste")
            echo "$(ls)" >&"${CHAT_PIPES[1]}"
            ;;
        "qui suis-je")
            echo "Vous discutez avec : $pseudo_destinataire" >&"${CHAT_PIPES[1]}"
            ;;
        "au revoir")
            echo "Au revoir, $pseudo_destinataire !" >&"${CHAT_PIPES[1]}"
            exit 0
            ;;
        li\ *)
            local file=$(echo "$command" | cut -d' ' -f2-)
            if [ -f "$file" ]; then
                cat "$file\n" >&"${CHAT_PIPES[1]}"
            else
                echo "Erreur : fichier '$file' introuvable." >&"${CHAT_PIPES[1]}"
            fi
            ;;
        *)
            reponse_liste_bot "$input"
            ;;
    esac
}

while read -r message <&"${CHAT_PIPES[0]}"; do
    reponse "$message"
done