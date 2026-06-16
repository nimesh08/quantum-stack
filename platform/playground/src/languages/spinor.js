export const spinorLanguage = {
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
export const spinorConfig = {
    comments: { lineComment: ";" },
    brackets: [["[", "]"], ["(", ")"]],
    autoClosingPairs: [
        { open: "[", close: "]" },
        { open: "(", close: ")" },
    ],
};
