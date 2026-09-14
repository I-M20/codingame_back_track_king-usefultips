# codingame_back_track_king

CodinGame Summer Challenge 2026

## Chosen algorithms

### BEAM search

#### "Rail placement choice"s création

1. Select all desired connection not yet built
2. For each of those
    - Find and create two rails group that are connected to the towns
    - Iterate over cross-product of those two groups to identify all potentials connection, and keep the shortest one (using manhattan distance)
3. We end up with all shortest path to build desired connections. A paht being 2 tiles/Coordonates

#### "Disrupt choice" création

All régions which respect all conditions :
- Not inked
- No town
- Opponent rail
- For each unique region rail within a valid connection :
    - Count all unique player rails inside the connection
- Sum rails count for both players
- Keep regions where opponent has a bigger count of rails than mine

Create only the best one

#### "Rail placement choice" application

Rule to place one player rails :

We want to apply a path/"rail placement choice". We get 2 coordonate, the first being the source and second the destination
The idea is to check around the source (4 adjacents cells) the closest cell (using A*) to the destination.
If draw, prioritize in order with :
- NORTH
- EAST
- SOUTH
- WEST
Once it's done, check the cost of the rail placement :
- 1 point de peinture pour placer un rail sur les plaines.
- 2 points de peinture pour placer un rail sur une rivière.
- 3 points de peinture pour placer un rail sur les montagnes.

and repeat until 3 points are spent.

Then add rails onto map. Be careful, both player must add they rails at the same time

#### Game engine turn choices application

- Start with state D
- Pick one of my rails creation
- Pick one of opp rails creation
- Apply both rails creation
- Apply chosen disrupts
- Ink regions
- Compute new player points
- End with state D+1

#### BEAM iterations

1. Start with parameter current_state
2. Copy current_state in turn_state
3. Generate rail choices with section "Rail placement choices création"
4. Generate my best disrupt choices with section "Disrupt choices"
4. Generate opp best disrupt choices with section "Disrupt choices"
7. Iterate over rails choices
   - Copy turn_state in current_state
   - Create state D+1 with section "Game engine turn choices application"
   - Use heuristic function to evaluate state D+1
   - Keep the state D+1 if heuristic score if best than current Bwidth lowest one
8. End with Bwidth new states

Bwidth = 20
maxDepth=10

### Heuristic

Dans un beam search, l'heuristic permet de comparer des état ayant le même état parent. Ces état viennent d'avoir leur score update siute au tour.
Donc un état qui créer de meilleur connection pour moi ou casse des connectino pour l'adversaire va impacter en conséquence les points.
Ce qui veut dire qu'on a pas besoin dans l'heuristic de récompenser/malus ses rails et les rails de l'adveraire sur les chemins les plus court existants.

Il faut juste diriger l'algo vers la création de ces chemins.
Pour ça on veut juste savoir les gap de distance entre les groupes de rails lié à 2 towns qui veulent être lié.

Si l'heuristic reste très basique alors on peut faire plus de simulation, et donc s'orienter vers des cas où les points augmente.

### Idées

#### GA pour construire un graph pondéré

Trouver la longeur des chemins de rails les plus court entre chaque ville (matrice de taille NbVille * NbVille)
Créer un graph pondéré avec comme configuration par défault les liaisons de ville demandé.
Faie un GA qui va couper et créer des liaisons pondéré pour minimiser la distance totale de TOUTE les liaisons du graph.
Les graph qui ne posède pas les liaisons de ville demandé doivent être extremement déavantagé.

FONTIONNE PAS :
- Les chemins doivent pouvoir être lié n'importe où, pas que sur des villes
- Construire un graph blobale ne rapporte pas beaucoup de points par rapport à faire pleins de liaisons rapidemment. Trop lent

## Game engine

### Lookup tables

On peut faire une LT qui garde une struct d'info "path" entre 2 cells :
    - Distance A*
    - List de région par lesquelles ont passe

Une structure associé pourrait permettre de retrouver tous les path qui passe par une région. Créé en même temps
De cette manière, lorsque la région est inked, on peut recalculer tous les paths qui l'utilisait


### Cache A* results instead of lookup tables

Each time we want a A* distance, verify if a cache entry exist :
- If so, verify the inkedRegion count is the same as the cached value :
    - If so, return it
    - Else, compute A* distance, save in cache with inkedRegion count
- If not, compute A* distance, save in cache with inkedRegion count

## Next steps

- Créer une heuristic
- partial_sort to BEAM_WIDTH instead of a full sort
- Cache/incrementalize openGapTotal — the single highest-value change. It re-does a full multi-component flood-fill per node when consecutive nodes differ by only ~3 rails.
<!-- - openGapTotal prends 1/2 du temps total.. Supprimer entierement et refaire le cache a* avec invalidation quand région supprimé. -->
<!-- - Lister les endroits ou on fait des floodfill/a* et mettre en cache tout ça -->
- Ne pas créer des moves uniquement sur les groupe de rails des villes pas encore lié :
    - Creer des moves sur les fin de chemin vers le rail le plus proche qui n'est pas du même groupe 

- Il faut qu'un choix de placement soit un ensemble de 3 rails et pas juste une src/dst
    - Des fois on veut 1 ou 2 rails sur la tache principale, et commencer immédiatement une autre tache
    -> Une depth de beam devrait être 1 rail

## Debug viewer

`tools/` contient une interface qui affiche la map et les valeurs internes de
l'algo : pour chaque case candidate du tour courant, le `resultingGap` que le
beam interne lui a donné. `main.cpp` reste compilable et jouable seul — c'est
`make check` qui le garantit, et le binaire de compétition est inchangé au
bit près (les hooks sont des macros vides hors `DEBUG_TOOL`).

```sh
make replay LOG=.colosseum/logs/firstenv/run-*/game_*_p0.events.jsonl
make viewer     # puis http://localhost:8000
```

### Commandes du Makefile

| commande | effet |
|---|---|
| `make` / `make play` | compile `main.cpp` seul → `a.out` (le binaire de compétition) |
| `make check` | vérifie que `main.cpp` compile sans l'outil — le garde-fou de la contrainte |
| `make debug` | construit `tools/btk-debug`, uniquement si `main.cpp` ou `debug_tool.cpp` ont changé |
| `make replay LOG=<jsonl>` | construit l'outil si besoin, vide les anciens dumps, puis rejoue la partie |
| `make viewer` | sert l'interface sur `http://localhost:8000` |
| `make clean` | supprime `a.out` et `tools/btk-debug` |

Variables : `LOG` (obligatoire pour `replay`), `TURNS` (défaut 100, plafonné à
la longueur de la partie), `DUMPS` (défaut `tools/dumps`), `PORT` (défaut 8000).

```sh
make replay LOG=<jsonl> TURNS=30     # s'arrêter au tour 30
make viewer PORT=8080                # si le port est déjà pris
```

`replay` efface `tools/dumps/turn_*.json` avant de rejouer : sans ça, une
partie plus courte laisserait derrière elle la fin de la précédente, et le
viewer listerait ces tours comme s'ils appartenaient à la nouvelle.

Sous WSL, un navigateur Windows n'atteint pas le `localhost` de la distro :
`serve.py` affiche au démarrage la seconde adresse (`http://172.x.x.x:8000/`)
qui, elle, fonctionne depuis Chrome. Le terminal intégré de VSCode redirige le
port tout seul, d'où `localhost` qui y marche.

### Images de tuiles (optionnel)

Déposer des PNG carrés dans `tools/tiles/` remplace les cases de couleur :

| fichier | remplace |
|---|---|
| `plains.png` `river.png` `mountain.png` | le terrain |
| `town.png` | les villes (l'id reste écrit par-dessus) |
| `inked.png` | les régions encrées |
| `rail_me.png` `rail_foe.png` `rail_neutral.png` | les pastilles de rail |

Tout est facultatif et indépendant : un fichier manquant retombe sur sa
couleur, donc un jeu partiel fonctionne. Les images n'ont pas besoin d'être
à la même taille — chacune est redimensionnée à la case (30 px). La couleur
du terrain est peinte dessous, donc une image à fond transparent se pose sur
sa teinte plutôt que sur du vide. Dès qu'une image est présente, la heatmap
passe en translucide (0.65) pour la laisser voir, et les chiffres prennent un
contour noir pour rester lisibles sur n'importe quel fond.

`tools/tiles/` est dans `.gitignore` — retirer la ligne pour versionner tes
images.

### Fichiers

- `tools/debug_tool.cpp` — `#include "../main.cpp"`, donc c'est le vrai moteur
  qui est observé, jamais une réimplémentation.
- `tools/serve.py` — sert le viewer et les dumps (stdlib seule).
- `tools/viewer.html` — Canvas : terrain, régions, rails, villes, encre, et la
  heatmap des candidats. La case jouée est entourée ; le panneau latéral
  compare le gain maximum au rail effectivement posé.

Mode live : `./tools/btk-debug live tools/dumps` se comporte comme le bot
(stdin/stdout) et dépose un dump par tour à côté. Utilisable directement comme
bot dans `colosseum.toml`, avec le bouton *Live* du viewer pour suivre.

L'outil garde le budget de `main.cpp` (30 ms, 900 au premier tour) : les
valeurs affichées sont celles que le bot jouera vraiment sur CodinGame.

Un replay reproduit le match de près, mais pas à l'identique, et la raison
tient au bot lui-même : **la recherche s'arrête sur l'horloge, pas sur un
nombre d'itérations, donc elle n'est pas déterministe**. Rejouer cinq fois le
même tour avec le même binaire sur la même entrée donne des sorties
différentes — le beam tronque à un point qui suit les micro-variations de
charge de la machine.

Mesuré sur un log v2.2 de 25 tours : 17 tours rejouent la sortie exacte, 3
posent les mêmes cases dans un autre ordre (sans conséquence pour l'arbitre),
5 diffèrent. Sur le premier rail — celui que la heatmap explique — 22 tours
sur 25 coïncident : la divergence porte presque toujours sur le 2ᵉ ou 3ᵉ rail.

Conséquence pratique : un dump décrit fidèlement *une* exécution de la
recherche à 30 ms, ce qui est bien ce qu'on veut inspecter, mais deux dumps du
même tour peuvent différer. Les valeurs de la heatmap, elles, sont stables :
`extendManhattanGap` ne dépend pas du temps.

## Explanations

## Versions

### v2.2

Infinite 'WAIT' turns bug resolved by finding the best non empty action when first beam depth is broken

Last moment in arena: -
First moment in arena: 676/1320 overall & Bronze league

### v2.1

Reduce time budget from 45ms to 30ms : No remaining timeouts
But many games are lost because of infinite WAIT action trhown each turn...

Last moment in arena: 708/1316 overall & Bronze league
First moment in arena: 100/400 Bronze

### v2.0

Nested beam searches: an outer one plans turns ahead, and for each of its
nodes an inner one decides that turn's rails one cell at a time. Both are
interruptible, playing the best line found when the turn budget runs out.

- Rail placement: every affordable cell touching the network, scored by how
    much it shortens the remaining wishes.
- Turn scoring (extendGap): per wish, the terrain distance from a newly
    laid cell to whichever of its two towns is farther.
- State scoring (evaluate): income difference per turn, minus GAP_PENALTY
    per cell of true remaining gap, from one multi-source flood fill
    (openGapTotal) that reads off where two components' floods meet.
- Disrupt choice: the region where the opponent owns the most connection
    rails more than we do. Four disrupts ink a region and erase its rails.

Last moment in arena: 230/450 Bronze
First moment in arena: 191/558 Bronze

### v1.0

- Find shortest distance amongs desired connections to build
- Skip connection if active
- Pick which region to disrupt: one with enemy rails, not yet inked,
    not containing one of our/their towns (can't disrupt those),
    preferring the one closest to being inked / with the most rails

Last moment in arena: 580/115 Bronze
First moment in arena: 191/558 Bronze

### v0.2

Last moment in arena: 320/558 Bronze
First moment in arena: -
