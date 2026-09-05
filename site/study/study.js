/* The installer page: a thin state machine over the worker.
 *
 * The worker owns Pyodide and the Python tools; this file owns the DOM.
 * Nothing is fetched until the user offers a deck, because the runtime is
 * ~15MB and most visitors to the site never come here.
 */

(function () {
  "use strict";

  var $ = function (id) {
    return document.getElementById(id);
  };

  // Left column: the reader's slots. The menus hold the deck's own field
  // names. The label used to describe the opposite, which read as nonsense
  // on a deck with no "Part of speech".
  var SLOTS = [
    ["headword", "Word"],
    ["reading", "Reading / pronunciation"],
    ["meaning", "Meaning"],
    ["partOfSpeech", "Part of speech"],
    ["sentence", "Example sentence"],
    ["sentenceReading", "Sentence reading"],
    ["sentenceMeaning", "Sentence translation"],
  ];

  var worker = null;
  var workerReady = false;
  var pendingBuffer = null; // an .apkg waiting for the worker to come up
  var onReady = null; // one queued action for when the worker is up
  var epoch = 0; // bumped on every reset; stale worker replies are dropped
  var opened = null; // last open_apkg result
  var sourceName = ""; // the file the user gave us, echoed in the report
  var converted = null; // last convert result
  var chosenDeck = null;

  // ---- worker lifecycle ---------------------------------------------------

  var lastProgress = "starting the worker";
  var stallTimer = null;

  function armStallWatchdog() {
    if (stallTimer) clearTimeout(stallTimer);
    // Generous: a slow connection on a cold cache is a few seconds, and a
    // very slow one maybe thirty. A minute with no progress at all is not
    // slow, it is stuck, and silence is the worst possible answer.
    stallTimer = setTimeout(function () {
      if (workerReady) return;
      setError(
        "The Python runtime has not started in 60 seconds. It stalled at: " +
          lastProgress +
          ". Two usual causes: a firewall or the host's bot protection" +
          " challenging this tab (reloading clears it; if not, wait a few" +
          " minutes and reload again), or the browser -- this page needs" +
          " Chrome or Edge; Safari and Firefox may hang here. If neither" +
          " fits, open the browser console (View > Developer > JavaScript" +
          " Console) and send what it says.",
      );
    }, 60000);
  }

  function ensureWorker() {
    if (worker) return;
    armStallWatchdog();
    worker = new Worker("/study/worker.js");
worker.onmessage = function (event) {
      var msg = event.data;
      if (msg.type === "progress") {
        lastProgress = msg.text;
        armStallWatchdog();
        var note =
          msg.text.indexOf("Starting the Python runtime") === 0 ||
          msg.text.indexOf("Downloading the Python runtime") === 0
            ? " (first visit only; later runs are instant)"
            : "";
        setProgress(msg.text + "…" + note);
      } else if (msg.type === "ready") {
        workerReady = true;
        if (stallTimer) clearTimeout(stallTimer);
        if (pendingBuffer) {
          var buffer = pendingBuffer;
          pendingBuffer = null;
          worker.postMessage({ type: "open", buffer: buffer }, [buffer]);
        }
        if (onReady) {
          var queued = onReady;
          onReady = null;
          queued();
        }
      } else if (
        msg.epoch !== undefined &&
        msg.epoch !== epoch &&
        ["converted", "fonts", "fontline", "zip", "deckfiles"].indexOf(
          msg.type,
        ) >= 0
      ) {
        // A reply from before the last reset: the deck it describes is gone.
        return;
      } else if (msg.type === "opened") {
        onOpened(msg.result);
      } else if (msg.type === "converted") {
        onConverted(msg.result);
      } else if (msg.type === "fontline") {
        var progressLine = $("typeProgress");
        progressLine.hidden = false;
        progressLine.textContent = msg.text;
      } else if (msg.type === "fonts") {
        onFonts(msg.result);
      } else if (msg.type === "deckfiles") {
        onDeckFiles(msg.files);
      } else if (msg.type === "zip") {
        onZip(msg.buffer);
      } else if (msg.type === "error") {
        setError("Something broke (" + msg.for + "): " + msg.message);
      }
    };
    worker.onerror = function (event) {
      if (stallTimer) clearTimeout(stallTimer);
      setError(
        "The worker failed to start: " +
          (event.message || "no message") +
          (event.filename
            ? " (" + event.filename + ":" + event.lineno + ")"
            : "") +
          ". A firewall or the host's bot protection can block it -- reload" +
          " the page to retry, and wait a few minutes if it repeats. If the" +
          " message names a script error instead, this page needs Chrome or" +
          " Edge.",
      );
    };
    worker.postMessage({ type: "init" });
  }

  // ---- step 1: taking the file -------------------------------------------

  function takeFile(file) {
    if (!file) return;
    sourceName = file.name;
    $("openSummary").hidden = true;
    $("reportBody").hidden = true;
    $("samplePanel").hidden = true;
    $("reportVerdict").textContent = "";
    $("summaryFacts").textContent = "";
    $("skipNotice").hidden = true;
    resetDownstream();
    reachedStep = 1;
    goTo(1);
    setProgress(
      "Reading " +
        file.name +
        (file.size > 20 * 1024 * 1024
          ? " (" +
            Math.round(file.size / 1024 / 1024) +
            " MB, this takes a moment)"
          : "") +
        "…",
    );
    $("dropzone").classList.add("is-busy");
    file.arrayBuffer().then(function (buffer) {
      ensureWorker();
      if (workerReady) {
        worker.postMessage({ type: "open", buffer: buffer }, [buffer]);
      } else {
        pendingBuffer = buffer;
      }
    });
  }

  function setProgress(text) {
    var p = $("openProgress");
    p.textContent = text;
    p.classList.remove("is-error");
    p.classList.add("is-busy");
    $("openStatus").hidden = false;
  }

  function setError(text) {
    var p = $("openProgress");
    p.textContent = text;
    p.classList.remove("is-busy");
    p.classList.add("is-error");
    $("openStatus").hidden = false;
  }

  function onOpened(result) {
    if (result.error) {
      setError(result.error);
      return;
    }
    opened = result;
    $("openStatus").hidden = true;

    var decks = result.decks;
    $("summaryTitle").textContent =
      decks.length === 1
        ? decks[0].name
        : "This package holds " + decks.length + " decks";

    var choices = $("deckChoices");
    choices.innerHTML = "";
    if (decks.length > 1) {
      decks.forEach(function (deck, index) {
        var label = document.createElement("label");
        label.className = "study-deck-choice";
        var radio = document.createElement("input");
        radio.type = "radio";
        radio.name = "deck";
        radio.value = deck.name;
        radio.checked = index === 0;
        radio.addEventListener("change", function () {
          convert(deck.name, null);
        });
        var count = document.createElement("span");
        count.className = "study-deck-cards";
        count.textContent = deck.cards + " cards";
        label.appendChild(radio);
        label.appendChild(document.createTextNode(deck.name));
        label.appendChild(count);
        choices.appendChild(label);
      });
    }

    $("sourceFile").textContent = sourceName ? "from " + sourceName : "";
    $("openProgress").classList.remove("is-busy");
    $("dropzone").classList.remove("is-busy");

    var warn = $("summaryWarn");
    if (result.cardsWithState === 0 && result.reviews === 0) {
      warn.textContent =
        "No review history came along, so every card starts new. That is" +
        " normal for a deck downloaded from AnkiWeb. If this is your own" +
        " deck and you wanted its history, export it again from Anki with" +
        ' "Include scheduling information" checked.';
      warn.hidden = false;
    } else {
      warn.hidden = true;
    }

    $("openSummary").hidden = false;
    convert(decks[0].name, null);
  }

  // ---- step 2: conversion and the report ---------------------------------

  function convert(deckName, mapping) {
    chosenDeck = deckName;
    resetDownstream();
    $("reportPlaceholder").hidden = false;
    $("reportPlaceholder").textContent = "Converting…";
    $("reportBody").hidden = true;
    $("samplePanel").hidden = true;
    if (reachedStep >= 2) goTo(2);
    worker.postMessage({
      type: "convert",
      epoch: epoch,
      deck: deckName,
      mapping: mapping,
      // Only a package carrying pictures needs the image packer downloaded.
      needsImages: !!(opened && opened.pictures > 0),
    });
  }

  // check_deck's labels, in the words a person would use, and whether the
  // problem is fatal to those cards or merely worth knowing.
  var PROBLEM_WORDS = {
    "a card whose faces do not work as a question and an answer": function (n) {
      return (
        n +
        (n === 1 ? " card arrives" : " cards arrive") +
        " with nothing to reveal, or with the answer already showing in" +
        " the question."
      );
    },
    "the question and the answer are the same text, deck-wide": function () {
      return (
        "On most cards the question and the answer are the same words, which" +
        " usually means one field is filling both. Check the dropdowns."
      );
    },
    "a reading or meaning the reader cannot draw": function (n) {
      return (
        n +
        (n === 1 ? " card has a reading" : " cards have readings") +
        " or meanings in a script the reader draws only in its built-in" +
        " type, which covers Latin letters. Those lines arrive blank. The" +
        " reader supports English and Chinese decks; other scripts are not" +
        " supported."
      );
    },
    "a character no installed font can draw at all": function (n) {
      return (
        n +
        (n === 1 ? " card uses characters" : " cards use characters") +
        " the reader has no font for, and would come out blank. Pick a font" +
        " file below that covers this language."
      );
    },
    "a glyph the headword face cannot draw": function (n) {
      return (
        n +
        (n === 1 ? " card has a character" : " cards have characters") +
        " the chosen font cannot draw. They would come out blank."
      );
    },
    "a Latin glyph the built-in serif cannot draw": function (n) {
      return (
        n +
        (n === 1 ? " card uses a character" : " cards use characters") +
        " outside the reader's built-in type. Those characters go missing."
      );
    },
    "headword too wide for the screen": function (n) {
      return n + " word(s) are too wide for the screen and will be cut.";
    },
  };

  function describeProblems(problems) {
    var lines = [];
    Object.keys(problems || {}).forEach(function (label) {
      var count = problems[label];
      var say = PROBLEM_WORDS[label];
      lines.push(say ? say(count) : count + " x " + label);
    });
    return lines;
  }

  function onConverted(result) {
    if (result.error) {
      $("reportPlaceholder").hidden = true;
      $("reportBody").hidden = false;
      reachedStep = Math.max(reachedStep, 2);
      goTo(2);
      // The plain-English reason belongs in the headline. A deck that
      // converts to nothing at all is now almost always one whose cloze
      // cards are all empty -- the hole was edited out and Anki left the
      // card behind -- so that is the sentence worth having.
      var why = "";
      var cloze = /(\d+) empty cloze card\(s\) skipped/.exec(result.error || "");
      if (cloze) {
        why =
          " All " +
          cloze[1] +
          " cards are empty cloze cards: their hole is no longer in the" +
          ' text. Anki deletes these under Tools > "Empty Cards".';
      }
      $("reportVerdict").textContent = "This deck did not convert." + why;
      $("reportVerdict").className = "study-verdict is-bad";
      // The tool's advice is a command line; there is no command line here.
      $("convertLog").textContent = (result.error || "").replace(
        /If the generic guess picked the wrong fields[\s\S]*$/,
        "",
      );
      $("checkLog").textContent = "";
      $("samplePanel").hidden = true;
      $("skipNotice").hidden = true;
      $("summaryFacts").textContent = "";
      $("summaryWarn").hidden = true;
      // Nothing was produced, so there is nowhere to go: a live Next button
      // here walked the user onto an empty step with no way back but the tabs.
      $("next2").disabled = true;
      reachedStep = 2;
      goTo(2);
      return;
    }
    converted = result;

    $("reportPlaceholder").hidden = true;
    $("reportBody").hidden = false;
    reachedStep = Math.max(reachedStep, 4);
    if (currentStep < 2) goTo(2);
    else goTo(currentStep);

    var verdict = $("reportVerdict");
    if (
      result.checkFailed ||
      (result.problems && Object.keys(result.problems).length)
    ) {
      verdict.textContent = "Converted, but read this first:";
      verdict.className = "study-verdict is-bad";
    } else {
      verdict.textContent = "Every card renders.";
      verdict.className = "study-verdict is-good";
    }

    var facts = [];
    if (result.cards !== null) {
      facts.push(result.cards + " cards will go on the device");
    }
    if (result.withState > 0) {
      facts.push(result.withState + " arrive with their review history");
    }
    if (result.newPerDay) {
      facts.push(
        result.newPerDay +
          " new cards a day, this deck's own Anki setting (change it in Anki's" +
          " deck options and convert again)",
      );
    }
    if (result.imagesPacked > 0) {
      facts.push(
        result.imagesPacked +
          (result.imagesPacked === 1 ? " image" : " images") +
          " packed",
      );
    }
    if (opened && opened.fonts.length) {
      facts.push("fonts found in the package: " + opened.fonts.join(", "));
    }
    $("summaryFacts").textContent =
      facts.join(" · ") + (facts.length ? "." : "");

    var notes = [];
    if (result.skipped > 0) {
      notes.push(
        result.skipped +
          (result.skipped === 1
            ? " card stays behind: "
            : " cards stay behind: ") +
          (result.clozeEmpty > 0
            ? "their cloze hole is no longer in the text, so there is nothing" +
              ' to ask. Anki deletes these under Tools > "Empty Cards".'
            : "their note type could not be read."),
      );
    }
    // What the package carried that the reader cannot use. Silence here read
    // as "nothing was lost" for a deck whose answers were all photographs.
    if (opened && opened.pictures > 0 && !result.imagesPacked) {
      // States the fact and not the consequence. The page knows how many
      // picture FILES the package carries; it does not know how many CARDS
      // reference one, because the packer reports that only as prose in the
      // log. The old wording asserted "cards ... arrive blank" for a deck
      // where the log said 0 packed, 0 unreadable and 33 cards without one --
      // three numbers that cannot produce a blank card between them. A reader
      // comparing the two had no way to tell which was lying.
      notes.push(
        opened.pictures +
          (opened.pictures === 1 ? " picture is" : " pictures are") +
          " in the package and not carried over. A card whose answer was ONLY" +
          " a picture would arrive blank; the images line in the log below" +
          " says how many cards actually use one.",
      );
    }
    if (opened && opened.audio > 0) {
      notes.push(
        opened.audio + " sound(s) are dropped: the reader has no speaker.",
      );
    }
    // Everything above is true of the DECK and stays true whatever font is
    // chosen next: cards that stay behind, pictures not carried over, sounds
    // dropped. Everything describeProblems adds is about the chosen font and
    // is rebuilt with it. Split them by where they came from rather than by
    // matching their words -- an allowlist of substrings kept "sound(s)" and
    // "picture(s)" and silently dropped the cards-stay-behind line, so a user
    // who picked a font before reading the summary was never told a card had
    // been left out of their deck at all. A disclosure about lost data cannot
    // depend on an unrelated choice.
    permanentNotes = notes.slice();
    describeProblems(result.problems).forEach(function (line) {
      notes.push(line);
    });
    var notice = $("skipNotice");
    notice.textContent = notes.join(" ");
    notice.hidden = notes.length === 0;

    $("convertLog").textContent =
      result.log + (result.imagesLog ? "\n" + result.imagesLog : "");
    $("checkLog").textContent = result.checkLog;
    // Every card broken is not a warning, it is a failure: a deck of blank
    // answers would be written to the card and reviewed as nothing. The
    // cloze path already stops; this makes the photo-answer path stop too.
    deckIsUnusable =
      result.cards > 0 &&
      (result.problems || {})[
        "a card whose faces do not work as a question and an answer"
      ] >= result.cards;
    var brokenAll = deckIsUnusable;
    $("next2").disabled = brokenAll;
    if (brokenAll) {
      verdict.textContent =
        "Every card in this deck comes out blank, so there is nothing to" +
        " write.";
      verdict.className = "study-verdict is-bad";
    }
    sampleList = result.samples && result.samples.length ? result.samples : [];
    sampleAt = 0;
    flaggedTotal = 0;
    Object.keys(result.problems || {}).forEach(function (k) {
      flaggedTotal += result.problems[k];
    });
    fillSample(sampleList.length ? sampleList[0] : result.sample);
    buildMapGrid(result);

    enableDownstream();
  }

  function buildMapGrid(result) {
    var grid = $("mapGrid");
    grid.innerHTML = "";
    if (!result.fields || !result.fields.length) {
      $("samplePanel").hidden = true;
      return;
    }
    SLOTS.forEach(function (slot) {
      var label = document.createElement("label");
      label.textContent = slot[1];
      var select = document.createElement("select");
      select.dataset.slot = slot[0];
      var none = document.createElement("option");
      none.value = "";
      none.textContent = "(not shown)";
      select.appendChild(none);
      result.fields.forEach(function (field) {
        var option = document.createElement("option");
        option.value = field;
        option.textContent = field;
        select.appendChild(option);
      });
      select.value = (result.guess && result.guess[slot[0]]) || "";
      select.addEventListener("change", function () {
        convert(chosenDeck, mappingFromGrid());
      });
      label.appendChild(select);
      grid.appendChild(label);
    });
    var note = $("mapNote");
    if (result.noteTypes > 1) {
      note.textContent =
        "This deck mixes " +
        result.noteTypes +
        " kinds of cards; these choices apply to all of them, so only" +
        " change things if the card on the left looks wrong.";
      note.hidden = false;
    } else {
      note.hidden = true;
    }
  }

  function mappingFromGrid() {
    // Every slot is sent, cleared ones included: sending only the filled
    // ones let the converter re-guess the blanks, so "(not shown)" silently
    // snapped back to whatever it had picked before.
    var mapping = [];
    var selects = $("mapGrid").querySelectorAll("select");
    selects.forEach(function (select) {
      mapping.push(select.dataset.slot + "=" + select.value);
    });
    return mapping.length ? mapping : null;
  }

  var sampleList = [];
  var sampleAt = 0;
  var flaggedTotal = 0;
  var permanentNotes = [];
  // Set when every card converts to a blank answer: there is nothing to
  // preview and nothing worth writing, and enableDownstream must not undo it.
  var deckIsUnusable = false;

  function fillSample(sample) {
    if (!sample) {
      $("samplePanel").hidden = true;
      return;
    }
    $("samplePanel").hidden = false;
    // A cloze card has no headword and no example sentence: its question is
    // the text with the hole and its answer is the same text filled in. Laid
    // into the same slots rather than given a panel of its own, so the
    // preview still reads top to bottom as question then answer -- and so
    // that a cloze deck does not preview as an empty card, which is what it
    // did when every slot it fills was one this list did not read.
    var parts = sample.clozeQuestion
      ? [
          ["sampleWord", sample.clozeQuestion],
          ["sampleReading", ""],
          ["sampleMeaning", sample.sentence],
          ["samplePos", ""],
          ["sampleSentence", ""],
          ["sampleSentenceMeaning", sample.meaning],
        ]
      : [
          ["sampleWord", sample.headword],
          ["sampleReading", sample.reading],
          ["sampleMeaning", sample.meaning],
          ["samplePos", sample.partOfSpeech],
          ["sampleSentence", sample.sentence],
          ["sampleSentenceMeaning", sample.sentenceMeaning],
        ];
    parts.forEach(function (pair) {
      var el = $(pair[0]);
      el.textContent = pair[1] || "";
      el.hidden = !pair[1];
    });

    // Which card this is, and a way to see another. One card is a sample, not
    // a proof; the ones the checker flagged are shown first because those are
    // where a wrong mapping actually shows up.
    var label = $("sampleWhich");
    label.innerHTML = "";
    if (sampleList.length > 1) {
      var flagged = sampleList.filter(function (c) {
        return c.flagged;
      }).length;
      var totalFlagged = flaggedTotal || flagged;
      label.appendChild(
        document.createTextNode(
          "Card " +
            (sample.index + 1) +
            (sample.reason ? " -- " + sample.reason + ". " : ". ") +
            (flagged
              ? "Showing " + flagged + " of " + totalFlagged + " flagged. "
              : ""),
        ),
      );
      var next = document.createElement("button");
      next.type = "button";
      next.className = "linklike";
      next.textContent = "Show me another card";
      next.addEventListener("click", function () {
        sampleAt = (sampleAt + 1) % sampleList.length;
        fillSample(sampleList[sampleAt]);
      });
      label.appendChild(next);
    }
  }

  // ---- step 3: the type ---------------------------------------------------

  var fonts = null; // last build_fonts result

  function startTypeStep() {
    $("typePlaceholder").hidden = true;
    $("typeBody").hidden = false;
    $("typeLog").hidden = true;
    $("typeProgress").hidden = true;

    // A deck the built-in face cannot draw gets the bundled CJK face built
    // for it straight away, rather than being told what it cannot have.
    var needsFont =
      converted.hasCjk ||
      (converted.problems &&
        converted.problems["a character no installed font can draw at all"]);
    if (needsFont && !(opened && opened.fonts.length)) {
      $("typeChoices").hidden = false;
      var cjk = faceRadio("cjk");
      if (cjk && !cjk.checked) {
        cjk.checked = true;
        faceBefore = "cjk";
        fetch("/study/NotoSansCJK.otf")
          .then(function (response) {
            return response.arrayBuffer();
          })
          .then(function (buffer) {
            buildFonts("custom", buffer);
          });
      }
      return;
    }

    if (converted.hasCjk) {
      $("typeChoices").hidden = !!(opened && opened.fonts.length);
      if (opened && opened.fonts.length > 0) {
        buildFonts("cjk", null);
      } else {
        var log = $("typeLog");
        log.hidden = false;
        log.textContent =
          "This deck needs a font for its characters, and the package" +
          " carries none.\n\nTwo ways forward: pick a font file yourself" +
          " above (any .ttf covering the script works, and the page fits it" +
          " to your longest word), or in Anki add the faces your template" +
          " uses to the collection's media (files named like _simsun.ttf)" +
          " and export again.";
      }
      return;
    }
    $("typeChoices").hidden = false;
  }

  function buildFonts(mode, ttfBuffer) {
    var progressLine = $("typeProgress");
    progressLine.hidden = false;
    progressLine.textContent =
      "Building faces… (a Chinese deck takes a minute)";
    $("typeLog").hidden = true;
    var message = { type: "fonts", epoch: epoch, mode: mode };
    if (ttfBuffer) {
      message.ttf = ttfBuffer;
      worker.postMessage(message, [ttfBuffer]);
    } else {
      worker.postMessage(message);
    }
  }

  function onFonts(result) {
    $("typeProgress").hidden = true;
    var log = $("typeLog");
    log.hidden = false;
    if (!converted) return; // a reset won the race; nothing to attach this to
    if (result.error) {
      log.textContent = result.error;
      // A Latin deck draws fine in the built-in face; a CJK deck without its
      // build stays unwritable rather than unreadable.
      if (!converted.hasCjk) {
        $("writeCard").disabled = !canPickFolders;
        $("writeZip").disabled = false;
        $("previewBoot").disabled = false;
        setWriteStatus("");
      } else {
        setWriteStatus("The font build failed, so this deck stays unwritten.");
      }
      return;
    }
    $("writeCard").disabled = deckIsUnusable || !canPickFolders;
    $("writeZip").disabled = deckIsUnusable;
    $("previewBoot").disabled = deckIsUnusable;
    if ($("writeStatus").textContent.indexOf("Waiting") === 0)
      setWriteStatus("");
    fonts = result;
    converted.files = result.files;
    log.textContent = result.log;

    // The checker's verdict, now against the real faces.
    $("checkLog").textContent = result.checkLog;
    var verdict = $("reportVerdict");
    var fontProblems = describeProblems(result.problems);
    if (result.checkFailed || fontProblems.length) {
      verdict.textContent = "Converted, but read this first:";
      verdict.className = "study-verdict is-bad";
    } else if (permanentNotes.length) {
      // "Every card renders." sat three lines above "1 card stays behind:
      // cloze cards have a hole in the question", on the same screen. Both
      // were true of different sets and the pair reads as the page
      // contradicting itself, which costs more trust than the skipped card
      // costs anything.
      verdict.textContent = "Every card that converted renders.";
      verdict.className = "study-verdict is-good";
    } else {
      verdict.textContent = "Every card renders.";
      verdict.className = "study-verdict is-good";
    }
    var notice = $("skipNotice");
    var all = permanentNotes.concat(fontProblems);
    notice.textContent = all.join(" ");
    notice.hidden = all.length === 0;

    // The per-card flags came from the pass BEFORE the fonts existed. Left
    // alone they kept saying "needs a font, and none is installed" beside a
    // headline reading "Every card renders."
    flaggedTotal = 0;
    Object.keys(result.problems || {}).forEach(function (k) {
      flaggedTotal += result.problems[k];
    });
    if (!flaggedTotal) {
      sampleList = sampleList.map(function (card) {
        return Object.assign({}, card, { flagged: false, reason: "" });
      });
      sampleList = sampleList.filter(function (card, i) {
        return i === 0;
      });
      sampleAt = 0;
      fillSample(sampleList[0]);
    }
  }

  // ---- step 4: the preview ------------------------------------------------

  var previewFrame = null;
  var previewListener = null;

  function killPreview() {
    if (previewListener) {
      window.removeEventListener("message", previewListener);
      previewListener = null;
    }
    if (previewFrame) {
      previewFrame.remove();
      previewFrame = null;
    }
    $("previewMount").hidden = true;
    $("previewStatus").textContent = "";
  }

  function bootPreview() {
    if (!converted) return;
    $("previewStatus").textContent = "Collecting the deck…";
    withDeckFiles(function (files) {
      killPreview();
      var mount = $("previewMount");
      mount.hidden = false;
      previewFrame = document.createElement("iframe");
      previewFrame.className = "study-preview-frame";
      previewFrame.title = "The device, running your deck";
      previewFrame.src = "/study/preview.html";
      mount.appendChild(previewFrame);

      var slug = converted.slug;
      var payload = {};
      var transfers = [];
      Object.keys(files).forEach(function (name) {
        payload["/fs_/study/" + slug + "/" + name] = files[name];
        transfers.push(files[name]);
      });
      var last = new TextEncoder().encode(slug).buffer;
      payload["/fs_/study/.last"] = last;
      transfers.push(last);

      var frame = previewFrame;
      var listener = function (event) {
        if (event.origin !== location.origin || !event.data) return;
        if (!frame.contentWindow || event.source !== frame.contentWindow)
          return;
        if (event.data.type === "preview-waiting") {
          previewFrame.contentWindow.postMessage(
            { type: "boot", files: payload },
            location.origin,
            transfers,
          );
          var cards = (converted && converted.cards) || 0;
          $("previewStatus").textContent =
            "Booting the firmware…" +
            (cards > 800
              ? " a deck this size takes a minute or so to load on the device"
              : " about ten seconds");
        } else if (event.data.type === "preview-ready") {
          $("previewStatus").textContent = "Booted.";
          window.removeEventListener("message", listener);
        } else if (event.data.type === "preview-error") {
          $("previewStatus").textContent =
            "The preview did not start: " + event.data.message;
          window.removeEventListener("message", listener);
        }
      };
      previewListener = listener;
      window.addEventListener("message", listener);
    });
  }

  // ---- downstream state ---------------------------------------------------

  function resetDownstream() {
    epoch++;
    deckIsUnusable = false;
    converted = null;
    fonts = null;
    var builtin = document.querySelector('input[name="face"][value="builtin"]');
    if (builtin) builtin.checked = true;
    faceBefore = "builtin";
    var ownName = $("ownName");
    if (ownName) ownName.textContent = "";
    killPreview();
    $("typePlaceholder").hidden = false;
    $("typeBody").hidden = true;
    $("previewPlaceholder").hidden = false;
    $("previewBody").hidden = true;
    $("writePlaceholder").hidden = false;
    $("writeBody").hidden = true;
  }

  function enableDownstream() {
    startTypeStep();
    $("previewPlaceholder").hidden = true;
    $("previewBody").hidden = false;
    $("writePlaceholder").hidden = true;
    $("writeBody").hidden = false;
    if (canPickFolders) $("writeStatus").textContent = "";
    // A CJK deck written without its faces is a deck the device cannot draw:
    // hold the write until the build lands (onFonts lifts this).
    var needsFonts = converted.hasCjk && opened && opened.fonts.length > 0;
    var block = needsFonts || deckIsUnusable;
    $("writeCard").disabled = block || !canPickFolders;
    $("writeZip").disabled = block;
    $("previewBoot").disabled = block;
    if (needsFonts) setWriteStatus("Waiting for the faces to build…");
    else if (deckIsUnusable)
      setWriteStatus(
        "Nothing to write: every card in this deck comes out blank.",
      );
  }

  function onDeckFiles(files) {
    if (deckFilesWaiter) {
      var waiter = deckFilesWaiter;
      deckFilesWaiter = null;
      waiter(files);
    }
  }

  var deckFilesWaiter = null;

  function withDeckFiles(callback) {
    if (!converted) return;
    deckFilesWaiter = callback;
    worker.postMessage({
      type: "deckfiles",
      epoch: epoch,
      names: converted.files,
    });
  }

  // ---- step 5: writing to the card ---------------------------------------

  var canPickFolders = "showDirectoryPicker" in window;

  function setWriteStatus(text) {
    $("writeStatus").textContent = text;
  }

  async function writeToCard() {
    if (!converted) return;
    var root;
    try {
      root = await window.showDirectoryPicker({ mode: "readwrite" });
    } catch (e) {
      // Only AbortError is the user closing the picker. Everything else is the
      // call failing -- and treating them alike made this button a silent
      // no-op: the API is present, the branch is taken, the call rejects, and
      // nothing appears. No dialog, no text, not even a console line. A person
      // clicks it, waits a minute, clicks again, and has no reason to connect
      // it to the zip sentence in the paragraph above.
      if (e && e.name === "AbortError") {
        setWriteStatus("Left the card untouched.");
        return;
      }
      console.error("showDirectoryPicker failed", e);
      setWriteStatus(
        "This browser would not open a folder picker (" +
          ((e && e.name) || "unknown error") +
          "). Use the zip button instead -- it unpacks at the card's root and " +
          "produces exactly the same thing.",
      );
      return;
    }
    var slug = converted.slug;
    try {
      var study = await root.getDirectoryHandle("study", { create: true });
      var existing = null;
      try {
        existing = await study.getDirectoryHandle(slug);
      } catch (e) {}
      if (existing) {
        var oldLog = null;
        try {
          oldLog = await (
            await (await existing.getFileHandle("revlog.dat")).getFile()
          ).arrayBuffer();
        } catch (e) {}
        if (oldLog && oldLog.byteLength > 0) {
          var records = Math.floor(oldLog.byteLength / 32);
          var go = window.confirm(
            "This card already has a deck named " +
              slug +
              " carrying " +
              records +
              " review record(s), possibly not yet synced back to Anki. " +
              "Replacing the deck erases them. Sync first (the step below) " +
              "if unsure, or press OK to replace it whole.",
          );
          if (!go) {
            setWriteStatus("Left the card untouched.");
            return;
          }
        }
        // Replace, not merge: stale fonts or images surviving underneath a
        // new conversion is exactly the surprise the report step rules out.
        await study.removeEntry(slug, { recursive: true });
      }

      setWriteStatus("Collecting the deck…");
      withDeckFiles(async function (files) {
        try {
          var deckDir = await study.getDirectoryHandle(slug, { create: true });
          var names = Object.keys(files);
          var written = 0;
          var bytes = 0;
          for (var i = 0; i < names.length; i++) {
            var parts = names[i].split("/");
            var dir = deckDir;
            for (var j = 0; j < parts.length - 1; j++) {
              dir = await dir.getDirectoryHandle(parts[j], { create: true });
            }
            var fileHandle = await dir.getFileHandle(parts[parts.length - 1], {
              create: true,
            });
            var writable = await fileHandle.createWritable();
            await writable.write(files[names[i]]);
            await writable.close();
            written++;
            bytes += files[names[i]].byteLength;
            setWriteStatus("Writing " + names[i] + "…");
          }
          setWriteStatus(
            "Done: " +
              written +
              " files, " +
              (bytes / 1024 / 1024).toFixed(1) +
              " MB under study/" +
              slug +
              "/. Eject the card, put it in the reader: Apps > STUDY.",
          );
        } catch (e) {
          setWriteStatus("Writing failed: " + (e.message || e));
        }
      });
    } catch (e) {
      setWriteStatus(
        "Could not open that folder for writing: " + (e.message || e),
      );
    }
  }

  function downloadZip() {
    if (!converted) return;
    setWriteStatus("Packing the zip…");
    worker.postMessage({ type: "zip", epoch: epoch, slug: converted.slug });
  }

  function onZip(buffer) {
    // The click may be swallowed (throttling, a blocked popup, a full disk)
    // and the page cannot see the file land. It used to print unpack
    // instructions unconditionally, so a failed download looked like a
    // finished one. Offer a real link that stays on the page instead.
    var name = converted.slug + "-deck.zip";
    var blob = new Blob([buffer], { type: "application/zip" });
    var url = URL.createObjectURL(blob);
    var a = document.createElement("a");
    a.href = url;
    a.download = name;
    a.click();

    var status = $("writeStatus");
    status.textContent = "";
    status.appendChild(
      document.createTextNode(
        // Says what it KNOWS, not where the file went. The page cannot see the
        // filesystem: it asked for this name and size, and the browser may
        // have renamed it, put it elsewhere, or left an older download of the
        // same name sitting there. Asserting the name and folder sent a
        // returning user to a three-week-old zip that matched the description
        // exactly, and they shipped it believing they had followed the page.
        "Your browser has been sent a deck named " +
          name +
          " (" +
          Math.max(1, Math.round(buffer.byteLength / 1024)) +
          " KB), saved wherever it puts downloads. Check the timestamp is from " +
          "just now -- an older download of the same name may still be there. " +
          "Unpack it at the ROOT of the SD card, so it creates study/" +
          converted.slug +
          "/. Then: Apps > STUDY. Nothing there? ",
      ),
    );
    var again = document.createElement("a");
    again.href = url;
    again.download = name;
    again.textContent = "Download it again";
    status.appendChild(again);
    // The object URL is deliberately not revoked: the fallback link above is
    // the whole point, and it dies with the page anyway.
  }

  // ---- wiring -------------------------------------------------------------

  var dropzone = $("dropzone");
  var filepick = $("filepick");

  dropzone.addEventListener("click", function () {
    filepick.click();
  });
  dropzone.addEventListener("keydown", function (event) {
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      filepick.click();
    }
  });
  filepick.addEventListener("change", function () {
    takeFile(filepick.files[0]);
    filepick.value = "";
  });

  ["dragenter", "dragover"].forEach(function (name) {
    dropzone.addEventListener(name, function (event) {
      event.preventDefault();
      dropzone.classList.add("dragging");
    });
  });
  ["dragleave", "drop"].forEach(function (name) {
    dropzone.addEventListener(name, function (event) {
      event.preventDefault();
      dropzone.classList.remove("dragging");
    });
  });
  dropzone.addEventListener("drop", function (event) {
    // Stop the document-level drop handler below from also firing on this same
    // drop: without it a deck dropped on the zone bubbles up and runs takeFile
    // a second time. Drops elsewhere on the page never reach here, so the
    // page-wide fallback still works.
    event.stopPropagation();
    var file = event.dataTransfer && event.dataTransfer.files[0];
    takeFile(file);
  });

  function loadFromUrl(url, label) {
    setProgress("Fetching " + label + "…");
    fetch(url)
      .then(function (response) {
        if (!response.ok)
          throw new Error(label + " did not load: " + response.status);
        return response.blob();
      })
      .then(function (blob) {
        takeFile(new File([blob], label));
      })
      .catch(function (error) {
        setError(String(error.message || error));
      });
  }

  $("sample").addEventListener("click", function () {
    loadFromUrl("/study/demo/sat-vocabulary.apkg", "sat-vocabulary.apkg");
  });

  $("previewBoot").addEventListener("click", bootPreview);
  $("next2").addEventListener("click", function () {
    goTo(3);
  });
  $("next3").addEventListener("click", function () {
    goTo(4);
  });

  // A deck dropped anywhere on the page counts: a fidgety user on step 3
  // should not need to find their way back to step 1 first.
  document.addEventListener("dragover", function (event) {
    event.preventDefault();
  });
  document.addEventListener("drop", function (event) {
    event.preventDefault();
    var file = event.dataTransfer && event.dataTransfer.files[0];
    if (file) takeFile(file);
  });

  $("writeCard").addEventListener("click", writeToCard);
  $("writeZip").addEventListener("click", downloadZip);
  if (!canPickFolders) {
    $("writeCard").disabled = true;
    setWriteStatus(
      "This browser cannot write folders (Chrome and Edge can); use the zip.",
    );
  }

  // The face radios: built-in does nothing (there is nothing to build),
  // bundled fetches the serif this site ships, own asks for a TTF. The
  // previous selection is remembered so cancelling the file picker does not
  // leave a radio claiming a font that never loaded.
  var faceBefore = "builtin";

  function faceRadio(value) {
    return document.querySelector('input[name="face"][value="' + value + '"]');
  }

  document.querySelectorAll('input[name="face"]').forEach(function (radio) {
    radio.addEventListener("change", function () {
      if (!converted) return;
      if (radio.value === "builtin") {
        faceBefore = "builtin";
        $("ownName").textContent = "";
        fonts = null;
        $("typeLog").hidden = true;
        $("typeProgress").hidden = true;
        convert(chosenDeck, null); // back to the fontless deck and report
      } else if (radio.value === "bundled" || radio.value === "cjk") {
        faceBefore = radio.value;
        $("ownName").textContent = "";
        fetch(
          radio.value === "cjk"
            ? "/study/NotoSansCJK.otf"
            : "/study/DejaVuSerif.ttf",
        )
          .then(function (response) {
            return response.arrayBuffer();
          })
          .then(function (buffer) {
            buildFonts("custom", buffer);
          });
      } else if (radio.value === "own") {
        $("ttfpick").click();
      }
    });
  });
  $("ttfChoose").addEventListener("click", function (event) {
    event.preventDefault();
    if (converted) $("ttfpick").click();
  });
  $("ttfpick").addEventListener("change", function () {
    var file = $("ttfpick").files[0];
    $("ttfpick").value = "";
    if (!file) {
      // Cancelled: fall back to whatever was really in effect.
      faceRadio(faceBefore).checked = true;
      return;
    }
    faceBefore = "own";
    faceRadio("own").checked = true;
    $("ownName").textContent = file.name;
    file.arrayBuffer().then(function (buffer) {
      buildFonts("custom", buffer);
    });
  });

  // Same-origin only, so a link cannot make this page fetch from elsewhere:
  // ?deck=demo/sat-vocabulary.apkg resolves under /study/.
  var params = new URLSearchParams(location.search);
  var deckParam = params.get("deck");
  if (deckParam) {
    var resolved = new URL(deckParam, location.origin + "/study/");
    if (resolved.origin === location.origin) {
      loadFromUrl(resolved.pathname, resolved.pathname.split("/").pop());
    }
  }

  // ---- the wizard shell ---------------------------------------------------
  // One viewport, one step at a time. This block owns only visibility; the
  // state machine above neither knows nor cares which step is on screen.

  var currentStep = 1;
  var reachedStep = 1;
  var stepSections = {
    1: $("stepDeck"),
    2: $("stepCheck"),
    3: $("stepTry"),
    4: $("stepWrite"),
  };
  var stepButtons = document.querySelectorAll("#stepper button");

  function goTo(step) {
    var changed = step !== currentStep;
    currentStep = step;
    if (step > reachedStep) reachedStep = step;
    Object.keys(stepSections).forEach(function (key) {
      stepSections[key].classList.toggle("is-current", Number(key) === step);
    });
    stepButtons.forEach(function (button) {
      var mine = Number(button.dataset.step);
      button.classList.toggle("is-current", mine === step);
      button.classList.toggle("is-done", mine < step);
      button.disabled = mine > reachedStep;
      if (mine === step) button.setAttribute("aria-current", "step");
      else button.removeAttribute("aria-current");
    });
    // A step change is a new screen, and it has to arrive at the top of itself.
    // The PAGE is the scroll container now; it used to be the step box, which
    // had its own overflow and its own scrollTop reset in the converter. When
    // the box stopped clipping, that reset went quietly dead and nothing took
    // its place: reading step 2 to the bottom and pressing Next put step 3 on
    // screen at scrollY 380, with the stepper and the title above the fold.
    //
    // Only on a real change. The converter calls goTo(currentStep) to redraw
    // the step somebody is already reading, and yanking the page to the top
    // under them mid-sentence is its own bug.
    if (changed) window.scrollTo(0, 0);
  }

  stepButtons.forEach(function (button) {
    button.addEventListener("click", function () {
      goTo(Number(button.dataset.step));
    });
  });
  // Exposed for the write step (phase 4) and tests; harmless otherwise.
  window.StudyInstaller = {
    withDeckFiles: withDeckFiles,
    state: function () {
      return {
        opened: opened,
        converted: converted,
        fonts: fonts,
        deck: chosenDeck,
      };
    },
  };
})();
