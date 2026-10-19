import { useState, useEffect, useMemo, useCallback, useRef } from 'react';
import SessionList from './features/sessions/SessionList';
import EventTimeline from './features/sessions/EventTimeline';
import TabStrip from './features/sessions/TabStrip';
import FileBrowser from './features/files/FileBrowser';
import SecretsManager from './features/secrets/SecretsManager';
import MobilePairing from './features/mobile/MobilePairing';
import DiscussionPanel from './features/discussions/DiscussionPanel';
import MenuBar, { type MenuDefinition } from './features/shell/MenuBar';
import HermesSettings from './features/hermes/HermesSettings';
import HermesUpdateDialog from './features/hermes/HermesUpdateDialog';

type Provider = CodexProvider;
type AppSettings = CodexSettings;
type Notice = { kind: 'info' | 'success' | 'error'; message: string };

const TABS_KEY = 'consiglio:open-tabs';
const ACTIVE_TAB_KEY = 'consiglio:active-tab';
const AUTO_UPDATE_KEY = 'consiglio:hermes-auto-update';

function readTabs(): string[] {
  try {
    const raw = window.localStorage.getItem(TABS_KEY);
    const parsed = raw ? JSON.parse(raw) : [];
    return Array.isArray(parsed) ? parsed.filter((entry): entry is string => typeof entry === 'string') : [];
  } catch {
    return [];
  }
}

export default function App() {
  const [sessions, setSessions] = useState<SessionRecord[]>([]);
  const [openTabs, setOpenTabs] = useState<string[]>(() => readTabs());
  const [activeTab, setActiveTab] = useState<string | null>(() => window.localStorage.getItem(ACTIVE_TAB_KEY));
  const [settings, setSettings] = useState<AppSettings | null>(null);
  const [notice, setNotice] = useState<Notice | null>(null);
  const [approvals, setApprovals] = useState<ApprovalRecord[]>([]);
  const [startupStatus, setStartupStatus] = useState<StartupStatus | null>(null);
  const [bootstrapProgress, setBootstrapProgress] = useState<BootstrapProgress | null>(null);
  const [isInitializing, setIsInitializing] = useState(true);
  const [isRefreshingStatus, setIsRefreshingStatus] = useState(false);
  const [showFiles, setShowFiles] = useState(false);
  const [showSecrets, setShowSecrets] = useState(false);
  const [showMobile, setShowMobile] = useState(false);
  const [showDiscussions, setShowDiscussions] = useState(false);
  const [showHermesSettings, setShowHermesSettings] = useState(false);
  const [showEndpoints, setShowEndpoints] = useState(false);
  const [showUpdateDialog, setShowUpdateDialog] = useState(false);
  const [models, setModels] = useState<AgentModelInfo[]>([]);
  const [hermesUpdate, setHermesUpdate] = useState<HermesUpdateStatus | null>(null);
  const [autoUpdate, setAutoUpdate] = useState<boolean>(() => window.localStorage.getItem(AUTO_UPDATE_KEY) === 'true');
  const [updateBusy, setUpdateBusy] = useState(false);
  const [discovering, setDiscovering] = useState(false);
  const tabsRef = useRef<string[]>(openTabs);
  tabsRef.current = openTabs;

  const activeSession = useMemo(
    () => sessions.find(session => session.id === activeTab) ?? null,
    [sessions, activeTab],
  );

  const notify = useCallback((kind: Notice['kind'], message: string) => setNotice({ kind, message }), []);

  // ─── Tabs ──────────────────────────────────────────────────────────────────

  const openTab = useCallback((sessionId: string) => {
    setOpenTabs(current => (current.includes(sessionId) ? current : [...current, sessionId]));
    setActiveTab(sessionId);
  }, []);

  const closeTab = useCallback((sessionId: string) => {
    setOpenTabs(current => {
      const next = current.filter(id => id !== sessionId);
      setActiveTab(active => (active === sessionId ? next[next.length - 1] ?? null : active));
      return next;
    });
  }, []);

  const cycleTab = useCallback((direction: 1 | -1) => {
    setActiveTab(current => {
      const tabs = tabsRef.current;
      if (tabs.length === 0) return current;
      const index = current ? tabs.indexOf(current) : -1;
      return tabs[(index + direction + tabs.length) % tabs.length] ?? current;
    });
  }, []);

  useEffect(() => {
    window.localStorage.setItem(TABS_KEY, JSON.stringify(openTabs));
  }, [openTabs]);

  useEffect(() => {
    if (activeTab) window.localStorage.setItem(ACTIVE_TAB_KEY, activeTab);
    else window.localStorage.removeItem(ACTIVE_TAB_KEY);
  }, [activeTab]);

  // ─── Session actions ───────────────────────────────────────────────────────

  const refreshSessions = useCallback(async () => {
    const loaded = await window.codexApi.listSessions();
    setSessions(loaded);
    return loaded;
  }, []);

  const handleStartSession = useCallback(async (options: {
    repository?: string;
    branch?: string;
    provider?: Provider;
    selectedLanProviderId?: string;
  } = {}) => {
    try {
      const provider = options.provider ?? settings?.defaultProvider ?? 'hermes';
      const result = await window.codexApi.startSession({
        repository: options.repository,
        branch: options.branch,
        provider,
        selectedLanProviderId: options.selectedLanProviderId,
      });
      await refreshSessions();
      openTab(result.sessionId);
      notify('success', provider === 'codex' ? 'Codex CLI task started.' : 'Hermes task started.');
    } catch (error) {
      notify('error', `Could not start a task: ${(error as Error).message}`);
    }
  }, [settings?.defaultProvider, openTab, refreshSessions, notify]);

  const handleReconnect = useCallback(async (sessionId: string) => {
    try {
      const reconnected = await window.codexApi.reconnectSession(sessionId);
      if (!reconnected) throw new Error('the session could not be resumed');
      openTab(sessionId);
      notify('success', 'Task resumed.');
    } catch (error) {
      notify('error', `Could not resume the task: ${(error as Error).message}`);
    }
  }, [openTab, notify]);

  const handleStopSession = useCallback(async (sessionId: string) => {
    try {
      await window.codexApi.stopSession(sessionId);
      await refreshSessions();
      notify('info', 'Task stopped. Its Hermes session stays available to resume.');
    } catch (error) {
      notify('error', `Could not stop the task: ${(error as Error).message}`);
    }
  }, [refreshSessions, notify]);

  const handleDeleteSession = useCallback(async (sessionId: string) => {
    try {
      await window.codexApi.deleteSession(sessionId);
      closeTab(sessionId);
      await refreshSessions();
      notify('info', 'Task removed from Consiglio.');
    } catch (error) {
      notify('error', `Could not remove the task: ${(error as Error).message}`);
    }
  }, [closeTab, refreshSessions, notify]);

  const handleSettingsChange = useCallback(async (next: Partial<CodexSettingsInput>) => {
    try {
      const saved = await window.codexApi.updateSettings(next);
      setSettings(saved);
      return saved;
    } catch (error) {
      notify('error', `Could not save settings: ${(error as Error).message}`);
      return null;
    }
  }, [notify]);

  const handleCopyText = useCallback(async (text: string, label: string) => {
    try {
      const copied = await window.codexApi.copyText(text);
      notify(copied ? 'success' : 'error', copied ? `${label} copied to clipboard.` : 'Clipboard unavailable.');
    } catch (error) {
      notify('error', `Could not copy ${label.toLowerCase()}: ${(error as Error).message}`);
    }
  }, [notify]);

  const handleOpenPath = useCallback(async (targetPath: string, label: string) => {
    try {
      const opened = await window.codexApi.openPath(targetPath);
      notify(opened ? 'success' : 'error', opened ? `${label} opened.` : `Could not open ${label.toLowerCase()}.`);
    } catch (error) {
      notify('error', `Could not open ${label.toLowerCase()}: ${(error as Error).message}`);
    }
  }, [notify]);

  const handleApprove = useCallback(async (approvalId: string) => {
    try {
      const approved = await window.codexApi.approveCommand(approvalId);
      if (!approved) throw new Error('the agent no longer waits on this request');
      setApprovals(await window.codexApi.getPendingApprovals());
      notify('success', 'Approval granted.');
    } catch (error) {
      notify('error', `Could not approve: ${(error as Error).message}`);
    }
  }, [notify]);

  const handleReject = useCallback(async (approvalId: string) => {
    try {
      const rejected = await window.codexApi.rejectCommand(approvalId);
      if (!rejected) throw new Error('the agent no longer waits on this request');
      setApprovals(await window.codexApi.getPendingApprovals());
      notify('info', 'Approval denied.');
    } catch (error) {
      notify('error', `Could not deny: ${(error as Error).message}`);
    }
  }, [notify]);

  const copyTranscript = useCallback(async () => {
    if (!activeTab) return;
    const events = await window.codexApi.getSessionEvents(activeTab);
    const markdown = events
      .filter(event => event.type !== 'system')
      .map(event => `**${event.type}**\n\n${event.content}`)
      .join('\n\n');
    await handleCopyText(markdown, 'Transcript');
  }, [activeTab, handleCopyText]);

  // ─── Startup ───────────────────────────────────────────────────────────────

  useEffect(() => {
    let cancelled = false;
    const unsubscribeBootstrap = window.codexApi.onBootstrapProgress(progress => setBootstrapProgress(progress));
    const initialize = async () => {
      try {
        const [loadedSessions, loadedSettings, status, loadedModels] = await Promise.all([
          window.codexApi.listSessions(),
          window.codexApi.getSettings(),
          window.codexApi.getStartupStatus(),
          window.codexApi.getModels().catch(() => [] as AgentModelInfo[]),
        ]);
        if (cancelled) return;
        setSessions(loadedSessions);
        setSettings(loadedSettings);
        setStartupStatus(status);
        setBootstrapProgress(status.bootstrap);
        setModels(loadedModels);

        const update = await window.codexApi.checkHermesUpdate().catch(() => null);
        if (cancelled) return;
        setHermesUpdate(update);
        if (update?.state === 'available') setShowUpdateDialog(true);

        const storedTabs = tabsRef.current.filter(id => loadedSessions.some(session => session.id === id));
        const preferred = storedTabs.length > 0 ? storedTabs : loadedSessions.slice(0, 1).map(session => session.id);
        setOpenTabs(preferred);
        const focus = activeTab && preferred.includes(activeTab) ? activeTab : preferred[0] ?? null;
        setActiveTab(focus);
        if (focus) await window.codexApi.reconnectSession(focus);
      } catch (error) {
        if (!cancelled) notify('error', `Could not initialize Consiglio: ${(error as Error).message}`);
      } finally {
        if (!cancelled) setIsInitializing(false);
      }
    };
    void initialize();
    return () => { cancelled = true; unsubscribeBootstrap(); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (!notice) return;
    const timeout = window.setTimeout(() => setNotice(null), 6000);
    return () => window.clearTimeout(timeout);
  }, [notice]);

  useEffect(() => {
    const unsubscribeApproval = window.codexApi.onApprovalRequest(() => {
      void window.codexApi.getPendingApprovals().then(setApprovals);
    });
    const unsubscribeSettings = window.codexApi.onSettingsChanged(setSettings);
    const unsubscribeSessions = window.codexApi.onSessionsUpdated(setSessions);
    const unsubscribeNew = window.codexApi.onNewSession(() => { void handleStartSession({}); });
    const unsubscribeConfig = window.codexApi.onHermesConfigChanged(result => {
      notify('info', `Hermes ${result.key}: ${result.before ?? '(unset)'} → ${result.after ?? '(unset)'}`);
    });
    return () => {
      unsubscribeApproval();
      unsubscribeSettings();
      unsubscribeSessions();
      unsubscribeNew();
      unsubscribeConfig();
    };
  }, [handleStartSession, notify]);

  useEffect(() => {
    let cancelled = false;
    const refresh = async () => {
      try {
        const pending = await window.codexApi.getPendingApprovals();
        if (!cancelled) setApprovals(pending);
      } catch { /* ignore */ }
    };
    void refresh();
    const interval = window.setInterval(refresh, 2000);
    return () => { cancelled = true; window.clearInterval(interval); };
  }, [activeTab]);

  // Tab keyboard handling: Ctrl+W closes, Ctrl+Tab cycles, Ctrl+1..9 jumps.
  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const meta = event.ctrlKey || event.metaKey;
      if (!meta) return;
      if (event.key.toLowerCase() === 'w' && activeTab) {
        event.preventDefault();
        closeTab(activeTab);
      } else if (event.key === 'Tab') {
        event.preventDefault();
        cycleTab(event.shiftKey ? -1 : 1);
      } else if (/^[1-9]$/.test(event.key)) {
        const target = tabsRef.current[Number(event.key) - 1];
        if (target) {
          event.preventDefault();
          setActiveTab(target);
        }
      }
    };
    document.addEventListener('keydown', onKeyDown);
    return () => document.removeEventListener('keydown', onKeyDown);
  }, [activeTab, closeTab, cycleTab]);

  // ─── Hermes updates ────────────────────────────────────────────────────────

  const anyTaskBusy = sessions.some(session => session.status === 'running');

  const applyUpdate = useCallback(async () => {
    setUpdateBusy(true);
    try {
      // Updating restarts the Hermes gateway, so keep it when a task is answering.
      const result = await window.codexApi.applyHermesUpdate({ restartGateway: !anyTaskBusy });
      notify(result.ok ? 'success' : 'error', result.ok
        ? 'Hermes updated. Restart Consiglio if the session list looks stale.'
        : `Hermes update failed: ${result.output}`);
      setShowUpdateDialog(false);
      setHermesUpdate(await window.codexApi.checkHermesUpdate().catch(() => null));
    } finally {
      setUpdateBusy(false);
    }
  }, [anyTaskBusy, notify]);

  const toggleAutoUpdate = useCallback((next: boolean) => {
    setAutoUpdate(next);
    window.localStorage.setItem(AUTO_UPDATE_KEY, String(next));
  }, []);

  useEffect(() => {
    if (!autoUpdate || !hermesUpdate || hermesUpdate.state !== 'available' || isInitializing) return;
    // Idle-only: an update restarts the gateway and would kill a running task.
    if (anyTaskBusy) return;
    void applyUpdate();
  }, [autoUpdate, hermesUpdate, isInitializing, anyTaskBusy, applyUpdate]);

  // ─── Menus (every entry performs a real action) ─────────────────────────────

  const menuModels: MenuDefinition['items'] = models.length === 0
    ? [{ group: 'Model (from Hermes)', label: 'No models reported yet', disabled: true }]
    : models.slice(0, 14).map(model => ({
      group: 'Model (from Hermes)',
      label: model.description ? `${model.name} — ${model.description}` : model.name,
      checked: activeSession?.model === model.id,
      disabled: !activeTab,
      onSelect: () => {
        if (!activeTab) return;
        void window.codexApi.setSessionModel(activeTab, model.id).then(ok => {
          notify(ok ? 'success' : 'error', ok ? `Model set to ${model.name}.` : 'Hermes refused the model change.');
          void refreshSessions();
        });
      },
    }));

  const menus: MenuDefinition[] = [
    {
      label: 'File',
      items: [
        { label: 'New task', shortcut: 'Ctrl+N', onSelect: () => void handleStartSession({}) },
        { separator: true },
        {
          label: 'Open workspace folder in the OS file manager',
          disabled: !activeSession?.repository,
          onSelect: () => activeSession?.repository && void handleOpenPath(activeSession.repository, 'Workspace folder'),
        },
        {
          label: 'Browse workspace in the Files panel',
          disabled: !activeTab,
          checked: showFiles,
          onSelect: () => setShowFiles(true),
        },
        { separator: true },
        { label: 'Close tab', shortcut: 'Ctrl+W', disabled: !activeTab, onSelect: () => activeTab && closeTab(activeTab) },
        { label: 'Stop task', disabled: !activeTab, onSelect: () => activeTab && void handleStopSession(activeTab) },
        { separator: true },
        { label: 'API keys and MCP secrets…', onSelect: () => setShowSecrets(true) },
        { label: 'Mobile companion pairing…', onSelect: () => setShowMobile(true) },
      ],
    },
    {
      label: 'View',
      items: [
        { label: 'Files panel', checked: showFiles, disabled: !activeTab, onSelect: () => setShowFiles(current => !current) },
        { label: 'Multi-agent discussion', checked: showDiscussions, onSelect: () => setShowDiscussions(current => !current) },
        { separator: true },
        { label: 'Copy whole transcript', disabled: !activeTab, onSelect: () => void copyTranscript() },
        { separator: true },
        { label: 'Next tab', shortcut: 'Ctrl+Tab', disabled: openTabs.length < 2, onSelect: () => cycleTab(1) },
        { label: 'Previous tab', shortcut: 'Ctrl+Shift+Tab', disabled: openTabs.length < 2, onSelect: () => cycleTab(-1) },
      ],
    },
    {
      label: 'Agent',
      items: [
        {
          group: 'Backend',
          label: 'Hermes Agent (default)',
          checked: settings?.defaultProvider !== 'codex',
          onSelect: () => void handleSettingsChange({ defaultProvider: 'hermes' }),
        },
        {
          group: 'Backend',
          label: 'Codex CLI',
          checked: settings?.defaultProvider === 'codex',
          onSelect: () => void handleSettingsChange({ defaultProvider: 'codex' }),
        },
        { separator: true },
        ...menuModels,
        { separator: true },
        { group: 'Approvals', label: 'Allow the pending request', disabled: approvals.length === 0, onSelect: () => approvals[0] && void handleApprove(approvals[0].id) },
        { group: 'Approvals', label: 'Deny the pending request', disabled: approvals.length === 0, onSelect: () => approvals[0] && void handleReject(approvals[0].id) },
        { separator: true },
        { label: 'Resume active task', disabled: !activeTab, onSelect: () => activeTab && void handleReconnect(activeTab) },
        { label: 'Model endpoints (Codex backend only)…', onSelect: () => setShowEndpoints(true) },
      ],
    },
    {
      label: 'Hermes',
      items: [
        { label: 'Hermes settings…', onSelect: () => setShowHermesSettings(true) },
        { label: 'Open Hermes config file', onSelect: () => void window.codexApi.openHermesConfig() },
        { label: 'Copy Hermes log path', onSelect: () => void window.codexApi.getHermesLogPath().then(path => handleCopyText(path, 'Hermes log path')) },
        { separator: true },
        {
          label: 'Check for Hermes updates',
          onSelect: () => {
            void window.codexApi.checkHermesUpdate().then(status => {
              setHermesUpdate(status);
              if (status.state === 'available') setShowUpdateDialog(true);
              notify('info', status.summary);
            });
          },
        },
        { label: 'Update Hermes now', disabled: !hermesUpdate || hermesUpdate.state !== 'available', onSelect: () => void applyUpdate() },
        { label: 'Update Hermes automatically on startup', checked: autoUpdate, onSelect: () => toggleAutoUpdate(!autoUpdate) },
        { separator: true },
        {
          label: 'Refresh provider checks',
          onSelect: () => void window.codexApi.getStartupStatus().then(status => {
            setStartupStatus(status);
            notify('info', 'Provider checks refreshed.');
          }),
        },
      ],
    },
  ];

  const health = summarizeHealth(startupStatus);
  const connection = activeSession ? describeConnection(activeSession, settings) : 'Hermes Agent';

  if (isInitializing && sessions.length === 0) {
    return <ProviderSetupScreen status={null} checking progress={bootstrapProgress} autoUpdate={autoUpdate} />;
  }

  if (sessions.length === 0) {
    return (
      <ProviderSetupScreen
        status={startupStatus}
        checking={isRefreshingStatus || Boolean(bootstrapProgress?.active)}
        progress={bootstrapProgress}
        autoUpdate={autoUpdate}
        onToggleAutoUpdate={toggleAutoUpdate}
        onRefresh={() => {
          setIsRefreshingStatus(true);
          void window.codexApi.getStartupStatus()
            .then(async status => {
              setStartupStatus(status);
              if (status.providerSetup.ready) await handleStartSession({});
            })
            .catch(error => notify('error', `Could not refresh checks: ${(error as Error).message}`))
            .finally(() => setIsRefreshingStatus(false));
        }}
        onStart={() => void handleStartSession({})}
        onOpenHermes={() => void window.codexApi.openHermesConfig()}
        onOpenHermesSettings={() => setShowHermesSettings(true)}
        onOpenCodex={() => void window.codexApi.openPath('https://github.com/openai/codex#installation')}
      />
    );
  }

  return (
    <div className="codex-app-shell">
      <MenuBar menus={menus} status={<span title={connection}>{connection}</span>} />

      <TabStrip
        tabs={openTabs}
        active={activeTab}
        sessions={sessions}
        onSelect={setActiveTab}
        onClose={closeTab}
        onNewTask={() => void handleStartSession({})}
      />

      {notice && (
        <div className={`codex-banner ${notice.kind === 'error' ? 'codex-notice-error' : notice.kind === 'success' ? 'codex-notice-success' : 'codex-notice-info'}`}>
          <span style={{ fontSize: 14 }}>{notice.message}</span>
          <button className="codex-button codex-button-secondary" onClick={() => setNotice(null)}>Dismiss</button>
        </div>
      )}

      <section className="codex-banner codex-health-banner" style={{ alignItems: 'stretch', background: 'rgba(255,255,255,0.035)', borderColor: health.borderColor }}>
        <div style={{ display: 'flex', flexDirection: 'column', gap: 4, flex: 1, minWidth: 0 }}>
          <span style={{ fontSize: 13, fontWeight: 600, color: health.color }}>{health.message}</span>
          {startupStatus && (
            <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
              {startupStatus.checks.map(check => (
                <HealthPill key={check.id} label={check.label} status={check.status} message={check.message} />
              ))}
            </div>
          )}
        </div>
        <button
          className="codex-button codex-button-secondary"
          disabled={isRefreshingStatus}
          onClick={() => {
            setIsRefreshingStatus(true);
            void window.codexApi.getStartupStatus()
              .then(status => { setStartupStatus(status); notify('info', status.appUpdate.message); })
              .catch(error => notify('error', `Could not refresh checks: ${(error as Error).message}`))
              .finally(() => setIsRefreshingStatus(false));
          }}
        >
          {isRefreshingStatus ? 'Checking…' : 'Refresh checks'}
        </button>
      </section>

      {approvals.length > 0 && (
        <section className="codex-banner codex-approval-banner" style={{ flexDirection: 'column', alignItems: 'stretch', gap: 10 }}>
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', gap: 12 }}>
            <span style={{ fontSize: 15, fontWeight: 700, color: '#d29922' }}>
              Hermes is asking permission ({approvals.length})
            </span>
            <span className="codex-help">Denying or ignoring this lets the agent continue without that action.</span>
          </div>
          {approvals.map(approval => (
            <div key={approval.id} style={{ background: 'rgba(0,0,0,0.2)', borderRadius: 10, padding: '10px 12px' }}>
              <div style={{ display: 'flex', justifyContent: 'space-between', gap: 10, marginBottom: 8 }}>
                <code style={{ fontSize: 13, color: '#c9d1d9', overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>
                  {approval.command}
                </code>
                {approval.sessionId && (
                  <span className="codex-chip" style={{ flexShrink: 0 }}>
                    <span className="codex-chip-label">session</span>
                    <span className="codex-chip-value">{approval.sessionId.slice(0, 14)}…</span>
                  </span>
                )}
              </div>
              <div style={{ display: 'flex', gap: 8, justifyContent: 'flex-end' }}>
                <button className="codex-button codex-button-primary" style={{ fontSize: 13 }} onClick={() => void handleApprove(approval.id)}>
                  Allow once
                </button>
                <button className="codex-button codex-button-danger" style={{ fontSize: 13 }} onClick={() => void handleReject(approval.id)}>
                  Deny
                </button>
              </div>
            </div>
          ))}
        </section>
      )}

      <div className="codex-shell-grid">
        <SessionList
          sessions={sessions}
          selected={activeTab}
          onSelect={openTab}
          onStartSession={options => void handleStartSession(options)}
          onReconnect={sessionId => void handleReconnect(sessionId)}
          onPickRepository={() => window.codexApi.pickFolder()}
          onCopyPath={handleCopyText}
          onOpenPath={handleOpenPath}
          onRequestNewSession={() => handleStartSession({})}
          onStopSession={sessionId => handleStopSession(sessionId)}
          onDeleteSession={sessionId => handleDeleteSession(sessionId)}
          settings={{
            defaultProvider: settings?.defaultProvider ?? 'hermes',
            lanProviders: settings?.lanProviders ?? [],
          }}
          connectionLabel={connection}
        />

        <section className="codex-panel" style={{ flex: 1, minWidth: 0 }}>
          <div className="codex-panel-header">
            <div className="codex-panel-title">
              <span className="codex-panel-heading">
                {activeSession?.repository ? sessionLabel(activeSession.repository) : 'No task selected'}
              </span>
              <span className="codex-kicker" title={connection}>
                {activeSession
                  ? `${activeSession.branch || 'current branch'} · ${connection}`
                  : 'Choose a task on the left, or start a new one'}
              </span>
            </div>
            <div style={{ display: 'flex', gap: 8, alignItems: 'center' }}>
              {activeSession?.status === 'running' && activeTab && (
                <button className="codex-button codex-button-danger" onClick={() => void handleStopSession(activeTab)}>
                  Stop
                </button>
              )}
              <button
                className={`codex-button ${showFiles ? 'codex-button-info' : 'codex-button-secondary'}`}
                disabled={!activeTab}
                onClick={() => setShowFiles(current => !current)}
                title="Show or hide the in-app file browser"
              >
                Files
              </button>
            </div>
          </div>

          <div style={{ flex: 1, minHeight: 0, overflow: 'hidden', display: 'flex', position: 'relative' }}>
            {showDiscussions ? (
              <DiscussionPanel onError={message => notify('error', message)} />
            ) : openTabs.length === 0 ? (
              <div className="codex-empty-state" style={{ flex: 1, display: 'flex', alignItems: 'center', justifyContent: 'center' }}>
                <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center', gap: 12 }}>
                  <span style={{ fontSize: 15 }}>No open tasks.</span>
                  <button className="codex-button codex-button-primary" onClick={() => void handleStartSession({})}>
                    Start a Hermes task
                  </button>
                </div>
              </div>
            ) : (
              <>
                {/* Every open tab stays mounted: switching never rebuilds a conversation. */}
                {openTabs.map(sessionId => (
                  <div
                    key={sessionId}
                    style={{ display: sessionId === activeTab ? 'flex' : 'none', flex: 1, minWidth: 0, minHeight: 0 }}
                  >
                    <EventTimeline
                      sessionId={sessionId}
                      compact
                      onCopySessionId={value => handleCopyText(value, 'Message')}
                      onRequestNewSession={() => handleStartSession({})}
                      onError={message => notify('error', message)}
                    />
                  </div>
                ))}
                {showFiles && activeTab && (
                  <FileBrowser
                    sessionId={activeTab}
                    onClose={() => setShowFiles(false)}
                    onError={message => notify('error', message)}
                  />
                )}
              </>
            )}
          </div>
        </section>
      </div>

      <SecretsManager open={showSecrets} onClose={() => setShowSecrets(false)} onNotice={notify} />
      <MobilePairing open={showMobile} onClose={() => setShowMobile(false)} onNotice={notify} />
      <HermesSettings open={showHermesSettings} onClose={() => setShowHermesSettings(false)} onNotice={notify} />
      <HermesUpdateDialog
        status={showUpdateDialog ? hermesUpdate : null}
        autoUpdate={autoUpdate}
        busy={updateBusy}
        onToggleAutoUpdate={toggleAutoUpdate}
        onUpdate={() => void applyUpdate()}
        onClose={() => setShowUpdateDialog(false)}
      />

      {showEndpoints && (
        <div className="codex-modal-backdrop" onMouseDown={() => setShowEndpoints(false)}>
          <section className="codex-new-task-dialog" onMouseDown={event => event.stopPropagation()}>
            <div className="codex-dialog-header">
              <div>
                <h2>Model endpoints</h2>
                <p>Only used by the Codex CLI backend. Hermes manages its own models and endpoints.</p>
              </div>
              <button className="codex-icon-button" onClick={() => setShowEndpoints(false)} aria-label="Close">×</button>
            </div>
            <div className="codex-task-options">
              {settings?.lanProviders.length
                ? settings.lanProviders.map(provider => (
                  <div className="codex-hermes-current" key={provider.id}>
                    <code>{provider.name || `${provider.host}:${provider.port}`}</code>
                    {provider.model ? ` · ${provider.model}` : ' · no model set'}
                  </div>
                ))
                : <span className="codex-help">No endpoints stored.</span>}
            </div>
            <div className="codex-dialog-actions">
              <button
                className="codex-button codex-button-primary"
                disabled={discovering}
                onClick={() => {
                  setDiscovering(true);
                  void window.codexApi.lanDiscover()
                    .then(result => {
                      notify('info', `Scanned the network: ${result.found} server(s), ${result.added} added.`);
                      return window.codexApi.getSettings().then(setSettings);
                    })
                    .catch(error => notify('error', `Discovery failed: ${(error as Error).message}`))
                    .finally(() => setDiscovering(false));
                }}
              >
                {discovering ? 'Scanning…' : 'Scan the local network'}
              </button>
              <button className="codex-button codex-button-secondary" onClick={() => setShowEndpoints(false)}>Close</button>
            </div>
          </section>
        </div>
      )}
    </div>
  );
}

function sessionLabel(repository?: string) {
  const trimmed = repository?.trim() || '';
  if (!trimmed) return 'Untitled task';
  const segments = trimmed.split(/[\\/]/).filter(Boolean);
  return segments[segments.length - 1] || trimmed;
}

function describeConnection(session: SessionRecord, settings: AppSettings | null) {
  if (session.agent === 'codex' || session.provider === 'codex'
    || session.provider === 'remote_llamacpp' || session.provider === 'lan' || session.provider === 'ollama') {
    return ['Codex CLI', session.model, session.baseUrl].filter(Boolean).join(' · ');
  }
  const model = session.model || settings?.defaultModel;
  return ['Hermes Agent', model].filter(Boolean).join(' · ');
}

function summarizeHealth(status: StartupStatus | null) {
  if (!status) return { message: 'Checking Hermes and provider readiness…', color: '#58a6ff', borderColor: 'rgba(88, 166, 255, 0.28)' };
  const hermesCheck = status.checks.find(check => check.id === 'hermes-agent');
  if (hermesCheck && hermesCheck.status === 'error') {
    return { message: 'Hermes was not found — Consiglio cannot run a task yet.', color: '#f85149', borderColor: 'rgba(248, 81, 73, 0.36)' };
  }
  const updateCheck = status.checks.find(check => check.id === 'hermes-update');
  if (updateCheck?.status === 'warning') {
    return { message: updateCheck.message, color: '#d29922', borderColor: 'rgba(210, 153, 34, 0.36)' };
  }
  if (status.checks.some(check => check.status === 'error')) {
    return { message: 'A startup check failed; tasks may not be reliable until it is fixed.', color: '#f85149', borderColor: 'rgba(248, 81, 73, 0.36)' };
  }
  return { message: 'Hermes is ready and Consiglio is connected.', color: '#3fb950', borderColor: 'rgba(63, 185, 80, 0.36)' };
}

function HealthPill({ label, status, message }: { label: string; status: HealthCheckItem['status']; message: string }) {
  const color = status === 'ok' ? '#3fb950' : status === 'error' ? '#f85149' : status === 'warning' ? '#d29922' : '#58a6ff';
  return (
    <div className="codex-chip" title={message} style={{ maxWidth: 420, borderColor: `${color}66` }}>
      <span className="codex-chip-label" style={{ color }}>{label}</span>
      <span className="codex-chip-value" style={{ overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap' }}>{message}</span>
    </div>
  );
}

function ProviderSetupScreen({
  status,
  checking,
  progress,
  autoUpdate,
  onToggleAutoUpdate,
  onRefresh,
  onStart,
  onOpenHermes,
  onOpenHermesSettings,
  onOpenCodex,
}: {
  status: StartupStatus | null;
  checking: boolean;
  progress: BootstrapProgress | null;
  autoUpdate: boolean;
  onToggleAutoUpdate?: (next: boolean) => void;
  onRefresh?: () => void;
  onStart?: () => void;
  onOpenHermes?: () => void;
  onOpenHermesSettings?: () => void;
  onOpenCodex?: () => void;
}) {
  const hermesCheck = status?.checks.find(check => check.id === 'hermes-agent');
  const codexCheck = status?.checks.find(check => check.id === 'codex-cli');
  const updateCheck = status?.checks.find(check => check.id === 'hermes-update');
  const hermesReady = Boolean(hermesCheck && hermesCheck.status === 'ok');

  return (
    <main className="provider-setup-shell">
      <section className="provider-setup-card" aria-live="polite">
        <div className="provider-setup-brand">Consiglio</div>
        <div className="provider-setup-heading">
          <span className="provider-setup-kicker">FIRST RUN</span>
          <h1>{checking ? 'Checking your Hermes install' : 'Consiglio drives Hermes'}</h1>
          <p>
            Consiglio is a shell over Hermes Agent: sessions, transcripts and models come from
            your Hermes configuration.
          </p>
        </div>

        {checking && progress && (
          <div className="provider-discovery-progress">
            <span className="provider-discovery-spinner" aria-hidden="true" />
            <div className="provider-discovery-copy">
              <strong>{progress.message}</strong>
              <div className="provider-discovery-track">
                <span style={{ width: `${Math.max(8, Math.round((progress.completed / Math.max(1, progress.total)) * 100))}%` }} />
              </div>
            </div>
          </div>
        )}

        <div className="provider-setup-options">
          <article className={`provider-setup-option${hermesReady ? ' is-ready' : ''}`}>
            <div className="provider-setup-option-title">
              <span>Hermes Agent</span>
              <span className="provider-setup-tag">Default backend</span>
            </div>
            <p>Consiglio talks to <code>hermes acp</code>. Models, providers and endpoints come from your Hermes config.</p>
            <div className="provider-setup-status">{hermesCheck?.message || 'Checking Hermes…'}</div>
            <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
              <button className="codex-button codex-button-primary" disabled={!hermesReady || !onStart} onClick={onStart}>
                Start a Hermes task
              </button>
              <button className="codex-button codex-button-secondary" onClick={onOpenHermesSettings}>Hermes settings</button>
              <button className="codex-button codex-button-secondary" onClick={onOpenHermes}>Open config file</button>
            </div>
          </article>

          <article className={`provider-setup-option${codexCheck?.status === 'ok' ? ' is-ready' : ''}`}>
            <div className="provider-setup-option-title">
              <span>Codex CLI</span>
              <span className="provider-setup-tag is-lan">Optional</span>
            </div>
            <p>Run the Codex CLI as an alternative backend, with its own profile and MCP servers.</p>
            <div className="provider-setup-status">{codexCheck?.message || 'Codex CLI not detected.'}</div>
            {codexCheck?.status !== 'ok' && (
              <button className="codex-button codex-button-secondary" onClick={onOpenCodex}>Install Codex</button>
            )}
          </article>
        </div>

        <div className="provider-setup-actions">
          <span>{updateCheck?.message || (checking ? progress?.message || 'Checking…' : 'Checks can be run again at any time.')}</span>
          <div style={{ display: 'flex', gap: 12, alignItems: 'center' }}>
            <label className="codex-update-checkbox" style={{ border: 'none', padding: 0, margin: 0 }}>
              <input
                type="checkbox"
                checked={autoUpdate}
                onChange={event => onToggleAutoUpdate?.(event.target.checked)}
              />
              <span>Auto-update Hermes on startup</span>
            </label>
            <button className="codex-button codex-button-secondary" onClick={onRefresh} disabled={checking || !onRefresh}>
              {checking ? 'Checking…' : 'Check again'}
            </button>
          </div>
        </div>
      </section>
    </main>
  );
}
