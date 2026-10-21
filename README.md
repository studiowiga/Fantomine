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
| OCTAVE | voix à -1, 0 ou +1 octave |
| Footswitch | clic court = bypass · appui long (0,6 s) = HOLD (fige note et volume ; clic court pour relâcher) |

Afficheur : clic sur le nom du preset = menu (12 presets usine, presets
utilisateur 1 à 9 à charger / enregistrer), flèches = preset précédent / suivant,
pastille UP / DOWN / BIPOLAR = sens de la pédale, barre EXP = pédale à la souris.

En bypass, toutes les lumières s'éteignent : néon du titre, traits des potards,
écran et LED. Le titre scintille brièvement au rallumage.

Clic droit sur la pédale : taille de la fenêtre (comme Ring Think : 67 % par défaut
= 661 × 495 px, aussi taille minimale ; 75 % ; 100 %),
CC de la pédale d'expression (MIDI Learn, CC 11 par défaut, CC 14 bits accepté),
footswitch MIDI (MIDI Learn), sens de la pédale.

## Pédale d'expression dans Reaper

Sur la piste guitare : **Input MIDI** = ton interface ou contrôleur (Record arm
inutile, activer le monitoring). Le plugin reçoit directement le CC de la pédale.
Alternative : paramètre `Expression` via **Param → Learn** dans Reaper.

Le footswitch peut aussi être piloté par les paramètres `Bypass` et `Hold`
(MIDI learn de Reaper), ou par un CC momentané via le menu clic droit.

Presets utilisateur enregistrés dans :
`%APPDATA%\Studio Wiga\Fantomine\UserPresets.xml` (Windows) ou
`~/Library/Application Support/Studio Wiga/Fantomine/UserPresets.xml` (macOS).
