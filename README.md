# Fantomine — Studio Wiga

Pédale VST3 (Windows / macOS) qui transforme la guitare en voix de thérémine.
Le plugin suit la note jouée, génère une voix fantôme qui glisse vers elle, et
la pédale d'expression MIDI courbe la hauteur en continu.

## Compilation

Pousser tous les fichiers à la racine du dépôt GitHub. Le workflow
`.github/workflows/build.yml` compile Windows et macOS ; les VST3 sont dans
l'onglet **Actions → Artifacts** (`Fantomine-VST3-Windows`, `Fantomine-VST3-macOS`).

- Windows : copier `Fantomine.vst3` dans `C:\Program Files\Common Files\VST3\`
- macOS : dézipper dans `~/Library/Audio/Plug-Ins/VST3/`, puis si besoin
  `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Fantomine.vst3`

## Fichiers du visuel

`background.png` (pédale éteinte, 1240 × 928), `title_on.png` (néon allumé),
`led_on.png`, `oct_handle.png`, polices `Inter-SemiBold.otf` / `Inter-Regular.otf`
(licence SIL OFL), tous à la racine du dépôt.

## Contrôles

| Contrôle | Rôle |
|---|---|
| GLIDE | vitesse du glissando entre les notes (2 ms → 700 ms) |
| RANGE | amplitude de la pédale, ±1 à ±24 demi-tons |
| TONE | timbre : sinus pur → anche nasale (formant « voix ») |
| VIBRATO | profondeur du vibrato humanisé (démarre en douceur sur chaque note) |
| SPEED | vitesse du vibrato, 3 à 9 Hz |
| BLEND | dosage guitare / voix (puissance constante) |
| OCTAVE | voix à -1, 0 ou +1 octave (ou en continu par pédale, façon Whammy) |
| Footswitch | clic court = bypass · appui long (0,6 s) = HOLD (fige note et volume ; clic court pour relâcher) |

Afficheur : clic sur le nom du preset = menu (12 presets usine, presets
utilisateur 1 à 9 à charger / enregistrer), flèches = preset précédent / suivant,
pastille UP / DOWN / BIPOLAR = sens de la pédale, barre EXP = pédale à la souris.

En bypass, toutes les lumières s'éteignent : néon du titre, traits des potards,
écran et LED. Le titre scintille brièvement au rallumage.

## Suivi de note

Une nouvelle note n'est acceptée que sur une attaque franche (médiator), un bend / slide /
hammer-on proche, ou une hauteur stable pendant que la corde sonne encore fort. Le ronflement
secteur (50/60 Hz) et la fin de note qui s'éteint ne peuvent plus remplacer la note jouée ;
la voix s'éteint quand la note retombe dans le bruit de fond.

## Guitares 7 cordes

Le suivi de note descend jusqu'à ~33 Hz (Do1) : une 7 cordes accordée en G
(Sol1, 49 Hz) passe sans problème, et on peut encore descendre d'environ 4 tons.

## Pédales d'expression MIDI (une par contrôle)

Cinq cibles pilotables, chacune avec son propre CC (MIDI Learn) :

| Cible | Effet de la pédale |
|---|---|
| PITCH | courbe la hauteur (Range / sens UP, DOWN, BIPOLAR) — CC 11 par défaut |
| OCTAVE | façon Whammy : le levier glisse en continu. Plage au choix : talon -1 oct → centre 0 → pointe +1 oct (défaut), talon 0 → pointe +1 oct, ou talon 0 → pointe -1 oct |
| VIBRATO, SPEED, BLEND | le potard suit la pédale |

Clic droit sur VIBRATO, SPEED, BLEND, le levier OCTAVE ou l'écran : « MIDI Learn »
en tête du menu, puis bouger la pédale. Une pédale = une fonction : apprendre un CC
le retire des autres cibles (évite qu'une pédale de whammy courbe aussi la hauteur).
Zone morte de 4 % au talon (et au centre en BIPOLAR) : la voix reste juste même si la
pédale n'envoie pas 0 pile au repos. Toucher le levier OCTAVE à la souris le remet sur ses 3 positions fixes.
Les paramètres `Octave Whammy` et `Whammy Range` sont aussi automatisables
depuis Reaper.

Clic droit sur la pédale : taille de la fenêtre (comme Ring Think : 67 % par défaut
= 661 × 495 px, aussi taille minimale ; 75 % ; 100 %),
pédales d'expression MIDI (CC 14 bits accepté pour PITCH), plage de la whammy,
footswitch MIDI (MIDI Learn), sens de la pédale de hauteur.

## Pédale d'expression dans Reaper

1. Fantomine sur la piste guitare (avant Genome).
2. Nouvelle piste « Pédales » : entrée MIDI = ton contrôleur (All channels), armée, monitoring activé.
3. Envoi de la piste Pédales vers la piste guitare : Audio = None, MIDI = All.
4. Le point MIDI de l'écran clignote quand la pédale bouge.

Alternative sans routage : **Param → Learn** dans la fenêtre FX de Reaper sur les
paramètres `Expression`, `Octave Whammy`, `Vibrato`, `Speed`, `Blend`, `Bypass`, `Hold`.

Manuel complet (anglais puis français) : `Fantomine_Manual_EN_FR.pdf`.

## Versions

- v1.2.0 : anti-gel v2 (JUCE 9.0.4 conservé, caches refaits par le timer, chien de garde, aucun appel à l'hôte depuis le thread audio)

- v1.2.1 : menu de taille de fenêtre : « 60 % (default) », 75 %, 100 % (plus de taille en pixels)
