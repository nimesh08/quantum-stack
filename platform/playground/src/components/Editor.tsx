import { Editor as MonacoEditor, OnMount } from "@monaco-editor/react";
import { useEffect, useRef } from "react";
import { spinorConfig, spinorLanguage } from "../languages/spinor";
import { phononLanguage } from "../languages/phonon";
import { photonLanguage } from "../languages/photon";

type Kind = "photon" | "phonon" | "spinor";

export interface EditorProps {
  value: string;
  kind: Kind;
  onChange: (s: string) => void;
}

const monacoLangFor: Record<Kind, string> = {
  spinor: "spinor",
  phonon: "phonon",
  photon: "photon",
};

export default function Editor({ value, kind, onChange }: EditorProps) {
  const registered = useRef(false);
  const onMount: OnMount = (_editor, monaco) => {
    if (registered.current) return;
    monaco.languages.register({ id: "spinor" });
    monaco.languages.setMonarchTokensProvider("spinor", spinorLanguage);
    monaco.languages.setLanguageConfiguration("spinor", spinorConfig);
    monaco.languages.register({ id: "phonon" });
    monaco.languages.setMonarchTokensProvider("phonon", phononLanguage);
    monaco.languages.register({ id: "photon" });
    monaco.languages.setMonarchTokensProvider("photon", photonLanguage);
    registered.current = true;
  };

  return (
    <MonacoEditor
      height="100%"
      defaultLanguage={monacoLangFor[kind]}
      language={monacoLangFor[kind]}
      theme="vs-dark"
      value={value}
      onChange={(v) => onChange(v ?? "")}
      onMount={onMount}
      options={{
        fontSize: 14,
        minimap: { enabled: false },
        scrollBeyondLastLine: false,
        wordWrap: "on",
        automaticLayout: true,
      }}
    />
  );
}
