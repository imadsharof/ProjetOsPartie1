pseudo_destinataire=$1
pseudo_utilisateur=${2:bot}
liste_bot="liste-bot.txt"

coproc CHAT_PIPES { ./chat "$pseudo_utilisateur" "$pseudo_destinataire" --bot; } 

function reponse_liste_bot(){
    local response=$(grep -m 1 "^${command} " "$liste_bot" | cut -d' ' -f2-)
            if [ -n "$response" ]; then
                echo "$response" >&"${CHAT_PROCESS[1]}"
            else
                echo "Commande non reconnue." >&"${CHAT_PROCESS[1]}"
            fi
            ;;
}

function reponse(){
    local input="$1"
    case "$input" in
        "liste")
            ls >&"${CHAT_PROCESS[1]}"
            ;;
        "qui suis-je")
            echo "Vous discutez avec : $pseudo_destinataire"
            ;;
        "au revoir")
            echo "Au revoir, $pseudo_utilisateur !"
            exit 0
            ;;
        li\ *)
            local file=$(echo "$command" | cut -d' ' -f2-)
            if [ -f "$file" ]; then
                cat "$file" >&"${CHAT_PROCESS[1]}"
            else
                echo "Erreur : fichier '$file' introuvable." >&"${CHAT_PROCESS[1]}"
            fi
            ;;
        *)
            reponse_liste_bot "$input"
            ;;
    esac
}

while read -r message <&"${CHAT_PROCESS[0]}"; do
    reponse "$message"
done