/* The installer's engine room: Pyodide plus the repository's own tools.
 *
 * Runs in a Web Worker so a slow conversion never freezes the page. The
 * protocol is small: the page posts {type, ...}, the worker answers with
 * {type, ...}; every Python call goes through web_glue.py, which returns
 * JSON strings. Nothing here parses Anki formats -- that is the whole point.
 */

importScripts("/pyodide/pyodide.js");

var pyodide = null;

function progress(text) {
  post("progress", { text: text });
}

async function init() {
  progress("Starting the Python runtime");
  pyodide = await loadPyodide({ indexURL: "/pyodide/" });
  progress("Loading sqlite, zstandard, fonttools, pillow");
  await pyodide.loadPackage(["sqlite3", "zstandard", "fonttools", "pillow"], {
    messageCallback: function () {},
  });
  progress("Fetching the conversion tools");
  var response = await fetch("/study/tools.zip");
  if (!response.ok)
    throw new Error("tools.zip did not load: " + response.status);
  var zip = await response.arrayBuffer();
  pyodide.FS.mkdirTree("/tools");
  pyodide.unpackArchive(zip, "zip", { extractDir: "/tools" });
  pyodide.FS.mkdirTree("/work");
  await pyodide.runPythonAsync(
    "import sys\n" +
      "sys.path.insert(0, '/tools/tools_local/study')\n" +
      "import web_glue\n",
  );
  post("ready");
}

function callGlue(expression) {
  var json = pyodide.runPython(expression);
  return JSON.parse(json);
}

async function ensureFt() {
  if (self.ftModule) return;
  progress("Loading the font engine");
  importScripts("/study/ft.js");
  self.ftModule = await createFtModule({
    locateFile: function (path) {
      return "/study/" + path;
    },
  });
  self.ftModule._ft_init();
}

var handlers = {
  open: function (msg) {
    pyodide.FS.writeFile("/work/deck.apkg", new Uint8Array(msg.buffer));
    post("opened", { result: callGlue("web_glue.open_apkg()") });
  },

  convert: function (msg) {
    pyodide.globals.set("_deck", msg.deck);
    pyodide.globals.set("_mapping", pyodide.toPy(msg.mapping || null));
    post("converted", {
      result: callGlue("web_glue.convert(_deck, _mapping)"),
    });
  },

  fonts: async function (msg) {
    await ensureFt();
    if (msg.ttf) {
      pyodide.FS.writeFile("/work/custom.ttf", new Uint8Array(msg.ttf));
    }
    pyodide.globals.set("_mode", msg.mode);
    pyodide.globals.set("_fontline", function (line) {
      post("fontline", { text: line });
    });
    post("fonts", {
      result: callGlue("web_glue.build_fonts(_mode, _fontline)"),
    });
  },

  zip: function (msg) {
    pyodide.globals.set("_slug", msg.slug);
    var bytes = pyodide.runPython("web_glue.make_zip(_slug)").toJs();
    self.postMessage({ type: "zip", epoch: currentEpoch, buffer: bytes.buffer }, [bytes.buffer]);
  },

  syncfiles: function (msg) {
    // Stage the card's decks and the user's collection for the replay.
    var FS = pyodide.FS;
    // Clear any previous staging wholesale.
    pyodide.runPython(
      "import shutil, pathlib\n" +
        "shutil.rmtree('/work/sync', ignore_errors=True)\n" +
        "pathlib.Path('/work/sync/decks').mkdir(parents=True)\n",
    );
    FS.writeFile("/work/sync/collection.anki2", new Uint8Array(msg.collection));
    Object.keys(msg.decks).forEach(function (deck) {
      FS.mkdirTree("/work/sync/decks/" + deck);
      var files = msg.decks[deck];
      Object.keys(files).forEach(function (name) {
        FS.writeFile(
          "/work/sync/decks/" + deck + "/" + name,
          new Uint8Array(files[name]),
        );
      });
    });
    post("syncfiles");
  },

  sync: function () {
    post("sync", { result: callGlue("web_glue.sync_local()") });
  },

  syncfile: function () {
    var bytes = pyodide.runPython("web_glue.sync_file()").toJs();
    self.postMessage(
      { type: "syncfile", epoch: currentEpoch, buffer: bytes.buffer },
      [bytes.buffer],
    );
  },

  deckfiles: function (msg) {
    var files = {};
    var transfers = [];
    for (var i = 0; i < msg.names.length; i++) {
      var name = msg.names[i];
      pyodide.globals.set("_name", name);
      var bytes = pyodide.runPython("web_glue.deck_file(_name)").toJs();
      files[name] = bytes.buffer;
      transfers.push(bytes.buffer);
    }
    self.postMessage({ type: "deckfiles", epoch: currentEpoch, files: files }, transfers);
  },
};

// Strictly sequential: the fonts handler awaits its module load, and a
// message interleaving at that point (a new deck dropped mid-build) would
// pull /work/deck out from under the build. Every reply carries the epoch
// its request arrived with, so the page can drop results it no longer wants.
var chain = Promise.resolve();
var currentEpoch = 0;

function post(type, extra) {
  var message = extra || {};
  message.type = type;
  message.epoch = currentEpoch;
  self.postMessage(message);
}

self.onmessage = function (event) {
  var msg = event.data;
  chain = chain
    .then(function () {
      currentEpoch = msg.epoch || 0;
      if (msg.type === "init") return init();
      if (!pyodide) throw new Error("worker not initialised yet");
      return handlers[msg.type](msg);
    })
    .catch(function (error) {
      console.error("[study worker]", error);
      post("error", {
        for: msg.type,
        message: String(error && error.message ? error.message : error),
      });
    });
};
