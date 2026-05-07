window.MathJax = {
  tex: {
    inlineMath: [
      ["\\(", "\\)"],
      ["$", "$"],
    ],
    displayMath: [
      ["\\[", "\\]"],
      ["$$", "$$"],
    ],
    processEscapes: true,
    processEnvironments: true,
  },
  options: {
    ignoreHtmlClass: "mermaid|.*|",
    processHtmlClass: "arithmatex",
  },
};

document.addEventListener("DOMContentLoaded", function () {
  if (typeof MathJax !== "undefined" && MathJax.typesetPromise) {
    MathJax.typesetPromise();
  }
});
