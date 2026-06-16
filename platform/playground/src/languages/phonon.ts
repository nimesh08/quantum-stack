import type * as monaco from "monaco-editor";

export const phononLanguage: monaco.languages.IMonarchLanguage = {
  keywords: [
    "target", "qubit", "bit", "int", "angle",
    "if", "else", "for", "in", "while",
    "def", "return",
    "measure", "reset", "barrier",
  ],
  gates: [
    "h", "x", "y", "z", "s", "sdg", "t", "tdg",
    "rx", "ry", "rz", "cx", "cz", "swap",
  ],
  tokenizer: {
    root: [
      [/;[^\n]*/, "comment"],
      [/\b(target|qubit|bit|int|angle|if|else|for|in|while|def|return|measure|reset|barrier)\b/, "keyword"],
      [/\b(h|x|y|z|s|sdg|t|tdg|rx|ry|rz|cx|cz|swap)\b/, "type"],
      [/\bpi\b/, "constant"],
      [/[a-zA-Z_][\w]*/, "identifier"],
      [/[0-9]+(\.[0-9]+)?/, "number"],
      [/[{}\[\]()=,]/, "delimiter"],
      [/[+\-*/<>=!]/, "operator"],
      [/\s+/, "white"],
    ],
  },
};
