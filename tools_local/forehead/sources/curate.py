#!/usr/bin/env python3
"""Apply the researched revision to the word lists.

Explicit CUT and ADD lists per category, so every change is auditable and the
reasoning survives. Run once; the .txt files are the source of truth afterwards.

WHY NOT AN AUTOMATIC FILTER. Word frequency was tried first and is the wrong
tool: scored against Google's 20k corpus, the entries it flags as too rare
include ALLIGATOR, PENGUIN, BROOM, CRAYON and TOOTHBRUSH. A web-frequency
corpus does not contain common concrete nouns in proportion to how well a
five-year-old knows them, and guessability at a party is concreteness and
cultural familiarity rather than how often a word appears in text. The filter
is kept in the README as a thing that was tried and rejected, because the next
person will otherwise try it too.

So: the published lists supply the EASY tier we never had, and the cuts below
are judgement calibrated against those lists rather than against my own taste
-- which is what produced SEPAK TAKRAW in a party game the first time.
"""
import pathlib
import re
import sys

# The generator is the authority on how wide a string is; importing its
# measurement rather than copying it means the two cannot drift.
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from gen_forehead_words import pixels  # noqa: E402

WORDS = pathlib.Path(__file__).resolve().parents[1] / "words"

# Spelling and punctuation, all of them real errors in the shipped lists.
FIX = {
    "GEPETTO": "GEPPETTO",
    "MONSTERS INC": "MONSTERS INC.",
    "DR STRANGELOVE": "DR. STRANGELOVE",
    "SINGIN IN THE RAIN": "SINGING IN THE RAIN",
    # British/American was mixed at random. The rest of the fork's user-facing
    # text is American, so these follow it.
    "CHANGING A TYRE": "CHANGING A TIRE",
    "SKIDDING TYRES": "SKIDDING TIRES",
    "SHOVELLING SNOW": "SHOVELING SNOW",
    "SNORKELLING": "SNORKELING",
    "YODELLING": "YODELING",
    "LIQUORICE": "LICORICE",
    "CHILLI": "CHILI",
    "LASAGNE": "LASAGNA",
    "HUMOUR": "HUMOR",
    "RUMOUR": "RUMOR",
    "FAVOURITISM": "FAVORITISM",
    "KNIGHT IN ARMOUR": "KNIGHT IN ARMOR",
    "SYNTHESISER": "SYNTHESIZER",
    "WINDSCREEN WIPERS": "WINDSHIELD WIPERS",
    "MOTORBIKE": "MOTORCYCLE",
    "BEETROOT": "BEETS",
    "CANDY FLOSS": "COTTON CANDY",
}

# Entries to REMOVE. Judgement, calibrated against how the published lists grade
# difficulty rather than against my own taste -- which is what produced SEPAK
# TAKRAW in a party game. Three kinds:
#   * too obscure to guess in a room (SEPAK TAKRAW, AUGEAN STABLES, OKAPI)
#   * not an instance of the category at all (OFFSIDE and STADIUM are not
#     sports; SNAKE-HAIRED WOMAN is a description, not a name)
#   * invented rather than known -- several STORYBOOK entries were phrases I
#     made up that sound like folk tales and are not any actual story
#
# Deliberately NOT a bigger list. A first pass cut 449 and took LIBRARIAN,
# LIFEGUARD and POET with it, which are exactly the words a party gets in two
# seconds. Over-cutting is the same error as over-including, pointed the other
# way.
CUT = {
    "animals": "OKAPI TAPIR MANDRILL DORMOUSE".split(),
    "sports": """SEPAK TAKRAW|RACQUETBALL|DRESSAGE|DECATHLON|PENTATHLON|BIATHLON|
        SHOWJUMPING|TOBOGGANING|OFFSIDE|DRIBBLING|WHISTLE|STADIUM|REFEREE|SUDOKU|
        JIGSAW|CROSS COUNTRY|HAMMER THROW|SHOT PUT|DISCUS""".split("|"),
    "myths": """AUGEAN STABLES|ELYSIAN FIELDS|ORACLE BONES|NINE REALMS|AMBROSIA|
        ARGONAUTS|DEMETER|DJINN|KAPPA|JACKALOPE|WILL-O-THE-WISP|
        DRYAD|WRAITH|HOBGOBLIN|CHANGELING|THE HUNDRED-EYED GIANT|
        SNAKE-HAIRED WOMAN|SALAMANDER OF FIRE|UNDERWORLD FERRYMAN|SHAPE SHIFTER|
        SPIRIT FOX|SKY SERPENT|MOON RABBIT|WORLD TURTLE|TWIN GODS|SWAN MAIDEN|
        WATER NYMPH|WHITE STAG|TREE SPIRIT|GREEN MAN|BROWNIE|CORNUCOPIA|ARGUS|
        MERMAID'S SONG|DRAGON'S HOARD|LAMP OF WISHES|
        HALL OF THE GODS|FOUNTAIN OF LIFE|IRON GOLEM|OGRE'S CLUB|PIXIE DUST|
        ROC'S EGG|MIRROR WORLD|LOST CITY|KING UNDER THE HILL|IMP OF MISCHIEF|
        SEANCE|MANDRAKE|THIRD EYE|EVIL EYE|POMEGRANATE SEEDS""".split("|"),
    "story": """ARIADNE'S THREAD|ANANSI|BABA YAGA'S HUT|BRIAR ROSE|DICK WHITTINGTON|
        THE SNOW QUEEN'S KISS|SLEEPING GUARDS|WHISPERING WOODS|SINGING BONES|
        LAMPLIGHTER|KING'S MESSENGER|WANDERING MINSTREL|VALIANT KNIGHT|
        STOLEN CROWN|MIDNIGHT CHIMES|HIDDEN STAIRCASE|ICE PALACE|CRYSTAL CAVE|
        GNOME KING|CITY OF GOLD|CAVE OF WONDERS|COACH AND HORSES|FAIRY RING|
        ENCHANTED ROSE|FORBIDDEN DOOR|GIANT'S CASTLE|MOUSE KING|
        OLD MAN OF THE SEA|RAT CATCHER|SEVEN LEAGUE BOOTS|SHIPWRECKED SAILOR|
        SNOW GLOBE|STRAW INTO GOLD|TALKING MIRROR|TOWN MOUSE|THE BRAVE TAILOR|
        TWELVE PRINCESSES|THE FISHERMAN'S WIFE|BREMEN MUSICIANS|
        THE SPHINX'S RIDDLE|BEANSTALK GIANT|GOOSE GIRL|SORCERER'S APPRENTICE|
        EMPEROR'S NEW CLOTHES|WILD SWANS|WALRUS AND THE CARPENTER|SCHEHERAZADE|
        THE SHOEMAKER|THE WALRUS|THE PRINCESS AND PEA""".split("|"),
    "tricky": """BUREAUCRACY|LOOPHOLE|PUNCTUALITY|PEER PRESSURE|SPORTSMANSHIP|
        FAVORITISM|HYPOCRISY|WISHFUL THINKING|WRITER'S BLOCK|BEGINNER'S LUCK|
        DISHONESTY|EAVESDROPPING|SENTIMENTAL|PERFECTIONISM|CONSEQUENCE|
        EXPECTATION|REPUTATION|RESPONSIBILITY|SELF-CONTROL|INFLATION|
        BANKRUPTCY|ETIQUETTE|GENEROSITY|HUMILITY|IMPATIENCE|INNOCENCE|
        INTUITION|MOMENTUM|OBSESSION|PERSUASION|PESSIMISM|PREJUDICE|
        SINCERITY|SOLITUDE|STUBBORNNESS|SUPERSTITION|SUSPICION|SYMPATHY|
        TEMPTATION|UNCERTAINTY|VANITY|COINCIDENCE|COMPROMISE|CONSCIENCE|
        DILEMMA|HESITATION|HOMESICKNESS|INSTINCT|KARMA""".split("|"),
    "science": """FULCRUM|CENTRIFUGE|CHLOROPHYLL|LITMUS PAPER|SEISMOGRAPH|
        VERTEBRATE|TECTONIC PLATE|ENZYME|ANTIBODY|PULSAR|PLASMA|INERTIA|
        VELOCITY|WAVELENGTH|SPECTRUM|HEMISPHERE|CONDENSATION|EVAPORATION|
        POLLINATION|AMOEBA|SPORE|FILAMENT|ALLOY|COMPOUND|DENSITY|BINARY|
        HYPOTHESIS|MUTATION|NEUTRON|PROTON|REFRACTION|VOLT|BOILING POINT|
        MELTING POINT|DWARF PLANET|TELESCOPE LENS|BLOOD CELL|LUNAR ECLIPSE|
        GREENHOUSE GAS|SULPHUR|METHANE|OZONE LAYER|TITANIUM|EROSION|
        ANTIMATTER|APOLLO 11|FAHRENHEIT|BAROMETER""".split("|"),
    "jobs": """SOMMELIER|COBBLER|GLASSBLOWER|STONEMASON|SURVEYOR|PHYSIOTHERAPIST|
        AUCTIONEER|FERRY CAPTAIN|CRANE OPERATOR|CRIMINAL LAWYER|ESTATE AGENT|
        AIR TRAFFIC CONTROLLER|MARINE BIOLOGIST|LIGHTHOUSE KEEPER|STREETSWEEPER|
        DANCE TEACHER|HOTEL MANAGER|TRAVEL AGENT|CARETAKER|MISSIONARY|
        WEATHER FORECASTER|STUNT DOUBLE|GRAVEDIGGER|BOOKSELLER""".split("|"),
    "music": """BARITONE|SOPRANO|TENOR|ALTO|RAGTIME|SKA|PANPIPES|LUTE|SITAR|
        ZITHER|MANDOLIN|METRONOME|MUSIC STAND|CHAMBER MUSIC|STRING QUARTET|
        QUARTET|ENCORE|ROADIE|GROUPIE|SOUND CHECK|REHEARSAL|VOCAL CORDS|
        THEME TUNE|GIG|BUSKING|MOSH PIT|STAGE DIVE|TURNTABLE|GRAMOPHONE|
        BUGLE|COWBELL|TIMPANI|OBOE|CLARINET|BONGO DRUMS|SNARE DRUM|BASS DRUM|
        DOUBLE BASS|FRENCH HORN|BEATBOXING|CHORD|DUET|HYMN|LYRICS|
        NATIONAL ANTHEM|PERCUSSION|SAMBA|TANGO|WALTZ|FLAMENCO|SOUL MUSIC|
        FOLK MUSIC|GOSPEL|BALLAD|ORGAN|HARP|BANJO|UKULELE|ACCORDION""".split("|"),
    "nature": """BOG|BRAMBLE|GLADE|ESTUARY|FJORD|PLATEAU|SEDIMENT|SHALE|TREELINE|
        WETLAND|MOOR|THICKET|GULLY|RAVINE|GORGE|TUNDRA|SAVANNA|LAGOON|
        DELTA|REEF|RIDGE|SLOPE|SUMMIT|CREEK|GROVE|HOLLOW|MARSH|SAPLING|
        SEEDLING|SPROUT|COMPOST|NECTAR|POLLEN|FUNGUS|MOSS|IVY|REED|VINE|
        WISTERIA|HEATHER|THISTLE|MISTLETOE|LAVENDER|CEDAR|LIMESTONE|GRANITE|
        CLAY|SEA BREEZE|ROCK POOL|SAND DUNE""".split("|"),
    "house": """DRAINPIPE|SHOEHORN|PLUG SOCKET|EXTENSION CORD|TOWEL RAIL|
        SPICE RACK|SOAP DISH|NIGHTLIGHT|FLANNEL|BATH MAT|BIN BAG|WINDOW SILL|
        WINDOW BOX|COLANDER|SIEVE|DUSTPAN|CLOTHES PEG|COAT HANGER|
        GRANDFATHER CLOCK|CHEST OF DRAWERS|LAUNDRY BASKET|RECYCLING BIN|
        SMOKE DETECTOR|FIRE ALARM|BABY MONITOR|DRESSING GOWN|TUMBLE DRYER|
        IRONING BOARD|PAINT ROLLER|TAPE MEASURE|SEWING KIT|NAIL CLIPPERS|
        OVEN GLOVE|WOODEN SPOON|ROLLING PIN|CUTTING BOARD|BOTTLE OPENER|
        CORKSCREW|MOUSETRAP|WHEELBARROW|WATERING CAN|GARDEN HOSE|GARDEN SHED|
        WASHING LINE|LETTERBOX|DOORMAT|FLOORBOARD|PHOTO ALBUM|PICTURE FRAME""".split("|"),
    "places": """PETRA|ANGKOR WAT|ZANZIBAR|LAPLAND|ACROPOLIS|GIBRALTAR|MALTA|
        MONACO|SEVILLE|NAPLES|QUEBEC|SIBERIA|AZTEC RUINS|GALAPAGOS|KREMLIN|
        MARRAKECH|SANTORINI|TRANSYLVANIA|VERSAILLES|YELLOWSTONE|ST PETERSBURG|
        BUENOS AIRES|HELSINKI|COPENHAGEN|OSLO|BUDAPEST|PRAGUE|REYKJAVIK|
        MONGOLIA|CROATIA|BELGIUM|NETHERLANDS|INDONESIA|BERMUDA|CASABLANCA|
        MEDITERRANEAN|PANAMA CANAL|NOTRE DAME|LOUVRE|SPHINX|MOUNT FUJI|
        NIAGARA FALLS|GREAT BARRIER REEF|GOLDEN GATE BRIDGE|BROOKLYN BRIDGE|
        BUCKINGHAM PALACE|EASTER ISLAND|DEAD SEA|MACHU PICCHU|KILIMANJARO""".split("|"),
    "movies": """NOSFERATU|METROPOLIS|THE THIRD MAN|SEVEN SAMURAI|VERTIGO|
        REAR WINDOW|CHINATOWN|FARGO|MEMENTO|THE STING|TWELVE ANGRY MEN|
        ROMAN HOLIDAY|BEN-HUR|SPARTACUS|TRAINSPOTTING|LABYRINTH|HOOK|BIG|IT|
        TRON|CITIZEN KANE|DR. STRANGELOVE|AMELIE|DJANGO|GOODFELLAS|
        MOULIN ROUGE|SISTER ACT|HAIRSPRAY|THE BIRDS|THE GREAT ESCAPE|
        THE ITALIAN JOB|STAND BY ME|WHIPLASH|PARASITE|OPPENHEIMER|
        SPIRITED AWAY|SINGING IN THE RAIN|THE BLUES BROTHERS|
        THE BREAKFAST CLUB|THE SIXTH SENSE|THE TRUMAN SHOW|CAST AWAY|HEAT""".split("|"),
    "people": """ZAHA HADID|KATHERINE JOHNSON|GRACE HOPPER|RICHARD FEYNMAN|
        MICHAEL FARADAY|LOUIS PASTEUR|EMMELINE PANKHURST|SACAGAWEA|EDITH PIAF|
        NINA SIMONE|BILLIE HOLIDAY|MILES DAVIS|RAY CHARLES|JOHNNY CASH|
        GUTENBERG|COPERNICUS|HIPPOCRATES|SUN TZU|ATTILA THE HUN|HANNIBAL|
        FERDINAND MAGELLAN|JAMES COOK|WILBUR WRIGHT|YURI GAGARIN|ARMSTRONG|
        JULES VERNE|FRANZ KAFKA|VIRGINIA WOOLF|EMILY DICKINSON|BEATRIX POTTER|
        DANTE|HOMER|LEO TOLSTOY|ERNEST HEMINGWAY|KARL MARX|NOSTRADAMUS|
        REMBRANDT|ADA LOVELACE|JESSE OWENS|LOUIS ARMSTRONG|MALALA|
        FLORENCE NIGHTINGALE|ALEXANDER FLEMING|GUSTAV EIFFEL|TIM BERNERS-LEE|
        JOAN OF ARC|HELEN KELLER|JANE GOODALL|MAYA ANGELOU|ROSA PARKS""".split("|"),
    "sound": """SUBMARINE PING|STATIC|VUVUZELA|BUGLE|FOGHORN|KAZOO|
        AIR RAID SIREN|RECORD SCRATCH|DIAL-UP MODEM|GARGLING|HICCUP|
        BOWLING STRIKE|FOOTBALL WHISTLE|GROWLING STOMACH|STOMACH GROWLING|
        SQUEAKY WHEEL|MOUSE SQUEAK|CHIRPING CRICKET|HONKING GEESE|
        NAIL ON CHALKBOARD|TICKING BOMB|CHAMPAGNE CORK|CASH REGISTER|
        SEWING MACHINE|WIND CHIMES|SLEIGH BELLS|WHIP CRACK|YODELING""".split("|"),
    "act": """SHOOT AN ARROW|SOWING SEEDS|SPINNING A PLATE|STACKING BOXES|
        TUNING A RADIO|WRINGING A TOWEL|THREADING A NEEDLE|SHARPENING A PENCIL|
        HANGING WALLPAPER|PARALLEL PARKING|CONDUCTING MUSIC|BALANCING A BOOK|
        DODGING TRAFFIC|IDENTIFYING MUSHROOMS|CARRYING A LADDER|
        CHANGING A TIRE|SAWING A PLANK|SCULPTING|LACING A SHOE|
        PLUCKING A GUITAR|TAKING YOUR PULSE|SLIDING DOWN A POLE""".split("|"),
    "food": """MINESTRONE|BOUILLABAISSE|CARBONARA|GNOCCHI|SHAWARMA|EDAMAME|
        SAFFRON|QUINOA|SHERBET|SHORTBREAD|TRIFLE|CUSTARD|SUET|SEAWEED|
        SARDINE|PRAWN|OYSTER|PAELLA|RISOTTO|SOURDOUGH|COTTAGE CHEESE|
        CAESAR SALAD|COLESLAW|MARMALADE|LICORICE|NUTMEG|CINNAMON|
        MAPLE SYRUP|OLIVE OIL|SOY SAUCE|VINEGAR|GRAVY|STUFFING|
        MINCE PIE|PUMPKIN PIE|RICE PUDDING|PORK CHOP|SAUSAGE ROLL|
        BAKED BEANS|LENTILS|BEETS|RADISH|ARTICHOKE|PARMESAN|CHEDDAR""".split("|"),
}

# Entries to ADD. The published easy tiers first, then more of the same
# character for the categories no source covered. The rule for every one: a
# child would get it, or it is a thing you can point at. That is the tier the
# first version had none of.
ADD = {
    "animals": "DOG COW PIG BIRD FISH BEAR".split(),
    "food": """BREAD APPLE CAKE CHEESE EGG BURGER RICE SOUP SALAD TEA WATER MILK
        ORANGE POTATO CHOCOLATE TACOS PANCAKES JUICE BUTTER SUGAR SALT HONEY
        LEMON GRAPES STRAWBERRY CHERRY PEACH CORN BEANS ONION TOMATO CUCUMBER
        BACON HAM STEAK CHICKEN FISH SANDWICH TOAST CEREAL
        YOGURT COOKIE DONUT MUFFIN PIE JAM SOUP""".split() + ["ICE CREAM"],
    "jobs": "DRIVER COOK POLICE DANCER CLOWN CHEF ACTOR ARTIST JUDGE GUARD".split(),
    "sports": """RUNNING SKATING JUMPING THROWING CATCHING KICKING RACING
        WALKING STRETCHING""".split(),
    "act": """SLEEPING EATING DRINKING WALKING JUMPING SITTING READING WRITING
        COOKING CLEANING DRIVING SINGING DANCING CRYING LAUGHING SMILING
        WAVING CLAPPING POINTING RUNNING THINKING LISTENING SHOUTING
        WHISPERING KNOCKING PUSHING PULLING LIFTING CARRYING THROWING
        CATCHING KICKING CLIMBING FALLING""".split() + ["SITTING DOWN", "STANDING UP"],
    "kids": """UNICORN PIRATE SPACESHIP MERMAID PRINCESS WIZARD FIREFIGHTER
        FAIRY CIRCUS COWBOY ROBOT GHOST WITCH SANTA SNOWMAN BALLOON BUBBLE
        SLIDE SWING KITE""".split(),
    "house": """TABLE CHAIR BED DOOR WINDOW CLOCK MIRROR SOFA FRIDGE OVEN SINK
        SHOWER TOILET TOWEL PILLOW BLANKET CARPET CURTAIN KEY PHONE COMPUTER
        PLATE CUP FORK SPOON KNIFE BOWL POT PAN SOAP BRUSH COMB GLUE TAPE
        PENCIL PAPER BIN LADDER HAMMER NAIL CANDLE PICTURE SHELF DESK
        BUCKET SPONGE BASKET""".split() + ["LIGHT SWITCH"],
    "movies": [

        "THE LION KING", "JURASSIC PARK", "BATMAN", "SUPERMAN", "SPIDER-MAN",
        "MINIONS", "CARS", "ENCANTO", "ALADDIN", "PETER PAN", "CINDERELLA",
        "TARZAN", "HERCULES", "THE AVENGERS", "IRON MAN", "THE HUNGER GAMES",
        "TWILIGHT", "NARNIA", "GREASE", "HOME ALONE", "PADDINGTON",
        "SCOOBY DOO", "ICE AGE", "MADAGASCAR", "KUNG FU PANDA",
        "DESPICABLE ME", "TANGLED", "BRAVE", "THE JUNGLE BOOK", "SNOW WHITE",
        "SLEEPING BEAUTY", "THE LITTLE MERMAID", "BEAUTY AND THE BEAST",
        "101 DALMATIANS", "THE MUPPETS", "STUART LITTLE", "FREE WILLY",
        "JUMANJI", "MRS. DOUBTFIRE", "GHOSTBUSTERS",
    ],
    "music": [
        "GUITAR", "PIANO", "DRUM", "VIOLIN", "FLUTE", "TRUMPET", "SAXOPHONE",
        "MICROPHONE", "SPEAKER", "RADIO", "SONG", "SINGER", "BAND", "CONCERT",
        "DANCING", "RAP", "ROCK MUSIC", "POP MUSIC", "JAZZ", "CHOIR",
        "WHISTLING", "CLAPPING", "KARAOKE", "GUITAR SOLO", "DRUMMER",
        "TRIANGLE", "TAMBOURINE", "XYLOPHONE", "MARACAS", "BAGPIPES",
        "CELLO", "TROMBONE", "TUBA", "KEYBOARD", "HEADPHONES", "EARPHONES",
        "MUSIC VIDEO", "DANCE FLOOR", "STAGE", "SPOTLIGHT", "CROWD",
        "APPLAUSE", "MELODY", "RHYTHM", "BEAT", "VOICE", "HUMMING",
        "LULLABY", "BIRTHDAY SONG", "CHRISTMAS CAROL", "MARCHING BAND",
        "ORCHESTRA", "CONDUCTOR", "MUSICAL", "SOUNDTRACK", "ALBUM",
        "VINYL RECORD", "JUKEBOX", "DISCO", "COUNTRY MUSIC", "REGGAE",
    ],
    "nature": [
        "TREE", "FLOWER", "GRASS", "LEAF", "RIVER", "LAKE", "SEA", "BEACH",
        "MOUNTAIN", "HILL", "FOREST", "DESERT", "ISLAND", "CAVE", "RAIN",
        "SNOW", "WIND", "SUN", "MOON", "STAR", "CLOUD", "STORM", "RAINBOW",
        "FIRE", "ROCK", "SAND", "MUD", "ICE", "WAVE", "VOLCANO",
    ],
    "people": [
        "MARILYN MONROE", "MICHAEL JACKSON", "MADONNA", "SHERLOCK HOLMES",
        "JAMES BOND", "SANTA CLAUS", "CLEOPATRA", "JULIUS CAESAR",
        "NAPOLEON", "ABRAHAM LINCOLN", "GEORGE WASHINGTON", "QUEEN VICTORIA",
        "MARTIN LUTHER KING", "MOTHER TERESA", "PRINCESS DIANA", "THE POPE",
        "ELVIS PRESLEY", "THE BEATLES", "MICKEY MOUSE", "SUPERMAN",
        "BATMAN", "SPIDER-MAN", "HARRY POTTER", "ROBIN HOOD", "DRACULA",
        "FRANKENSTEIN", "TARZAN", "PETER PAN", "CINDERELLA", "SNOW WHITE",
        "ALICE", "PINOCCHIO", "SCROOGE", "ROMEO AND JULIET", "KING ARTHUR",
        "MERLIN", "HERCULES", "ZEUS", "NOAH", "MOSES", "ADAM AND EVE",
        "BUDDHA", "GALILEO", "ISAAC NEWTON", "CHARLES DARWIN",
    ],
    # PLACES is geography: countries, cities, landmarks, natural features. The
    # everyday venues that used to seed it (SUPERMARKET, FARM, HOSPITAL, ...)
    # were added as an easy tier and contradicted the category's own hint,
    # "PLACES AND LANDMARKS". They live in kids now, which is where an easy
    # thing-you-can-point-at belongs.
    "places": [
        "MACHU PICCHU", "MOUNT FUJI", "NIAGARA FALLS", "VICTORIA FALLS",
        "KILIMANJARO", "SANTORINI", "PRAGUE", "BUDAPEST", "COPENHAGEN",
        "MARRAKESH", "PATAGONIA", "GALAPAGOS", "YELLOWSTONE",
        "MOUNT RUSHMORE", "ANGKOR WAT", "PETRA", "THE LOUVRE",
        "GREAT BARRIER REEF", "PANAMA CANAL", "BORA BORA",
    ],
    "science": [
        "ROCKET", "PLANET", "STAR", "MOON", "SUN", "EARTH", "GRAVITY",
        "MAGNET", "BATTERY", "MICROSCOPE", "TELESCOPE", "EXPERIMENT",
        "LABORATORY", "ROBOT", "COMPUTER", "ELECTRICITY",
    ],
    "sound": [
        "THUNDER", "RAIN", "WIND", "DOORBELL", "ALARM", "PHONE RINGING",
        "BABY CRYING", "DOG BARKING", "CAT MEOWING", "COW MOOING",
        "BIRD SINGING", "CAR HORN", "SIREN", "TRAIN", "AIRPLANE",
        "CLAPPING", "LAUGHING", "SNEEZING", "COUGHING", "SNORING",
        "WHISTLING", "KNOCKING", "TICKING CLOCK", "BOILING KETTLE",
    ],
    "tricky": [
        "DREAM", "GALAXY", "TIME", "ECLIPSE", "HARMONY", "CHANCE", "GRAVITY",
        "SHADOW", "ECHO", "SILENCE", "BALANCE", "FREEDOM", "LOVE", "FEAR",
        "ANGER", "JOY", "SLEEP", "MEMORY", "HOPE", "PEACE", "COLOR",
        "NUMBER", "WEIGHT", "SPEED", "HEAT", "COLD", "DARKNESS", "LIGHT",
        "NOISE", "SMELL", "TASTE", "TOUCH", "HUNGER", "THIRST", "TIRED",
    ],
}


# A second pass. The first left five lists under the floor, because many of the
# additions were words the list already had -- which is itself a small result:
# the originals were not missing these, they were missing the EASY ones, and
# several of my "easy" picks were already in.
ADD["movies"] += [
    "E.T.", "GREMLINS", "THE GOONIES", "MARY POPPINS",
    "THE SOUND OF MUSIC", "WILLY WONKA", "MATILDA", "THE MASK", "SHREK 2",
    "FINDING DORY", "INSIDE OUT", "RATATOUILLE", "MONSTERS INC.", "WALL-E",
    "UP", "COCO", "MOANA", "FROZEN 2", "TOY STORY 2",
]
ADD["music"] += [
    "GUITARIST", "PIANIST", "DRUM KIT", "MUSIC NOTE", "SHEET MUSIC",
    "DANCE MUSIC", "LOVE SONG", "RINGTONE", "PLAYLIST",
    "EARWORM", "SOLO", "TRIO", "BASS", "TREBLE", "TEMPO", "TUNE",
    "CHORUS", "VERSE", "FESTIVAL", "TICKET", "BACKSTAGE",
    "AMPLIFIER", "GUITAR PICK", "DRUMSTICKS", "PIANO KEYS", "MUSIC BOX",
    "WHISTLE", "HARMONICA", "RECORDER", "BELL", "GONG", "CHIME",
    "SINGALONG", "HUMMING", "OPERA SINGER", "ROCK STAR", "POP STAR",
]
ADD["people"] += [
    # No CHRISTOPHER COLUMBUS: 280px against a 276px results column, so it would
    # ship clipped. The bare surname was tried and is worse -- Columbus is a city
    # in Ohio as often as it is the man, and the room cannot clue past that.
    "JULIUS CAESAR", "ALEXANDER THE GREAT", "MARCO POLO",
    "WALT DISNEY", "HENRY FORD", "THOMAS EDISON", "NIKOLA TESLA",
    "MARIE CURIE", "STEPHEN HAWKING", "MUHAMMAD ALI", "PELE", "USAIN BOLT",
    "SERENA WILLIAMS", "MICHAEL JORDAN", "BRUCE LEE", "CHARLIE BROWN",
    "TOM AND JERRY", "BUGS BUNNY", "DONALD DUCK",
]
ADD["science"] += [
    "ATOM", "MOLECULE", "DNA", "VOLCANO", "EARTHQUAKE", "RAINBOW", "MAGNET",
    "MIRROR", "LENS", "PRISM", "COMET", "METEOR", "ASTRONAUT", "SATELLITE",
    "SPACE STATION", "BLACK HOLE", "MILKY WAY", "SOLAR SYSTEM",
]
ADD["sound"] += [
    "DRIPPING TAP", "FOOTSTEPS", "HEARTBEAT", "BREAKING GLASS", "POPCORN",
    "ZIPPER", "SCISSORS", "TYPING", "SPLASHING", "CRUNCHING", "SIZZLING",
    "BUZZING BEE", "HISSING SNAKE", "ROARING LION", "HOWLING WOLF",
]

ADD["movies"] += [
    "THE PRINCESS BRIDE", "FORREST GUMP", "THE MUMMY", "MEN IN BLACK",
    # "PIRATES OF THE CARIBBEAN" is the title and it is over the pixel cap; the
    # shortened form is not the name of anything.
    "NIGHT AT THE MUSEUM", "THE KARATE KID",
    "SPACE JAM", "THE LEGO MOVIE", "SING",
]
ADD["people"] += [
    "WINNIE THE POOH", "SNOOPY", "POPEYE", "ASTERIX", "TINTIN", "GARFIELD",
    "HOMER SIMPSON", "SCOOBY DOO", "SPONGEBOB", "PIKACHU", "MARIO",
    "CAPTAIN AMERICA", "WONDER WOMAN", "THE HULK",
]



# ---------------------------------------------------------------------------
# Second pass: what a cold reviewer found after the first revision shipped.
#
# The first pass fixed 22 hand-listed strings. That is why the Britishisms
# below survived it -- they were never in the table, and a table of literals
# only ever fixes what somebody already noticed. These are the systematic ones,
# found by reading every file rather than by spot-checking.

FIX.update({
    # British spellings in an American-English game. Every replacement is
    # inside both the character and the pixel cap; measured, not assumed.
    "ALUMINIUM": "ALUMINUM",
    "JEWELLER": "JEWELER",
    "OMELETTE": "OMELET",
    "NEWSREADER": "NEWS ANCHOR",
    "FOOTBALLER": "FOOTBALL PLAYER",
    "RACING DRIVER": "RACE CAR DRIVER",
    "TRAIN DRIVER": "TRAIN ENGINEER",
    "TORCH": "FLASHLIGHT",
    "BIN": "TRASH CAN",
    "BREAD BIN": "BREAD BOX",
    "TEA TOWEL": "DISH TOWEL",
    "WARDROBE": "CLOSET",
    "MASHED POTATO": "MASHED POTATOES",
    "AUTUMN": "FALL",
    "CINEMA": "MOVIE THEATER",
    "BOBSLEIGH": "BOBSLED",
    "SLEDGING": "SLEDDING",
    "ATHLETICS": "TRACK AND FIELD",
    "DRIPPING TAP": "DRIPPING FAUCET",
    "ICE CREAM VAN": "ICE CREAM TRUCK",
    "SWEETCORN": "CORN ON THE COB",
    "NAAN BREAD": "NAAN",

    # Proper nouns that were wrong. A wrong title is worse than a missing one:
    # the room clues the film it can see and the holder answers with the name
    # the film actually has.
    "SCOOBY DOO": "SCOOBY-DOO",
    "JACK-O-LANTERN": "JACK-O'-LANTERN",
    "LITTLE BO PEEP": "LITTLE BO-PEEP",
    # Nobody is called Johann Bach. The full name is over the pixel cap, and
    # the surname alone is what a room shouts anyway.
    "JOHANN BACH": "BACH",
})

CUT.update({
    "food": CUT.get("food", []) + [
        # One answer, two cards. The deck deals them as separate words and the
        # room cannot tell the holder which spelling to say.
        "DOUGHNUT", "YOGHURT", "GRAPE", "PANCAKE", "TACO", "FRIES",
        # British, and each collides with the American entry already present.
        "BISCUIT", "CHIPS", "PORRIDGE", "BREAD ROLL",
    ],
    "movies": CUT.get("movies", []) + [
        "LION KING", "MRS DOUBTFIRE",
        # The real title is "PIRATES OF THE CARIBBEAN", which is over the pixel
        # cap. Ship no entry rather than a wrong proper noun.
        "PIRATES OF CARIBBEAN",
    ],
    "story": CUT.get("story", []) + [
        "THE GOLDEN GOOSE",
        # "JACK AND BEANSTALK" is not the title and the real one does not fit.
        "JACK AND BEANSTALK",
    ],
    "myths": CUT.get("myths", []) + ["NINE-HEADED HYDRA"],
    "music": CUT.get("music", []) + ["SPEAKERS", "MOUTH ORGAN"],
    "people": CUT.get("people", []) + [
        # A city in Ohio as often as the man, and the unambiguous form is over
        # the pixel cap. Both spellings go: the short one is ambiguous and the
        # long one is 280px against a 276px column.
        "COLUMBUS", "CHRISTOPHER COLUMBUS",
    ],
    "jobs": CUT.get("jobs", []) + [
        "POSTMAN",   # MAIL CARRIER is present
        "CHEMIST",   # means pharmacist in the US, and PHARMACIST is present
        # POLICE and POLICE OFFICER are one answer. POLICE is the anchored
        # one -- it is in the published easy tier -- so the longer variant goes.
        "POLICE OFFICER",
    ],
    "nature": CUT.get("nature", []) + ["WOODLAND"],  # FOREST is present
    "sports": CUT.get("sports", []) + [
        "NETBALL", "SNOOKER",   # not played where this game is played
        "PING PONG",            # TABLE TENNIS is present
        "QUIDDITCH",            # fictional, in a list of real sports
    ],
    "sound": CUT.get("sound", []) + [
        # Word-order twins: one sound, two cards. The survivor of each pair is
        # the form the ADD tables above use, so this file has one house style
        # rather than two spellings of the same noise.
        "BARKING DOG", "PURRING CAT", "CLOCK TICKING", "WOLF HOWL",
        "TELEPHONE RINGING",  # PHONE RINGING is present
    ],
    "house": CUT.get("house", []) + ["HOT WATER BOTTLE"],
})

# Replacements, so the cuts above do not push a list under the floor. Easy-tier
# and concrete, which is the half these lists were short of.
for _slug, _words in {
    # movies was two entries above the floor before three wrong or duplicated
    # titles came out of it, so the replacements are not optional. All of these
    # are real titles at their real length -- "HOW TO TRAIN A DRAGON" was
    # rejected for the same reason PIRATES was: the film is called something
    # else, and a shortened title is a wrong answer the room cannot clue.
    "movies": [
        "FANTASIA", "ROBIN HOOD", "LILO AND STITCH", "WRECK-IT RALPH",
        "THE LORAX", "TROLLS", "HAPPY FEET", "CHICKEN RUN", "CORALINE",
        "THE IRON GIANT", "ANASTASIA", "THE CROODS",
    ],
    "myths": ["GHOST", "GOLDEN FLEECE", "PEGASUS"],
    "music": ["MICROPHONE", "LULLABY", "CHOIR"],
    "sports": ["DODGEBALL", "SOFTBALL", "LACROSSE", "BADMINTON", "HIGH JUMP"],
    # No CHURCH BELL: CHURCH BELLS is already there, and the two are one answer.
    "sound": ["BABY CRYING", "FOOTSTEPS", "DOORBELL", "WHISTLE"],
    "house": ["TELEVISION", "REMOTE CONTROL", "COFFEE TABLE"],
    "science": ["MICROSCOPE", "TELESCOPE"],
}.items():
    ADD.setdefault(_slug, []).extend(_words)


# ---------------------------------------------------------------------------
# Third pass: MYTHS, MUSIC and TRICKY rebuilt from published sources.
#
# Mario played the shipped game and called these three "clearly not good". He
# was right, and none of it is taste: they were cards that CANNOT BE WON.
#
#   myths   nine indistinguishable small magical humanoids. The room clues
#           "small magic person" and there are nine correct answers.
#   music   ten mutually-correct theory nouns. The room hums; SONG, TUNE,
#           MELODY and RHYTHM are simultaneously right; one scores.
#   tricky  exact-synonym clusters. The room clues "green-eyed monster", the
#           holder says ENVY, the card says JEALOUSY, no point. A category can
#           be hard on purpose; it cannot mark a right answer wrong.
#
# THIS PASS OVERRULES THE FIRST ONE, in both directions, and that is the point
# of keeping it as a separate layer rather than editing the tables above.
# The first pass worked from memory and got music backwards: it ADDED the
# theory nouns and CUT the instruments. It also swept seven real, published
# myth entries (FENRIR, FREYA, HARPY, KELPIE, ODYSSEUS, PERSEUS, SATYR) into a
# purge of confabulations like SALAMANDER OF FIRE. Where the two disagree, the
# researched decision wins -- but the disagreement is RESOLVED here explicitly,
# never by letting one silently shadow the other.
LATE = {
    "myths": (
        [
            "DWARF", "IMP", "NYMPH", "PIXIE", "SASQUATCH", "WOLFMAN", "GORGON", "GHOST SHIP",
            "MIDAS TOUCH", "COCKATRICE", "FIRE BIRD", "SEA MONSTER", "SEA SERPENT",
            "GIANT SQUID", "STORM GIANT", "MOUNTAIN TROLL", "MUMMY'S CURSE",
            "THE PHARAOH'S CURSE", "ARES", "ARTEMIS", "CUPID", "CHARIOT OF THE SUN",
            "RIVER OF THE DEAD", "BLACK CAT", "BROOMSTICK", "CRYSTAL BALL", "LUCKY CHARM",
            "OUIJA BOARD", "TAROT CARDS", "SKELETON KEY", "WISHING WELL", "SHADOW PUPPET",
            "STONE CIRCLE", "BERMUDA TRIANGLE", "FLYING CARPET", "INVISIBLE CLOAK",
            "MAGIC POTION", "SPELLBOOK", "ENCHANTED SWORD", "GOLDEN APPLE",
            "JACK-O'-LANTERN", "THUNDERBOLT", "TRIDENT", "WINGED SANDALS", "HAUNTED HOUSE",
            "RAINBOW BRIDGE", "FOUNTAIN OF YOUTH", "PHILOSOPHER'S STONE", "LABYRINTH",
            "JINN", "VOODOO DOLL",
        ],
        [
            "DIONYSUS", "PAN", "HEPHAESTUS", "PERSEPHONE", "CHARON", "NIKE", "NEMESIS",
            "HARPY", "SATYR", "SCYLLA", "TYPHON", "NEMEAN LION", "ARACHNE", "CIRCE",
            "PERSEUS", "ODYSSEUS", "NARCISSUS", "SISYPHUS", "STYX", "TROJAN HORSE", "RA",
            "OSIRIS", "HORUS", "BASTET", "THOTH", "FENRIR", "RAGNAROK", "FREYA", "SLEIPNIR",
            "MIDGARD SERPENT", "YGGDRASIL", "HEIMDALL", "BALDUR", "MERLIN", "KELPIE",
            "BABA YAGA", "MONKEY KING", "KITSUNE", "ONI", "QUETZALCOATL", "LA LLORONA",
            "ANANSI", "MAUI", "MOTHMAN", "WENDIGO", "JERSEY DEVIL", "GENIE", "GRENDEL",
            "BEHEMOTH", "FRANKENSTEIN", "CTHULHU", "KRAMPUS",
        ],
    ),
    "music": (
        [
            "SONG", "TUNE", "MELODY", "RHYTHM", "BEAT", "TEMPO", "VERSE", "CHORUS", "TREBLE",
            "SOLO", "FIDDLE", "EARPHONES", "LOUDSPEAKER", "WHISTLE", "WHISTLING", "CLAPPING",
            "BASS", "RECORDER", "KEYBOARD", "ACOUSTIC GUITAR", "BASS GUITAR", "GUITAR SOLO",
            "GUITARIST", "GUITAR PICK", "DRUM KIT", "DRUM SOLO", "STEEL DRUM", "GRAND PIANO",
            "PIANIST", "PIANO KEYS", "BAND PRACTICE", "BRASS BAND", "MARCHING BAND",
            "ALBUM COVER", "BACKING SINGER", "CHIME", "CONCERT HALL", "OPERA SINGER",
            "ROCK AND ROLL", "SOUND SYSTEM", "DANCE MUSIC", "DANCING", "HUMMING",
        ],
        [
            "ACCORDION", "BANJO", "UKULELE", "HARP", "CLARINET", "FRENCH HORN",
            "DOUBLE BASS", "CASTANETS", "COWBELL", "CHURCH ORGAN", "DIDGERIDOO", "SITAR",
            "PAN PIPES", "BLUES", "TECHNO", "FOLK MUSIC", "FLAMENCO", "MARIACHI", "TANGO",
            "WALTZ", "SLOW DANCE", "ENCORE", "MOSH PIT", "ROADIE", "HEADLINER",
            "OPENING ACT", "SOUND CHECK", "WORLD TOUR", "AIR GUITAR", "METRONOME",
            "MUSIC STAND", "TREBLE CLEF", "CASSETTE TAPE", "BOOMBOX", "LYRICS",
            "NATIONAL ANTHEM", "NURSERY RHYME", "WEDDING MARCH", "ELEVATOR MUSIC", "DUET",
            "TONE DEAF", "PERFECT PITCH", "HIGH NOTE", "BEATBOXING", "FANFARE", "SERENADE",
            "HEADBANGING", "CONGA LINE",
        ],
    ),
    "tricky": (
        [
            "ANXIETY", "BARGAIN", "BETRAYAL", "BRIBERY", "CHANCE", "CHARITY", "CRITICISM",
            "DEMOCRACY", "DISCIPLINE", "DOUBT", "DUTY", "ENVY", "EQUALITY", "FAIRNESS",
            "FAITH", "FEAR", "GRIEF", "GUT FEELING", "HAPPINESS", "HARMONY", "HONESTY",
            "HUMOR", "INSPIRATION", "LEADERSHIP", "MERCY", "MOOD", "MOTIVATION", "OPTIMISM",
            "PITY", "POVERTY", "PRIVACY", "PROGRESS", "PURPOSE", "RISK", "ROUTINE", "RUMOR",
            "SHAME", "SILENCE", "SMALL TALK", "TENSION", "TIRED", "TRADITION", "TRAGEDY",
            "TRUST", "WORRY",
        ],
        [
            "ALGORITHM", "BODY LANGUAGE", "BRAIN FREEZE", "BURNOUT", "BUTTERFLY EFFECT",
            "CLAUSTROPHOBIA", "CLIFFHANGER", "CONSCIENCE", "CONSPIRACY THEORY",
            "CULTURE SHOCK", "DIGITAL DETOX", "EMOTIONAL BAGGAGE", "FIRE DRILL",
            "GOOSEBUMPS", "HANGOVER", "HOROSCOPE", "HYPNOSIS", "IMPOSTER SYNDROME",
            "INFINITY", "INSOMNIA", "JET LAG", "JOB INTERVIEW", "KARMA", "MAGIC",
            "MORAL DILEMMA", "PARALLEL UNIVERSE", "PHOTO BOMB", "PLOT TWIST",
            "REALITY CHECK", "ROAD TRIP", "SELF-CARE", "SOCIAL EXPERIMENT", "STAGE FRIGHT",
            "SUPERSTITION", "TEMPTATION", "TRAFFIC JAM", "VIRTUAL REALITY", "WI-FI",
            "WRITER'S BLOCK",
        ],
    ),
}

for _slug, (_cut, _add) in LATE.items():
    _late_cut, _late_add = set(_cut), set(_add)
    # An earlier ADD that this pass cuts is withdrawn, and an earlier CUT that
    # this pass adds is lifted. Without this the two layers would sit in the
    # tables contradicting each other, and main()'s disjointness assert would
    # fire on 52 entries with no way to tell which decision was the later one.
    if _slug in ADD:
        ADD[_slug] = [a for a in ADD[_slug] if " ".join(a.split()).upper() not in _late_cut]
    if _slug in CUT:
        CUT[_slug] = [c for c in CUT[_slug] if c.strip() not in _late_add]
    CUT.setdefault(_slug, []).extend(_cut)
    ADD.setdefault(_slug, []).extend(_add)

# .split() IS THE TRAP IN THIS FILE. It splits on whitespace, so any multi-word
# entry written into one of the strings above is silently shredded into its
# words: "SITTING DOWN STANDING UP" became four entries, and act.txt shipped
# with the entries DOWN and UP in it. So did food ("ICE CREAM" -> ICE, CREAM)
# and house ("LIGHT SWITCH" -> LIGHT, SWITCH). Six junk cards from one habit.
#
# Single words in a triple-quoted block are fine and readable. ANYTHING WITH A
# SPACE goes in an explicit list, appended with +[...], where a space cannot be
# mistaken for a separator.


def main():
    # An entry in both tables oscillates: added by one run, cut by the next,
    # for as long as nobody looks. Two top-up entries were in both.
    for slug in set(CUT) | set(ADD):
        cuts = {c.strip() for c in CUT.get(slug, []) if c.strip()}
        adds = set()
        for a in ADD.get(slug, []):
            a = " ".join(a.split()).upper()
            adds.add(FIX.get(a, a))
        both = cuts & adds
        assert not both, f"{slug}: in both CUT and ADD: {sorted(both)}"

    changed = 0
    print(f"{'list':9} {'was':>4} {'fix':>4} {'cut':>4} {'add':>4} {'now':>4}")
    for path in sorted(WORDS.glob("*.txt")):
        head, body = path.read_text().split("\n\n", 1)
        entries = [l.strip() for l in body.splitlines() if l.strip()]
        was = len(entries)

        fixed = [FIX.get(e, e) for e in entries]
        nfix = sum(1 for a, b in zip(entries, fixed) if a != b)

        cuts = {c.strip() for c in CUT.get(path.stem, []) if c.strip()}
        kept = [e for e in fixed if e not in cuts]
        ncut = len(fixed) - len(kept)

        have = set(kept)
        added = []
        for a in ADD.get(path.stem, []):
            a = " ".join(a.split()).upper()
            # Through FIX as well, because ADD runs AFTER it. Without this an
            # addition is exempt from every correction in this file, and worse:
            # ADD re-adds the uncorrected spelling on every run, so the file
            # ends up holding BOTH forms. That is exactly how SCOOBY DOO and
            # SCOOBY-DOO both shipped, in two categories, from a table whose
            # whole job was to remove duplicates.
            a = FIX.get(a, a)
            if a and a not in have:
                have.add(a)
                added.append(a)
        final = sorted(set(kept) | set(added))

        # The caps the generator enforces, applied here so a bad addition is
        # caught at the point it is written rather than at the next build.
        for e in final:
            assert re.fullmatch(r"[A-Z0-9 '&.-]+", e), f"{path.stem}: bad chars in {e!r}"
            assert len(e) <= 22, f"{path.stem}: {e!r} is {len(e)} chars, cap is 22"
            wide = round(pixels(e, 14))
            assert wide <= 276, (
                f"{path.stem}: {e!r} is {wide}px at the 14px cut and the results "
                f"column is 276px -- shorten it or drop it, but do not invent a "
                f"shorter name for it"
            )

        path.write_text(head + "\n\n" + "\n".join(final) + "\n")
        changed += 1
        floor = "" if len(final) > 128 else f"   UNDER THE 129 FLOOR by {129 - len(final)}"
        print(f"{path.stem:9} {was:4} {nfix:4} {ncut:4} {len(added):4} {len(final):4}{floor}")
    print(f"\n{changed} lists rewritten")


if __name__ == "__main__":
    main()
