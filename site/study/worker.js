/* The installer's engine room: Pyodide plus the repository's own tools.
 *
 * Runs in a Web Worker so a slow conversion never freezes the page. The
 * protocol is small: the page posts {type, ...}, the worker answers with
 * {type, ...}; every Python call goes through web_glue.py, which returns
 * JSON strings. Nothing here parses Anki formats -- that is the whole point.
 */

// Any failure here is fatal and silent by default: importScripts throws
// synchronously, before onmessage exists, so the page would see nothing at
// all. Report it through the same channel as everything else.
try {
  importScripts("/pyodide/pyodide.js");
} catch (e) {
  self.postMessage({
    type: "error",
    for: "loading pyodide.js",
    message: String((e && e.message) || e),
  });
  throw e;
}

var pyodide = null;

function progress(text) {
  post("progress", { text: text });
}

// The runtime's biggest file, fetched by us before Pyodide asks for it, so
// the wait can be counted out loud. Pyodide gives no progress callback, and a
// static "Starting the Python runtime" over an 80-second cold-cache download
// is indistinguishable from a hang -- which is exactly how it read the first
// time someone hit a fresh deploy. Pyodide then takes it from the HTTP cache.
async function prefetchRuntime() {
  try {
    const response = await fetch("/pyodide/pyodide.asm.wasm");
    if (!response.ok || !response.body) return;
    // content-length is what crossed the wire; the reader hands back decoded
    // bytes. Those are one number only for an unencoded body, and this file
    // ships pre-compressed (see site/README.md): 2.4MB on the wire, 8.6MB
    // decoded. Dividing one by the other announced "154%" on its way to 354%.
    // No header carries the decoded size, so when the body is encoded there is
    // no honest percentage to show and the megabyte count is the readout.
    let total = response.headers.get("content-encoding")
      ? 0
      : Number(response.headers.get("content-length")) || 0;
    const reader = response.body.getReader();
    let loaded = 0;
    // Report at most four times a second. Posting on EVERY chunk turned a
    // 0.6-second download into three minutes: a brotli stream arrives in
    // thousands of small pieces, and a message per piece saturates the page's
    // main thread badly enough to starve this read loop. Measured, not
    // guessed -- plain fetch+arrayBuffer of the same file is 612ms.
    // Start the clock now rather than at zero, so the first report is a
    // quarter-second of real bytes instead of "0.0 MB so far", and a download
    // that finishes before then says nothing at all -- which is the truth.
    let lastPost = Date.now();
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      loaded += value.length;
      // The same mismatch, caught by arithmetic rather than by a header, in
      // case a browser ever declines to show us content-encoding.
      if (total && loaded > total) total = 0;
      const now = Date.now();
      if (now - lastPost < 250) continue;
      lastPost = now;
      progress(
        total
          ? "Downloading the Python runtime, " +
              Math.round((loaded / total) * 100) +
              "%"
          : "Downloading the Python runtime, " +
              (loaded / 1048576).toFixed(1) +
              " MB so far",
      );
    }
  } catch (e) {
    // A failed prefetch costs nothing: loadPyodide fetches it itself.
  }
}

async function init() {
  progress("Starting the Python runtime");
  await prefetchRuntime();
  progress("Starting the Python runtime");
  pyodide = await loadPyodide({ indexURL: "/pyodide/" });
  // Pillow is NOT here: it is a megabyte, it only serves make_images.py, and
  // the pictures it would pack are not carried to the reader anyway. It is
  // loaded on demand below for the one deck shape that uses them, so the
  // common English deck never pays for it on a cold cache.
  progress("Loading sqlite, zstandard and fonttools");
  await pyodide.loadPackage(["sqlite3", "zstandard", "fonttools"], {
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

  convert: async function (msg) {
    if (msg.needsImages && !self.pillowLoaded) {
      progress("Loading the image packer");
      // Raced against a deadline: a challenged or firewalled fetch can hang
      // without erroring, and the page's stall watchdog only guards the
      // pre-ready boot -- a mid-flow hang here spun forever once, live.
      await Promise.race([
        pyodide.loadPackage("pillow", { messageCallback: function () {} }),
        new Promise(function (ignore, reject) {
          setTimeout(function () {
            reject(
              new Error(
                "the image packer download did not finish in 45 seconds." +
                  " A firewall or the host's bot protection may be blocking" +
                  " this tab; reload the page and retry."
              )
            );
          }, 45000);
        }),
      ]);
      self.pillowLoaded = true;
    }
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
    self.postMessage(
      { type: "zip", epoch: currentEpoch, buffer: bytes.buffer },
      [bytes.buffer],
    );
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
    self.postMessage(
      { type: "deckfiles", epoch: currentEpoch, files: files },
      transfers,
    );
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
