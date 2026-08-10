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
      } else if (msg.type === "opened") {
        onOpened(msg.result);
      } else if (msg.type === "converted") {
        onConverted(msg.result);
      } else if (msg.type === "deckfiles") {
        onDeckFiles(msg.files);
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

  // ---- steps 3 and 4 placeholders ----------------------------------------

  function resetDownstream() {
    converted = null;
    $("previewPlaceholder").hidden = false;
    $("previewMount").hidden = true;
    $("writePlaceholder").hidden = false;
    $("writeBody").hidden = true;
  }

  function enableDownstream() {
    // Preview (step 3) and writing (step 4) hang off the converted deck.
    $("writePlaceholder").hidden = true;
    $("writeBody").hidden = false;
    $("writeStatus").textContent = "";
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
      return { opened: opened, converted: converted, deck: chosenDeck };
    },
  };
})();
