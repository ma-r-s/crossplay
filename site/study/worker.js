/* The installer's engine room: Pyodide plus the repository's own tools.
 *
 * Runs in a Web Worker so a slow conversion never freezes the page. The
 * protocol is small: the page posts {type, ...}, the worker answers with
 * {type, ...}; every Python call goes through web_glue.py, which returns
 * JSON strings. Nothing here parses Anki formats -- that is the whole point.
 */

importScripts("/pyodide/pyodide.js");

var pyodide = null;

function post(type, extra) {
  var message = extra || {};
  message.type = type;
  self.postMessage(message);
}

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
    self.postMessage({ type: "deckfiles", files: files }, transfers);
  },
};

self.onmessage = function (event) {
  var msg = event.data;
  Promise.resolve()
    .then(function () {
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
