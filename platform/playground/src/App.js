import { jsx as _jsx, Fragment as _Fragment, jsxs as _jsxs } from "react/jsx-runtime";
import { Navigate, Route, Routes } from "react-router-dom";
import Playground from "./pages/Playground";
import Jobs from "./pages/Jobs";
import Settings from "./pages/Settings";
import Login from "./pages/Login";
import { useAuth } from "./hooks/useAuth";
function Protected({ children }) {
    const { isAuthed } = useAuth();
    if (!isAuthed)
        return _jsx(Navigate, { to: "/login", replace: true });
    return _jsx(_Fragment, { children: children });
}
export default function App() {
    return (_jsxs(Routes, { children: [_jsx(Route, { path: "/login", element: _jsx(Login, {}) }), _jsx(Route, { path: "/", element: _jsx(Protected, { children: _jsx(Playground, {}) }) }), _jsx(Route, { path: "/jobs", element: _jsx(Protected, { children: _jsx(Jobs, {}) }) }), _jsx(Route, { path: "/settings", element: _jsx(Protected, { children: _jsx(Settings, {}) }) })] }));
}
