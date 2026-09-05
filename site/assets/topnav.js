/* The top bar's narrow-screen menu.
 *
 * The bar holds the name and up to five links. At 320px those labels do not
 * share a line: the stylesheet used to hide THE SHELF and PLAY NEARBY below
 * 720px -- the two that lead to what the site is for -- and the one it kept,
 * ANKI DECKS, still wrapped to two 49px lines inside a 50px bar at 320, 390 and
 * 414. /study/ did the same with INSTALL THE FIRMWARE. So on a narrow bar the
 * links move into a panel and this button opens it.
 *
 * The class comes from here rather than living in the markup so that a page
 * whose scripts failed keeps the plain inline bar instead of a button that
 * opens nothing. Both pages that have a .topbar load this file; a page without
 * one is left alone.
 */
(function () {
  "use strict";

  var bar = document.querySelector(".topbar");
  var nav = bar && bar.querySelector(".topnav");
  var btn = bar && bar.querySelector(".topnav-toggle");
  if (!bar || !nav || !btn) return;

  bar.classList.add("has-menu");

  function set(open) {
    btn.setAttribute("aria-expanded", open ? "true" : "false");
    nav.classList.toggle("is-open", open);
  }

  btn.addEventListener("click", function () {
    set(btn.getAttribute("aria-expanded") !== "true");
  });

  // A jump inside the page would otherwise leave the panel covering the very
  // heading it jumped to.
  nav.addEventListener("click", function (ev) {
    if (ev.target.closest("a")) set(false);
  });

  document.addEventListener("keydown", function (ev) {
    if (ev.key === "Escape") set(false);
  });

  document.addEventListener("click", function (ev) {
    if (!ev.target.closest(".topbar")) set(false);
  });

  // Asked of the button rather than of a width: the breakpoint is the
  // stylesheet's to choose, and a copy of the number here is a copy that goes
  // stale on its own. A hidden button has no offsetParent, and that is exactly
  // the state in which the panel must not be left open.
  window.addEventListener("resize", function () {
    if (btn.offsetParent === null) set(false);
  });
})();
