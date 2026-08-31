"""Entries taken from published, play-tested party word lists.

Read rather than remembered. See README.md in this directory for the sources
and for why a published entry counts as evidence and a remembered one does not.

Two uses:

  * as an ANCHOR -- an entry of ours that also appears here has been played by
    strangers, so a frequency score that calls it rare is the score being wrong;
  * as a SOURCE for the easy tier, which the first version of our lists did not
    have at all. Every list below leads with words a child gets instantly, and
    that is the third of each category we were missing.

Kept as data rather than folded into the word files so the provenance survives:
next time somebody asks where a word came from, the answer is in the repository
rather than in a chat log.
"""

# how-to-play-charades.com, charades.io, gamingrooms.net -- graded easy/medium/
# hard where the source graded them. Only the tiers we were short of are
# reproduced in full; the hard tiers are deliberately thin, because ours already
# skewed that way.
EASY = {
    "animals": """CAT DOG COW PIG SHEEP HORSE RABBIT MOUSE BIRD FISH SNAKE BEAR
        LION TIGER ELEPHANT MONKEY DUCK FROG CHICKEN GOAT""".split(),
    "food": """PIZZA BREAD APPLE BANANA CAKE CHEESE EGG BURGER PASTA RICE SOUP
        SALAD COFFEE TEA WATER MILK ORANGE CARROT POTATO CHOCOLATE""".split(),
    "jobs": """DOCTOR TEACHER DRIVER COOK POLICE SINGER DANCER PILOT FARMER
        SOLDIER NURSE PAINTER PLUMBER WAITER BAKER""".split(),
    "sports": """FOOTBALL BASKETBALL TENNIS SWIMMING RUNNING CYCLING YOGA BOXING
        SKATING SKIING SURFING GOLF HOCKEY VOLLEYBALL KARATE""".split(),
    "act": """SLEEPING EATING DRINKING WALKING RUNNING JUMPING SITTING READING
        WRITING COOKING CLEANING DRIVING SINGING DANCING CRYING""".split(),
    "kids": """TRAIN UNICORN PIRATE SPACESHIP DINOSAUR MERMAID RAINBOW PRINCESS
        WIZARD DRAGON FIREFIGHTER FAIRY OWL CIRCUS STAR""".split(),
}

MEDIUM = {
    "animals": """KANGAROO PENGUIN OCTOPUS CROCODILE GIRAFFE CAMEL OWL EAGLE
        WOLF FOX BAT TURTLE SQUIRREL""".split(),
    "food": """SUSHI TACOS CROISSANT PANCAKES SMOOTHIE POPCORN SPAGHETTI
        WATERMELON PINEAPPLE COCONUT MUSHROOM AVOCADO HUMMUS""".split(),
    "jobs": """LAWYER ENGINEER ARCHITECT PHOTOGRAPHER JOURNALIST SCIENTIST
        MECHANIC CARPENTER FIREFIGHTER ASTRONAUT SURGEON DENTIST MAGICIAN""".split(),
    "sports": """WRESTLING CLIMBING DIVING FENCING ARCHERY BOWLING SAILING ROWING
        SNOWBOARDING SKATEBOARDING CURLING POLO CRICKET""".split(),
}

# Whole-phrase entries, which the split() lists above cannot carry.
PHRASES = {
    "act": [
        "BRUSHING TEETH",
        "TYING SHOES",
        "RIDING A BIKE",
        "PLAYING GUITAR",
        "TAKING A PHOTO",
        "FLIPPING PANCAKES",
        "ROWING A BOAT",
        "MILKING A COW",
        "BLOWING UP A BALLOON",
        "PLANTING A TREE",
        "FEEDING A BABY",
        "TAKING A SELFIE",
        "SNORING LOUDLY",
        "SINGING OPERA",
        "GOING FISHING",
        "EATING A WATERMELON",
        "MOVING LIKE A ROBOT",
        "GRILLING A BURGER",
    ],
    "movies": [
        "TITANIC",
        "JAWS",
        "STAR WARS",
        "FROZEN",
        "THE LION KING",
        "AVATAR",
        "FORREST GUMP",
        "INCEPTION",
        "THE MATRIX",
        "TOY STORY",
        "SHREK",
        "THE WIZARD OF OZ",
        "HARRY POTTER",
        "INDIANA JONES",
        "ROCKY",
        "BACK TO THE FUTURE",
        "THE GODFATHER",
        "PULP FICTION",
        "CASABLANCA",
    ],
    "people": [
        "ALBERT EINSTEIN",
        "MARILYN MONROE",
        "ELVIS PRESLEY",
        "MICHAEL JACKSON",
        "MADONNA",
        "BOB MARLEY",
        "STEVE JOBS",
        "BILL GATES",
        "MAHATMA GANDHI",
        "NELSON MANDELA",
        "MOTHER TERESA",
        "PABLO PICASSO",
        "MOZART",
        "BEETHOVEN",
        "SHAKESPEARE",
        "CHARLIE CHAPLIN",
        "SHERLOCK HOLMES",
        "JAMES BOND",
        "FRIDA KAHLO",
        "LEONARDO DA VINCI",
    ],
    "tricky": [
        # The published abstract tier is far more concrete than ours was:
        # things you can mime a shape or a feeling for, not civic nouns.
        "DREAM",
        "GALAXY",
        "TIME",
        "ECLIPSE",
        "JUSTICE",
        "HARMONY",
        "CHANCE",
        "GRAVITY",
        "SILENCE",
        "SHADOW",
        "ECHO",
        "BALANCE",
        "FREEDOM",
        "LUCK",
    ],
}


def proven():
    """Every published entry, upper-cased, as one set."""
    out = set()
    for table in (EASY, MEDIUM):
        for words in table.values():
            out.update(w.upper() for w in words)
    for phrases in PHRASES.values():
        out.update(p.upper() for p in phrases)
    return out
