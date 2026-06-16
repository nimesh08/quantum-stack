import { Navigate, Route, Routes } from "react-router-dom";
import Playground from "./pages/Playground";
import Jobs from "./pages/Jobs";
import Settings from "./pages/Settings";
import Login from "./pages/Login";
import { useAuth } from "./hooks/useAuth";

function Protected({ children }: { children: React.ReactNode }) {
  const { isAuthed } = useAuth();
  if (!isAuthed) return <Navigate to="/login" replace />;
  return <>{children}</>;
}

export default function App() {
  return (
    <Routes>
      <Route path="/login" element={<Login />} />
      <Route
        path="/"
        element={
          <Protected>
            <Playground />
          </Protected>
        }
      />
      <Route
        path="/jobs"
        element={
          <Protected>
            <Jobs />
          </Protected>
        }
      />
      <Route
        path="/settings"
        element={
          <Protected>
            <Settings />
          </Protected>
        }
      />
    </Routes>
  );
}
