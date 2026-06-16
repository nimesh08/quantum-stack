# Customising Monaco — `playground`

The playground uses [@monaco-editor/react](https://www.npmjs.com/package/@monaco-editor/react)
to embed the Monaco editor (the same one VS Code uses).

## Adding a language definition

Three Monarch grammars ship under
[`src/languages/`](https://github.com/nimesh08/quantum-stack/tree/main/platform/playground/src/languages):
`spinor.ts`, `phonon.ts`, `photon.ts`.

To add a fourth language (e.g. an OpenQASM 3 highlighter):

1. Write a Monarch grammar in `src/languages/qasm3.ts`. Pattern off
   `spinor.ts`.
2. Register it in [`Editor.tsx`](https://github.com/nimesh08/quantum-stack/blob/main/platform/playground/src/components/Editor.tsx):
   ```ts
   monaco.languages.register({ id: "qasm3" });
   monaco.languages.setMonarchTokensProvider("qasm3", qasm3Language);
   ```
3. Add it to the kind picker in the playground page.

## Theme

The playground uses Monaco's `vs-dark` theme. Custom themes go in
[`Editor.tsx`](https://github.com/nimesh08/quantum-stack/blob/main/platform/playground/src/components/Editor.tsx)
via `monaco.editor.defineTheme(name, themeData)`.

## Keybindings

Defaults follow VS Code. Extend with
`editor.addCommand(monaco.KeyMod | monaco.KeyCode, fn)` after mount.

## See also

[Install](install.md), [Dev workflow](dev_workflow.md), [Monaco docs](https://microsoft.github.io/monaco-editor/)
