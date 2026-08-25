# Guess Who, on generated faces

A design, and the argument against it, before any code.

The idea: the twenty-four characters are not drawn by hand. They are generated
by the same avatar system that already gives every device its face, so in a
nearby game the person you are hunting for **is the other player**, and the
answer to "who are you?" is the name and face they already carry around.

That is a genuinely good idea and it does not survive contact with the rules
unmodified. This file says why, and what to build instead.

---

## 1. What is already there

`docs/identity.md` and the player memory: every device has a three-word name
(SPIKY GLAD GLUM, CURLY SHADES MUTTON) and a face drawn from it. The face is not
decoration -- it is how the other device shows you who it found, and it appears
in the status capsule of every link game.

So the raw material exists. The question is whether it makes a Guess Who board.

---

## 2. The rules Guess Who actually has

Twenty-four characters on a board. Each player secretly holds one. You take
turns asking a yes/no question about appearance -- "do they have a beard?" --
and fold down every character the answer eliminates. First to name the other's
character wins.

The load-bearing part is not the faces. It is that **every character differs
from every other along a small set of shared, askable attributes**, and that the
attributes partition the set usefully. A good Guess Who board has attributes
that split roughly in half, so a well-chosen question halves the field and the
game is about five or six questions long.

---

## 3. The three problems

### The avatar system does not have askable attributes

This is the one that kills the naive version. A generated face is a hash of a
name into a drawing. It has features, but they are not a **vocabulary**: there is
no enumerated "has a beard / does not" that both devices agree on, and there is
no guarantee that a random twenty-four faces contain a question that splits them
usefully.

Generate twenty-four faces from twenty-four random names and you get a board
where the only reliable question is "is it that one?", which is not Guess Who, it
is twenty-four coin flips.

**The fix is to invert the generator.** Do not generate a face and then look for
attributes in it. Define the attribute vocabulary first -- six or seven binary
features -- and generate the face **from** the attribute vector. Then a
character IS its attribute vector, the drawing is a rendering of it, and every
question is answerable by construction.

That is the same discipline as everything else here: a promised property is
built in rather than sampled for. And it makes the board's fairness a
construction rather than a hope -- pick 24 distinct vectors and you know every
question splits the field exactly as the vectors say.

### Your own face is not on the board

The idea's whole appeal is that in multiplayer you hunt for _the other player_.
But the other player's face is generated from their name, which is a
three-word phrase they did not choose from an attribute vocabulary. It will not
be one of the twenty-four unless you make it one.

Two honest options:

- **The board is drawn from the two players' names plus twenty-two generated
  ones.** Both real faces are on it, both players know that, and the game keeps
  its hook. The cost is that the two real faces must be _forced into_ the
  attribute vocabulary, which means the face on the board is not identical to
  the face in the capsule. That is a visible lie and I think it is fatal.
- **The name is the character, and the face is generated from the ATTRIBUTES of
  the name.** That is: change the avatar generator so faces have always been
  built from an attribute vector derived from the name. Then a player's real
  face already has askable attributes, and it is on the board unmodified.

The second is the right answer and it is a bigger change than the game: it
touches `identity` and every screen that shows a face. It should be done as its
own piece of work, before the game, not inside it.

### One device, two secrets

Guess Who is two players each holding a secret, looking at their own board.
`linkplay` handles that fine -- each device folds its own board down, and only
the questions and answers cross. But **the asking is the game**, and the
question is a natural-language sentence.

A handheld cannot let you type "do they have a beard?" and have the other device
understand it. So the questions have to be a menu, which means the vocabulary is
finite and visible, which means the game is really: pick one of six attributes,
learn one bit, repeat.

That is fine, and it is what every digital Guess Who does. But it should be
designed as what it is -- **a deduction game with six binary probes** -- rather
than pretending to be the conversation. Once you accept that, it is closer to
Mastermind than to the board game, and the interesting design question becomes
whether six bits over twenty-four characters is enough of a game. log2(24) is
4.6, so a perfect player needs five questions. That is a two-minute game, which
is right for this device.

---

## 4. What I would build

**Not now.** Ahead of it in value: the hardware-button rethink (which affects
every existing app), and the identity-attributes change this game depends on.

When it is built:

- **Attributes first, faces second.** Six binary features chosen so that the
  twenty-four characters are exactly the vectors of a chosen 24-subset of the
  64 possible -- picked so each feature splits close to 12/12 and no two
  characters collide. That is a table, generated once and committed, not a
  runtime search.
- **The face generator takes an attribute vector.** Which means changing
  `identity` so that a name maps to a vector and the vector maps to a face,
  rather than a name mapping straight to a face. Then every player's existing
  face keeps working and gains askable attributes for free.
- **Solo is the same game against a bot** that holds a character and answers
  honestly. The bot cannot cheat by construction if it commits to its character
  before the first question -- and that is worth a test, because "the opponent
  picks its answer to keep the game going" is the obvious cheat and it is
  invisible from the outside.
- **The board is 4x6 of faces**, folded ones dithered rather than removed, for
  the same reason a full Connect Four column dims rather than vanishing.

---

## 5. The criticism, stated plainly

The idea as pitched -- "the characters are our avatars, and in multiplayer you
are hunting the other player" -- is a good hook attached to a generator that
cannot support it. The faces are hashes, and hashes do not have askable
attributes.

It is fixable, but the fix is not in the game. It is in the identity layer, and
it is a change to something that already ships and already looks right on six
screens. That is a real cost and it should be paid deliberately rather than
discovered halfway through building a game.

The version that does not pay it -- generate twenty-four faces, invent
attributes by inspecting them -- produces a board where the questions do not
reliably split the field, and a Guess Who where a question can eliminate one
character is not a game.
