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

  function isOpen() {
    return btn.getAttribute("aria-expanded") === "true";
  }

  // returnFocus matters only when the panel is being closed from the keyboard.
  // display:none on the nav destroys whatever focus was inside it, dropping the
  // caret on BODY so the next Tab restarts at the top of the document -- so
  // Escape hands focus back to the button it came from. A close caused by a
  // click or a jump must NOT do that: the person is already somewhere else, and
  // pulling focus back to the bar would undo the move they just made.
  function set(open, returnFocus) {
    var was = isOpen();
    btn.setAttribute("aria-expanded", open ? "true" : "false");
    nav.classList.toggle("is-open", open);
    if (!open && was && returnFocus) btn.focus();
  }

  btn.addEventListener("click", function () {
    set(!isOpen());
  });

  // Any link in the BAR, not just in the panel: the wordmark is an anchor to
  // the top of the page and it sits outside .topnav, so a listener on the panel
  // alone let it jump the page and leave the panel covering the heading it had
  // jumped to. The toggle is a <button>, so it never matches this.
  bar.addEventListener("click", function (ev) {
    if (ev.target.closest && ev.target.closest("a")) set(false);
  });

  document.addEventListener("keydown", function (ev) {
    if (ev.key === "Escape") set(false, true);
  });

  document.addEventListener("click", function (ev) {
    if (!ev.target.closest || !ev.target.closest(".topbar")) set(false);
  });

  // Asked of the button rather than of a width: the breakpoint is the
  // stylesheet's to choose, and a copy of the number here is a copy that goes
  // stale on its own. A hidden button has no offsetParent, and that is exactly
  // the state in which the panel must not be left open.
  window.addEventListener("resize", function () {
    if (btn.offsetParent === null) set(false);
  });
})();
