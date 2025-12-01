# PCManFM-Qt TODO List

This file tracks work items, planned features, and technical debt for the PCManFM-Qt project. Maintainers and contributors can use this to coordinate development efforts.

## Modernization and Backend Work
- [ ] Replace remaining libfm-qt UI dependencies with core interfaces where practical
- [ ] Expand Qt backend coverage (remote URIs, trash, volumes) without GIO
- [ ] Remove legacy libfm/libfm-qt code paths and includes

## UI/UX Improvements
- [ ] Harden keyboard navigation and shortcuts (focus/selection in split view, Delete handling)
- [ ] Keep View/Sort menus aligned with active tab state
- [ ] Audit menu/actions to remove unsupported legacy entries

## Stability and Technical Debt
- [ ] Eliminate GLib/GIO runtime warnings from remaining libfm-qt usage
- [ ] Fix any build warnings introduced by backend removal
- [ ] Add error-handling/confirmation coverage for destructive ops

## Documentation and Process
- [ ] Keep HACKING.md in sync with ongoing architecture changes
- [ ] Document backend responsibilities and current gaps
- [ ] Refresh release/build notes after major refactors

## Testing
- [ ] Add targeted tests for Qt file ops (rename vs copy/move semantics)
- [ ] Add smoke tests for selection/focus rules in split view and tabs
