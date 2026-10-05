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
 *
 * When the visitor's own request fails -- their IP spent its 60, or their
 * network cannot reach api.github.com -- it asks /api/latest once. That is
 * the site's proxy of the same GitHub answer: it keeps its last good copy and
 * serves it stale when GitHub refuses it, so it can still name a version when
 * GitHub cannot be asked at all. Second, not first: its budget is Vercel's
 * shared egress, which is exactly the budget the browser request exists to
 * avoid. A null here used to end the install with "Could not reach GitHub to
 * find the latest release" (card #585) even while the site held a good answer.
 */
(function () {
  "use strict";

  var REPO = "ma-r-s/crossplay";
  var pending = null;

  function ask(url) {
    return fetch(url)
      .then(function (r) {
        return r.ok ? r.json() : null;
      })
      .then(function (body) {
        return body && body.tag_name ? body : null;
      })
      .catch(function () {
        return null;
      });
  }

  window.crossplayLatestRelease = function () {
    if (!pending)
      pending = ask("https://api.github.com/repos/" + REPO + "/releases/latest").then(
        function (release) {
          return release || ask("/api/latest");
        },
      );
    return pending;
  };
})();
