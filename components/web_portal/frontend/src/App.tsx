import { ErrorBoundary } from './components/Common/ErrorBoundary';
import { Providers } from './components/System/Providers';
import { DataLayer } from './components/System/DataLayer';
import { AppShell } from './components/Layout/AppShell';
/** Root application component */
function App() {
  return (
    <ErrorBoundary>
      <Providers>
        <DataLayer />
        <AppShell />
      </Providers>
    </ErrorBoundary>
  );
}

export default App;
