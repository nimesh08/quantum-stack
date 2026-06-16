// Monaco Monarch language definition for Spinor (.spn).
// Tokens come from spinor/parser/cpp/lib/Lexer.{h,cpp}.
import type * as monaco from "monaco-editor";

export const spinorLanguage: monaco.languages.IMonarchLanguage = {
  keywords: ["target", "qubit", "bit", "measure", "reset", "barrier", "generic"],
  gates: [
    "h", "x", "y", "z", "s", "sdg", "t", "tdg",
    "rx", "ry", "rz",
    "cx", "cz", "swap",
    "ecr", "ms", "rzz", "sx", "sxdg",
  ],
  tokenizer: {
    root: [
      [/;[^\n]*/, "comment"],
      [/\b(target|qubit|bit|measure|reset|barrier|generic)\b/, "keyword"],
      [/\b(h|x|y|z|s|sdg|t|tdg|rx|ry|rz|cx|cz|swap|ecr|ms|rzz|sx|sxdg)\b/, "type"],
      [/\bpi\b/, "constant"],
      [/[a-zA-Z_][\w]*/, "identifier"],
      [/[0-9]+(\.[0-9]+)?/, "number"],
      [/[\[\](){},]/, "delimiter"],
      [/[+\-*/=]/, "operator"],
      [/\s+/, "white"],
    ],
  },
};

export const spinorConfig: monaco.languages.LanguageConfiguration = {
  comments: { lineComment: ";" },
  brackets: [["[", "]"], ["(", ")"]],
  autoClosingPairs: [
    { open: "[", close: "]" },
    { open: "(", close: ")" },
  ],
};
