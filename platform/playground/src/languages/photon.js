export const photonLanguage = {
    keywords: [
        "target", "kernel", "QReg", "Oracle",
        "int", "angle", "bit",
        "if", "else", "for", "in", "return",
        "import",
    ],
    tokenizer: {
        root: [
            [/\/\/[^\n]*/, "comment"],
            [/\b(target|kernel|QReg|Oracle|int|angle|bit|if|else|for|in|return|import)\b/, "keyword"],
            [/[a-zA-Z_][\w]*/, "identifier"],
            [/[0-9]+(\.[0-9]+)?/, "number"],
            [/"[^"]*"/, "string"],
            [/[{}\[\]()=,;.]/, "delimiter"],
            [/[+\-*/<>=!]/, "operator"],
            [/\s+/, "white"],
        ],
    },
};
