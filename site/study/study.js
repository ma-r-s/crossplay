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

  function ensureWorker() {
    if (worker) return;
    worker = new Worker("/study/worker.js");
    worker.onmessage = function (event) {
      var msg = event.data;
      if (msg.type === "progress") {
        var note =
          msg.text.indexOf("Starting the Python runtime") === 0
            ? " (first visit downloads the converter, ~15 MB; later runs are instant)"
            : "";
        setProgress(msg.text + "…" + note);
      } else if (msg.type === "ready") {
        workerReady = true;
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
      } else if (msg.type === "syncfiles") {
        onSyncStaged();
      } else if (msg.type === "sync") {
        onSyncDone(msg.result);
      } else if (msg.type === "syncfile") {
        onSyncFile(msg.buffer);
      } else if (msg.type === "error") {
        setError("Something broke (" + msg.for + "): " + msg.message);
      }
    };
    worker.onerror = function (event) {
      setError("The worker failed to start: " + event.message);
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
    setProgress("Reading " + file.name + "…");
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

    var warn = $("summaryWarn");
    if (result.cardsWithState === 0 && result.reviews === 0) {
      warn.textContent =
        "No scheduling information came along, so every card will start new." +
        " If this deck has history in Anki, re-export it with" +
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
    });
  }

  // check_deck's labels, in the words a person would use, and whether the
  // problem is fatal to those cards or merely worth knowing.
  var PROBLEM_WORDS = {
    "a card whose faces do not work as a question and an answer": function (n) {
      return (
        n +
        (n === 1 ? " card arrives" : " cards arrive") +
        " broken: nothing to reveal, or the answer already showing in the" +
        " question. Usually the deck itself has those gaps."
      );
    },
    "the question and the answer are the same text, deck-wide": function () {
      return (
        "On most cards the question and the answer are the same words, which" +
        " usually means one field is filling both. Check the dropdowns."
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
      $("reportVerdict").textContent = "This deck did not convert.";
      $("reportVerdict").className = "study-verdict is-bad";
      $("convertLog").textContent = result.error;
      $("checkLog").textContent = "";
      $("samplePanel").hidden = true;
      $("skipNotice").hidden = true;
      $("summaryFacts").textContent = "";
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
    if (result.checkFailed || (result.problems && Object.keys(result.problems).length)) {
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
    $("summaryFacts").textContent = facts.join(" · ") + (facts.length ? "." : "");

    var notes = [];
    if (result.skipped > 0) {
      notes.push(
        result.skipped +
          (result.skipped === 1 ? " card stays behind: " : " cards stay behind: ") +
          (result.clozeSkipped > 0
            ? "cloze cards have a hole in the question, and this card format" +
              " has nowhere to put a hole. They stay in Anki."
            : "their note type could not be read."),
      );
    }
    // What the package carried that the reader cannot use. Silence here read
    // as "nothing was lost" for a deck whose answers were all photographs.
    if (opened && opened.pictures > 0 && !result.imagesPacked) {
      notes.push(
        opened.pictures +
          " picture(s) in this deck are not carried over; cards whose answer" +
          " was only a picture arrive blank.",
      );
    }
    if (opened && opened.audio > 0) {
      notes.push(
        opened.audio +
          " sound(s) are dropped: the reader has no speaker.",
      );
    }
    describeProblems(result.problems).forEach(function (line) {
      notes.push(line);
    });

    var notice = $("skipNotice");
    notice.textContent = notes.join(" ");
    notice.hidden = notes.length === 0;

    $("convertLog").textContent =
      result.log + (result.imagesLog ? "\n" + result.imagesLog : "");
    $("checkLog").textContent = result.checkLog;
    $("next2").disabled = false;
    sampleList = result.samples && result.samples.length ? result.samples : [];
    sampleAt = 0;
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
    var mapping = [];
    var selects = $("mapGrid").querySelectorAll("select");
    selects.forEach(function (select) {
      if (select.value) mapping.push(select.dataset.slot + "=" + select.value);
    });
    return mapping.length ? mapping : null;
  }

  var sampleList = [];
  var sampleAt = 0;

  function fillSample(sample) {
    if (!sample) {
      $("samplePanel").hidden = true;
      return;
    }
    $("samplePanel").hidden = false;
    var parts = [
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
      label.appendChild(
        document.createTextNode(
          "Card " +
            (sample.index + 1) +
            (sample.flagged ? ", one the checker flagged. " : ". ") +
            (flagged
              ? flagged + " flagged card(s) to look at. "
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
    $("writeCard").disabled = !canPickFolders;
    $("writeZip").disabled = false;
    $("previewBoot").disabled = false;
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
    } else {
      verdict.textContent = "Every card renders.";
      verdict.className = "study-verdict is-good";
    }
    var notice = $("skipNotice");
    notice.textContent = fontProblems.join(" ");
    notice.hidden = fontProblems.length === 0;
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
          $("previewStatus").textContent =
            "Booting the firmware… (about ten seconds)";
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
    $("writeCard").disabled = needsFonts || !canPickFolders;
    $("writeZip").disabled = needsFonts;
    $("previewBoot").disabled = needsFonts;
    if (needsFonts) setWriteStatus("Waiting for the faces to build…");
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
      return; // user cancelled the picker
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
    var blob = new Blob([buffer], { type: "application/zip" });
    var a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = converted.slug + "-deck.zip";
    a.click();
    URL.revokeObjectURL(a.href);
    setWriteStatus(
      "Unpack the zip at the ROOT of the SD card, so it creates study/" +
        converted.slug +
        "/. Then: Apps > STUDY.",
    );
  }

  // ---- the sync-back flow -------------------------------------------------

  var syncDecks = null; // {deckName: {"revlog.dat": buf, "cards.dat": buf}}
  var syncProfileHandle = null;
  var syncCollection = null; // ArrayBuffer, read fresh at replay time

  function setSyncStatus(text) {
    $("syncStatus").textContent = text;
  }

  async function pickSyncCard() {
    var root;
    try {
      root = await window.showDirectoryPicker({ mode: "read" });
    } catch (e) {
      return;
    }
    try {
      var study = await root.getDirectoryHandle("study");
    } catch (e) {
      setSyncStatus(
        "That folder has no study/ directory; pick the SD card's root.",
      );
      return;
    }
    syncDecks = {};
    var found = [];
    for await (var entry of study.values()) {
      if (entry.kind !== "directory" || entry.name === "fonts") continue;
      var deck = {};
      var reviews = 0;
      try {
        var revlog = await (
          await (await entry.getFileHandle("revlog.dat")).getFile()
        ).arrayBuffer();
        var cards = await (
          await (await entry.getFileHandle("cards.dat")).getFile()
        ).arrayBuffer();
        deck["revlog.dat"] = revlog;
        deck["cards.dat"] = cards;
        reviews = Math.floor(revlog.byteLength / 32);
      } catch (e) {
        continue;
      }
      if (reviews > 0) {
        syncDecks[entry.name] = deck;
        found.push(entry.name + " (" + reviews + " review record(s))");
      }
    }
    if (!found.length) {
      setSyncStatus("No deck on that card has reviews waiting. Nothing to do.");
      syncDecks = null;
      return;
    }
    setSyncStatus(
      "Found: " + found.join(", ") + ". Now pick the Anki profile folder.",
    );
    $("syncProfile").disabled = false;
  }

  async function pickSyncProfile() {
    var profile;
    try {
      profile = await window.showDirectoryPicker({ mode: "readwrite" });
    } catch (e) {
      return;
    }
    var collectionHandle;
    try {
      collectionHandle = await profile.getFileHandle("collection.anki2");
    } catch (e) {
      setSyncStatus(
        "No collection.anki2 in that folder. The profile folder is inside " +
          "Anki2, usually named after you (or 'User 1').",
      );
      return;
    }
    syncProfileHandle = profile;
    var size = (await collectionHandle.getFile()).size;
    setSyncStatus(
      "Collection found (" +
        (size / 1024 / 1024).toFixed(1) +
        " MB). Quit Anki if it is open, then replay.",
    );
    $("syncRun").disabled = false;
    $("syncRun").classList.add("primary");
  }

  // The browser cannot see processes, but SQLite's sidecar files are Anki's
  // fingerprint on disk: a non-empty -wal/-journal means unflushed writes,
  // and an existing -shm means some connection is (or died) holding it open.
  async function ankiLooksOpen(profile) {
    try {
      var shm = await profile.getFileHandle("collection.anki2-shm");
      if (shm) return "collection.anki2-shm exists";
    } catch (e) {}
    var names = ["collection.anki2-wal", "collection.anki2-journal"];
    for (var i = 0; i < names.length; i++) {
      try {
        var extra = await (await profile.getFileHandle(names[i])).getFile();
        if (extra.size > 0) return names[i] + " is not empty";
      } catch (e) {}
    }
    return null;
  }

  async function runSync() {
    if (!syncDecks || !syncProfileHandle) return;
    ensureWorker();
    if (!workerReady) {
      onReady = runSync;
      setSyncStatus("Loading the Python runtime first…");
      return;
    }
    // Check and read NOW, not at pick time: minutes may have passed, and an
    // Anki session in between would otherwise be replayed over and reverted.
    var open = await ankiLooksOpen(syncProfileHandle);
    if (open) {
      setSyncStatus(
        "Anki looks open (" +
          open +
          "). Quit it fully, then replay. If Anki crashed recently, open and" +
          " quit it once so it cleans up, then come back.",
      );
      return;
    }
    try {
      syncCollection = await (
        await (
          await syncProfileHandle.getFileHandle("collection.anki2")
        ).getFile()
      ).arrayBuffer();
    } catch (e) {
      setSyncStatus("Could not read collection.anki2: " + (e.message || e));
      return;
    }
    setSyncStatus("Replaying…");
    $("syncRun").disabled = true;
    var transfers = [syncCollection];
    Object.keys(syncDecks).forEach(function (deck) {
      Object.keys(syncDecks[deck]).forEach(function (name) {
        transfers.push(syncDecks[deck][name]);
      });
    });
    worker.postMessage(
      { type: "syncfiles", collection: syncCollection, decks: syncDecks },
      transfers,
    );
    syncCollection = null;
    syncDecks = null;
  }

  function onSyncStaged() {
    worker.postMessage({ type: "sync" });
  }

  var syncResult = null;

  function onSyncDone(result) {
    syncResult = result;
    var log = $("syncLog");
    log.hidden = false;
    if (result.error) {
      log.textContent = result.error;
      setSyncStatus("The replay did not run.");
      return;
    }
    log.textContent = result.log;
    if (result.failed) {
      setSyncStatus(
        "The replay hit a problem; nothing was written back. The log above has the tool's own words.",
      );
      return;
    }
    setSyncStatus("Backing up and writing the collection…");
    worker.postMessage({ type: "syncfile" });
  }

  async function onSyncFile(buffer) {
    try {
      var stamp = Math.floor(Date.now() / 1000);
      var backupName = "collection.anki2.before-sync-" + stamp;
      var current = await syncProfileHandle.getFileHandle("collection.anki2");
      var currentBytes = await (await current.getFile()).arrayBuffer();
      var backup = await syncProfileHandle.getFileHandle(backupName, {
        create: true,
      });
      var writable = await backup.createWritable();
      await writable.write(currentBytes);
      await writable.close();

      var out = await current.createWritable();
      await out.write(buffer);
      await out.close();
      setSyncStatus(
        "Done. Backup: " +
          backupName +
          ". Open Anki: your reviews are there; press Sync to carry them to AnkiWeb.",
      );
    } catch (e) {
      setSyncStatus("Writing the collection back failed: " + (e.message || e));
    }
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
    $("syncCard").disabled = true;
    setSyncStatus(
      "This browser cannot open folders (Chrome and Edge can); use the" +
        " command-line sync from the repository instead.",
    );
  } else {
    $("syncCard").addEventListener("click", pickSyncCard);
    $("syncProfile").addEventListener("click", pickSyncProfile);
    $("syncRun").addEventListener("click", runSync);
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
    currentStep = step;
    if (step > reachedStep) reachedStep = step;
    Object.keys(stepSections).forEach(function (key) {
      stepSections[key].classList.toggle("is-current", Number(key) === step);
    });
    $("modeSync").classList.remove("is-current");
    stepButtons.forEach(function (button) {
      var mine = Number(button.dataset.step);
      button.classList.toggle("is-current", mine === step);
      button.classList.toggle("is-done", mine < step);
      button.disabled = mine > reachedStep;
      if (mine === step) button.setAttribute("aria-current", "step");
      else button.removeAttribute("aria-current");
    });
    $("modeInstall").setAttribute("aria-selected", "true");
    $("modeSyncBtn").setAttribute("aria-selected", "false");
    $("stepper").classList.remove("is-hidden");
  }

  function showSyncMode() {
    Object.keys(stepSections).forEach(function (key) {
      stepSections[key].classList.remove("is-current");
    });
    $("modeSync").classList.add("is-current");
    $("modeInstall").setAttribute("aria-selected", "false");
    $("modeSyncBtn").setAttribute("aria-selected", "true");
    $("stepper").classList.add("is-hidden");
  }

  stepButtons.forEach(function (button) {
    button.addEventListener("click", function () {
      goTo(Number(button.dataset.step));
    });
  });
  $("modeInstall").addEventListener("click", function () {
    goTo(currentStep);
  });
  $("modeSyncBtn").addEventListener("click", showSyncMode);
  $("toSync").addEventListener("click", showSyncMode);

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
