/* The repository's latest release, asked for once per page.
 *
 * Two things on the front page want the version: the Install button names it,
 * and the report form uses it as the version field's placeholder. GitHub's
 * unauthenticated API allows 60 requests an hour per IP -- which is the whole
 * reason install.js asks from the visitor's browser rather than from
 * /api/firmware -- so two identical requests on one load halve how many visits
 * a person gets before the button can no longer name a version. It was two for
 * a day: the placeholder was added with a fetch of its own. Both callers share
 * this promise now, and /report/, which has no installer, is still one request.
 *
 * It never rejects. A caller gets null and shows what it can without a version:
 * the Install button still installs, the report form just has no placeholder.
 * The repository name lives here rather than in each caller so the two cannot
 * ask about different repositories.
 */
(function () {
  "use strict";

  var REPO = "ma-r-s/crossplay";
  var pending = null;

  window.crossplayLatestRelease = function () {
    if (!pending)
      pending = fetch(
        "https://api.github.com/repos/" + REPO + "/releases/latest",
      )
        .then(function (r) {
          return r.ok ? r.json() : null;
        })
        .catch(function () {
          return null;
        });
    return pending;
  };
})();
