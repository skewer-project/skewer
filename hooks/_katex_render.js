/**
 * Small stdin → stdout wrapper that renders LaTeX to HTML via KaTeX.
 * Called by hooks/latex_render.py via npx -y -p katex.
 *
 * Usage:
 *   DISPLAY_MODE=1 node _katex_render.js < input.tex > output.html
 */
const katex = require("katex");

let input = "";
process.stdin.setEncoding("utf8");
process.stdin.on("data", (chunk) => (input += chunk));
process.stdin.on("end", () => {
  const displayMode = process.env.DISPLAY_MODE === "1";
  try {
    process.stdout.write(
      katex.renderToString(input.trim(), {
        displayMode,
        throwOnError: false,
        strict: false,
      })
    );
  } catch (e) {
    // On failure, output the raw LaTeX so the page isn't broken
    process.stderr.write("[katex] render error: " + e.message + "\n");
    process.stdout.write(input);
  }
});
