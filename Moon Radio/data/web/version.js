window.MOON_RADIO_VERSION = "0.3.0";

(function () {
  function applyVersion() {
    document.querySelectorAll("[data-app-version]").forEach(function (element) {
      element.textContent = "Moon Radio · v" + window.MOON_RADIO_VERSION;
    });
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", applyVersion);
  } else {
    applyVersion();
  }
})();
