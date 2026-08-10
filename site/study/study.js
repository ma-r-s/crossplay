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
    "headword",
    "reading",
    "meaning",
    "partOfSpeech",
    "sentence",
    "sentenceReading",
    "sentenceMeaning",
  ];

  var worker = null;
  var workerReady = false;
  var pendingBuffer = null; // an .apkg waiting for the worker to come up
  var onReady = null; // one queued action for when the worker is up
  var opened = null; // last open_apkg result
  var converted = null; // last convert result
  var chosenDeck = null;

  // ---- worker lifecycle ---------------------------------------------------

  function ensureWorker() {
    if (worker) return;
    worker = new Worker("/study/worker.js");
    worker.onmessage = function (event) {
      var msg = event.data;
      if (msg.type === "progress") {
        setProgress(msg.text + "…");
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
    $("openSummary").hidden = true;
    resetDownstream();
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
    $("openStatus").hidden = false;
  }

  function setError(text) {
    var p = $("openProgress");
    p.textContent = text;
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

    var facts = [result.cards + " cards"];
    if (result.cardsWithState > 0) {
      facts.push(
        result.cardsWithState + " with review history that will come along",
      );
    }
    if (result.fonts.length) {
      facts.push("fonts in the package: " + result.fonts.join(", "));
    }
    if (result.images > 0) {
      facts.push(result.images + (result.images === 1 ? " image" : " images"));
    }
    $("summaryFacts").textContent = facts.join(" · ") + ".";

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
    worker.postMessage({ type: "convert", deck: deckName, mapping: mapping });
  }

  function onConverted(result) {
    if (result.error) {
      $("reportPlaceholder").hidden = true;
      $("reportBody").hidden = false;
      $("reportVerdict").textContent = "This deck did not convert.";
      $("reportVerdict").className = "study-verdict is-bad";
      $("convertLog").textContent = result.error;
      $("checkLog").textContent = "";
      buildMapGrid();
      return;
    }
    converted = result;

    $("reportPlaceholder").hidden = true;
    $("reportBody").hidden = false;

    var verdict = $("reportVerdict");
    if (result.hasCjk && opened && opened.fonts.length === 0) {
      verdict.textContent =
        "This deck needs CJK faces and the package carries none.";
      verdict.className = "study-verdict is-bad";
    } else if (result.checkFailed) {
      verdict.textContent = "Converted, with things you should see:";
      verdict.className = "study-verdict is-bad";
    } else {
      verdict.textContent = "Every card renders.";
      verdict.className = "study-verdict is-good";
    }

    $("convertLog").textContent =
      result.log + (result.imagesLog ? "\n" + result.imagesLog : "");
    $("checkLog").textContent = result.checkLog;
    buildMapGrid();

    enableDownstream();
  }

  function buildMapGrid() {
    var grid = $("mapGrid");
    if (grid.childElementCount) return;
    SLOTS.forEach(function (slot) {
      var label = document.createElement("label");
      label.textContent = slot;
      var input = document.createElement("input");
      input.type = "text";
      input.placeholder = "Anki field name";
      input.dataset.slot = slot;
      label.appendChild(input);
      grid.appendChild(label);
    });
  }

  function mappingFromGrid() {
    var mapping = [];
    var inputs = $("mapGrid").querySelectorAll("input");
    inputs.forEach(function (input) {
      var value = input.value.trim();
      if (value) mapping.push(input.dataset.slot + "=" + value);
    });
    return mapping.length ? mapping : null;
  }

  // ---- step 3: the type ---------------------------------------------------

  var fonts = null; // last build_fonts result

  function startTypeStep() {
    $("typePlaceholder").hidden = true;
    $("typeBody").hidden = false;
    $("typeLog").hidden = true;
    $("typeProgress").hidden = true;

    if (converted.hasCjk) {
      $("typeChoices").hidden = true;
      if (opened && opened.fonts.length > 0) {
        buildFonts("cjk", null);
      } else {
        var log = $("typeLog");
        log.hidden = false;
        log.textContent =
          "This deck uses Chinese characters, and the package carries no" +
          " fonts to draw them with.\nIn Anki, add the faces your template" +
          " uses to the collection's media (files named like _simsun.ttf)," +
          " then export again.";
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
    var message = { type: "fonts", mode: mode };
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
    if (result.error) {
      log.textContent = result.error;
      return;
    }
    fonts = result;
    converted.files = result.files;
    log.textContent = result.log;

    // The checker's verdict, now against the real faces.
    $("checkLog").textContent = result.checkLog;
    var verdict = $("reportVerdict");
    if (result.checkFailed) {
      verdict.textContent = "Converted, with things you should see:";
      verdict.className = "study-verdict is-bad";
    } else {
      verdict.textContent = "Every card renders.";
      verdict.className = "study-verdict is-good";
    }
  }

  // ---- step 4: the preview ------------------------------------------------

  var previewFrame = null;

  function killPreview() {
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

      var listener = function (event) {
        if (event.origin !== location.origin || !event.data) return;
        if (event.data.type === "preview-waiting") {
          previewFrame.contentWindow.postMessage(
            { type: "boot", files: payload },
            location.origin,
            transfers,
          );
          $("previewStatus").textContent =
            "Booting the firmware… (about ten seconds)";
        } else if (event.data.type === "preview-ready") {
          $("previewStatus").textContent =
            "This is the real firmware. Tap the panel the way you would tap the reader.";
          window.removeEventListener("message", listener);
        } else if (event.data.type === "preview-error") {
          $("previewStatus").textContent =
            "The preview did not start: " + event.data.message;
          window.removeEventListener("message", listener);
        }
      };
      window.addEventListener("message", listener);
    });
  }

  // ---- downstream state ---------------------------------------------------

  function resetDownstream() {
    converted = null;
    fonts = null;
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
    worker.postMessage({ type: "deckfiles", names: converted.files });
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
          var reviews = Math.floor(oldLog.byteLength / 32);
          var go = window.confirm(
            "This card already has a deck named " +
              slug +
              " with " +
              reviews +
              " review(s) not yet synced back to Anki. Overwriting erases " +
              "them. Sync first (the step below), or press OK to overwrite.",
          );
          if (!go) {
            setWriteStatus("Left the card untouched.");
            return;
          }
        }
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
    worker.postMessage({ type: "zip", slug: converted.slug });
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
  var syncCollection = null; // ArrayBuffer

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
    // The browser cannot see processes, but a hot journal is Anki's own
    // fingerprint on disk: refuse rather than race it.
    var names = ["collection.anki2-wal", "collection.anki2-journal"];
    for (var i = 0; i < names.length; i++) {
      try {
        var extra = await (await profile.getFileHandle(names[i])).getFile();
        if (extra.size > 0) {
          setSyncStatus(
            "Anki looks open (" +
              names[i] +
              " is not empty). Quit Anki fully, then pick the folder again.",
          );
          return;
        }
      } catch (e) {}
    }
    syncProfileHandle = profile;
    syncCollection = await (await collectionHandle.getFile()).arrayBuffer();
    setSyncStatus(
      "Collection found (" +
        (syncCollection.byteLength / 1024 / 1024).toFixed(1) +
        " MB). Quit Anki if it is open, then replay.",
    );
    $("syncRun").disabled = false;
  }

  function runSync() {
    if (!syncDecks || !syncCollection) return;
    ensureWorker();
    if (!workerReady) {
      onReady = runSync;
      setSyncStatus("Loading the Python runtime first…");
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

  $("remap").addEventListener("click", function () {
    if (chosenDeck) convert(chosenDeck, mappingFromGrid());
  });

  $("previewBoot").addEventListener("click", bootPreview);

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
  // bundled fetches the serif this site ships, own asks for a TTF.
  document.querySelectorAll('input[name="face"]').forEach(function (radio) {
    radio.addEventListener("change", function () {
      if (!converted) return;
      if (radio.value === "builtin") {
        fonts = null;
        $("typeLog").hidden = true;
        $("typeProgress").hidden = true;
        convert(chosenDeck, null); // back to the fontless deck and report
      } else if (radio.value === "bundled") {
        fetch("/study/DejaVuSerif.ttf")
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
  $("ttfpick").addEventListener("change", function () {
    var file = $("ttfpick").files[0];
    $("ttfpick").value = "";
    if (!file) return;
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
